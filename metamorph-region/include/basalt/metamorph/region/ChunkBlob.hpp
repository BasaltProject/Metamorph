#pragma once

#include <basalt/wire/nbt/NbtTag.hpp>

#include <bit>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace basalt::metamorph::region::chunkBlob {

static_assert(std::endian::native == std::endian::little, "chunk blobs store words as they sit in memory");

inline constexpr std::uint8_t kMagic = 0xB5;
inline constexpr std::uint8_t kVersion = 1;
inline constexpr std::size_t kBlockCells = 16 * 16 * 16;
inline constexpr std::size_t kBiomeCells = 4 * 4 * 4;
inline constexpr int kBlockMinBits = 4;
inline constexpr int kBlockMaxIndirectBits = 8;
inline constexpr int kBiomeMinBits = 1;
inline constexpr int kBiomeMaxIndirectBits = 3;

class ByteWriter {
public:
    explicit ByteWriter(std::vector<std::uint8_t>& out) : out_(out) {}

    void u8(std::uint8_t value) { out_.push_back(value); }

    void u16(std::uint16_t value) { raw(&value, sizeof value); }

    void u32(std::uint32_t value) { raw(&value, sizeof value); }

    void i32(std::int32_t value) { raw(&value, sizeof value); }

    void i64(std::int64_t value) { raw(&value, sizeof value); }

    void varint(std::uint32_t value) {
        while (value >= 0x80) {
            out_.push_back(static_cast<std::uint8_t>(value | 0x80));
            value >>= 7;
        }
        out_.push_back(static_cast<std::uint8_t>(value));
    }

    void bytes(std::span<const std::uint8_t> data) { out_.insert(out_.end(), data.begin(), data.end()); }

    void text(std::string_view data) { out_.insert(out_.end(), data.begin(), data.end()); }

    void words(std::span<const std::uint64_t> data) { raw(data.data(), data.size_bytes()); }

    [[nodiscard]] std::size_t size() const noexcept { return out_.size(); }

    void patchU32(std::size_t at, std::uint32_t value) { std::memcpy(out_.data() + at, &value, sizeof value); }

private:
    void raw(const void* data, std::size_t size) {
        const auto* first = static_cast<const std::uint8_t*>(data);
        out_.insert(out_.end(), first, first + size);
    }

    std::vector<std::uint8_t>& out_;
};

/// Reads never throw: a short read sets a sticky flag and returns zeros, checked once by the caller.
class ByteReader {
public:
    explicit ByteReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] std::uint8_t u8() {
        std::uint8_t value = 0;
        fill(&value, sizeof value);
        return value;
    }

    [[nodiscard]] std::uint16_t u16() {
        std::uint16_t value = 0;
        fill(&value, sizeof value);
        return value;
    }

    [[nodiscard]] std::uint32_t u32() {
        std::uint32_t value = 0;
        fill(&value, sizeof value);
        return value;
    }

    [[nodiscard]] std::int32_t i32() { return static_cast<std::int32_t>(u32()); }

    [[nodiscard]] std::int64_t i64() {
        std::int64_t value = 0;
        fill(&value, sizeof value);
        return value;
    }

    [[nodiscard]] std::uint32_t varint() {
        std::uint32_t value = 0;
        for (unsigned shift = 0; shift < 35; shift += 7) {
            const std::uint8_t next = u8();
            value |= static_cast<std::uint32_t>(next & 0x7F) << shift;
            if ((next & 0x80) == 0 || failed_) {
                return value;
            }
        }
        failed_ = true;
        return 0;
    }

    [[nodiscard]] std::span<const std::uint8_t> take(std::size_t size) {
        if (failed_ || bytes_.size() - pos_ < size) {
            failed_ = true;
            return {};
        }
        const std::span<const std::uint8_t> view = bytes_.subspan(pos_, size);
        pos_ += size;
        return view;
    }

    [[nodiscard]] bool failed() const noexcept { return failed_; }

    [[nodiscard]] std::size_t remaining() const noexcept { return bytes_.size() - pos_; }

