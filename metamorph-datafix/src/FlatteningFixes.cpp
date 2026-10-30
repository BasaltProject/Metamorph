#include "FixSupport.hpp"
#include "FlatteningTables.hpp"
#include "LegacyStates.hpp"
#include "RenameSteps.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>

namespace basalt::metamorph::datafix {
namespace {

using wire::nbt::NbtCompound;
using wire::nbt::NbtList;
using wire::nbt::NbtTag;
using wire::nbt::NbtType;

[[nodiscard]] NbtCompound* compoundAt(NbtCompound& parent, std::string_view key) {
    NbtTag* held = parent.find(key);
    return held == nullptr ? nullptr : held->get<NbtCompound>();
}

[[nodiscard]] NbtList* listAt(NbtCompound& parent, std::string_view key) {
    NbtTag* held = parent.find(key);
    return held == nullptr ? nullptr : held->get<NbtList>();
}

[[nodiscard]] std::int64_t whole(const NbtTag* tag, std::int64_t fallback) {
    if (tag == nullptr) {
        return fallback;
    }
    if (const auto* v = tag->get<std::int8_t>(); v != nullptr) {
        return *v;
    }
    if (const auto* v = tag->get<std::int16_t>(); v != nullptr) {
        return *v;
    }
    if (const auto* v = tag->get<std::int32_t>(); v != nullptr) {
        return *v;
    }
    if (const auto* v = tag->get<std::int64_t>(); v != nullptr) {
        return *v;
    }
    return fallback;
}

[[nodiscard]] NbtCompound& tagOf(NbtCompound& stack) {
    if (NbtCompound* tag = compoundAt(stack, "tag"); tag != nullptr) {
        return *tag;
    }
    stack.set("tag", NbtTag::ofCompound(NbtCompound{}));
    return *compoundAt(stack, "tag");
}

[[nodiscard]] std::string lowered(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

template <typename Table>
[[nodiscard]] std::unordered_map<std::string, std::string> mapOf(const Table& table) {
    std::unordered_map<std::string, std::string> map;
    for (const auto& pair : table) {
        map.emplace(std::string{pair.from}, std::string{pair.to});
    }
    return map;
}

/// A block rename step over the palette of every section of a chunk in the `Level` format.
void renamePalette(NbtCompound& chunk, const renames::Step& step) {
    NbtCompound* level = compoundAt(chunk, "Level");
    if (level == nullptr) {
        return;
    }
    forEachCompound(*level, "Sections", [&](NbtCompound& section) {
        forEachCompound(section, "Palette", [&](NbtCompound& entry) {
            const std::string name = namespaced(entry.getString("Name"));
            for (std::size_t i = 0; i < step.count; ++i) {
                if (step.pairs[i].from == name) {
                    entry.putString("Name", std::string{step.pairs[i].to});
                    return;
                }
            }
        });
    });
}

void renameItem(NbtCompound& stack, const renames::Step& step) {
    const std::string name = namespaced(stack.getString("id"));
    for (std::size_t i = 0; i < step.count; ++i) {
        if (step.pairs[i].from == name) {
            stack.putString("id", std::string{step.pairs[i].to});
            return;
        }
    }
}

// parity: `ItemStackMapIdFix`, the damage of a filled map as its map id.
void mapId(NbtCompound& stack, const Context&) {
    if (namespaced(stack.getString("id")) == "minecraft:filled_map") {
        tagOf(stack).putInt("map", static_cast<std::int32_t>(whole(stack.find("Damage"), 0)));
    }
}

// parity: `ItemStackTheFlatteningFix`, items by id and damage to items by name.
void flattenItem(NbtCompound& stack, const Context&) {
    static const std::unordered_map<std::string, std::string> kItems = mapOf(flattening::kItems);
    static const std::set<std::string, std::less<>> kKnown = [] {
        std::set<std::string, std::less<>> known;
        for (const auto& pair : flattening::kItems) {
            known.emplace(pair.from.substr(0, pair.from.rfind('.')));
        }
        return known;
    }();
    if (!stack.contains("id")) {
        return;
    }
    const std::string id = namespaced(stack.getString("id"));
    const auto data = static_cast<std::int32_t>(whole(stack.find("Damage"), 0));
    if (kKnown.contains(id)) {
        auto found = kItems.find(id + "." + std::to_string(data));
        if (found == kItems.end()) {
            found = kItems.find(id + ".0");
        }
        if (found != kItems.end()) {
            stack.putString("id", found->second);
        }
    }
    if (std::ranges::find(flattening::kDamageItems, std::string_view{id}) != flattening::kDamageItems.end()) {
        tagOf(stack).putInt("Damage", data);
    }
    stack.remove("Damage");
}

// parity: `ItemStackSpawnEggFix`, the generic egg of 1.12 that names its mob in the tag.
void genericSpawnEgg(NbtCompound& stack, const Context&) { convertSpawnEgg(stack, "minecraft:spawn_egg"); }

// parity: `EntityWolfColorFix`.
void wolfColor(NbtCompound& entity, const Context&) {
    if (namespaced(entity.getString("id")) != "minecraft:wolf" || !entity.contains("CollarColor")) {
        return;
    }
    entity.putByte("CollarColor", static_cast<std::int8_t>(15 - whole(entity.find("CollarColor"), 0)));
}

// parity: `BlockEntityBannerColorFix`.
void bannerColors(NbtCompound& blockEntity, const Context&) {
    if (namespaced(blockEntity.getString("id")) != "minecraft:banner") {
        return;
    }
    if (blockEntity.contains("Base")) {
        blockEntity.putInt("Base", static_cast<std::int32_t>(15 - whole(blockEntity.find("Base"), 0)));
    }
    forEachCompound(blockEntity, "Patterns", [](NbtCompound& pattern) {
        if (pattern.contains("Color")) {
            pattern.putInt("Color", static_cast<std::int32_t>(15 - whole(pattern.find("Color"), 0)));
        }
    });
}

[[nodiscard]] NbtTag stateTag(const legacy::State& state) {
    NbtCompound tag;
    tag.putString("Name", state.name);
    if (!state.properties.empty()) {
        NbtCompound properties;
        for (const auto& [key, value] : state.properties) {
            properties.putString(key, value);
        }
        tag.set("Properties", NbtTag::ofCompound(std::move(properties)));
    }
    return NbtTag::ofCompound(std::move(tag));
}

// parity: `BlockEntityBlockStateFix`, the block a piston carries as a block state.
void pistonState(NbtCompound& blockEntity, const Context&) {
    if (namespaced(blockEntity.getString("id")) != "minecraft:piston") {
        return;
    }
    const auto block = static_cast<std::int32_t>(whole(blockEntity.find("blockId"), 0));
    const auto data = static_cast<std::int32_t>(whole(blockEntity.find("blockData"), 0)) & 15;
    blockEntity.remove("blockId");
    blockEntity.remove("blockData");
    blockEntity.set("blockState", stateTag(legacy::stateOf(block << 4 | data)));
}

[[nodiscard]] std::int32_t blockIdOf(const NbtTag* tag) {
    if (tag == nullptr) {
        return 0;
    }
    if (const auto* name = tag->get<std::string>(); name != nullptr) {
        for (const auto& entry : flattening::kBlockIds) {
            if (entry.name == *name) {
                return entry.id;
            }
        }
        return 0;
    }
    return static_cast<std::int32_t>(whole(tag, 0));
}

/// parity: `EntityBlockStateFix.updateBlockToBlockState`.
void blockToBlockState(NbtCompound& entity, std::string_view oldField, std::string_view dataField,
                       std::string_view newField) {
    if (!entity.contains(oldField)) {
        return;
    }
    const std::int32_t block = blockIdOf(entity.find(oldField));
    const auto data = static_cast<std::int32_t>(whole(entity.find(dataField), 0)) & 15;
    entity.remove(oldField);
    entity.remove(dataField);
    entity.set(newField, stateTag(legacy::stateOf(block << 4 | data)));
}

// parity: `EntityBlockStateFix`.
void entityBlockStates(NbtCompound& entity, const Context&) {
    static const std::set<std::string_view> kProjectiles{
        "minecraft:egg",          "minecraft:ender_pearl",    "minecraft:fireball",
        "minecraft:potion",       "minecraft:small_fireball", "minecraft:snowball",
        "minecraft:wither_skull", "minecraft:xp_bottle"};
    static const std::set<std::string_view> kMinecarts{"minecraft:commandblock_minecart", "minecraft:minecart",
                                                       "minecraft:chest_minecart",        "minecraft:furnace_minecart",
                                                       "minecraft:tnt_minecart",          "minecraft:hopper_minecart",
                                                       "minecraft:spawner_minecart"};
    const std::string id = namespaced(entity.getString("id"));
    if (id == "minecraft:falling_block") {
        std::int32_t block = 0;
        if (entity.contains("Block")) {
            block = blockIdOf(entity.find("Block"));
        } else if (entity.contains("TileID")) {
            block = static_cast<std::int32_t>(whole(entity.find("TileID"), 0));
        } else {
            block = static_cast<std::int32_t>(whole(entity.find("Tile"), 0)) & 255;
        }
        const auto data = static_cast<std::int32_t>(whole(entity.find("Data"), 0)) & 15;
        entity.remove("Block");
        entity.remove("Data");
        entity.remove("TileID");
        entity.remove("Tile");
        entity.set("BlockState", stateTag(legacy::stateOf(block << 4 | data)));
    } else if (id == "minecraft:enderman") {
        blockToBlockState(entity, "carried", "carriedData", "carriedBlockState");
    } else if (id == "minecraft:arrow" || id == "minecraft:spectral_arrow") {
        blockToBlockState(entity, "inTile", "inData", "inBlockState");
    } else if (kProjectiles.contains(id)) {
        entity.remove("inTile");
    } else if (kMinecarts.contains(id)) {
        blockToBlockState(entity, "DisplayTile", "DisplayData", "DisplayState");
    }
}

// parity: `EntityItemFrameDirectionFix`.
void itemFrameDirection(NbtCompound& entity, const Context&) {
    if (namespaced(entity.getString("id")) != "minecraft:item_frame") {
        return;
    }
    switch (whole(entity.find("Facing"), 0)) {
    case 0:
        entity.putByte("Facing", 3);
        break;
    case 1:
        entity.putByte("Facing", 4);
        break;
    case 3:
        entity.putByte("Facing", 5);
        break;
    default:
        entity.putByte("Facing", 2);
        break;
    }
}

[[nodiscard]] std::string plainTextComponent(std::string_view text) {
    std::string out = "{\"text\":\"";
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\r':
            out += "\\r";
            break;
        default:
            out += c;
        }
    }
    return out + "\"}";
}

void customNameToComponent(NbtCompound& holder, std::string_view key, bool plainString = false) {
    const NbtTag* held = holder.find(key);
    const std::string* name = held == nullptr ? nullptr : held->get<std::string>();
    if (name == nullptr) {
        return;
    }
    if (name->empty()) {
        holder.remove(key);
    } else if (!plainString) {
        holder.putString(key, plainTextComponent(*name));
    }
}

// parity: `EntityCustomNameToComponentFix`.
void entityCustomName(NbtCompound& entity, const Context&) {
    customNameToComponent(entity, "CustomName",
                          namespaced(entity.getString("id")) == "minecraft:commandblock_minecart");
}

// parity: `ItemCustomNameToComponentFix`.
void itemCustomName(NbtCompound& stack, const Context&) {
    NbtCompound* tag = compoundAt(stack, "tag");
    NbtCompound* display = tag == nullptr ? nullptr : compoundAt(*tag, "display");
    if (display != nullptr) {
        customNameToComponent(*display, "Name");
    }
}

// parity: `BlockEntityCustomNameToComponentFix`.
void blockEntityCustomName(NbtCompound& blockEntity, const Context&) {
    static const std::set<std::string_view> kNameable{
        "minecraft:beacon",        "minecraft:banner",    "minecraft:brewing_stand", "minecraft:chest",
        "minecraft:trapped_chest", "minecraft:dispenser", "minecraft:dropper",       "minecraft:enchanting_table",
        "minecraft:ender_chest",   "minecraft:furnace",   "minecraft:hopper",        "minecraft:shulker_box"};
    if (blockEntity.contains("id") && !kNameable.contains(namespaced(blockEntity.getString("id")))) {
        return;
    }
    customNameToComponent(blockEntity, "CustomName");
}

// parity: `EntityPaintingMotiveFix`.
void paintingMotive(NbtCompound& entity, const Context&) {
    if (namespaced(entity.getString("id")) != "minecraft:painting" || !entity.contains("Motive")) {
        return;
    }
    static const std::map<std::string, std::string> kMotives{
        {"donkeykong", "donkey_kong"}, {"burningskull", "burning_skull"}, {"skullandroses", "skull_and_roses"}};
    const std::string motive = lowered(std::string{entity.getString("Motive")});
    const auto found = kMotives.find(motive);
    entity.putString("Motive", namespaced(found == kMotives.end() ? motive : found->second));
}

// parity: `ChunkToProtochunkFix`.
void chunkToProtochunk(NbtCompound& chunk, const Context&) {
    NbtCompound* level = compoundAt(chunk, "Level");
    if (level == nullptr) {
        return;
    }
    const bool terrainPopulated = level->getBoolean("TerrainPopulated");
    const bool lightPopulated = !level->contains("LightPopulated") || level->getBoolean("LightPopulated");
    level->putString("Status", terrainPopulated ? (lightPopulated ? "mobs_spawned" : "decorated") : "carved");
    level->putBoolean("hasLegacyStructureData", true);
    if (NbtTag* biomes = level->find("Biomes"); biomes != nullptr) {
        if (const auto* bytes = biomes->get<std::vector<std::int8_t>>(); bytes != nullptr) {
            std::vector<std::int32_t> converted(256, 0);
            for (std::size_t i = 0; i < converted.size() && i < bytes->size(); ++i) {
                converted[i] = static_cast<std::uint8_t>((*bytes)[i]);
            }
            *biomes = NbtTag::ofIntArray(std::move(converted));
        }
    }
    if (const NbtList* ticks = listAt(*level, "TileTicks"); ticks != nullptr) {
        std::vector<NbtTag> perSection;
        std::vector<std::vector<NbtTag>> grouped(16);
        for (const NbtTag& entry : *ticks) {
            const NbtCompound* tick = entry.get<NbtCompound>();
            if (tick == nullptr) {
                continue;
            }
            const int x = static_cast<int>(whole(tick->find("x"), 0));
            const int y = static_cast<int>(whole(tick->find("y"), 0));
            const int z = static_cast<int>(whole(tick->find("z"), 0));
            const auto packed = static_cast<std::int16_t>((x & 15) | ((y & 15) << 4) | ((z & 15) << 8));
            if ((y >> 4) >= 0 && (y >> 4) < 16) {
                grouped[static_cast<std::size_t>(y >> 4)].push_back(NbtTag::ofShort(packed));
            }
        }
        for (auto& shorts : grouped) {
            perSection.push_back(
                NbtTag::ofList(NbtList{shorts.empty() ? NbtType::End : NbtType::Short, std::move(shorts)}));
        }
        level->remove("TileTicks");
        level->set("ToBeTicked", NbtTag::ofList(NbtList{NbtType::List, std::move(perSection)}));
    }
}

// parity: `ColorlessShulkerEntityFix`.
void colorlessShulker(NbtCompound& entity, const Context&) {
    if (namespaced(entity.getString("id")) == "minecraft:shulker" && whole(entity.find("Color"), 0) == 10) {
        entity.putByte("Color", 16);
    }
}

[[nodiscard]] std::string shulkerBox(const std::string& name) {
    return name == "minecraft:purple_shulker_box" ? "minecraft:shulker_box" : name;
}

void colorlessShulkerBlock(NbtCompound& chunk, const Context&) {
    NbtCompound* level = compoundAt(chunk, "Level");
    if (level == nullptr) {
        return;
    }
    forEachCompound(*level, "Sections", [](NbtCompound& section) {
        forEachCompound(section, "Palette", [](NbtCompound& entry) {
            entry.putString("Name", shulkerBox(namespaced(entry.getString("Name"))));
        });
    });
}

void colorlessShulkerItem(NbtCompound& stack, const Context&) {
    stack.putString("id", shulkerBox(namespaced(stack.getString("id"))));
}

// parity: `HeightmapRenamingFix`.
void heightmapNames(NbtCompound& chunk, const Context&) {
    NbtCompound* level = compoundAt(chunk, "Level");
    NbtCompound* maps = level == nullptr ? nullptr : compoundAt(*level, "Heightmaps");
    if (maps == nullptr) {
        return;
    }
    const auto copy = [&](std::string_view from, std::initializer_list<std::string_view> to) {
        const NbtTag* held = maps->find(from);
        if (held == nullptr) {
            return;
        }
        const NbtTag value = *held;
        maps->remove(from);
        for (const std::string_view name : to) {
            maps->set(name, value);
        }
    };
    copy("LIQUID", {"WORLD_SURFACE_WG"});
    copy("SOLID", {"OCEAN_FLOOR_WG", "OCEAN_FLOOR"});
    copy("LIGHT", {"LIGHT_BLOCKING"});
    copy("RAIN", {"MOTION_BLOCKING", "MOTION_BLOCKING_NO_LEAVES"});
}

// parity: `ItemStackEnchantmentNamesFix`.
void enchantmentNames(NbtCompound& stack, const Context&) {
    NbtCompound* tag = compoundAt(stack, "tag");
    if (tag == nullptr) {
        return;
    }
    const auto nameOf = [](std::int64_t id) -> std::string {
        for (const auto& entry : flattening::kEnchantments) {
            if (entry.id == id) {
                return std::string{entry.name};
            }
        }
        return "null";
    };
    const auto convert = [&](NbtList& list) {
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (NbtCompound* element = list[i].get<NbtCompound>(); element != nullptr) {
                element->putString("id", nameOf(whole(element->find("id"), 0)));
            }
        }
    };
    if (NbtList* old = listAt(*tag, "ench"); old != nullptr) {
        convert(*old);
        rename(*tag, "ench", "Enchantments");
    }
    if (NbtList* stored = listAt(*tag, "StoredEnchantments"); stored != nullptr) {
        convert(*stored);
    }
}

