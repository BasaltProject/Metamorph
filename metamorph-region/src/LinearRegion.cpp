#include <basalt/metamorph/region/LinearRegion.hpp>
#include <basalt/wire/io/Compression.hpp>
#include <basalt/wire/io/FileIO.hpp>

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace basalt::metamorph::region {
namespace {

constexpr std::uint64_t kSignature = 0xC3FF13183CCA9D9AULL;
constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kHeaderSize = 32;
constexpr std::size_t kFooterSize = 8;
constexpr std::size_t kInnerHeaderSize = LinearRegion::kSlotCount * 8;

void writeIntAt(std::span<std::uint8_t> into, std::size_t offset, std::uint32_t value) {
    into[offset] = static_cast<std::uint8_t>(value >> 24);
    into[offset + 1] = static_cast<std::uint8_t>(value >> 16);
    into[offset + 2] = static_cast<std::uint8_t>(value >> 8);
    into[offset + 3] = static_cast<std::uint8_t>(value);
}

void writeShortAt(std::span<std::uint8_t> into, std::size_t offset, std::uint16_t value) {
    into[offset] = static_cast<std::uint8_t>(value >> 8);
    into[offset + 1] = static_cast<std::uint8_t>(value);
}

void writeLongAt(std::span<std::uint8_t> into, std::size_t offset, std::uint64_t value) {
    for (std::size_t i = 0; i < 8; ++i) {
        into[offset + i] = static_cast<std::uint8_t>(value >> (56 - (i * 8)));
    }
}

[[nodiscard]] std::uint64_t readLongAt(std::span<const std::uint8_t> from, std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value = (value << 8) | from[offset + i];
    }
    return value;
}

[[nodiscard]] std::uint32_t readIntAt(std::span<const std::uint8_t> from, std::size_t offset) {
    return (static_cast<std::uint32_t>(from[offset]) << 24) | (static_cast<std::uint32_t>(from[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(from[offset + 2]) << 8) | static_cast<std::uint32_t>(from[offset + 3]);
}

[[nodiscard]] std::uint16_t readShortAt(std::span<const std::uint8_t> from, std::size_t offset) {
    return static_cast<std::uint16_t>((from[offset] << 8) | from[offset + 1]);
}

} // namespace

std::string_view describe(LinearError error) {
    switch (error) {
    case LinearError::TooShort:
        return "too short to be a Linear region";
    case LinearError::BadSignature:
        return "bad superblock signature";
    case LinearError::UnsupportedVersion:
        return "unsupported version";
    case LinearError::BadFooter:
        return "bad footer signature";
    case LinearError::LengthMismatch:
        return "file size does not match the declared compressed length";
    case LinearError::TruncatedTable:
        return "decompressed region smaller than its own chunk table";
    case LinearError::TruncatedChunks:
        return "decompressed size does not match the chunk table";
    case LinearError::ChunkCountMismatch:
    default:
        return "chunk count mismatch between the header and the table";
    }
}

std::size_t LinearRegion::index(std::int32_t localX, std::int32_t localZ) {
    if (localX < 0 || localX >= kDimension || localZ < 0 || localZ >= kDimension) {
        throw std::out_of_range{"chunk coordinates out of range: (" + std::to_string(localX) + ", " +
                                std::to_string(localZ) + ")"};
    }
    return static_cast<std::size_t>(localX) + (static_cast<std::size_t>(localZ) * kDimension);
}

std::span<const std::uint8_t> LinearRegion::chunkAt(std::int32_t localX, std::int32_t localZ) const {
    const std::size_t at = index(localX, localZ);
    return present_[at] ? std::span<const std::uint8_t>{chunks_[at]} : std::span<const std::uint8_t>{};
}

bool LinearRegion::hasChunkAt(std::int32_t localX, std::int32_t localZ) const {
    return present_[index(localX, localZ)];
}

std::int32_t LinearRegion::timestampAt(std::int32_t localX, std::int32_t localZ) const {
    return timestamps_[index(localX, localZ)];
}

void LinearRegion::setChunk(std::int32_t localX, std::int32_t localZ, std::vector<std::uint8_t> data,
                            std::int32_t timestamp) {
    const std::size_t at = index(localX, localZ);
    residentBytes_ += static_cast<std::int64_t>(data.size()) - static_cast<std::int64_t>(chunks_[at].size());
    chunks_[at] = std::move(data);
    timestamps_[at] = timestamp;
    present_[at] = true;
}

void LinearRegion::clearChunk(std::int32_t localX, std::int32_t localZ) {
    const std::size_t at = index(localX, localZ);
    residentBytes_ -= static_cast<std::int64_t>(chunks_[at].size());
    chunks_[at].clear();
    chunks_[at].shrink_to_fit();
    timestamps_[at] = 0;
    present_[at] = false;
}

LinearRegion::Framing LinearRegion::framing() const {
    Framing built;
    built.table.assign(kInnerHeaderSize, 0);
    for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
        if (!present_[slot]) {
            continue;
        }
        if (timestamps_[slot] > built.newestTimestamp) {
            built.newestTimestamp = timestamps_[slot];
        }
        ++built.chunkCount;
        writeIntAt(built.table, slot * 8, static_cast<std::uint32_t>(chunks_[slot].size()));
        writeIntAt(built.table, (slot * 8) + 4, static_cast<std::uint32_t>(timestamps_[slot]));
    }
    return built;
}

bool LinearRegion::writeTo(const std::string& path, int compressionLevel) const {
    const Framing built = framing();

    std::array<std::uint8_t, kHeaderSize> header{};
    writeLongAt(header, 0, kSignature);
    header[8] = kVersion;
    writeLongAt(header, 9, static_cast<std::uint64_t>(built.newestTimestamp));
    header[17] = static_cast<std::uint8_t>(compressionLevel);
    writeShortAt(header, 18, static_cast<std::uint16_t>(built.chunkCount));
    writeIntAt(header, 20, 0);
    writeLongAt(header, 24, 0);

    // Written beside the target and moved over it, so a crash leaves the old region or the new one, never half.
    const std::string temporary = path + ".tmp";
    std::optional<wire::io::FileWriter> writer = wire::io::FileWriter::open(temporary);
    if (!writer.has_value()) {
        return false;
    }
    const auto abandon = [&writer, &temporary] {
        writer->close();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    };
    writer->write(header);

    std::size_t compressedSize = 0;
    {
        wire::io::zstd::StreamCompressor compressor{[&writer, &compressedSize](std::span<const std::uint8_t> block) {
                                                        writer->write(block);
                                                        compressedSize += block.size();
                                                    },
                                                    compressionLevel};
        if (!compressor.valid()) {
            return abandon();
        }
        if (!compressor.feed(built.table)) {
            return abandon();
        }
        for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
            if (present_[slot] && !compressor.feed(chunks_[slot])) {
                return abandon();
            }
        }
        if (!compressor.finish()) {
            return abandon();
        }
    }

    std::array<std::uint8_t, kFooterSize> footer{};
    writeLongAt(footer, 0, kSignature);
    writer->write(footer);

    std::array<std::uint8_t, 4> patch{};
    writeIntAt(patch, 0, static_cast<std::uint32_t>(compressedSize));
    writer->seek(20);
    writer->write(patch);
    if (!writer->sync()) {
        return abandon();
    }
    writer->close();
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

std::vector<std::uint8_t> LinearRegion::write(int compressionLevel) const {
    const Framing built = framing();

    std::vector<std::uint8_t> compressed;
    {
        wire::io::zstd::StreamCompressor compressor{[&compressed](std::span<const std::uint8_t> block) {
                                                        compressed.insert(compressed.end(), block.begin(), block.end());
                                                    },
                                                    compressionLevel};
        static_cast<void>(compressor.feed(built.table));
        for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
            if (present_[slot]) {
                static_cast<void>(compressor.feed(chunks_[slot]));
            }
        }
        static_cast<void>(compressor.finish());
    }

    std::vector<std::uint8_t> out(kHeaderSize + compressed.size() + kFooterSize, 0);
    writeLongAt(out, 0, kSignature);
    out[8] = kVersion;
    writeLongAt(out, 9, static_cast<std::uint64_t>(built.newestTimestamp));
    out[17] = static_cast<std::uint8_t>(compressionLevel);
    writeShortAt(out, 18, static_cast<std::uint16_t>(built.chunkCount));
    writeIntAt(out, 20, static_cast<std::uint32_t>(compressed.size()));
    writeLongAt(out, 24, 0);
    std::copy(compressed.begin(), compressed.end(), out.begin() + static_cast<std::ptrdiff_t>(kHeaderSize));
    writeLongAt(out, kHeaderSize + compressed.size(), kSignature);
    return out;
}

