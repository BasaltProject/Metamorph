#pragma once

#include <basalt/metamorph/datafix/DataFixer.hpp>
#include <basalt/metamorph/region/AnvilRegion.hpp>
#include <basalt/metamorph/region/LinearRegion.hpp>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace basalt::metamorph::cli {

enum class Format { Anvil, Linear };

struct RegionFile {
    std::filesystem::path path;
    std::filesystem::path relative;
    Format format = Format::Anvil;
    std::optional<datafix::Domain> domain;
};

/// Every `r.<x>.<z>.mca` and `.linear` under a world, whichever dimension folder it sits in.
[[nodiscard]] std::vector<RegionFile> regionFilesIn(const std::filesystem::path& world);

[[nodiscard]] std::expected<region::LinearRegion, std::string> load(const RegionFile& file);

[[nodiscard]] std::expected<void, std::string> store(const region::LinearRegion& region, Format format,
                                                     const std::filesystem::path& path);

[[nodiscard]] const char* extensionOf(Format format);

} // namespace basalt::metamorph::cli