// parity: `EntityTheRenameningFix`, `EntityPufferfishRenameFix` and `EntityCodSalmonFix`.
void entityRenames(NbtCompound& entity, const Context&) {
    static const std::unordered_map<std::string, std::string> kRenames = mapOf(flattening::kEntityRenames);
    static const std::unordered_map<std::string, std::string> kCodSalmon = mapOf(flattening::kCodSalmonEntities);
    std::string id = namespaced(entity.getString("id"));
    if (id == "minecraft:puffer_fish") {
        id = "minecraft:pufferfish";
    }
    if (const auto found = kCodSalmon.find(id); found != kCodSalmon.end()) {
        id = found->second;
    }
    if (id.starts_with("minecraft:bred_")) {
        id = "minecraft:" + id.substr(15);
    }
    if (const auto found = kRenames.find(id); found != kRenames.end()) {
        id = found->second;
    }
    if (entity.contains("id")) {
        entity.putString("id", id);
    }
}

void renameItemsFrom(NbtCompound& stack, const std::unordered_map<std::string, std::string>& renames) {
    const auto found = renames.find(namespaced(stack.getString("id")));
    if (found != renames.end()) {
        stack.putString("id", found->second);
    }
}

// parity: `ItemRenameFix` of the great renaming of 1.13.
void renamingOfItems(NbtCompound& stack, const Context&) {
    static const std::unordered_map<std::string, std::string> kRenames = mapOf(flattening::kItemRenames);
    renameItemsFrom(stack, kRenames);
}

