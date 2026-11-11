#include "DomainSupport.hpp"
#include "FlatteningTables.hpp"
#include "LegacyIdTables.hpp"
#include "RecipeRenames.hpp"

#include <basalt/wire/nbt/Snbt.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace basalt::metamorph::datafix {
namespace {

using namespace support;
using wire::nbt::NbtCompound;
using wire::nbt::NbtList;
using wire::nbt::NbtTag;
using wire::nbt::NbtType;

constexpr std::uint32_t kChunkOnly = domainBit(Domain::Chunk);

template <std::size_t N>
[[nodiscard]] std::string renamed(std::string_view name, const recipes::Rename (&table)[N]) {
    const std::string full = namespaced(name);
    for (const recipes::Rename& pair : table) {
        if (pair.from == full) {
            return std::string{pair.to};
        }
    }
    return full;
}

// parity: `ItemRenameFix` for the item named `from`.
template <const char* From, const char* To>
void renameLegacyItem(NbtCompound& stack, const Context&) {
    if (namespaced(stack.getString("id")) == From) {
        stack.putString("id", To);
    }
}

constexpr char kCookedFished[] = "minecraft:cooked_fished";
constexpr char kCookedFish[] = "minecraft:cooked_fish";
constexpr char kTotem[] = "minecraft:totem";
constexpr char kTotemOfUndying[] = "minecraft:totem_of_undying";

void keepPackedOfNothing(NbtCompound&, const Context&) {}

// parity: `NamespacedTypeRenameFix` over the recipe book of a player.
template <std::size_t N>
void renameRecipeBook(NbtCompound& root, const recipes::Rename (&table)[N]) {
    NbtCompound* book = compoundAt(root, "recipeBook");
    for (const std::string_view key : {"recipes", "toBeDisplayed"}) {
        NbtList* list = book == nullptr ? nullptr : listAt(*book, key);
        for (std::size_t i = 0; list != nullptr && i < list->size(); ++i) {
            if (const std::string* name = (*list)[i].get<std::string>(); name != nullptr) {
                (*list)[i] = NbtTag::ofString(renamed(*name, table));
            }
        }
    }
}

void recipeBook1502(NbtCompound& root, const Context&) { renameRecipeBook(root, recipes::kRecipes1502); }

void recipeBook1510(NbtCompound& root, const Context&) { renameRecipeBook(root, recipes::kRecipes1510); }

void sugarRecipe(NbtCompound& root, const Context&) {
    static constexpr recipes::Rename kSugar[]{{"minecraft:sugar", "minecraft:sugar_from_sugar_cane"}};
    renameRecipeBook(root, kSugar);
}

void stonecutterRecipes(NbtCompound& root, const Context&) {
    renameRecipeBook(root, recipes::kRecipes4769);
    forEachCompound(root, "block_entities", [](NbtCompound& furnace) {
        NbtCompound* used = compoundAt(furnace, "RecipesUsed");
        if (used == nullptr) {
            return;
        }
        NbtCompound fixed;
        for (std::size_t i = 0; i < used->size(); ++i) {
            fixed.set(renamed(used->names()[i], recipes::kRecipes4769), used->values()[i]);
        }
        *used = std::move(fixed);
    });
}

// parity: `NamespacedTypeRenameFix` over the game events a listener holds.
void gameEvents(NbtCompound& holder, const Context&) {
    NbtCompound* listener = compoundAt(holder, "listener");
    NbtCompound* event = listener == nullptr ? nullptr : compoundAt(*listener, "event");
    NbtTag* name = event == nullptr ? nullptr : event->find("game_event");
    if (const std::string* text = name == nullptr ? nullptr : name->get<std::string>(); text != nullptr) {
        *name = NbtTag::ofString(renamed(*text, recipes::kGameEvents3084));
    }
}

void gameEventsOfBlockEntity(NbtCompound& blockEntity, const Context& context) {
    const std::string id = namespaced(blockEntity.getString("id"));
    if (id == "minecraft:sculk_sensor" || id == "minecraft:sculk_shrieker") {
        gameEvents(blockEntity, context);
    }
}

void gameEventsOfEntity(NbtCompound& entity, const Context& context) {
    const std::string id = namespaced(entity.getString("id"));
    if (id == "minecraft:warden" || id == "minecraft:allay") {
        gameEvents(entity, context);
    }
}

// parity: `EntityCatSplitFix`, whose first branch sets a flag on a copy it throws away.
void splitCat(NbtCompound& entity, const Context&) {
    const std::int32_t type = entity.getInt("CatType");
    if (type > 0 && type < 4) {
        entity.putString("OwnerUUID", std::string{entity.getString("OwnerUUID")});
        entity.putString("id", "minecraft:cat");
    }
}

// parity: `ChunkStatusFix`.
void fullChunkStatus(NbtCompound& chunk, const Context&) {
    NbtCompound* level = compoundAt(chunk, "Level");
    if (level != nullptr && level->getString("Status", "empty") == "postprocessed") {
        level->putString("Status", "fullchunk");
    }
}

// parity: `WeaponSmithChestLootTableFix`.
void weaponsmithLoot(NbtCompound& chest, const Context&) {
    if (chest.getString("LootTable") == "minecraft:chests/village_blacksmith") {
        chest.putString("LootTable", "minecraft:chests/village/village_weaponsmith");
    }
}

// parity: `CatTypeFix`.
void catType(NbtCompound& cat, const Context&) {
    if (cat.getInt("CatType") == 9) {
        cat.putInt("CatType", 10);
    }
}

[[nodiscard]] std::string_view professionOf(std::int32_t profession, std::int32_t career) {
    switch (profession) {
    case 0:
        return career == 2   ? "minecraft:fisherman"
               : career == 3 ? "minecraft:shepherd"
               : career == 4 ? "minecraft:fletcher"
                             : "minecraft:farmer";
    case 1:
        return career == 2 ? "minecraft:cartographer" : "minecraft:librarian";
    case 2:
        return "minecraft:cleric";
    case 3:
        return career == 2 ? "minecraft:weaponsmith" : career == 3 ? "minecraft:toolsmith" : "minecraft:armorer";
    case 4:
        return career == 2 ? "minecraft:leatherworker" : "minecraft:butcher";
    default:
        return profession == 5 ? "minecraft:nitwit" : "minecraft:none";
    }
}

// parity: `VillagerDataFix`.
void villagerProfession(NbtCompound& entity, const Context&) {
    const auto asInt = [&entity](std::string_view key, std::int32_t fallback) {
        const std::optional<std::int64_t> value = whole(entity.find(key));
        return value.has_value() ? static_cast<std::int32_t>(*value) : fallback;
    };
    NbtCompound data;
    data.putString("type", "minecraft:plains");
    data.putString("profession", std::string{professionOf(asInt("Profession", 0), asInt("Career", 0))});
    if (const NbtTag* level = entity.find("CareerLevel"); level != nullptr) {
        data.set("level", *level);
    } else {
        data.putInt("level", 1);
    }
    entity.remove("Profession");
    entity.remove("Career");
    entity.remove("CareerLevel");
    entity.set("VillagerData", NbtTag::ofCompound(std::move(data)));
}

[[nodiscard]] std::int32_t minXpOfLevel(std::int32_t level) {
    static constexpr std::array<std::int32_t, 5> kThresholds{0, 10, 50, 100, 150};
    return kThresholds[static_cast<std::size_t>(std::clamp(level - 1, 0, 4))];
}

// parity: `VillagerRebuildLevelAndXpFix`.
void villagerLevelAndXp(NbtCompound& villager, const Context&) {
    NbtCompound* data = compoundAt(villager, "VillagerData");
    std::int32_t level = data == nullptr ? 0 : static_cast<std::int32_t>(whole(data->find("level")).value_or(0));
    if (level == 0 || level == 1) {
        NbtCompound* offers = compoundAt(villager, "Offers");
        const NbtList* recipes = offers == nullptr ? nullptr : offers->getList("Recipes");
        const auto count = static_cast<std::int32_t>(recipes == nullptr ? 0 : recipes->size());
        level = std::clamp(count / 2, 1, 5);
        if (level > 1) {
            if (data == nullptr) {
                villager.set("VillagerData", NbtTag::ofCompound({}));
                data = compoundAt(villager, "VillagerData");
            }
            data->putInt("level", level);
        }
    }
    if (!real(villager.find("Xp")).has_value()) {
        villager.putInt("Xp", minXpOfLevel(level));
    }
}

// parity: `ZombieVillagerRebuildXpFix`.
void zombieVillagerXp(NbtCompound& villager, const Context&) {
    if (real(villager.find("Xp")).has_value()) {
        return;
    }
    const NbtCompound* data = villager.getCompound("VillagerData");
    const std::optional<std::int64_t> level = data == nullptr ? std::nullopt : whole(data->find("level"));
    villager.putInt("Xp", minXpOfLevel(level.has_value() ? static_cast<std::int32_t>(*level) : 1));
}

// parity: `RemoveGolemGossipFix`.
void removeGolemGossip(NbtCompound& villager, const Context&) {
    NbtList* gossips = listAt(villager, "Gossips");
    if (gossips == nullptr) {
        return;
    }
    std::vector<NbtTag> kept;
    for (const NbtTag& gossip : *gossips) {
        const NbtCompound* fields = gossip.get<NbtCompound>();
        if (fields == nullptr || fields->getString("Type") != "golem") {
            kept.push_back(gossip);
        }
    }
    *gossips = NbtList{kept.empty() ? NbtType::End : (*gossips).elementType(), std::move(kept)};
}

constexpr std::string_view kIllagerBanner = "\"translate\":\"block.minecraft.illager_banner\"";
constexpr std::string_view kOminousBanner = "\"translate\":\"block.minecraft.ominous_banner\"";

void replaceBannerName(std::string& text) {
    for (std::size_t at = text.find(kIllagerBanner); at != std::string::npos; at = text.find(kIllagerBanner, at)) {
        text.replace(at, kIllagerBanner.size(), kOminousBanner);
        at += kOminousBanner.size();
    }
}

// parity: `OminousBannerRenameFix`.
void ominousBannerItem(NbtCompound& stack, const Context&) {
    if (namespaced(stack.getString("id")) != "minecraft:white_banner") {
        return;
    }
    NbtCompound* tag = compoundAt(stack, "tag");
    NbtCompound* display = tag == nullptr ? nullptr : compoundAt(*tag, "display");
    NbtTag* name = display == nullptr ? nullptr : display->find("Name");
    if (std::string* text = name == nullptr ? nullptr : name->get<std::string>(); text != nullptr) {
        replaceBannerName(*text);
    }
}

// parity: `OminousBannerBlockEntityRenameFix`.
void ominousBannerBlockEntity(NbtCompound& banner, const Context&) {
    NbtTag* name = banner.find("CustomName");
    if (std::string* text = name == nullptr ? nullptr : name->get<std::string>(); text != nullptr) {
        replaceBannerName(*text);
    }
}

// parity: `FurnaceRecipeFix`, the numbered recipe slots as a map of recipe to amount.
void furnaceRecipesUsed(NbtCompound& furnace, const Context&) {
    const std::string id = namespaced(furnace.getString("id"));
    if (id != "minecraft:furnace" && id != "minecraft:blast_furnace" && id != "minecraft:smoker") {
        return;
    }
    const std::int32_t size = furnace.getInt("RecipesUsedSize");
    furnace.remove("RecipesUsedSize");
    NbtCompound used;
    for (std::int32_t i = 0; i < size; ++i) {
        const std::string location = "RecipeLocation" + std::to_string(i);
        const std::string amount = "RecipeAmount" + std::to_string(i);
        const NbtTag* recipe = furnace.find(location);
        const std::string* name = recipe == nullptr ? nullptr : recipe->get<std::string>();
        const std::int32_t count = static_cast<std::int32_t>(whole(furnace.find(amount)).value_or(0));
        if (count > 0 && name != nullptr) {
            used.putInt(namespaced(*name), count);
        }
        furnace.remove(location);
        furnace.remove(amount);
    }
    furnace.set("RecipesUsed", NbtTag::ofCompound(std::move(used)));
}

// parity: `LodestoneCompassComponentFix`.
void lodestoneTracker(NbtCompound& components, const Context&) {
    NbtTag* held = components.find("minecraft:lodestone_target");
    NbtCompound* old = held == nullptr ? nullptr : held->get<NbtCompound>();
    if (old == nullptr) {
        return;
    }
    NbtCompound tracker = *old;
    const NbtTag* pos = tracker.find("pos");
    const NbtTag* dimension = tracker.find("dimension");
    if (pos != nullptr && dimension != nullptr) {
        NbtCompound target;
        target.set("pos", *pos);
        target.set("dimension", *dimension);
        tracker.remove("pos");
        tracker.remove("dimension");
        tracker.set("target", NbtTag::ofCompound(std::move(target)));
    } else {
        tracker.remove("pos");
        tracker.remove("dimension");
    }
    components.remove("minecraft:lodestone_target");
    components.set("minecraft:lodestone_tracker", NbtTag::ofCompound(std::move(tracker)));
}

// parity: `LegacyBlockPropertyRenameAndFix` for the test block mode.
void testBlockMode(NbtCompound& state, const Context&) {
    if (state.getString("Name") != "minecraft:test_block") {
        return;
    }
    NbtCompound* properties = compoundAt(state, "Properties");
    if (properties != nullptr) {
        rename(*properties, "test_block_mode", "mode");
    }
}

[[nodiscard]] bool isBed(const NbtCompound* data) {
    return data != nullptr && namespaced(data->getString("id")) == "minecraft:bed";
}

// parity: `RemoveBlockEntityTagFix` for beds, in an item.
void bedDataOfItem(NbtCompound& components, const Context&) {
    if (isBed(components.getCompound("minecraft:block_entity_data"))) {
        components.remove("minecraft:block_entity_data");
    }
}

// parity: `RemoveBlockEntityTagFix` for beds, in a falling block.
void bedDataOfFallingBlock(NbtCompound& entity, const Context&) {
    if (isBed(entity.getCompound("TileEntityData"))) {
        entity.remove("TileEntityData");
    }
}

// parity: `RemoveBlockEntityTagFix` for beds, in the block entities of a chunk.
void bedBlockEntities(NbtCompound& chunk, const Context&) {
    NbtList* list = listAt(chunk, "block_entities");
    if (list == nullptr) {
        return;
    }
    std::vector<NbtTag> kept;
    for (const NbtTag& blockEntity : *list) {
        if (!isBed(blockEntity.get<NbtCompound>())) {
            kept.push_back(blockEntity);
        }
    }
    *list = NbtList{kept.empty() ? NbtType::End : list->elementType(), std::move(kept)};
}

[[nodiscard]] bool isDroppedBlockEntity(const NbtCompound* data) {
    if (data == nullptr) {
        return false;
    }
    const std::string id = namespaced(data->getString("id"));
    return id == "minecraft:noteblock" || id == "minecraft:flower_pot";
}

// parity: `RemoveBlockEntityTagFix` for note blocks and flower pots, in an item.
void flattenedBlockDataOfItem(NbtCompound& stack, const Context&) {
    NbtCompound* tag = compoundAt(stack, "tag");
    if (tag != nullptr && isDroppedBlockEntity(tag->getCompound("BlockEntityTag"))) {
        tag->remove("BlockEntityTag");
    }
}

// parity: `RemoveBlockEntityTagFix` for note blocks and flower pots, in a falling block.
void flattenedBlockDataOfFallingBlock(NbtCompound& entity, const Context&) {
    if (isDroppedBlockEntity(entity.getCompound("TileEntityData"))) {
        entity.remove("TileEntityData");
    }
}

// parity: `RemoveBlockEntityTagFix` for note blocks and flower pots, in a structure template.
void flattenedBlockDataOfStructure(NbtCompound& chunk, const Context&) {
    NbtList* list = listAt(chunk, "block_entities");
    if (list == nullptr) {
        return;
    }
    std::vector<NbtTag> kept;
    for (const NbtTag& blockEntity : *list) {
        if (!isDroppedBlockEntity(blockEntity.get<NbtCompound>())) {
            kept.push_back(blockEntity);
        }
    }
    *list = NbtList{kept.empty() ? NbtType::End : list->elementType(), std::move(kept)};
}

[[nodiscard]] std::string attributeRenamed(std::string_view name) {
    return name == "minecraft:nameplate_distance" ? "minecraft:name_tag_distance" : std::string{name};
}

// parity: `RenameNameplateToNameTagFix`, over the modifiers of an item.
void nameTagDistanceOfItem(NbtCompound& components, const Context&) {
    NbtList* modifiers = listAt(components, "minecraft:attribute_modifiers");
    for (std::size_t i = 0; modifiers != nullptr && i < modifiers->size(); ++i) {
        NbtCompound* modifier = (*modifiers)[i].get<NbtCompound>();
        const std::string* type = modifier == nullptr || modifier->find("type") == nullptr
                                      ? nullptr
                                      : modifier->find("type")->get<std::string>();
        if (type != nullptr) {
            modifier->putString("type", attributeRenamed(*type));
        }
    }
}

// parity: `RenameNameplateToNameTagFix`, over the attributes of an entity or a player.
void nameTagDistanceOfEntity(NbtCompound& entity, const Context&) {
    forEachCompound(entity, "attributes", [](NbtCompound& attribute) {
        const std::string* id = attribute.find("id") == nullptr ? nullptr : attribute.find("id")->get<std::string>();
        if (id != nullptr) {
            attribute.putString("id", attributeRenamed(*id));
        }
    });
}

// parity: `ItemLoreFix`.
void loreAsComponents(NbtCompound& stack, const Context&) {
    NbtCompound* tag = compoundAt(stack, "tag");
    NbtCompound* display = tag == nullptr ? nullptr : compoundAt(*tag, "display");
    NbtList* lore = display == nullptr ? nullptr : listAt(*display, "Lore");
    for (std::size_t i = 0; lore != nullptr && i < lore->size(); ++i) {
        if (const std::string* line = (*lore)[i].get<std::string>(); line != nullptr) {
            (*lore)[i] = NbtTag::ofString(plainTextJson(*line));
        }
    }
}

[[nodiscard]] std::int64_t blockStateIndex(std::span<const std::int64_t> packed, std::size_t index, std::size_t bits) {
    const std::size_t bit = index * bits;
    const std::size_t word = bit >> 6;
    const std::size_t offset = bit & 63;
    const std::uint64_t mask = (std::uint64_t{1} << bits) - 1;
    std::uint64_t value = static_cast<std::uint64_t>(packed[word]) >> offset;
    if (offset + bits > 64 && word + 1 < packed.size()) {
        value |= static_cast<std::uint64_t>(packed[word + 1]) << (64 - offset);
    }
    return static_cast<std::int64_t>(value & mask);
}

// parity: `TrappedChestBlockEntityFix`.
void trappedChests(NbtCompound& chunk, const Context&) {
    NbtCompound* level = compoundAt(chunk, "Level");
    const NbtList* sections = level == nullptr ? nullptr : level->getList("Sections");
    if (sections == nullptr) {
        return;
    }
    std::vector<std::int32_t> locations;
    for (const NbtTag& held : *sections) {
        const NbtCompound* section = held.get<NbtCompound>();
        const NbtList* palette = section == nullptr ? nullptr : section->getList("Palette");
        if (palette == nullptr || section->getLongArray("BlockStates").empty()) {
            continue;
        }
        std::vector<bool> chest(palette->size(), false);
        bool any = false;
        for (std::size_t i = 0; i < palette->size(); ++i) {
            const NbtCompound* entry = (*palette)[i].get<NbtCompound>();
            chest[i] = entry != nullptr && entry->getString("Name") == "minecraft:trapped_chest";
            any = any || chest[i];
        }
        if (!any) {
            continue;
        }
        std::size_t bits = 4;
        while ((std::size_t{1} << bits) < palette->size()) {
            ++bits;
        }
        const std::span<const std::int64_t> packed = section->getLongArray("BlockStates");
        for (std::size_t i = 0; i < 4096 && ((i * bits) >> 6) < packed.size(); ++i) {
            const auto block = static_cast<std::size_t>(blockStateIndex(packed, i, bits));
            if (block < chest.size() && chest[block]) {
                locations.push_back(static_cast<std::int32_t>(section->getByte("Y")) << 12 |
                                    static_cast<std::int32_t>(i));
            }
        }
    }
    const std::int32_t chunkX = level->getInt("xPos");
    const std::int32_t chunkZ = level->getInt("zPos");
    forEachCompound(*level, "TileEntities", [&](NbtCompound& blockEntity) {
        const std::int32_t x = blockEntity.getInt("x") - (chunkX << 4);
        const std::int32_t y = blockEntity.getInt("y");
        const std::int32_t z = blockEntity.getInt("z") - (chunkZ << 4);
        const std::int32_t index = (y << 8) | (z << 4) | x;
        if (std::ranges::find(locations, index) != locations.end()) {
            blockEntity.putString("id", "minecraft:trapped_chest");
        }
    });
}

// parity: `LegacyDimensionIdFix` for the dimension of a player.
void playerDimension(NbtCompound& player, const Context&) {
    const NbtTag* held = player.find("Dimension");
    if (held == nullptr || held->get<std::string>() != nullptr || !whole(held).has_value()) {
        return;
    }
    const std::int64_t id = *whole(held);
    player.putString("Dimension", id == -1  ? "minecraft:the_nether"
                                  : id == 1 ? "minecraft:the_end"
                                            : "minecraft:overworld");
}

// parity: `PlayerRespawnDataFix`.
void respawnData(NbtCompound& player, const Context&) {
    NbtCompound* respawn = compoundAt(player, "respawn");
    if (respawn == nullptr) {
        return;
    }
    const std::optional<double> angle = real(respawn->find("angle"));
    respawn->putString("dimension", std::string{respawn->getString("dimension", "minecraft:overworld")});
    respawn->putFloat("yaw", angle.has_value() ? static_cast<float>(*angle) : 0.0F);
    respawn->putFloat("pitch", 0.0F);
    respawn->remove("angle");
}

// parity: `NamespacedTypeRenameFix` over the biome palettes of a chunk, for the removed deep warm ocean.
void deepWarmOceanOfChunk(NbtCompound& chunk, const Context&) {
    forEachCompound(chunk, "sections", [](NbtCompound& section) {
        NbtCompound* biomes = compoundAt(section, "biomes");
        NbtList* palette = biomes == nullptr ? nullptr : listAt(*biomes, "palette");
        for (std::size_t i = 0; palette != nullptr && i < palette->size(); ++i) {
            const std::string* name = (*palette)[i].get<std::string>();
            if (name != nullptr && namespaced(*name) == "minecraft:deep_warm_ocean") {
                (*palette)[i] = NbtTag::ofString("minecraft:warm_ocean");
            }
        }
    });
}

// parity: `BlockEntityJukeboxFix`, which keeps the old `Record` as vanilla's discarded `remove` does.
void jukeboxRecord(NbtCompound& jukebox, const Context&) {
    const std::int32_t record = jukebox.getInt("Record");
    if (record <= 0) {
        return;
    }
    const auto named =
        std::ranges::find_if(legacyIds::kItemNames, [record](const auto& entry) { return entry.first == record; });
    if (named == legacyIds::kItemNames.end()) {
        return;
    }
    const std::string key = std::string{named->second} + ".0";
    const auto flat = std::ranges::find_if(flattening::kItems, [&key](const auto& pair) { return pair.from == key; });
    if (flat == flattening::kItems.end()) {
        return;
    }
    NbtCompound stack;
    stack.putString("id", std::string{flat->to});
    stack.putByte("Count", 1);
    jukebox.set("RecordItem", NbtTag::ofCompound(std::move(stack)));
}

// parity: `VillagerTradeFix`.
void carvedPumpkinTrades(NbtCompound& villager, const Context&) {
    NbtCompound* offers = compoundAt(villager, "Offers");
    if (offers == nullptr) {
        return;
    }
    forEachCompound(*offers, "Recipes", [](NbtCompound& trade) {
        for (const std::string_view key : {"buy", "buyB", "sell"}) {
            NbtCompound* stack = compoundAt(trade, key);
            if (stack != nullptr && namespaced(stack->getString("id")) == "minecraft:carved_pumpkin") {
                stack->putString("id", "minecraft:pumpkin");
            }
        }
    });
}

struct SpawnerConfig {
    std::string_view key;
    std::string normal;
    std::string ominous;
};

[[nodiscard]] NbtCompound parsed(const std::string& text) {
    std::expected<NbtTag, wire::nbt::NbtError> tag = wire::nbt::snbt::parse(text);
    const NbtCompound* compound = tag.has_value() ? tag->get<NbtCompound>() : nullptr;
    return compound == nullptr ? NbtCompound{} : *compound;
}

constexpr std::string_view kEjectLoot =
    "loot_tables_to_eject: [{data: \"minecraft:spawners/ominous/trial_chamber/key\", weight: 3}, "
    "{data: \"minecraft:spawners/ominous/trial_chamber/consumables\", weight: 7}]";

[[nodiscard]] std::string normalConfig(std::string_view simultaneous, std::string_view perPlayer,
                                       std::string_view potentials, std::string_view ticks) {
    return "{simultaneous_mobs: " + std::string{simultaneous} +
           ", simultaneous_mobs_added_per_player: " + std::string{perPlayer} +
           ", spawn_potentials: " + std::string{potentials} + ", ticks_between_spawn: " + std::string{ticks} + "}";
}

[[nodiscard]] std::string singleMob(std::string_view entity) {
    return "[{data: {entity: " + std::string{entity} + "}, weight: 1}]";
}

[[nodiscard]] std::string equipped(std::string_view entity, std::string_view table) {
    return "[{data: {entity: " + std::string{entity} + ", equipment: {loot_table: \"minecraft:equipment/" +
           std::string{table} + "\", slot_drop_chances: 0.0f}}, weight: 1}]";
}

[[nodiscard]] std::string ominousWith(std::string_view extra) {
    return "{" + std::string{kEjectLoot} + (extra.empty() ? "" : ", " + std::string{extra}) + "}";
}

[[nodiscard]] const std::vector<SpawnerConfig>& spawnerConfigs() {
    static const std::vector<SpawnerConfig> kConfigs = [] {
        std::vector<SpawnerConfig> all;
        const auto mob = [](std::string_view id) { return "{id: \"minecraft:" + std::string{id} + "\"}"; };
        all.push_back({"trial_chamber/breeze",
                       "{simultaneous_mobs: 1.0f, simultaneous_mobs_added_per_player: 0.5f, spawn_potentials: " +
                           singleMob(mob("breeze")) +
                           ", ticks_between_spawn: 20, total_mobs: 2.0f, "
                           "total_mobs_added_per_player: 1.0f}",
                       ominousWith("simultaneous_mobs: 2.0f, total_mobs: 4.0f")});
        const auto add = [&](std::string_view key, std::string_view simultaneous, std::string_view perPlayer,
                             std::string_view ticks, const std::string& entity, std::string_view table,
                             std::string_view lessEquipment) {
            all.push_back({key, normalConfig(simultaneous, perPlayer, singleMob(entity), ticks),
                           table.empty() ? ominousWith(lessEquipment)
                                         : ominousWith("spawn_potentials: " + equipped(entity, table))});
        };
        add("trial_chamber/melee/husk", "3.0f", "0.5f", "20", mob("husk"), "trial_chamber_melee", "");
        add("trial_chamber/melee/spider", "3.0f", "0.5f", "20", mob("spider"), "",
            "simultaneous_mobs: 4.0f, total_mobs: 12.0f");
        add("trial_chamber/melee/zombie", "3.0f", "0.5f", "20", mob("zombie"), "trial_chamber_melee", "");
        add("trial_chamber/ranged/poison_skeleton", "3.0f", "0.5f", "20", mob("bogged"), "trial_chamber_ranged", "");
        add("trial_chamber/ranged/skeleton", "3.0f", "0.5f", "20", mob("skeleton"), "trial_chamber_ranged", "");
        add("trial_chamber/ranged/stray", "3.0f", "0.5f", "20", mob("stray"), "trial_chamber_ranged", "");
        add("trial_chamber/slow_ranged/poison_skeleton", "4.0f", "2.0f", "160", mob("bogged"), "trial_chamber_ranged",
            "");
        add("trial_chamber/slow_ranged/skeleton", "4.0f", "2.0f", "160", mob("skeleton"), "trial_chamber_ranged", "");
        add("trial_chamber/slow_ranged/stray", "4.0f", "2.0f", "160", mob("stray"), "trial_chamber_ranged", "");
        add("trial_chamber/small_melee/baby_zombie", "2.0f", "0.5f", "20", "{IsBaby: 1b, id: \"minecraft:zombie\"}",
            "trial_chamber_melee", "");
        add("trial_chamber/small_melee/cave_spider", "3.0f", "0.5f", "20", mob("cave_spider"), "",
            "simultaneous_mobs: 4.0f, total_mobs: 12.0f");
        add("trial_chamber/small_melee/silverfish", "3.0f", "0.5f", "20", mob("silverfish"), "",
            "simultaneous_mobs: 4.0f, total_mobs: 12.0f");
        all.push_back({"trial_chamber/small_melee/slime",
                       normalConfig("3.0f", "0.5f",
                                    "[{data: {entity: {Size: 1, id: \"minecraft:slime\"}}, weight: 3}, "
                                    "{data: {entity: {Size: 2, id: \"minecraft:slime\"}}, weight: 1}]",
                                    "20"),
                       ominousWith("simultaneous_mobs: 4.0f, total_mobs: 12.0f")});
        return all;
    }();
    return kConfigs;
}

[[nodiscard]] NbtCompound withoutDefaults(NbtCompound tag) {
    const auto dropIf = [&tag](std::string_view key, double value) {
        if (real(tag.find(key)) == value) {
            tag.remove(key);
        }
    };
    dropIf("spawn_range", 4.0);
    dropIf("total_mobs", 6.0);
    dropIf("simultaneous_mobs", 2.0);
    dropIf("total_mobs_added_per_player", 2.0);
    dropIf("simultaneous_mobs_added_per_player", 1.0);
    dropIf("ticks_between_spawn", 40.0);
    return tag;
}

// parity: `TrialSpawnerConfigInRegistryFix`, a config equal to a vanilla one becomes its registry key.
void trialSpawnerRegistry(NbtCompound& spawner, const Context&) {
    const NbtCompound* normal = spawner.getCompound("normal_config");
    const NbtCompound* ominous = spawner.getCompound("ominous_config");
    if (normal == nullptr || ominous == nullptr) {
        return;
    }
    for (const SpawnerConfig& config : spawnerConfigs()) {
        const NbtCompound vanillaNormal = parsed(config.normal);
        const NbtCompound vanillaOminous = parsed(config.ominous);
        NbtCompound merged = vanillaNormal;
        for (std::size_t i = 0; i < vanillaOminous.size(); ++i) {
            merged.set(vanillaOminous.names()[i], vanillaOminous.values()[i]);
        }
        if (*normal == vanillaNormal &&
            (*ominous == vanillaOminous || *ominous == merged || *ominous == withoutDefaults(merged))) {
            const std::string base = "minecraft:" + std::string{config.key};
            spawner.putString("normal_config", base + "/normal");
            spawner.putString("ominous_config", base + "/ominous");
            return;
        }
    }
}

} // namespace

