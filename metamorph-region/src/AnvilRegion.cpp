#include <basalt/metamorph/region/AnvilRegion.hpp>
#include <basalt/wire/io/Compression.hpp>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace basalt::metamorph::region {
namespace {

constexpr std::size_t kSectorSize = 4096;
constexpr std::size_t kHeaderSize = kSectorSize;
constexpr std::size_t kTimestampsSize = kSectorSize;
constexpr std::uint8_t kCompressionZlib = 2;
constexpr std::uint8_t kCompressionNone = 3;

[[nodiscard]] std::uint32_t readInt32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) | static_cast<std::uint32_t>(bytes[offset + 3]);
}

} // namespace

std::string_view describe(AnvilError error) {
    switch (error) {
    case AnvilError::TooShort:
        return "too short to be an Anvil region";
    case AnvilError::OutOfBounds:
        return "a chunk points outside the region file";
    case AnvilError::UnsupportedCompression:
        return "unsupported chunk compression";
    case AnvilError::ChunkTooLarge:
        return "a chunk does not fit in an Anvil region";
    case AnvilError::CorruptChunk:
    default:
        return "a chunk could not be decompressed";
    }
}

std::expected<LinearRegion, AnvilError> anvilRegion::read(std::span<const std::uint8_t> bytes) {
    // parity: `RegionFile` opens a zero-length file as a region with no chunks.
    if (bytes.empty()) {
        return LinearRegion{};
    }
    if (bytes.size() < kHeaderSize + kTimestampsSize) {
        return std::unexpected{AnvilError::TooShort};
    }

    LinearRegion region;
    for (std::size_t slot = 0; slot < LinearRegion::kSlotCount; ++slot) {
        const std::size_t headerOffset = slot * 4;
        const std::size_t sectorOffset = (static_cast<std::size_t>(bytes[headerOffset]) << 16) |
                                         (static_cast<std::size_t>(bytes[headerOffset + 1]) << 8) |
                                         static_cast<std::size_t>(bytes[headerOffset + 2]);
        const std::uint8_t sectorCount = bytes[headerOffset + 3];
        // Both zero is the empty slot the header uses for a chunk that was never generated.
        if (sectorOffset == 0 || sectorCount == 0) {
            continue;
        }

        const std::int32_t timestamp = static_cast<std::int32_t>(readInt32(bytes, kTimestampsSize + (slot * 4)));
        const std::size_t chunkStart = sectorOffset * kSectorSize;
        if (chunkStart + 5 > bytes.size()) {
            return std::unexpected{AnvilError::OutOfBounds};
        }
        const std::size_t length = readInt32(bytes, chunkStart);
        const std::uint8_t compressionType = bytes[chunkStart + 4];
        // The declared length counts the compression byte, so the payload is one shorter.
        if (length == 0 || chunkStart + 4 + length > bytes.size()) {
            return std::unexpected{AnvilError::OutOfBounds};
        }
        const std::span<const std::uint8_t> compressed = bytes.subspan(chunkStart + 5, length - 1);

        std::vector<std::uint8_t> raw;
        if (compressionType == kCompressionZlib) {
            std::expected<std::vector<std::uint8_t>, wire::io::CompressionError> decompressed =
                wire::io::zlib::decompressUnknownSize(compressed);
            if (!decompressed.has_value()) {
                return std::unexpected{AnvilError::CorruptChunk};
            }
            raw = *std::move(decompressed);
        } else if (compressionType == kCompressionNone) {
            raw.assign(compressed.begin(), compressed.end());
        } else {
            return std::unexpected{AnvilError::UnsupportedCompression};
        }

        const std::int32_t localX = static_cast<std::int32_t>(slot % LinearRegion::kDimension);
        const std::int32_t localZ = static_cast<std::int32_t>(slot / LinearRegion::kDimension);
        region.setChunk(localX, localZ, std::move(raw), timestamp);
    }
    return region;
}

std::expected<std::vector<std::uint8_t>, AnvilError> anvilRegion::write(const LinearRegion& region) {
    constexpr std::size_t kMaxSectors = 255;
    std::vector<std::uint8_t> file(kHeaderSize + kTimestampsSize, 0);
    const auto put32 = [&file](std::size_t at, std::uint32_t value) {
        file[at] = static_cast<std::uint8_t>(value >> 24);
        file[at + 1] = static_cast<std::uint8_t>(value >> 16);
        file[at + 2] = static_cast<std::uint8_t>(value >> 8);
        file[at + 3] = static_cast<std::uint8_t>(value);
    };
    for (std::size_t slot = 0; slot < LinearRegion::kSlotCount; ++slot) {
        const auto localX = static_cast<std::int32_t>(slot % LinearRegion::kDimension);
        const auto localZ = static_cast<std::int32_t>(slot / LinearRegion::kDimension);
        if (!region.hasChunkAt(localX, localZ)) {
            continue;
        }
        const auto compressed = wire::io::zlib::compress(region.chunkAt(localX, localZ));
        if (!compressed.has_value()) {
            return std::unexpected{AnvilError::CorruptChunk};
        }
        const std::size_t used = 5 + compressed->size();
        const std::size_t sectors = (used + kSectorSize - 1) / kSectorSize;
        if (sectors > kMaxSectors) {
            return std::unexpected{AnvilError::ChunkTooLarge};
        }
        const std::size_t first = file.size() / kSectorSize;
        put32(slot * 4, static_cast<std::uint32_t>((first << 8) | sectors));
        put32(kTimestampsSize + slot * 4, static_cast<std::uint32_t>(region.timestampAt(localX, localZ)));
        const std::size_t start = file.size();
        file.resize(start + sectors * kSectorSize, 0);
        put32(start, static_cast<std::uint32_t>(compressed->size() + 1));
        file[start + 4] = kCompressionZlib;
        std::ranges::copy(*compressed, file.begin() + static_cast<std::ptrdiff_t>(start + 5));
    }
    return file;
}

} // namespace basalt::metamorph::region