void renamingOfEggs(NbtCompound& stack, const Context&) {
    static const std::unordered_map<std::string, std::string> kEggs = [] {
        auto eggs = mapOf(flattening::kCodSalmonEggs);
        eggs.emplace("minecraft:puffer_fish_spawn_egg", "minecraft:pufferfish_spawn_egg");
        return eggs;
    }();
    renameItemsFrom(stack, kEggs);
}

// parity: `ChunkStatusFix` and `ChunkStatusFix2`.
void chunkStatus(NbtCompound& chunk, const Context&) {
    NbtCompound* level = compoundAt(chunk, "Level");
    if (level == nullptr) {
        return;
    }
    std::string status{level->contains("Status") ? level->getString("Status") : "empty"};
    if (status == "postprocessed") {
        status = "fullchunk";
    }
    static const std::map<std::string, std::string> kStatuses{
        {"structure_references", "empty"},   {"biomes", "empty"},       {"base", "surface"},  {"carved", "carvers"},
        {"liquid_carved", "liquid_carvers"}, {"decorated", "features"}, {"lighted", "light"}, {"mobs_spawned", "spawn"},
        {"finalized", "heightmaps"},         {"fullchunk", "full"}};
    const auto found = kStatuses.find(status);
    level->putString("Status", found == kStatuses.end() ? "empty" : found->second);
}

