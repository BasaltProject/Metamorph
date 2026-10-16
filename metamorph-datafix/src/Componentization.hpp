#pragma once

#include <basalt/wire/nbt/NbtTag.hpp>

namespace basalt::metamorph::datafix {

/// parity: `ItemStackComponentizationFix`, for one item stack in the format before components.
void componentize(wire::nbt::NbtCompound& stack);

/// parity: `ItemStackComponentizationFix.fixProfile`, a skull owner as a resolvable profile.
[[nodiscard]] wire::nbt::NbtTag fixProfile(const wire::nbt::NbtTag& owner);

} // namespace basalt::metamorph::datafix
