#pragma once

#include <basalt/wire/nbt/NbtTag.hpp>

#include <cstdint>
#include <string_view>

namespace basalt::metamorph::datafix {

/// parity: `GameRuleRegistryFix`, the string-valued `GameRules` of an old `level.dat` as the namespaced rules of today.
[[nodiscard]] wire::nbt::NbtCompound gameRulesOfLegacy(const wire::nbt::NbtCompound& legacy);

/// parity: `LegacyWorldBorderFix`, the flat `Border*` fields of an old `level.dat` as a world border; `warning_time` is
/// kept as written, as the official upgrade does.
[[nodiscard]] wire::nbt::NbtCompound worldBorderOfLegacy(const wire::nbt::NbtCompound& level);

/// parity: the fixes of `DataFixTypes.SAVED_DATA_*` between `fromVersion` and today, for the `data` of one saved file.
/// Covers random sequences, raids and the scoreboard; any other file is left as it is.
void upgradeSavedData(std::string_view name, wire::nbt::NbtCompound& data, std::int32_t fromVersion);

} // namespace basalt::metamorph::datafix