// parity: `ChunkLightRemoveFix`.
void removeLightFlag(NbtCompound& chunk, const Context&) {
    if (NbtCompound* level = compoundAt(chunk, "Level"); level != nullptr) {
        level->remove("isLightOn");
    }
}

// parity: `ChunkBiomeFix`, the 256 columns of biomes as the 1024 cells of a quarter-resolution volume.
void biomeVolume(NbtCompound& chunk, const Context&) {
    NbtCompound* level = compoundAt(chunk, "Level");
    NbtTag* held = level == nullptr ? nullptr : level->find("Biomes");
    if (held == nullptr) {
        return;
    }
    std::vector<std::int32_t> old;
    if (const auto* ints = held->get<std::vector<std::int32_t>>(); ints != nullptr) {
        old = *ints;
    } else if (const auto* bytes = held->get<std::vector<std::int8_t>>(); bytes != nullptr) {
        old.assign(bytes->begin(), bytes->end());
    }
    if (old.size() != 256) {
        return;
    }
    std::vector<std::int32_t> volume(1024, 0);
    for (int z = 0; z < 4; ++z) {
        for (int x = 0; x < 4; ++x) {
            volume[static_cast<std::size_t>(z << 2 | x)] =
                old[static_cast<std::size_t>(((z << 2) + 2) << 4 | ((x << 2) + 2))];
        }
    }
    for (int slice = 1; slice < 64; ++slice) {
        std::copy_n(volume.begin(), 16, volume.begin() + slice * 16);
    }
    *held = NbtTag::ofIntArray(std::move(volume));
}

