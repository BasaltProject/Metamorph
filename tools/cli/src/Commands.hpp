#pragma once

#include "Worlds.hpp"

#include <filesystem>

namespace basalt::metamorph::cli {

/// Rewrites every region of `world` in `format` under `output`, leaving the input untouched.
[[nodiscard]] int convert(const std::filesystem::path& world, Format format, const std::filesystem::path& output);

/// Brings the chunks of every region to the current world version, keeping each file's format.
[[nodiscard]] int upgrade(const std::filesystem::path& world, const std::filesystem::path& output);

} // namespace basalt::metamorph::cli
