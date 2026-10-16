#pragma once

#include <basalt/wire/nbt/NbtTag.hpp>

#include <cstdint>

namespace basalt::metamorph::datafix {

/// The world version this build writes. parity: the `DataVersion` of 26.3.
inline constexpr std::int32_t kCurrentVersion = 5023;

/// What the root being upgraded is, since a fix may only make sense for one of them.
/// `Entities` is the `entities/` file of a vanilla chunk, `Entities` list included.
enum class Domain : std::uint8_t { Chunk, Player, Entities };

struct Context {
    /// parity: `__context.dimension`, which `BlendingDataFix` reads.
    bool overworld = true;
};

/// parity: `DataFixerUpper.update`, running every fix after `fromVersion` up to `toVersion` in order.
void upgrade(Domain domain, wire::nbt::NbtCompound& root, std::int32_t fromVersion, const Context& context = {},
             std::int32_t toVersion = kCurrentVersion);

} // namespace basalt::metamorph::datafix