class PackedBits {
public:
    PackedBits(int bits, std::vector<std::int64_t> data) : bits_(bits), data_(std::move(data)) {
        data_.resize(static_cast<std::size_t>((4096 * bits_ + 63) / 64), 0);
    }

    [[nodiscard]] int bits() const { return bits_; }

    [[nodiscard]] const std::vector<std::int64_t>& raw() const { return data_; }

    [[nodiscard]] int get(int index) const {
        const int bitIndex = index * bits_;
        const int startLong = bitIndex >> 6;
        const int endLong = ((index + 1) * bits_ - 1) >> 6;
        const int startBit = bitIndex & 63;
        const std::uint64_t mask = (std::uint64_t{1} << bits_) - 1;
        std::uint64_t value = static_cast<std::uint64_t>(data_[static_cast<std::size_t>(startLong)]) >> startBit;
        if (startLong != endLong) {
            value |= static_cast<std::uint64_t>(data_[static_cast<std::size_t>(endLong)]) << (64 - startBit);
        }
        return static_cast<int>(value & mask);
    }

    void set(int index, int value) {
        const int bitIndex = index * bits_;
        const int startLong = bitIndex >> 6;
        const int endLong = ((index + 1) * bits_ - 1) >> 6;
        const int startBit = bitIndex & 63;
        const std::uint64_t mask = (std::uint64_t{1} << bits_) - 1;
        auto& first = data_[static_cast<std::size_t>(startLong)];
        first = static_cast<std::int64_t>((static_cast<std::uint64_t>(first) & ~(mask << startBit)) |
                                          ((static_cast<std::uint64_t>(value) & mask) << startBit));
        if (startLong != endLong) {
            auto& second = data_[static_cast<std::size_t>(endLong)];
            const int shift = 64 - startBit;
            second = static_cast<std::int64_t>((static_cast<std::uint64_t>(second) & ~(mask >> shift)) |
                                               ((static_cast<std::uint64_t>(value) & mask) >> shift));
        }
    }

private:
    int bits_;
    std::vector<std::int64_t> data_;
};

