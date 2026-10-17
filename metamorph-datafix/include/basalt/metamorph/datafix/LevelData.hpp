#pragma once

#include <basalt/wire/nbt/NbtTag.hpp>

namespace basalt::metamorph::datafix {

/// parity: `GameRuleRegistryFix`, the string-valued `GameRules` of an old `level.dat` as the namespaced rules of today.
[[nodiscard]] wire::nbt::NbtCompound gameRulesOfLegacy(const wire::nbt::NbtCompound& legacy);

} // namespace basalt::metamorph::datafix