void addLegacyGapFixes(std::vector<Fix>& fixes) {
    constexpr std::uint32_t kAll = kTreeDomains | domainBit(Domain::Hotbar);
    fixes.insert(
        fixes.end(),
        {
            {502, Target::LegacyStack, {}, renameLegacyItem<kCookedFished, kCookedFish>, "ItemRenameFix", kAll},
            {820, Target::LegacyStack, {}, renameLegacyItem<kTotem, kTotemOfUndying>, "ItemRenameFix", kAll},
            {1500, Target::Root, {}, keepPackedOfNothing, "BlockEntityKeepPacked", kChunkOnly},
            {1451, Target::BlockEntity, "minecraft:jukebox", jukeboxRecord, "BlockEntityJukeboxFix"},
            {1451, Target::Entity, "minecraft:villager", carvedPumpkinTrades, "VillagerTradeFix"},
            {1451, Target::LegacyStack, {}, flattenedBlockDataOfItem, "RemoveBlockEntityTagFix", kAll},
            {1451, Target::Entity, "minecraft:falling_block", flattenedBlockDataOfFallingBlock,
             "RemoveBlockEntityTagFix"},
            {1451,
             Target::Root,
             {},
             flattenedBlockDataOfStructure,
             "RemoveBlockEntityTagFix",
             domainBit(Domain::Structure)},
            {1502, Target::Root, {}, recipeBook1502, "NamespacedTypeRenameFix", domainBit(Domain::Player)},
            {1510, Target::Root, {}, recipeBook1510, "NamespacedTypeRenameFix", domainBit(Domain::Player)},
            {1624, Target::Root, {}, trappedChests, "TrappedChestBlockEntityFix", kChunkOnly},
            {1803, Target::LegacyStack, {}, loreAsComponents, "ItemLoreFix", kAll},
            {1904, Target::Entity, "minecraft:ocelot", splitCat, "EntityCatSplitFix"},
            {1905, Target::Root, {}, fullChunkStatus, "ChunkStatusFix", kChunkOnly},
            {1914, Target::BlockEntity, "minecraft:chest", weaponsmithLoot, "WeaponSmithChestLootTableFix"},
            {1917, Target::Entity, "minecraft:cat", catType, "CatTypeFix"},
            {1918, Target::Entity, "minecraft:villager", villagerProfession, "VillagerDataFix"},
            {1918, Target::Entity, "minecraft:zombie_villager", villagerProfession, "VillagerDataFix"},
            {1948, Target::LegacyStack, {}, ominousBannerItem, "OminousBannerRenameFix", kAll},
            {1953, Target::BlockEntity, "minecraft:banner", ominousBannerBlockEntity,
             "OminousBannerBlockEntityRenameFix"},
            {1955, Target::Entity, "minecraft:villager", villagerLevelAndXp, "VillagerRebuildLevelAndXpFix"},
            {1955, Target::Entity, "minecraft:zombie_villager", zombieVillagerXp, "ZombieVillagerRebuildXpFix"},
            {1963, Target::Entity, "minecraft:villager", removeGolemGossip, "RemoveGolemGossipFix"},
            {2100, Target::Root, {}, sugarRecipe, "NamespacedTypeRenameFix", domainBit(Domain::Player)},
            {2501, Target::BlockEntity, {}, furnaceRecipesUsed, "FurnaceRecipeFix"},
            {2537, Target::Root, {}, playerDimension, "LegacyDimensionIdFix", domainBit(Domain::Player)},
            {2843, Target::Root, {}, deepWarmOceanOfChunk, "NamespacedTypeRenameFix", kChunkOnly},
            {3084, Target::BlockEntity, {}, gameEventsOfBlockEntity, "NamespacedTypeRenameFix"},
            {3084, Target::Entity, {}, gameEventsOfEntity, "NamespacedTypeRenameFix"},
            {3820, Target::Components, {}, lodestoneTracker, "LodestoneCompassComponentFix", kAll},
            {3938,
             Target::Root,
             {},
             keepPackedOfNothing,
             "ProjectileStoredWeaponFix",
             kChunkOnly | domainBit(Domain::Entities)},
            {4061, Target::BlockEntity, "minecraft:trial_spawner", trialSpawnerRegistry,
             "TrialSpawnerConfigInRegistryFix"},
            {4305,
             Target::Palette,
             {},
             testBlockMode,
             "LegacyBlockPropertyRenameAndFix",
             kChunkOnly | domainBit(Domain::Structure)},
            {4548, Target::Root, {}, respawnData, "PlayerRespawnDataFix", domainBit(Domain::Player)},
            {4769,
             Target::Root,
             {},
             stonecutterRecipes,
             "NamespacedTypeRenameFix",
             kChunkOnly | domainBit(Domain::Player)},
            {4885, Target::Components, {}, bedDataOfItem, "RemoveBlockEntityTagFix", kAll},
            {4885, Target::Entity, "minecraft:falling_block", bedDataOfFallingBlock, "RemoveBlockEntityTagFix"},
            {4885,
             Target::Root,
             {},
             bedBlockEntities,
             "RemoveBlockEntityTagFix",
             kChunkOnly | domainBit(Domain::Structure)},
            {4888, Target::Components, {}, nameTagDistanceOfItem, "RenameNameplateToNameTagFix", kAll},
            {4888, Target::Entity, {}, nameTagDistanceOfEntity, "RenameNameplateToNameTagFix"},
        });
}

} // namespace basalt::metamorph::datafix