[[nodiscard]] int ceilLog2(int value) {
    return value == 0 ? 0 : static_cast<int>(std::ceil(std::log(value) / std::log(2.0)));
}

const std::map<std::string, int> kLeaves{{"minecraft:acacia_leaves", 0},   {"minecraft:birch_leaves", 1},
                                         {"minecraft:dark_oak_leaves", 2}, {"minecraft:jungle_leaves", 3},
                                         {"minecraft:oak_leaves", 4},      {"minecraft:spruce_leaves", 5}};
const std::set<std::string> kLogs{"minecraft:acacia_bark",
                                  "minecraft:birch_bark",
                                  "minecraft:dark_oak_bark",
                                  "minecraft:jungle_bark",
                                  "minecraft:oak_bark",
                                  "minecraft:spruce_bark",
                                  "minecraft:acacia_log",
                                  "minecraft:birch_log",
                                  "minecraft:dark_oak_log",
                                  "minecraft:jungle_log",
                                  "minecraft:oak_log",
                                  "minecraft:spruce_log",
                                  "minecraft:stripped_acacia_log",
                                  "minecraft:stripped_birch_log",
                                  "minecraft:stripped_dark_oak_log",
                                  "minecraft:stripped_jungle_log",
                                  "minecraft:stripped_oak_log",
                                  "minecraft:stripped_spruce_log"};

/// parity: `LeavesFix.LeavesSection`, one section of a chunk with the leaves and logs found in its palette.
class LeavesSection {
public:
    explicit LeavesSection(NbtCompound& section) : section_(section) {
        index = static_cast<int>(whole(section.find("Y"), 0));
        if (const NbtTag* held = section.find("Palette"); held != nullptr && held->get<NbtList>() != nullptr) {
            for (const NbtTag& entry : *held->get<NbtList>()) {
                palette_.push_back(entry.get<NbtCompound>() != nullptr ? *entry.get<NbtCompound>() : NbtCompound{});
            }
        }
        skippable_ = classify();
        if (!skippable_) {
            std::vector<std::int64_t> states;
            if (const NbtTag* held = section.find("BlockStates");
                held != nullptr && held->get<std::vector<std::int64_t>>() != nullptr) {
                states = *held->get<std::vector<std::int64_t>>();
            }
            storage_.emplace(std::max(4, ceilLog2(static_cast<int>(palette_.size()))), std::move(states));
        }
    }

    [[nodiscard]] bool skippable() const { return skippable_; }

    [[nodiscard]] int block(int position) const { return storage_->get(position); }

