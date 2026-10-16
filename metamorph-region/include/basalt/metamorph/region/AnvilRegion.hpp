#pragma once

#include <basalt/metamorph/region/LinearRegion.hpp>

#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

namespace basalt::metamorph::region {

enum class AnvilError : std::uint8_t {
    TooShort,
    OutOfBounds,
    UnsupportedCompression,
    CorruptChunk,
    ChunkTooLarge,
};

[[nodiscard]] std::string_view describe(AnvilError error);

namespace anvilRegion {

[[nodiscard]] std::expected<LinearRegion, AnvilError> read(std::span<const std::uint8_t> bytes);

/// A region file with every chunk zlib-compressed; a chunk of 1 MiB or more does not fit and is an error.
[[nodiscard]] std::expected<std::vector<std::uint8_t>, AnvilError> write(const LinearRegion& region);

} // namespace anvilRegion

} // namespace basalt::metamorph::region
