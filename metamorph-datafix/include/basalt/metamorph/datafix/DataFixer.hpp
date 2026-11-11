#pragma once

#include <basalt/wire/nbt/NbtTag.hpp>

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>

namespace basalt::metamorph::datafix {

/// The world version this build writes. parity: the `DataVersion` of 26.3.
inline constexpr std::int32_t kCurrentVersion = 5023;

/// What the root being upgraded is, since a fix may fit one only; `Entities` is a vanilla `entities/` file.
/// `Level` is the `Data` compound of `level.dat`, `Structure` a template, `SavedData` the `data` of a saved data file,
/// `Stats` and `Advancements` the JSON-shaped trees of a player's statistics and advancements.
enum class Domain : std::uint8_t {
    Chunk,
    Player,
    Entities,
    Level,
    Structure,
    PoiChunk,
    Options,
    Hotbar,
    WorldGenSettings,
    DebugProfile,
    SavedData,
    Stats,
    Advancements
};

/// The name each domain has in the native service and the plugin API; empty for none.
[[nodiscard]] std::string_view domainName(Domain domain);
[[nodiscard]] bool domainFromName(std::string_view name, Domain& domain);

struct Context {
    /// parity: `__context.dimension`, which `BlendingDataFix` reads.
    bool overworld = true;
    /// parity: `__context.generator` being `minecraft:noise`, which makes an old chunk blend into new terrain.
    bool noiseGenerator = true;
    /// Which saved data file a `SavedData` root is (`raids`, `scoreboard`...), since each has its own fixes.
    std::string_view savedData;
    /// Set by a fix that cannot go on, as vanilla's throws for an old custom world; the data is then left as is.
    std::string* failure = nullptr;
};

/// parity: `DataFixerUpper.update`, running every fix after `fromVersion` up to `toVersion` in order.
/// Returns what a fix refused with, or empty; `root` is meaningless after a refusal.
std::string upgrade(Domain domain, wire::nbt::NbtCompound& root, std::int32_t fromVersion, const Context& context = {},
                    std::int32_t toVersion = kCurrentVersion);

/// The first vanilla step after `fromVersion` up to `toVersion` that this build cannot run for the domain.
/// Empty when every step is covered; the caller must refuse to upgrade otherwise rather than skip a fix.
[[nodiscard]] std::string missingStep(Domain domain, std::int32_t fromVersion, std::int32_t toVersion = kCurrentVersion,
                                      std::string_view savedData = {});

/// A vanilla fix class registered in the fixer, at the schema `version` and `subVersion` it was added.
struct StepInfo {
    std::int32_t version;
    std::int32_t subVersion;
    std::string_view name;
    /// The domains this step touches, one bit per `Domain` enumerator.
    std::uint32_t domains;
};

/// Every fix step of the vanilla fixer, in registration order.
[[nodiscard]] std::span<const StepInfo> steps();

/// One schema version of the vanilla fixer and the class that defines its types.
struct SchemaInfo {
    std::int32_t version;
    std::int32_t subVersion;
    std::string_view name;
};

[[nodiscard]] std::span<const SchemaInfo> schemas();

/// parity: `Schema.getVersionKey`, the version times ten plus the sub version.
[[nodiscard]] constexpr std::int32_t versionKey(std::int32_t version, std::int32_t subVersion) {
    return version * 10 + subVersion;
}

/// Runs the native step `name` that vanilla registered at `versionKey`; false when this build has no such step.
[[nodiscard]] bool applyStep(std::string_view name, std::int32_t versionKey, Domain domain,
                             wire::nbt::NbtCompound& root, const Context& context = {});

/// A block state with its name and properties.
struct LegacyBlockState {
    std::string name;
    std::map<std::string, std::string> properties;
};

/// parity: `BlockStateData.getTag`, the 1.13 state of the pre-1.13 `block << 4 | data`.
[[nodiscard]] LegacyBlockState legacyBlockState(std::int32_t id);

} // namespace basalt::metamorph::datafix