    [[nodiscard]] bool isLog(int block) const { return logIds_.contains(block); }

    [[nodiscard]] bool isLeaf(int block) const { return leafIds_.contains(block); }

    [[nodiscard]] int distanceOf(int block) const {
        if (isLog(block)) {
            return 0;
        }
        const NbtCompound* properties = palette_[static_cast<std::size_t>(block)].getCompound("Properties");
        return properties == nullptr ? 0 : std::atoi(std::string{properties->getString("distance")}.c_str());
    }

    void setDistance(int position, int block, int distance) {
        const NbtCompound base = palette_[static_cast<std::size_t>(block)];
        const std::string name{base.getString("Name")};
        const NbtCompound* properties = base.getCompound("Properties");
        const bool persistent = properties != nullptr && properties->getString("persistent") == "true";
        const int stateId = stateIdOf(name, persistent, distance);
        if (!stateToId_.contains(stateId)) {
            const int id = static_cast<int>(palette_.size());
            leafIds_.insert(id);
            stateToId_[stateId] = id;
            palette_.push_back(leafTag(name, persistent, distance));
        }
        const int id = stateToId_[stateId];
        if ((1 << storage_->bits()) <= id) {
            PackedBits wider{storage_->bits() + 1, {}};
            for (int i = 0; i < 4096; ++i) {
                wider.set(i, storage_->get(i));
            }
            storage_.emplace(std::move(wider));
        }
        storage_->set(position, id);
    }

    void write() {
        if (skippable_) {
            return;
        }
        section_.set("BlockStates", NbtTag::ofLongArray(storage_->raw()));
        std::vector<NbtTag> entries;
        for (const NbtCompound& entry : palette_) {
            entries.push_back(NbtTag::ofCompound(entry));
        }
        section_.set("Palette", NbtTag::ofList(NbtList{NbtType::Compound, std::move(entries)}));
    }

    int index = 0;

private:
    static int stateIdOf(const std::string& name, bool persistent, int distance) {
        return kLeaves.at(name) << 5 | (persistent ? 16 : 0) | distance;
    }

    static NbtCompound leafTag(const std::string& name, bool persistent, int distance) {
        NbtCompound properties;
        properties.putString("persistent", persistent ? "true" : "false");
        properties.putString("distance", std::to_string(distance));
        NbtCompound tag;
        tag.set("Properties", NbtTag::ofCompound(std::move(properties)));
        tag.putString("Name", name);
        return tag;
    }

    bool classify() {
        for (std::size_t i = 0; i < palette_.size(); ++i) {
            const std::string name{palette_[i].getString("Name")};
            if (kLeaves.contains(name)) {
                const NbtCompound* properties = palette_[i].getCompound("Properties");
                const bool persistent = properties != nullptr && properties->getString("decayable") == "false";
                leafIds_.insert(static_cast<int>(i));
                stateToId_[stateIdOf(name, persistent, 7)] = static_cast<int>(i);
                palette_[i] = leafTag(name, persistent, 7);
            }
            if (kLogs.contains(name)) {
                logIds_.insert(static_cast<int>(i));
            }
        }
        return leafIds_.empty() && logIds_.empty();
    }

    NbtCompound& section_;
    std::vector<NbtCompound> palette_;
    std::set<int> leafIds_;
    std::set<int> logIds_;
    std::map<int, int> stateToId_;
    std::optional<PackedBits> storage_;
    bool skippable_ = true;
};

[[nodiscard]] int sideMaskOf(bool west, bool east, bool north, bool south) {
    int s = 0;
    if (north) {
        s |= east ? 2 : (west ? 128 : 1);
    } else if (south) {
        s |= west ? 32 : (east ? 8 : 16);
    } else if (east) {
        s |= 4;
    } else if (west) {
        s |= 64;
    }
    return s;
}