std::expected<LinearRegion, LinearError> LinearRegion::read(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kHeaderSize + kFooterSize) {
        return std::unexpected{LinearError::TooShort};
    }
    if (readLongAt(bytes, 0) != kSignature) {
        return std::unexpected{LinearError::BadSignature};
    }
    const std::uint8_t version = bytes[8];
    if (version != 1 && version != 2) {
        return std::unexpected{LinearError::UnsupportedVersion};
    }
    // Bytes 9..16 are the newest timestamp and 17 the compression level: both informational.
    const std::int32_t declaredChunkCount = readShortAt(bytes, 18);
    const std::size_t compressedLength = readIntAt(bytes, 20);

    if (readLongAt(bytes, bytes.size() - kFooterSize) != kSignature) {
        return std::unexpected{LinearError::BadFooter};
    }
    if (kHeaderSize + compressedLength + kFooterSize != bytes.size()) {
        return std::unexpected{LinearError::LengthMismatch};
    }

    wire::io::zstd::StreamDecompressor reader{bytes.subspan(kHeaderSize, compressedLength)};
    std::vector<std::uint8_t> table(kInnerHeaderSize);
    if (!reader.readFully(table)) {
        return std::unexpected{LinearError::TruncatedTable};
    }

    LinearRegion region;
    std::int32_t realChunkCount = 0;
    for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
        const std::uint32_t size = readIntAt(table, slot * 8);
        const std::int32_t timestamp = static_cast<std::int32_t>(readIntAt(table, (slot * 8) + 4));
        if (size == 0) {
            continue;
        }
        std::vector<std::uint8_t> blob(size);
        if (!reader.readFully(blob)) {
            return std::unexpected{LinearError::TruncatedChunks};
        }
        region.residentBytes_ += size;
        region.chunks_[slot] = std::move(blob);
        region.timestamps_[slot] = timestamp;
        region.present_[slot] = true;
        ++realChunkCount;
    }
    if (!reader.frameComplete()) {
        return std::unexpected{LinearError::TruncatedChunks};
    }
    if (realChunkCount != declaredChunkCount) {
        return std::unexpected{LinearError::ChunkCountMismatch};
    }
    return region;
}

} // namespace basalt::metamorph::region
