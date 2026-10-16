#pragma once

#include <basalt/wire/nbt/NbtTag.hpp>

#include <functional>

namespace basalt::metamorph::datafix {

using Edit = std::function<void(wire::nbt::NbtCompound&)>;

/// Every item stack anywhere under `root`: a compound with a string `id` and a numeric `count`.
void forEachStack(wire::nbt::NbtCompound& root, const Edit& edit);

/// The `components` of every item stack under `root`.
void forEachComponents(wire::nbt::NbtCompound& root, const Edit& edit);

} // namespace basalt::metamorph::datafix