// parity: `LeavesFix`, the distance leaves keep to their nearest log, found from the logs outwards.
void leavesDistance(NbtCompound& chunk, const Context&) {
    NbtCompound* level = compoundAt(chunk, "Level");
    NbtList* sections = level == nullptr ? nullptr : listAt(*level, "Sections");
    if (sections == nullptr) {
        return;
    }
    std::map<int, std::unique_ptr<LeavesSection>> byIndex;
    for (std::size_t i = 0; i < sections->size(); ++i) {
        if (NbtCompound* section = (*sections)[i].get<NbtCompound>(); section != nullptr) {
            auto parsed = std::make_unique<LeavesSection>(*section);
            const int y = parsed->index;
            byIndex[y] = std::move(parsed);
        }
    }
    const bool allSkippable = std::ranges::all_of(byIndex, [](const auto& entry) { return entry.second->skippable(); });
    if (allSkippable) {
        return;
    }
    int sides = 0;
    std::array<std::set<int>, 7> queue;
    for (const auto& [y, section] : byIndex) {
        if (section->skippable()) {
            continue;
        }
        for (int i = 0; i < 4096; ++i) {
            const int block = section->block(i);
            if (section->isLog(block)) {
                queue[0].insert(section->index << 12 | i);
            } else if (section->isLeaf(block)) {
                const int x = i & 15;
                const int z = (i >> 4) & 15;
                sides |= sideMaskOf(x == 0, x == 15, z == 0, z == 15);
            }
        }
    }
    static constexpr std::array<std::array<int, 3>, 6> kDirections{
        {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}}};
    for (int i = 1; i < 7; ++i) {
        for (const int position : queue[static_cast<std::size_t>(i - 1)]) {
            const int x = position & 15;
            const int y = (position >> 8) & 255;
            const int z = (position >> 4) & 15;
            for (const auto& direction : kDirections) {
                const int nx = x + direction[0];
                const int ny = y + direction[1];
                const int nz = z + direction[2];
                if (nx < 0 || nx > 15 || nz < 0 || nz > 15 || ny < 0 || ny > 255) {
                    continue;
                }
                const auto found = byIndex.find(ny >> 4);
                if (found == byIndex.end() || found->second->skippable()) {
                    continue;
                }
                LeavesSection& section = *found->second;
                const int inSection = (ny & 15) << 8 | nz << 4 | nx;
                const int block = section.block(inSection);
                if (section.isLeaf(block) && section.distanceOf(block) > i) {
                    section.setDistance(inSection, block, i);
                    queue[static_cast<std::size_t>(i)].insert(ny << 8 | nz << 4 | nx);
                }
            }
        }
    }
    for (auto& [y, section] : byIndex) {
        section->write();
    }
    if (sides != 0) {
        NbtCompound upgrade;
        if (const NbtCompound* held = compoundAt(*level, "UpgradeData"); held != nullptr) {
            upgrade = *held;
        }
        upgrade.putByte("Sides", static_cast<std::int8_t>(whole(upgrade.find("Sides"), 0) | sides));
        level->set("UpgradeData", NbtTag::ofCompound(std::move(upgrade)));
    }
}

} // namespace

void addFlatteningFixes(std::vector<Fix>& fixes) {
    for (const renames::Step& step : renames::kSteps) {
        if (step.block) {
            fixes.push_back({step.version, Target::Chunk, {}, [&step](NbtCompound& chunk, const Context&) {
                                 renamePalette(chunk, step);
                             }});
        } else {
            fixes.push_back({step.version, Target::LegacyStack, {}, [&step](NbtCompound& stack, const Context&) {
                                 renameItem(stack, step);
                             }});
        }
    }
    fixes.insert(fixes.end(), {
                                  {1451, Target::LegacyStack, {}, mapId},
                                  {1451, Target::LegacyStack, {}, flattenItem},
                                  {1451, Target::LegacyStack, {}, genericSpawnEgg},
                                  {1451, Target::Entity, {}, wolfColor},
                                  {1451, Target::BlockEntity, {}, bannerColors},
                                  {1451, Target::BlockEntity, {}, pistonState},
                                  {1451, Target::Entity, {}, entityBlockStates},
                                  {1456, Target::Entity, {}, itemFrameDirection},
                                  {1458, Target::Entity, {}, entityCustomName},
                                  {1458, Target::LegacyStack, {}, itemCustomName},
                                  {1458, Target::BlockEntity, {}, blockEntityCustomName},
                                  {1460, Target::Entity, {}, paintingMotive},
                                  {1466, Target::Chunk, {}, chunkToProtochunk},
                                  {1474, Target::Entity, {}, colorlessShulker},
                                  {1474, Target::Chunk, {}, colorlessShulkerBlock},
                                  {1474, Target::LegacyStack, {}, colorlessShulkerItem},
                                  {1483, Target::LegacyStack, {}, renamingOfEggs},
                                  {1484, Target::Chunk, {}, heightmapNames},
                                  {1494, Target::LegacyStack, {}, enchantmentNames},
                                  {1496, Target::Chunk, {}, leavesDistance},
                                  {1510, Target::LegacyStack, {}, renamingOfItems},
                                  {1510, Target::Entity, {}, entityRenames},
                                  {1911, Target::Chunk, {}, chunkStatus},
                                  {1961, Target::Chunk, {}, removeLightFlag},
                                  {2202, Target::Chunk, {}, biomeVolume},
                              });
}

} // namespace basalt::metamorph::datafix
