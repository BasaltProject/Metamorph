#pragma once

#include <basalt/metamorph/region/LinearRegion.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace basalt::metamorph::region {

enum class RegionFormat : std::uint8_t { Empty, Anvil, Linear, Bsw, Mixed };

[[nodiscard]] std::string_view nameOf(RegionFormat format);

[[nodiscard]] const char* extensionOf(RegionFormat format);

/// What the `r.<x>.<z>.<ext>` files of a folder are; `Mixed` when more than one format is there.
[[nodiscard]] RegionFormat detectRegionFormat(const std::string& regionFolder);

/// Whether at least one `r.<x>.<z>` file of that format is in the folder.
[[nodiscard]] bool hasRegionFiles(const std::string& regionFolder, RegionFormat format);

inline constexpr const char* kBswDictionaryName = "bsw.dict";

struct ConvertOptions {
    int level = 3;
    /// Train a dictionary from the first chunks of the source when the target is bsw.
    bool dictionary = true;
    std::function<void(std::size_t done, std::size_t total)> progress;
    /// Runs on each source region before it is written, so a caller can fold in data kept elsewhere.
    std::function<void(std::int32_t regionX, std::int32_t regionZ, LinearRegion& region)> prepare;
    unsigned threads = 0;
};

struct ConvertReport {
    std::size_t regions = 0;
    std::size_t chunks = 0;
    std::size_t mismatches = 0;
    std::size_t failed = 0;
    std::string error;

    [[nodiscard]] bool ok() const noexcept { return failed == 0 && mismatches == 0 && error.empty(); }
};

/// Converts every region of `source` into `target` (a new folder), then decodes both sides and compares every chunk.
[[nodiscard]] ConvertReport convertRegionFolder(const std::string& source, const std::string& target, RegionFormat to,
                                                const ConvertOptions& options);

} // namespace basalt::metamorph::region
