#include "FixSupport.hpp"
#include "Tables.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>

namespace basalt::metamorph::datafix {
namespace {

using wire::nbt::NbtCompound;
using wire::nbt::NbtList;
using wire::nbt::NbtTag;
using wire::nbt::NbtType;

constexpr int kNewMinSection = -4;
constexpr int kOldSectionCount = 16;
constexpr int kNewSectionCount = 24;
constexpr int kBlocksPerSection = 4096;

[[nodiscard]] NbtCompound* compoundAt(NbtCompound& parent, std::string_view key) {
    NbtTag* held = parent.find(key);
    return held == nullptr ? nullptr : held->get<NbtCompound>();
}

[[nodiscard]] NbtList* listAt(NbtCompound& parent, std::string_view key) {
    NbtTag* held = parent.find(key);
    return held == nullptr ? nullptr : held->get<NbtList>();
}

[[nodiscard]] std::optional<std::int64_t> whole(const NbtTag* tag) {
    if (tag == nullptr) {
        return std::nullopt;
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
    return std::nullopt;
}

[[nodiscard]] NbtTag listOf(NbtType type, std::vector<NbtTag> elements) {
    return NbtTag::ofList(NbtList{elements.empty() ? NbtType::End : type, std::move(elements)});
}

[[nodiscard]] NbtTag emptyList() { return NbtTag::ofList(NbtList{NbtType::End, {}}); }

template <typename Table>
[[nodiscard]] std::optional<std::string_view> lookup(const Table& table, std::string_view name) {
    for (const tables::Pair& pair : table) {
        if (pair.from == name) {
            return pair.to;
        }
    }
    return std::nullopt;
}

void forEachOldSection(NbtCompound& chunk, const std::function<void(NbtCompound&)>& edit) {
    if (NbtCompound* level = compoundAt(chunk, "Level"); level != nullptr) {
        forEachCompound(*level, "Sections", edit);
    }
}

/// Renames blocks in the palette of every section of a chunk still in the format with a `Level`.
void renameBlocks(NbtCompound& chunk, const std::function<std::optional<std::string>(const std::string&)>& renamer) {
    forEachOldSection(chunk, [&](NbtCompound& section) {
        forEachCompound(section, "Palette", [&](NbtCompound& entry) {
            if (const auto renamed = renamer(namespaced(entry.getString("Name"))); renamed.has_value()) {
                entry.putString("Name", *renamed);
            }
        });
    });
}

template <typename Table>
[[nodiscard]] Fix blockRename(std::int32_t version, const Table& table) {
    return {version, Target::Chunk, {}, [&table](NbtCompound& chunk, const Context&) {
                renameBlocks(chunk, [&table](const std::string& name) -> std::optional<std::string> {
                    const auto found = lookup(table, name);
                    return found.has_value() ? std::optional{std::string{*found}} : std::nullopt;
                });
            }};
}

template <typename Table>
[[nodiscard]] Fix itemRename(std::int32_t version, const Table& table) {
    return {version, Target::LegacyStack, {}, [&table](NbtCompound& stack, const Context&) {
                if (const auto found = lookup(table, namespaced(stack.getString("id"))); found.has_value()) {
                    stack.putString("id", std::string{*found});
                }
            }};
}

// parity: `CauldronRenameFix`.
void cauldronRename(NbtCompound& chunk, const Context&) {
    forEachOldSection(chunk, [](NbtCompound& section) {
        forEachCompound(section, "Palette", [](NbtCompound& entry) {
            if (namespaced(entry.getString("Name")) != "minecraft:cauldron") {
                return;
            }
            const NbtCompound* properties = compoundAt(entry, "Properties");
            const std::string_view level = properties == nullptr ? "0" : properties->getString("level", "0");
            if (level == "0") {
                entry.remove("Properties");
            } else {
                entry.putString("Name", "minecraft:water_cauldron");
            }
        });
    });
}

const std::array<tables::Pair, 1> kGrassPath{{{"minecraft:grass_path", "minecraft:dirt_path"}}};
const std::array<tables::Pair, 2> kCaveVines{{{"minecraft:cave_vines_head", "minecraft:cave_vines"},
                                              {"minecraft:cave_vines_body", "minecraft:cave_vines_plant"}}};
const std::array<tables::Pair, 1> kAzalea{{{"minecraft:azalea_leaves_flowers", "minecraft:flowering_azalea_leaves"}}};

// parity: `AbstractArrowPickupFix`.
void arrowPickup(NbtCompound& entity, const Context&) {
    const std::string id = namespaced(entity.getString("id"));
    if ((id != "minecraft:arrow" && id != "minecraft:spectral_arrow" && id != "minecraft:trident") ||
        entity.contains("pickup")) {
        return;
    }
    const bool fromPlayer = entity.contains("player") ? entity.getBoolean("player") : true;
    entity.putByte("pickup", fromPlayer ? 1 : 0);
    entity.remove("player");
}

// parity: `SpawnerDataFix`.
void spawnerData(NbtCompound& holder, const Context&) {
    if (NbtTag* data = holder.find("SpawnData"); data != nullptr && data->get<NbtCompound>() != nullptr) {
        NbtCompound wrapped;
        wrapped.set("entity", *data);
        *data = NbtTag::ofCompound(std::move(wrapped));
    }
    NbtList* potentials = listAt(holder, "SpawnPotentials");
    if (potentials == nullptr) {
        return;
    }
    std::vector<NbtTag> entries;
    for (std::size_t i = 0; i < potentials->size(); ++i) {
        const NbtCompound* old = (*potentials)[i].get<NbtCompound>();
        if (old == nullptr) {
            continue;
        }
        NbtCompound inner = *old;
        const auto weight = whole(inner.find("Weight")).value_or(1);
        const NbtTag* held = inner.find("Entity");
        const std::optional<NbtTag> entity = held == nullptr ? std::nullopt : std::optional{*held};
        inner.remove("Weight");
        inner.remove("Entity");
        NbtCompound data = inner;
        if (entity.has_value()) {
            data.set("entity", *entity);
        }
        NbtCompound entry;
        entry.putInt("weight", static_cast<std::int32_t>(weight));
        entry.set("data", NbtTag::ofCompound(std::move(data)));
        entries.push_back(NbtTag::ofCompound(std::move(entry)));
    }
    holder.set("SpawnPotentials", listOf(NbtType::Compound, std::move(entries)));
}

void spawnerOfBlockEntity(NbtCompound& blockEntity, const Context& context) {
    if (namespaced(blockEntity.getString("id")) == "minecraft:mob_spawner") {
        spawnerData(blockEntity, context);
    }
}

void spawnerOfMinecart(NbtCompound& entity, const Context& context) {
    if (namespaced(entity.getString("id")) == "minecraft:spawner_minecart") {
        spawnerData(entity, context);
    }
}

[[nodiscard]] int ceilLog2(int value) {
    return value == 0 ? 0 : static_cast<int>(std::ceil(std::log(value) / std::log(2.0)));
}

[[nodiscard]] std::string_view biomeNameOf(std::int32_t id) {
    for (const tables::NumberedName& biome : tables::kBiomesById) {
        if (biome.id == id) {
            return biome.name;
        }
    }
    return "minecraft:plains";
}

[[nodiscard]] NbtTag pallettedContainer(NbtTag palette) {
    NbtCompound container;
    container.set("palette", std::move(palette));
    return NbtTag::ofCompound(std::move(container));
}

[[nodiscard]] NbtTag pallettedContainer(NbtTag palette, NbtTag storage) {
    NbtCompound container;
    container.set("palette", std::move(palette));
    container.set("data", std::move(storage));
    return NbtTag::ofCompound(std::move(container));
}

/// parity: `ChunkHeightAndBiomeFix.makeBiomeContainer`, 64 cells of a section as a palette and packed indices.
[[nodiscard]] NbtTag biomeContainer(const std::function<std::int32_t(int)>& source) {
    std::vector<std::int32_t> order;
    std::map<std::int32_t, int> indexOf;
    for (int i = 0; i < 64; ++i) {
        const std::int32_t biome = source(i);
        if (!indexOf.contains(biome)) {
            indexOf[biome] = static_cast<int>(order.size());
            order.push_back(biome);
        }
    }
    std::vector<NbtTag> palette;
    for (const std::int32_t biome : order) {
        palette.push_back(NbtTag::ofString(std::string{biomeNameOf(biome)}));
    }
    const int bits = ceilLog2(static_cast<int>(order.size()));
    NbtTag names = listOf(NbtType::String, std::move(palette));
    if (bits == 0) {
        return pallettedContainer(std::move(names));
    }
    const int valuesPerLong = 64 / bits;
    const int length = (64 + valuesPerLong - 1) / valuesPerLong;
    std::vector<std::int64_t> storage(static_cast<std::size_t>(length), 0);
    int cell = 0;
    int bit = 0;
    for (int i = 0; i < 64; ++i) {
        storage[static_cast<std::size_t>(cell)] |= static_cast<std::int64_t>(indexOf[source(i)]) << bit;
        bit += bits;
        if (bit + bits > 64) {
            ++cell;
            bit = 0;
        }
    }
    return pallettedContainer(std::move(names), NbtTag::ofLongArray(std::move(storage)));
}

[[nodiscard]] std::vector<NbtTag> biomeContainers(NbtCompound& level, bool increaseHeight, int minSection,
                                                  bool& wasIncreasedHeight) {
    std::vector<NbtTag> containers(increaseHeight ? 24 : 16);
    std::vector<std::int32_t> old;
    if (const NbtTag* held = level.find("Biomes");
        held != nullptr && held->get<std::vector<std::int32_t>>() != nullptr) {
        old = *held->get<std::vector<std::int32_t>>();
    }
    const auto biomeAt = [&old](std::size_t index) { return old[index] & 255; };
    if (old.size() == 1536) {
        wasIncreasedHeight = true;
        for (int section = 0; section < 24; ++section) {
            containers[static_cast<std::size_t>(section)] =
                biomeContainer([&](int i) { return biomeAt(static_cast<std::size_t>(section * 64 + i)); });
        }
    } else if (old.size() == 1024) {
        for (int section = 0; section < 16; ++section) {
            containers[static_cast<std::size_t>(section - minSection)] =
                biomeContainer([&](int i) { return biomeAt(static_cast<std::size_t>(section * 64 + i)); });
        }
        if (increaseHeight) {
            const NbtTag below = biomeContainer([&](int i) { return biomeAt(static_cast<std::size_t>(i % 16)); });
            const NbtTag above =
                biomeContainer([&](int i) { return biomeAt(static_cast<std::size_t>(i % 16 + 1008)); });
            for (int i = 0; i < 4; ++i) {
                containers[static_cast<std::size_t>(i)] = below;
            }
            for (int i = 20; i < 24; ++i) {
                containers[static_cast<std::size_t>(i)] = above;
            }
        }
    } else {
        const NbtTag plains = pallettedContainer(listOf(NbtType::String, {NbtTag::ofString("minecraft:plains")}));
        std::ranges::fill(containers, plains);
    }
    return containers;
}

/// parity: `ChunkHeightAndBiomeFix.makeOptimizedPalettedContainer`, a palette padded when its data needs more bits.
[[nodiscard]] NbtTag blockStateContainer(const NbtTag& palette, const NbtTag& data) {
    const NbtList* list = palette.get<NbtList>();
    if (list == nullptr || list->size() == 1) {
        return pallettedContainer(palette);
    }
    NbtList padded = *list;
    const std::vector<std::int64_t>* longs = data.get<std::vector<std::int64_t>>();
    const long long bitsInData = longs == nullptr ? 0 : static_cast<long long>(longs->size()) * 64;
    const long long estimated = bitsInData / kBlocksPerSection;
    const int expected = ceilLog2(static_cast<int>(list->size()));
    if (estimated > expected) {
        NbtCompound air;
        air.putString("Name", "minecraft:air");
        const int minimum = (1 << (estimated - 1)) + 1;
        for (int i = 0; i < minimum - static_cast<int>(list->size()); ++i) {
            padded.push(NbtTag::ofCompound(air));
        }
    }
    return pallettedContainer(NbtTag::ofList(std::move(padded)), data);
}

struct Bedrock {
    std::vector<NbtCompound> palette;
    std::vector<std::int64_t> data;

    [[nodiscard]] const NbtCompound* at(int x, int y, int z) const {
        if (palette.empty()) {
            return nullptr;
        }
        if (palette.size() == 1) {
            return &palette.front();
        }
        const int bits = std::max(4, ceilLog2(static_cast<int>(palette.size())));
        const int perLong = 64 / bits;
        const int index = (y << 4 | z) << 4 | x;
        const int cell = index / perLong;
        if (cell < 0 || cell >= static_cast<int>(data.size())) {
            return nullptr;
        }
        const int shift = (index - cell * perLong) * bits;
        const auto mask = (std::int64_t{1} << bits) - 1;
        const auto entry = static_cast<std::size_t>((data[static_cast<std::size_t>(cell)] >> shift) & mask);
        return entry < palette.size() ? &palette[entry] : nullptr;
    }
};

const std::set<std::string_view> kAfterSurface{"surface", "carvers", "liquid_carvers", "features",
                                               "light",   "spawn",   "heightmaps",     "full"};
const std::set<std::string_view> kAfterNoise{"noise", "surface",    "carvers", "liquid_carvers", "features", "light",
                                             "spawn", "heightmaps", "full"};
const std::set<std::string_view> kBeforeFeatures{"minecraft:air",           "minecraft:basalt",
                                                 "minecraft:bedrock",       "minecraft:blackstone",
                                                 "minecraft:calcite",       "minecraft:cave_air",
                                                 "minecraft:coarse_dirt",   "minecraft:crimson_nylium",
                                                 "minecraft:dirt",          "minecraft:end_stone",
                                                 "minecraft:grass_block",   "minecraft:gravel",
                                                 "minecraft:ice",           "minecraft:lava",
                                                 "minecraft:mycelium",      "minecraft:nether_wart_block",
                                                 "minecraft:netherrack",    "minecraft:orange_terracotta",
                                                 "minecraft:packed_ice",    "minecraft:podzol",
                                                 "minecraft:powder_snow",   "minecraft:red_sand",
                                                 "minecraft:red_sandstone", "minecraft:sand",
                                                 "minecraft:sandstone",     "minecraft:snow_block",
                                                 "minecraft:soul_sand",     "minecraft:soul_soil",
                                                 "minecraft:stone",         "minecraft:terracotta",
                                                 "minecraft:warped_nylium", "minecraft:warped_wart_block",
                                                 "minecraft:water",         "minecraft:white_terracotta"};

void predictStatusBeforeSurface(NbtCompound& level, std::set<std::string> blocksInChunk) {
    const NbtTag* held = level.find("Status");
    if (held == nullptr || held->get<std::string>() == nullptr) {
        return;
    }
    const std::string status = *held->get<std::string>();
    if (kAfterSurface.contains(status)) {
        return;
    }
    blocksInChunk.erase("minecraft:air");
    const bool hasNonAir = !blocksInChunk.empty();
    for (auto it = blocksInChunk.begin(); it != blocksInChunk.end();) {
        it = kBeforeFeatures.contains(*it) ? blocksInChunk.erase(it) : std::next(it);
    }
    if (!blocksInChunk.empty()) {
        level.putString("Status", "liquid_carvers");
    } else if (status != "noise" && !hasNonAir) {
        if (status == "biomes") {
            level.putString("Status", "structure_references");
        }
    } else {
        level.putString("Status", "noise");
    }
}

void carvingMasks(NbtCompound& level, int sectionCount, int addedBelow) {
    NbtCompound masks;
    if (const NbtCompound* old = compoundAt(level, "CarvingMasks"); old != nullptr) {
        for (std::size_t i = 0; i < old->names().size(); ++i) {
            const auto* bytes = old->values()[i].get<std::vector<std::int8_t>>();
            std::vector<std::int64_t> longs(static_cast<std::size_t>(64 * sectionCount), 0);
            if (bytes != nullptr) {
                for (std::size_t b = 0; b < bytes->size(); ++b) {
                    const std::size_t at = static_cast<std::size_t>(64 * addedBelow) + b / 8;
                    if (at < longs.size()) {
                        longs[at] |= static_cast<std::int64_t>(static_cast<std::uint8_t>((*bytes)[b])) << (8 * (b % 8));
                    }
                }
            }
            masks.set(old->names()[i], NbtTag::ofLongArray(std::move(longs)));
        }
    }
    level.set("CarvingMasks", NbtTag::ofCompound(std::move(masks)));
}

void padEntries(NbtCompound& level, std::string_view key) {
    std::vector<NbtTag> list;
    if (const NbtList* held = listAt(level, key); held != nullptr) {
        list.assign(held->begin(), held->end());
    }
    if (list.size() == 24) {
        return;
    }
    for (int i = 0; i < 4; ++i) {
        list.insert(list.begin(), emptyList());
        list.push_back(emptyList());
    }
    level.set(key, NbtTag::ofList(NbtList{NbtType::List, std::move(list)}));
}

void shiftHeightmaps(NbtCompound& level) {
    NbtCompound* maps = compoundAt(level, "Heightmaps");
    if (maps == nullptr) {
        return;
    }
    for (const std::string_view type :
         {"WORLD_SURFACE_WG", "WORLD_SURFACE", "WORLD_SURFACE_IGNORE_SNOW", "OCEAN_FLOOR_WG", "OCEAN_FLOOR",
          "MOTION_BLOCKING", "MOTION_BLOCKING_NO_LEAVES"}) {
        NbtTag* held = maps->find(type);
        auto* longs = held == nullptr ? nullptr : held->get<std::vector<std::int64_t>>();
        if (longs == nullptr) {
            continue;
        }
        for (std::int64_t& value : *longs) {
            std::int64_t shifted = 0;
            for (int bit = 0; bit + 9 <= 64; bit += 9) {
                const std::int64_t height = (value >> bit) & 511;
                const std::int64_t fixed = height == 0 ? 0 : std::min<std::int64_t>(height + 64, 511);
                shifted |= fixed << bit;
            }
            value = shifted;
        }
    }
}

void shiftUpgradeIndices(NbtCompound& level) {
    NbtCompound* upgrade = compoundAt(level, "UpgradeData");
    NbtCompound* indices = upgrade == nullptr ? nullptr : compoundAt(*upgrade, "Indices");
    if (indices == nullptr) {
        return;
    }
    NbtCompound shifted;
    for (std::size_t i = 0; i < indices->names().size(); ++i) {
        const std::string& key = indices->names()[i];
        char* end = nullptr;
        const long value = std::strtol(key.c_str(), &end, 10);
        if (end != key.c_str() && *end == '\0') {
            shifted.set(std::to_string(value - kNewMinSection), indices->values()[i]);
        }
    }
    upgrade->set("Indices", NbtTag::ofCompound(std::move(shifted)));
}

// parity: `ChunkHeightAndBiomeFix`.
void heightAndBiome(NbtCompound& chunk, const Context& context) {
    NbtCompound* level = compoundAt(chunk, "Level");
    if (level == nullptr) {
        return;
    }
    const bool overworld = context.overworld;
    const int minSection = overworld ? kNewMinSection : 0;
    bool wasIncreasedHeight = false;
    const std::vector<NbtTag> containers = biomeContainers(*level, overworld, minSection, wasIncreasedHeight);
    NbtCompound airState;
    airState.putString("Name", "minecraft:air");
    const NbtTag airContainer = pallettedContainer(listOf(NbtType::Compound, {NbtTag::ofCompound(airState)}));
    std::set<std::string> blocksInChunk;
    std::set<int> done;
    std::optional<Bedrock> bedrock;
    std::vector<NbtTag> sections;
    if (NbtList* old = listAt(*level, "Sections"); old != nullptr) {
        for (std::size_t i = 0; i < old->size(); ++i) {
            NbtCompound section = *(*old)[i].get<NbtCompound>();
            const int y = static_cast<int>(whole(section.find("Y")).value_or(0));
            NbtTag states = airContainer;
            const NbtTag* palette = section.find("Palette");
            if (palette != nullptr && palette->get<NbtList>() != nullptr) {
                for (const NbtTag& entry : *palette->get<NbtList>()) {
                    if (const NbtCompound* held = entry.get<NbtCompound>(); held != nullptr) {
                        blocksInChunk.insert(held->contains("Name") ? namespaced(held->getString("Name"))
                                                                    : "minecraft:air");
                    }
                }
                if (const NbtTag* data = section.find("BlockStates"); data != nullptr) {
                    states = blockStateContainer(*palette, *data);
                }
            }
            const int index = y - minSection;
            if (index >= 0 && index < static_cast<int>(containers.size())) {
                section.set("biomes", containers[static_cast<std::size_t>(index)]);
            }
            done.insert(y);
            if (y == 0) {
                Bedrock found;
                const NbtCompound* container = states.get<NbtCompound>();
                if (const NbtTag* list = container->find("palette");
                    list != nullptr && list->get<NbtList>() != nullptr) {
                    for (const NbtTag& entry : *list->get<NbtList>()) {
                        found.palette.push_back(entry.get<NbtCompound>() != nullptr ? *entry.get<NbtCompound>()
                                                                                    : NbtCompound{});
                    }
                }
                if (const NbtTag* data = container->find("data");
                    data != nullptr && data->get<std::vector<std::int64_t>>() != nullptr) {
                    found.data = *data->get<std::vector<std::int64_t>>();
                }
                bedrock = std::move(found);
            }
            section.set("block_states", std::move(states));
            section.remove("Palette");
            section.remove("BlockStates");
            sections.push_back(NbtTag::ofCompound(std::move(section)));
        }
    }
    for (std::size_t i = 0; i < containers.size(); ++i) {
        const int y = static_cast<int>(i) + minSection;
        if (done.insert(y).second) {
            NbtCompound section;
            section.putInt("Y", y);
            section.set("block_states", airContainer);
            section.set("biomes", containers[i]);
            sections.push_back(NbtTag::ofCompound(std::move(section)));
        }
    }
    std::ranges::stable_sort(sections, [](const NbtTag& left, const NbtTag& right) {
        return whole(left.get<NbtCompound>()->find("Y")).value_or(0) <
               whole(right.get<NbtCompound>()->find("Y")).value_or(0);
    });
    level->set("Sections", listOf(NbtType::Compound, std::move(sections)));

    if (overworld) {
        predictStatusBeforeSurface(*level, blocksInChunk);
    }
    level->remove("Biomes");
    if (!overworld) {
        carvingMasks(*level, kOldSectionCount, 0);
        return;
    }
    if (wasIncreasedHeight) {
        carvingMasks(*level, kNewSectionCount, 0);
        return;
    }
    shiftHeightmaps(*level);
    padEntries(*level, "LiquidsToBeTicked");
    padEntries(*level, "PostProcessing");
    padEntries(*level, "ToBeTicked");
    carvingMasks(*level, kNewSectionCount, 4);
    shiftUpgradeIndices(*level);
    if (!context.noiseGenerator) {
        return;
    }
    const NbtTag* statusTag = level->find("Status");
    if (statusTag == nullptr || statusTag->get<std::string>() == nullptr) {
        return;
    }
    const std::string last = *statusTag->get<std::string>();
    if (last == "empty") {
        return;
    }
    NbtCompound blending;
    blending.putBoolean("old_noise", kAfterNoise.contains(last));
    level->set("blending_data", NbtTag::ofCompound(std::move(blending)));
    if (!bedrock.has_value()) {
        return;
    }
    std::vector<bool> missing(256, false);
    bool anyBedrock = last == "noise";
    int missingCount = 0;
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            const NbtCompound* state = bedrock->at(x, 0, z);
            const std::string name = state == nullptr ? "" : std::string{state->getString("Name")};
            if (state != nullptr && name == "minecraft:air") {
                missing[static_cast<std::size_t>(z * 16 + x)] = true;
                ++missingCount;
            }
            anyBedrock = anyBedrock || (state != nullptr && name == "minecraft:bedrock");
        }
    }
    if (anyBedrock && missingCount != 256) {
        std::vector<std::int64_t> words(4, 0);
        for (std::size_t i = 0; i < 256; ++i) {
            if (missing[i]) {
                words[i / 64] |= std::int64_t{1} << (i % 64);
            }
        }
        while (!words.empty() && words.back() == 0) {
            words.pop_back();
        }
        NbtCompound retrogen;
        retrogen.putString("target_status", last == "full" ? "heightmaps" : last);
        retrogen.set("missing_bedrock", NbtTag::ofLongArray(std::move(words)));
        level->set("below_zero_retrogen", NbtTag::ofCompound(std::move(retrogen)));
        level->putString("Status", "empty");
    }
    level->putBoolean("isLightOn", false);
}

[[nodiscard]] std::string liquidAt(const NbtCompound* state) {
    if (state == nullptr) {
        return "minecraft:empty";
    }
    const std::string block{state->getString("Name")};
    const NbtCompound* properties = state->getCompound("Properties");
    const std::string_view level = properties == nullptr ? "0" : properties->getString("level", "0");
    if (block == "minecraft:water") {
        return level == "0" ? "minecraft:water" : "minecraft:flowing_water";
    }
    if (block == "minecraft:lava") {
        return level == "0" ? "minecraft:lava" : "minecraft:flowing_lava";
    }
    static const std::set<std::string_view> kAlwaysWaterlogged{"minecraft:bubble_column", "minecraft:kelp",
                                                               "minecraft:kelp_plant", "minecraft:seagrass",
                                                               "minecraft:tall_seagrass"};
    const bool waterlogged = properties != nullptr && properties->getString("waterlogged") == "true";
    return kAlwaysWaterlogged.contains(block) || waterlogged ? "minecraft:water" : "minecraft:empty";
}

[[nodiscard]] std::string blockAt(const NbtCompound* state) {
    return state == nullptr ? "minecraft:air"
                            : std::string{state->contains("Name") ? state->getString("Name") : "minecraft:air"};
}

// parity: `ChunkProtoTickListFix`.
void protoTickList(NbtCompound& chunk, const Context&) {
    NbtCompound* level = compoundAt(chunk, "Level");
    if (level == nullptr) {
        return;
    }
    if (NbtTag* liquid = level->find("LiquidTicks"); liquid != nullptr) {
        level->set("fluid_ticks", *liquid);
        level->remove("LiquidTicks");
    }
    std::map<int, Bedrock> containers;
    int lowest = 0;
    if (NbtList* sections = listAt(*level, "Sections"); sections != nullptr) {
        for (std::size_t i = 0; i < sections->size(); ++i) {
            NbtCompound* section = (*sections)[i].get<NbtCompound>();
            if (section == nullptr || !section->contains("Y")) {
                continue;
            }
            const int y = static_cast<int>(whole(section->find("Y")).value_or(0));
            if (section->contains("biomes")) {
                lowest = std::min(lowest, y);
            }
            if (NbtCompound* states = compoundAt(*section, "block_states"); states != nullptr) {
                Bedrock container;
                if (NbtList* palette = listAt(*states, "palette"); palette != nullptr) {
                    for (std::size_t p = 0; p < palette->size(); ++p) {
                        const NbtCompound* entry = (*palette)[p].get<NbtCompound>();
                        container.palette.push_back(entry != nullptr ? *entry : NbtCompound{});
                    }
                }
                if (const NbtTag* data = states->find("data");
                    data != nullptr && data->get<std::vector<std::int64_t>>() != nullptr) {
                    container.data = *data->get<std::vector<std::int64_t>>();
                }
                containers[y] = std::move(container);
            }
        }
    }
    level->putByte("yPos", static_cast<std::int8_t>(lowest));
    if (level->contains("TileTicks") || level->contains("fluid_ticks")) {
        return;
    }
    const int chunkX = static_cast<int>(whole(level->find("xPos")).value_or(0));
    const int chunkZ = static_cast<int>(whole(level->find("zPos")).value_or(0));
    const auto makeTicks = [&](std::string_view key, bool liquid) {
        std::vector<NbtTag> ticks;
        const NbtList* perSection = listAt(*level, key);
        for (std::size_t index = 0; perSection != nullptr && index < perSection->size(); ++index) {
            const int sectionY = static_cast<int>(index) + lowest;
            const auto found = containers.find(sectionY);
            const NbtList* positions = (*perSection)[index].get<NbtList>();
            for (std::size_t p = 0; positions != nullptr && p < positions->size(); ++p) {
                const auto packed = whole(&(*positions)[p]).value_or(-1);
                if (packed <= 0) {
                    continue;
                }
                const int x = static_cast<int>(packed & 15);
                const int y = static_cast<int>((packed >> 4) & 15);
                const int z = static_cast<int>((packed >> 8) & 15);
                const NbtCompound* state = found == containers.end() ? nullptr : found->second.at(x, y, z);
                NbtCompound tick;
                tick.putString("i", liquid ? liquidAt(state) : blockAt(state));
                tick.putInt("x", chunkX * 16 + x);
                tick.putInt("y", sectionY * 16 + y);
                tick.putInt("z", chunkZ * 16 + z);
                tick.putInt("t", 0);
                tick.putInt("p", 0);
                ticks.push_back(NbtTag::ofCompound(std::move(tick)));
            }
        }
        return ticks;
    };
    std::vector<NbtTag> fluid = makeTicks("LiquidsToBeTicked", true);
    std::vector<NbtTag> blocks = makeTicks("ToBeTicked", false);
    level->set("TileTicks", listOf(NbtType::Compound, std::move(blocks)));
    level->remove("ToBeTicked");
    level->remove("LiquidsToBeTicked");
    level->set("fluid_ticks", listOf(NbtType::Compound, std::move(fluid)));
}

// parity: `ChunkRenamesFix`, the end of the `Level` tag.
void chunkRenames(NbtCompound& chunk, const Context&) {
    NbtCompound* held = compoundAt(chunk, "Level");
    if (held == nullptr) {
        return;
    }
    NbtCompound level = *held;
    chunk.remove("Level");
    rename(level, "TileEntities", "block_entities");
    rename(level, "TileTicks", "block_ticks");
    rename(level, "Entities", "entities");
    rename(level, "Sections", "sections");
    if (NbtCompound* structures = compoundAt(level, "Structures"); structures != nullptr) {
        rename(*structures, "Starts", "starts");
    }
    rename(level, "Structures", "structures");
    for (std::size_t i = 0; i < level.names().size(); ++i) {
        chunk.set(level.names()[i], level.values()[i]);
    }
}

void biomeRenames(NbtCompound& chunk, const std::function<std::optional<std::string>(const std::string&)>& renamer) {
    NbtCompound* level = compoundAt(chunk, "Level");
    NbtCompound& holder = level != nullptr ? *level : chunk;
    forEachCompound(holder, level != nullptr ? "Sections" : "sections", [&](NbtCompound& section) {
        NbtCompound* biomes = compoundAt(section, "biomes");
        NbtList* palette = biomes == nullptr ? nullptr : listAt(*biomes, "palette");
        for (std::size_t i = 0; palette != nullptr && i < palette->size(); ++i) {
            if (const auto* name = (*palette)[i].get<std::string>(); name != nullptr) {
                if (const auto renamed = renamer(namespaced(*name)); renamed.has_value()) {
                    (*palette)[i] = NbtTag::ofString(*renamed);
                }
            }
        }
    });
}

// parity: `NamespacedTypeRenameFix` over biomes, for the caves and cliffs update.
void cavesAndCliffsBiomes(NbtCompound& chunk, const Context&) {
    biomeRenames(chunk, [](const std::string& name) -> std::optional<std::string> {
        const auto found = lookup(tables::kCavesAndCliffsBiomes, name);
        return found.has_value() ? std::optional{std::string{*found}} : std::nullopt;
    });
}

// parity: `OverreachingTickFix` and `NamespacedTypeRenameFix` for the deep warm ocean that was removed.
void overreachingTicks(NbtCompound& chunk, const Context&) {
    const int chunkX = static_cast<int>(whole(chunk.find("xPos")).value_or(0));
    const int chunkZ = static_cast<int>(whole(chunk.find("zPos")).value_or(0));
    const auto extract = [&](std::string_view source, std::string_view target) {
        const NbtList* ticks = listAt(chunk, source);
        if (ticks == nullptr) {
            return;
        }
        std::vector<NbtTag> out;
        for (const NbtTag& entry : *ticks) {
            const NbtCompound* tick = entry.get<NbtCompound>();
            if (tick == nullptr) {
                continue;
            }
            const int distX = std::abs(chunkX - (static_cast<int>(whole(tick->find("x")).value_or(0)) >> 4));
            const int distZ = std::abs(chunkZ - (static_cast<int>(whole(tick->find("z")).value_or(0)) >> 4));
            if ((distX != 0 || distZ != 0) && distX <= 1 && distZ <= 1) {
                out.push_back(entry);
            }
        }
        if (out.empty()) {
            return;
        }
        NbtCompound upgrade;
        if (const NbtCompound* held = compoundAt(chunk, "UpgradeData"); held != nullptr) {
            upgrade = *held;
        }
        upgrade.set(target, listOf(NbtType::Compound, std::move(out)));
        chunk.set("UpgradeData", NbtTag::ofCompound(std::move(upgrade)));
    };
    extract("block_ticks", "neighbor_block_ticks");
    extract("fluid_ticks", "neighbor_fluid_ticks");
    biomeRenames(chunk, [](const std::string& name) -> std::optional<std::string> {
        return name == "minecraft:deep_warm_ocean" ? std::optional<std::string>{"minecraft:warm_ocean"} : std::nullopt;
    });
}

struct Conversion {
    std::vector<std::pair<std::vector<std::string_view>, std::string_view>> byBiome;
    std::string_view fallback;
};

const std::map<std::string, Conversion>& structureConversions() {
    static const std::map<std::string, Conversion> kConversions{
        {"mineshaft",
         {{{{"minecraft:badlands", "minecraft:eroded_badlands", "minecraft:wooded_badlands"},
            "minecraft:mineshaft_mesa"}},
          "minecraft:mineshaft"}},
        {"shipwreck",
         {{{{"minecraft:beach", "minecraft:snowy_beach"}, "minecraft:shipwreck_beached"}}, "minecraft:shipwreck"}},
        {"ocean_ruin",
         {{{{"minecraft:warm_ocean", "minecraft:lukewarm_ocean", "minecraft:deep_lukewarm_ocean"},
            "minecraft:ocean_ruin_warm"}},
          "minecraft:ocean_ruin_cold"}},
        {"village",
         {{{{"minecraft:desert"}, "minecraft:village_desert"},
           {{"minecraft:savanna"}, "minecraft:village_savanna"},
           {{"minecraft:snowy_plains"}, "minecraft:village_snowy"},
           {{"minecraft:taiga"}, "minecraft:village_taiga"}},
          "minecraft:village_plains"}},
        {"ruined_portal",
         {{{{"minecraft:desert"}, "minecraft:ruined_portal_desert"},
           {{"minecraft:badlands", "minecraft:eroded_badlands", "minecraft:wooded_badlands",
             "minecraft:windswept_hills", "minecraft:windswept_forest", "minecraft:windswept_gravelly_hills",
             "minecraft:savanna_plateau", "minecraft:windswept_savanna", "minecraft:stony_shore", "minecraft:meadow",
             "minecraft:frozen_peaks", "minecraft:jagged_peaks", "minecraft:stony_peaks", "minecraft:snowy_slopes"},
            "minecraft:ruined_portal_mountain"},
           {{"minecraft:bamboo_jungle", "minecraft:jungle", "minecraft:sparse_jungle"},
            "minecraft:ruined_portal_jungle"},
           {{"minecraft:deep_frozen_ocean", "minecraft:deep_cold_ocean", "minecraft:deep_ocean",
             "minecraft:deep_lukewarm_ocean", "minecraft:frozen_ocean", "minecraft:ocean", "minecraft:cold_ocean",
             "minecraft:lukewarm_ocean", "minecraft:warm_ocean"},
            "minecraft:ruined_portal_ocean"}},
          "minecraft:ruined_portal"}},
        {"pillager_outpost", {{}, "minecraft:pillager_outpost"}},
        {"mansion", {{}, "minecraft:mansion"}},
        {"jungle_pyramid", {{}, "minecraft:jungle_pyramid"}},
        {"desert_pyramid", {{}, "minecraft:desert_pyramid"}},
        {"igloo", {{}, "minecraft:igloo"}},
        {"swamp_hut", {{}, "minecraft:swamp_hut"}},
        {"stronghold", {{}, "minecraft:stronghold"}},
        {"monument", {{}, "minecraft:monument"}},
        {"fortress", {{}, "minecraft:fortress"}},
        {"endcity", {{}, "minecraft:end_city"}},
        {"buried_treasure", {{}, "minecraft:buried_treasure"}},
        {"nether_fossil", {{}, "minecraft:nether_fossil"}},
        {"bastion_remnant", {{}, "minecraft:bastion_remnant"}}};
    return kConversions;
}

[[nodiscard]] std::optional<std::string> configuredStructure(const std::string& oldKey, NbtCompound& chunk) {
    std::string key = oldKey;
    std::ranges::transform(key, key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto found = structureConversions().find(key);
    if (found == structureConversions().end()) {
        return std::nullopt;
    }
    std::string result{found->second.fallback};
    if (!found->second.byBiome.empty()) {
        std::vector<std::pair<std::string_view, int>> counts;
        forEachCompound(chunk, "sections", [&](NbtCompound& section) {
            const NbtCompound* biomes = section.getCompound("biomes");
            const NbtList* palette = biomes == nullptr ? nullptr : biomes->getList("palette");
            for (std::size_t i = 0; palette != nullptr && i < palette->size(); ++i) {
                const auto* name = (*palette)[i].get<std::string>();
                if (name == nullptr) {
                    continue;
                }
                for (const auto& [names, target] : found->second.byBiome) {
                    if (std::ranges::find(names, std::string_view{*name}) == names.end()) {
                        continue;
                    }
                    const auto existing = std::ranges::find(counts, target, &std::pair<std::string_view, int>::first);
                    if (existing == counts.end()) {
                        counts.emplace_back(target, 1);
                    } else {
                        ++existing->second;
                    }
                }
            }
        });
        const std::pair<std::string_view, int>* best = nullptr;
        for (const auto& count : counts) {
            if (best == nullptr || count.second > best->second) {
                best = &count;
            }
        }
        if (best != nullptr) {
            result = std::string{best->first};
        }
    }
    return result;
}

// parity: `StructuresBecomeConfiguredFix`.
void structuresBecomeConfigured(NbtCompound& chunk, const Context&) {
    NbtCompound* structures = compoundAt(chunk, "structures");
    if (structures == nullptr) {
        return;
    }
    if (NbtCompound* starts = compoundAt(*structures, "starts"); starts != nullptr) {
        NbtCompound fixed;
        for (std::size_t i = 0; i < starts->names().size(); ++i) {
            const NbtCompound* start = starts->values()[i].get<NbtCompound>();
            if (start == nullptr || (start->contains("id") && start->getString("id") == "INVALID") ||
                !start->contains("id")) {
                continue;
            }
            const auto key = configuredStructure(starts->names()[i], chunk);
            if (!key.has_value() || fixed.contains(*key)) {
                continue;
            }
            NbtCompound copy = *start;
            copy.putString("id", *key);
            fixed.set(*key, NbtTag::ofCompound(std::move(copy)));
        }
        structures->set("starts", NbtTag::ofCompound(std::move(fixed)));
    }
    if (NbtCompound* references = compoundAt(*structures, "References"); references != nullptr) {
        NbtCompound fixed;
        for (std::size_t i = 0; i < references->names().size(); ++i) {
            const auto* longs = references->values()[i].get<std::vector<std::int64_t>>();
            if (longs == nullptr || longs->empty()) {
                continue;
            }
            const auto key = configuredStructure(references->names()[i], chunk);
            if (!key.has_value()) {
                continue;
            }
            if (NbtTag* existing = fixed.find(*key);
                existing != nullptr && existing->get<std::vector<std::int64_t>>() != nullptr) {
                existing->get<std::vector<std::int64_t>>()->insert(existing->get<std::vector<std::int64_t>>()->end(),
                                                                   longs->begin(), longs->end());
            } else {
                fixed.set(*key, references->values()[i]);
            }
        }
        structures->set("References", NbtTag::ofCompound(std::move(fixed)));
    }
}

} // namespace

void addFixesFrom2600(std::vector<Fix>& fixes) {
    fixes.insert(fixes.end(), {
                                  {2679, Target::Chunk, {}, cauldronRename},
                                  blockRename(2680, kGrassPath),
                                  itemRename(2680, kGrassPath),
                                  blockRename(2690, tables::kCopperOxidation),
                                  itemRename(2690, tables::kCopperOxidation),
                                  blockRename(2691, tables::kWaxedCopperSuffixes),
                                  itemRename(2691, tables::kWaxedCopperSuffixes),
                                  blockRename(2696, tables::kGrimstoneToDeepslate),
                                  itemRename(2696, tables::kGrimstoneToDeepslate),
                                  blockRename(2700, kCaveVines),
                                  {2702, Target::Entity, {}, arrowPickup},
                                  blockRename(2717, kAzalea),
                                  itemRename(2717, kAzalea),
                                  {2831, Target::BlockEntity, {}, spawnerOfBlockEntity},
                                  {2831, Target::Entity, {}, spawnerOfMinecart},
                                  {2832, Target::Chunk, {}, heightAndBiome},
                                  {2838, Target::Chunk, {}, cavesAndCliffsBiomes},
                                  {2841, Target::Chunk, {}, protoTickList},
                                  {2842, Target::Chunk, {}, chunkRenames},
                                  {2843, Target::Chunk, {}, overreachingTicks},
                                  {2970, Target::Chunk, {}, structuresBecomeConfigured},
                              });
}

} // namespace basalt::metamorph::datafix
