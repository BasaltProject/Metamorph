#pragma once

#include "Worlds.hpp"

#include <filesystem>
#include <string_view>

namespace basalt::metamorph::cli {

/// Rewrites every region of `world` in `format` under `output`, leaving the input untouched.
[[nodiscard]] int convert(const std::filesystem::path& world, Format format, const std::filesystem::path& output);

/// Converts every region folder of `world` to `to` ("anvil", "linear" or "bsw"), verifying each chunk afterwards.
[[nodiscard]] int convertFolders(const std::filesystem::path& world, std::string_view to,
                                 const std::filesystem::path& output);

/// Brings the chunks of every region to the current world version, keeping each file's format.
[[nodiscard]] int upgrade(const std::filesystem::path& world, const std::filesystem::path& output);

} // namespace basalt::metamorph::cli
