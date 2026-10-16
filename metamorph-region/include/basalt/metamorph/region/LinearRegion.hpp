#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace basalt::metamorph::region {

enum class LinearError : std::uint8_t {
    TooShort,
    BadSignature,
    UnsupportedVersion,
    BadFooter,
    LengthMismatch,
    TruncatedTable,
    TruncatedChunks,
    ChunkCountMismatch,
};

[[nodiscard]] std::string_view describe(LinearError error);

class LinearRegion {
public:
    static constexpr std::int32_t kDimension = 32;
    static constexpr std::size_t kSlotCount = static_cast<std::size_t>(kDimension) * kDimension;
    static constexpr int kDefaultCompressionLevel = 1;

    LinearRegion() = default;

    [[nodiscard]] static std::expected<LinearRegion, LinearError> read(std::span<const std::uint8_t> bytes);

    [[nodiscard]] std::int64_t residentBytes() const noexcept { return residentBytes_; }

    [[nodiscard]] std::span<const std::uint8_t> chunkAt(std::int32_t localX, std::int32_t localZ) const;

    [[nodiscard]] bool hasChunkAt(std::int32_t localX, std::int32_t localZ) const;

    [[nodiscard]] std::int32_t timestampAt(std::int32_t localX, std::int32_t localZ) const;

    void setChunk(std::int32_t localX, std::int32_t localZ, std::vector<std::uint8_t> data, std::int32_t timestamp);

    void clearChunk(std::int32_t localX, std::int32_t localZ);

    bool writeTo(const std::string& path, int compressionLevel = kDefaultCompressionLevel) const;

    [[nodiscard]] std::vector<std::uint8_t> write(int compressionLevel = kDefaultCompressionLevel) const;

private:
    [[nodiscard]] static std::size_t index(std::int32_t localX, std::int32_t localZ);

    struct Framing {
        std::vector<std::uint8_t> table;
        std::int64_t newestTimestamp = 0;
        std::int32_t chunkCount = 0;
    };

    [[nodiscard]] Framing framing() const;

    std::array<std::vector<std::uint8_t>, kSlotCount> chunks_;
    std::array<std::int32_t, kSlotCount> timestamps_{};
    std::array<bool, kSlotCount> present_{};
    std::int64_t residentBytes_ = 0;
};

} // namespace basalt::metamorph::region
