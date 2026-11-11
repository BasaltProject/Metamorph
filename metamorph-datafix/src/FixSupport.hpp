#pragma once

#include <basalt/metamorph/datafix/DataFixer.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace basalt::metamorph::datafix {

/// `Root` runs on the whole tree whatever the domain, for fixes that find their own places.
enum class Target : std::uint8_t { Chunk, BlockEntity, ItemStack, LegacyStack, Components, Palette, Root, Entity };

[[nodiscard]] constexpr std::uint32_t domainBit(Domain domain) { return 1U << static_cast<std::uint32_t>(domain); }

/// The domains a fix is written for unless it says otherwise: the trees that hold chunks, players and entities.
inline constexpr std::uint32_t kTreeDomains =
    domainBit(Domain::Chunk) | domainBit(Domain::Player) | domainBit(Domain::Entities);

struct Fix {
    std::int32_t version = 0;
    Target target = Target::Chunk;
    /// The block entity or entity id a fix is for; empty means every one.
    std::string_view blockEntityId;
    std::function<void(wire::nbt::NbtCompound&, const Context&)> apply;
    /// The vanilla class this runs, as `steps()` lists it; empty for an unnamed one.
    std::string_view name;
    /// For `kTreeDomains` fixes the engine reaches the other domains through their subtrees; any other mask is exact.
    std::uint32_t domains = kTreeDomains;
};

[[nodiscard]] std::string namespaced(std::string_view text);

/// Every entity under `root` and the passengers riding them, whether in an entity file, a chunk or the player.
void forEachEntity(Domain domain, wire::nbt::NbtCompound& root,
                   const std::function<void(wire::nbt::NbtCompound&)>& edit);

void rename(wire::nbt::NbtCompound& compound, std::string_view from, std::string_view to);

void forEachCompound(wire::nbt::NbtCompound& root, std::string_view listKey,
                     const std::function<void(wire::nbt::NbtCompound&)>& edit);

/// parity: `ExtraDataFixUtils.getPlainTranslationKey`, a component that is only `{translate: key}`.
[[nodiscard]] std::string_view plainTranslationKey(const wire::nbt::NbtTag* component);

void addFixesFrom0100(std::vector<Fix>& fixes);

void addEntityFixes(std::vector<Fix>& fixes);

void addFixesFrom1200(std::vector<Fix>& fixes);

void addFlatteningFixes(std::vector<Fix>& fixes);

/// parity: `ItemStackSpawnEggFix`, for the egg item `itemType` that names its mob in `tag.EntityTag.id`.
void convertSpawnEgg(wire::nbt::NbtCompound& stack, std::string_view itemType);

void addFixesFrom1400(std::vector<Fix>& fixes);

void addFixesFrom2600(std::vector<Fix>& fixes);

void addFixesFrom3000(std::vector<Fix>& fixes);

void addFixesFrom3400(std::vector<Fix>& fixes);

void addFixesFrom3800(std::vector<Fix>& fixes);

void addTextFixes(std::vector<Fix>& fixes);

void addFixesFrom4000(std::vector<Fix>& fixes);

void addFixesFrom4300(std::vector<Fix>& fixes);

void addFixesFrom4996(std::vector<Fix>& fixes);

void addLevelFixes(std::vector<Fix>& fixes);

void addWorldGenFixes(std::vector<Fix>& fixes);

void addStructureFixes(std::vector<Fix>& fixes);

void addOptionsFixes(std::vector<Fix>& fixes);

void addLegacyGapFixes(std::vector<Fix>& fixes);

void addSavedDataFixes(std::vector<Fix>& fixes);

/// A template as a chunk holding its palettes as sections, so the chunk fixes that reach block states apply to it.
[[nodiscard]] wire::nbt::NbtCompound structureAsChunk(const wire::nbt::NbtCompound& structure);

/// Takes the palettes, block entities and entities a fixed chunk holds back into the template it came from.
void chunkIntoStructure(const wire::nbt::NbtCompound& chunk, wire::nbt::NbtCompound& structure);

/// Whether a vanilla saved data fix step concerns the saved data file `savedData`.
[[nodiscard]] bool savedDataStepFits(std::string_view step, std::string_view savedData);

/// The `Fix` list the engine runs, sorted by version.
[[nodiscard]] const std::vector<Fix>& allFixes();

} // namespace basalt::metamorph::datafix