private:
    void fill(void* into, std::size_t size) {
        if (failed_ || bytes_.size() - pos_ < size) {
            failed_ = true;
            return;
        }
        std::memcpy(into, bytes_.data() + pos_, size);
        pos_ += size;
    }

    std::span<const std::uint8_t> bytes_;
    std::size_t pos_ = 0;
    bool failed_ = false;
};

[[nodiscard]] constexpr int bitsFor(std::size_t entries) noexcept {
    int bits = 0;
    while ((std::size_t{1} << bits) < entries) {
        bits++;
    }
    return bits;
}

/// The packed words of `cells` values at `bits` each, with entries running across word borders.
[[nodiscard]] constexpr std::size_t packedWordCount(std::size_t cells, int bits) noexcept {
    return (cells * static_cast<std::size_t>(bits) + 63) / 64;
}

struct Header {
    std::int32_t dataVersion = 0;
    std::int32_t xPos = 0;
    std::int32_t zPos = 0;
    std::int32_t minSection = 0;
    std::int64_t inhabitedTime = 0;
    std::string_view status;
};

struct SectionEntry {
    std::int8_t y = 0;
    std::span<const std::uint8_t> payload;
};

/// A parsed blob: every span points into the bytes it was parsed from.
struct View {
    Header header;
    std::vector<std::string_view> names;
    std::vector<SectionEntry> sections;
    std::span<const std::uint8_t> rest;
};

/// A block state spelled `id[property=value,...]` with the properties in name order.
struct StateText {
    std::string id;
    std::map<std::string, std::string> properties;
};

[[nodiscard]] std::string formatState(const StateText& state);

[[nodiscard]] std::optional<StateText> parseState(std::string_view text);

[[nodiscard]] inline bool isBlob(std::span<const std::uint8_t> bytes) noexcept {
    return !bytes.empty() && bytes[0] == kMagic;
}

[[nodiscard]] std::optional<View> parse(std::span<const std::uint8_t> bytes);

/// One palette of a section: references into the blob's name table plus the packed cell indices.
struct Palette {
    std::vector<std::uint32_t> refs;
    int bits = 0;
    std::span<const std::uint8_t> words;
};

/// A section payload: block states, biomes, and the keys of the NBT section that nothing here models.
struct SectionPayload {
    Palette blocks;
    Palette biomes;
    std::span<const std::uint8_t> extras;
};

[[nodiscard]] bool parseSection(std::span<const std::uint8_t> payload, SectionPayload& out);

/// Emits a palette; `words` is empty for a single entry.
void writePalette(ByteWriter& out, std::span<const std::uint32_t> refs, int bits, std::span<const std::uint64_t> words);

struct SectionSpan {
    std::int8_t y = 0;
    std::size_t offset = 0;
    std::size_t length = 0;
};

/// Lays a blob out: header, name table, section directory, section payloads, then the rest compound bytes.
void assemble(std::vector<std::uint8_t>& out, const Header& header, std::span<const std::string> names,
              std::span<const SectionSpan> sections, std::span<const std::uint8_t> payloads,
              std::span<const std::uint8_t> rest);

/// The NBT chunk as a blob; empty when it is not of the current version or has a shape the blob cannot hold.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> fromNbt(const wire::nbt::NbtCompound& root,
                                                               std::int32_t dataVersion);

/// The NBT form of a blob, with sections in the vanilla layout.
[[nodiscard]] std::optional<wire::nbt::NbtCompound> toNbt(std::span<const std::uint8_t> blob);

/// What a region keeps for NBT chunk bytes: a blob when one fits, the NBT bytes themselves otherwise.
[[nodiscard]] std::vector<std::uint8_t> encode(std::span<const std::uint8_t> nbtBytes, std::int32_t dataVersion);

/// NBT chunk bytes for stored bytes, which are returned as they are when they are NBT already; empty when unreadable.
[[nodiscard]] std::vector<std::uint8_t> decode(std::span<const std::uint8_t> stored);

/// Same content, whatever the order of compound keys or the width of the packed section data.
[[nodiscard]] bool equivalent(std::span<const std::uint8_t> nbtA, std::span<const std::uint8_t> nbtB);

} // namespace basalt::metamorph::region::chunkBlob
