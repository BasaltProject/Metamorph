#include "DomainSupport.hpp"
#include "FlatteningTables.hpp"
#include "LegacyStates.hpp"

#include <basalt/metamorph/datafix/LevelData.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

namespace basalt::metamorph::datafix {
namespace {

using namespace support;
using wire::nbt::NbtCompound;
using wire::nbt::NbtList;
using wire::nbt::NbtTag;
using wire::nbt::NbtType;

constexpr std::uint32_t kLevel = domainBit(Domain::Level);

[[nodiscard]] std::int32_t intOf(const NbtCompound& holder, std::string_view key, std::int32_t fallback) {
    return static_cast<std::int32_t>(whole(holder.find(key)).value_or(fallback));
}

[[nodiscard]] bool boolOf(const NbtCompound& holder, std::string_view key, bool fallback) {
    const std::optional<std::int64_t> value = whole(holder.find(key));
    return value.has_value() ? *value != 0 : fallback;
}

/// parity: `NumberUtils.toInt`, the default when the text is not a whole number.
[[nodiscard]] std::int32_t toInt(const std::string& text, std::int32_t fallback) {
    char* end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    return !text.empty() && end == text.c_str() + text.size() ? static_cast<std::int32_t>(parsed) : fallback;
}

/// parity: `Splitter.on(separator).limit(limit).splitToList`, which keeps empty parts.
[[nodiscard]] std::vector<std::string> split(std::string_view text, char separator, std::size_t limit) {
    std::vector<std::string> parts;
    while (parts.size() + 1 < limit) {
        const std::size_t at = text.find(separator);
        if (at == std::string_view::npos) {
            break;
        }
        parts.emplace_back(text.substr(0, at));
        text.remove_prefix(at + 1);
    }
    parts.emplace_back(text);
    return parts;
}

[[nodiscard]] std::int32_t blockIdOfName(const std::string& name) {
    for (const auto& entry : flattening::kBlockIds) {
        if (entry.name == name) {
            return entry.id;
        }
    }
    return 0;
}

constexpr std::string_view kDefaultFlat = "minecraft:bedrock,2*minecraft:dirt,minecraft:grass_block;1;village";

[[nodiscard]] std::string flatOptions(const std::string& options) {
    if (options.empty()) {
        return std::string{kDefaultFlat};
    }
    const std::vector<std::string> parts = split(options, ';', 5);
    std::size_t next = 1;
    std::int32_t version = 0;
    std::string layerInfo = parts[0];
    if (parts.size() > 1) {
        version = toInt(parts[0], 0);
        layerInfo = parts[1];
        next = 2;
    }
    if (version < 0 || version > 3) {
        return std::string{kDefaultFlat};
    }
    std::string result;
    bool first = true;
    for (const std::string& layer : split(layerInfo, ',', SIZE_MAX)) {
        const std::vector<std::string> sized = split(layer, version < 3 ? 'x' : '*', 2);
        const std::int32_t height = sized.size() == 2 ? toInt(sized[0], 0) : 1;
        const std::vector<std::string> blockParts = split(sized.size() == 2 ? sized[1] : sized[0], ':', 3);
        const std::size_t nameIndex = blockParts[0] == "minecraft" ? 1 : 0;
        const std::string blockString = nameIndex < blockParts.size() ? blockParts[nameIndex] : std::string{};
        const std::int32_t blockId = version == 3 ? blockIdOfName("minecraft:" + blockString) : toInt(blockString, 0);
        const std::int32_t data = blockParts.size() > nameIndex + 1 ? toInt(blockParts[nameIndex + 1], 0) : 0;
        if (!first) {
            result += ',';
        }
        first = false;
        if (height != 1) {
            result += std::to_string(height) + "*";
        }
        result += legacy::stateOf(blockId << 4 | data).name;
    }
    for (std::size_t i = next; i < parts.size(); ++i) {
        result += ';' + parts[i];
    }
    return result;
}

[[nodiscard]] bool equalsIgnoreCase(std::string_view left, std::string_view right) {
    return std::ranges::equal(left, right, [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
}

// parity: `LevelFlatGeneratorInfoFix`.
void flatGeneratorInfo(NbtCompound& level, const Context&) {
    if (!equalsIgnoreCase(level.getString("generatorName"), "flat")) {
        return;
    }
    if (const NbtTag* options = level.find("generatorOptions"); options != nullptr) {
        if (const std::string* text = options->get<std::string>(); text != nullptr) {
            level.putString("generatorOptions", flatOptions(*text));
        }
    }
}

constexpr std::pair<int, std::string_view> kFlatBiomes[]{
    {0, "minecraft:ocean"},
    {1, "minecraft:plains"},
    {2, "minecraft:desert"},
    {3, "minecraft:mountains"},
    {4, "minecraft:forest"},
    {5, "minecraft:taiga"},
    {6, "minecraft:swamp"},
    {7, "minecraft:river"},
    {8, "minecraft:nether"},
    {9, "minecraft:the_end"},
    {10, "minecraft:frozen_ocean"},
    {11, "minecraft:frozen_river"},
    {12, "minecraft:snowy_tundra"},
    {13, "minecraft:snowy_mountains"},
    {14, "minecraft:mushroom_fields"},
    {15, "minecraft:mushroom_field_shore"},
    {16, "minecraft:beach"},
    {17, "minecraft:desert_hills"},
    {18, "minecraft:wooded_hills"},
    {19, "minecraft:taiga_hills"},
    {20, "minecraft:mountain_edge"},
    {21, "minecraft:jungle"},
    {22, "minecraft:jungle_hills"},
    {23, "minecraft:jungle_edge"},
    {24, "minecraft:deep_ocean"},
    {25, "minecraft:stone_shore"},
    {26, "minecraft:snowy_beach"},
    {27, "minecraft:birch_forest"},
    {28, "minecraft:birch_forest_hills"},
    {29, "minecraft:dark_forest"},
    {30, "minecraft:snowy_taiga"},
    {31, "minecraft:snowy_taiga_hills"},
    {32, "minecraft:giant_tree_taiga"},
    {33, "minecraft:giant_tree_taiga_hills"},
    {34, "minecraft:wooded_mountains"},
    {35, "minecraft:savanna"},
    {36, "minecraft:savanna_plateau"},
    {37, "minecraft:badlands"},
    {38, "minecraft:wooded_badlands_plateau"},
    {39, "minecraft:badlands_plateau"},
    {40, "minecraft:small_end_islands"},
    {41, "minecraft:end_midlands"},
    {42, "minecraft:end_highlands"},
    {43, "minecraft:end_barrens"},
    {44, "minecraft:warm_ocean"},
    {45, "minecraft:lukewarm_ocean"},
    {46, "minecraft:cold_ocean"},
    {47, "minecraft:deep_warm_ocean"},
    {48, "minecraft:deep_lukewarm_ocean"},
    {49, "minecraft:deep_cold_ocean"},
    {50, "minecraft:deep_frozen_ocean"},
    {127, "minecraft:the_void"},
    {129, "minecraft:sunflower_plains"},
    {130, "minecraft:desert_lakes"},
    {131, "minecraft:gravelly_mountains"},
    {132, "minecraft:flower_forest"},
    {133, "minecraft:taiga_mountains"},
    {134, "minecraft:swamp_hills"},
    {140, "minecraft:ice_spikes"},
    {149, "minecraft:modified_jungle"},
    {151, "minecraft:modified_jungle_edge"},
    {155, "minecraft:tall_birch_forest"},
    {156, "minecraft:tall_birch_hills"},
    {157, "minecraft:dark_forest_hills"},
    {158, "minecraft:snowy_taiga_mountains"},
    {160, "minecraft:giant_spruce_taiga"},
    {161, "minecraft:giant_spruce_taiga_hills"},
    {162, "minecraft:modified_gravelly_mountains"},
    {163, "minecraft:shattered_savanna"},
    {164, "minecraft:shattered_savanna_plateau"},
    {165, "minecraft:eroded_badlands"},
    {166, "minecraft:modified_wooded_badlands_plateau"},
};

[[nodiscard]] std::string_view flatBiomeName(const std::string& id) {
    if (id == "167") {
        return "minecraft:modified_badlands_plateau";
    }
    for (const auto& [number, name] : kFlatBiomes) {
        if (std::to_string(number) == id) {
            return name;
        }
    }
    return "minecraft:plains";
}

[[nodiscard]] std::string lowered(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

struct FlatLayer {
    std::int32_t height;
    std::string block;
};

/// parity: `LevelDataGeneratorOptionsFix.getLayersInfoFromString`, empty when a height is not a number.
[[nodiscard]] std::vector<FlatLayer> flatLayers(const std::string& input) {
    std::vector<FlatLayer> layers;
    for (const std::string& depth : split(input, ',', SIZE_MAX)) {
        const std::vector<std::string> parts = split(depth, '*', 2);
        std::int32_t height = 1;
        if (parts.size() == 2) {
            char* end = nullptr;
            const long parsed = std::strtol(parts[0].c_str(), &end, 10);
            if (parts[0].empty() || end != parts[0].c_str() + parts[0].size()) {
                return {};
            }
            height = static_cast<std::int32_t>(parsed);
        }
        layers.push_back({height, parts.back()});
    }
    return layers;
}

[[nodiscard]] NbtCompound flatSettings(const std::string& options) {
    std::string biome = "minecraft:plains";
    std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> structures;
    std::vector<FlatLayer> layers;
    const std::vector<std::string> parts = split(options, ';', SIZE_MAX);
    if (!options.empty()) {
        layers = flatLayers(parts[0]);
    }
    if (!options.empty() && !layers.empty()) {
        if (parts.size() > 1) {
            biome = std::string{flatBiomeName(parts[1])};
        }
        if (parts.size() > 2) {
            for (const std::string& structure : split(lowered(parts[2]), ',', SIZE_MAX)) {
                const std::size_t open = structure.find('(');
                const std::string name = structure.substr(0, open);
                if (name.empty()) {
                    continue;
                }
                std::vector<std::pair<std::string, std::string>> settings;
                const std::string rest = open == std::string::npos ? std::string{} : structure.substr(open + 1);
                if (!rest.empty() && rest.back() == ')' && rest.size() > 1) {
                    for (const std::string& part : split(rest.substr(0, rest.size() - 1), ' ', SIZE_MAX)) {
                        const std::vector<std::string> pair = split(part, '=', 2);
                        if (pair.size() == 2) {
                            settings.emplace_back(pair[0], pair[1]);
                        }
                    }
                }
                std::erase_if(structures, [&name](const auto& entry) { return entry.first == name; });
                structures.emplace_back(name, std::move(settings));
            }
        } else {
            structures.emplace_back("village", std::vector<std::pair<std::string, std::string>>{});
        }
    } else {
        layers = {{1, "minecraft:bedrock"}, {2, "minecraft:dirt"}, {1, "minecraft:grass_block"}};
        structures.emplace_back("village", std::vector<std::pair<std::string, std::string>>{});
    }
    std::vector<NbtCompound> layerTags;
    for (const FlatLayer& layer : layers) {
        NbtCompound entry;
        entry.putInt("height", layer.height);
        entry.putString("block", layer.block);
        layerTags.push_back(std::move(entry));
    }
    NbtCompound structureTags;
    for (const auto& [name, settings] : structures) {
        NbtCompound entry;
        for (const auto& [key, value] : settings) {
            entry.putString(key, value);
        }
        structureTags.set(name, NbtTag::ofCompound(std::move(entry)));
    }
    NbtCompound result;
    result.set("layers", compoundList(std::move(layerTags)));
    result.putString("biome", biome);
    result.set("structures", NbtTag::ofCompound(std::move(structureTags)));
    return result;
}

// parity: `LevelDataGeneratorOptionsFix`.
void generatorOptions(NbtCompound& level, const Context&) {
    const std::string generator = lowered(std::string{level.getString("generatorName")});
    const NbtTag* held = level.find("generatorOptions");
    const std::string* options = held == nullptr ? nullptr : held->get<std::string>();
    if (generator == "flat") {
        level.set("generatorOptions", NbtTag::ofCompound(flatSettings(options == nullptr ? std::string{} : *options)));
    } else if (generator == "buffet" && options != nullptr) {
        if (const auto parsed = wire::common::parseJson(*options); parsed.has_value()) {
            level.set("generatorOptions", jsonAsNbt(*parsed));
        }
    }
}

// parity: `LevelUUIDFix`.
void levelUuids(NbtCompound& level, const Context&) {
    if (NbtCompound* dimensions = compoundAt(level, "DimensionData"); dimensions != nullptr) {
        for (const std::string& name : dimensions->names()) {
            if (NbtCompound* dimension = compoundAt(*dimensions, name); dimension != nullptr) {
                if (NbtCompound* fight = compoundAt(*dimension, "DragonFight"); fight != nullptr) {
                    replaceUuidLeastMost(*fight, "DragonUUID", "Dragon");
                }
            }
        }
    }
    replaceUuidString(level, "WanderingTraderId", "WanderingTraderId");
    if (NbtCompound* events = compoundAt(level, "CustomBossEvents"); events != nullptr) {
        for (const std::string& name : events->names()) {
            NbtCompound* event = compoundAt(*events, name);
            NbtList* players = event == nullptr ? nullptr : listAt(*event, "Players");
            for (std::size_t i = 0; players != nullptr && i < players->size(); ++i) {
                const NbtCompound* player = (*players)[i].get<NbtCompound>();
                if (const std::optional<NbtTag> uuid = player == nullptr ? std::nullopt : uuidFromMl(*player);
                    uuid.has_value()) {
                    (*players)[i] = *uuid;
                }
            }
            if (players != nullptr) {
                *players = NbtList{players->empty() ? NbtType::End : (*players)[0].type(),
                                   std::vector<NbtTag>(players->begin(), players->end())};
            }
        }
    }
}

// parity: `LevelLegacyWorldGenSettingsFix`.
void legacyWorldGenSettings(NbtCompound& level, const Context&) {
    static constexpr std::array<std::string_view, 7> kOldKeys{
        "RandomSeed",  "generatorName", "generatorOptions", "generatorVersion", "legacy_custom_options",
        "MapFeatures", "BonusChest"};
    NbtCompound settings;
    if (const NbtCompound* held = compoundAt(level, "WorldGenSettings"); held != nullptr) {
        settings = *held;
    }
    for (const std::string_view key : kOldKeys) {
        if (const NbtTag* old = level.find(key); old != nullptr) {
            settings.set(key, *old);
            level.remove(key);
        }
    }
    level.set("WorldGenSettings", NbtTag::ofCompound(std::move(settings)));
}

void removeFeatureFlags(NbtCompound& level, const std::set<std::string>& flags) {
    std::vector<NbtTag> inactive;
    if (const NbtList* removed = level.getList("removed_features"); removed != nullptr) {
        inactive.assign(removed->begin(), removed->end());
    }
    NbtList* enabled = listAt(level, "enabled_features");
    if (enabled == nullptr) {
        return;
    }
    std::vector<NbtTag> kept;
    for (const NbtTag& feature : *enabled) {
        const std::string* name = feature.get<std::string>();
        if (name != nullptr && flags.contains(*name)) {
            inactive.push_back(feature);
        } else {
            kept.push_back(feature);
        }
    }
    *enabled = NbtList{kept.empty() ? NbtType::End : kept.front().type(), std::move(kept)};
    if (!inactive.empty()) {
        level.set("removed_features", NbtTag::ofList(NbtList{inactive.front().type(), std::move(inactive)}));
    }
}

// parity: `FeatureFlagRemoveFix` for the experimental 1.20 toggle.
void removeUpdate120(NbtCompound& level, const Context&) { removeFeatureFlags(level, {"minecraft:update_1_20"}); }

// parity: `FeatureFlagRemoveFix` for the experimental 1.21 toggle.
void removeUpdate121(NbtCompound& level, const Context&) { removeFeatureFlags(level, {"minecraft:update_1_21"}); }

// parity: `FeatureFlagRemoveFix` for the bundle toggle.
void removeBundle(NbtCompound& level, const Context&) { removeFeatureFlags(level, {"minecraft:bundle"}); }

// parity: `FeatureFlagRemoveFix` for the winter drop toggle.
void removeWinterDrop(NbtCompound& level, const Context&) { removeFeatureFlags(level, {"minecraft:winter_drop"}); }

// parity: `FeatureFlagRemoveFix` for the locator bar toggle.
void removeLocatorBar(NbtCompound& level, const Context&) { removeFeatureFlags(level, {"minecraft:locator_bar"}); }

// parity: `LegacyDragonFightFix`.
void legacyDragonFight(NbtCompound& level, const Context&) {
    if (level.contains("DragonFight")) {
        return;
    }
    NbtCompound fight;
    if (NbtCompound* dimensions = compoundAt(level, "DimensionData"); dimensions != nullptr) {
        if (NbtCompound* end = compoundAt(*dimensions, "1"); end != nullptr) {
            if (const NbtCompound* held = compoundAt(*end, "DragonFight"); held != nullptr) {
                fight = *held;
            }
        }
    }
    if (const NbtTag* exit = fight.find("ExitPortalLocation"); exit != nullptr) {
        if (const NbtCompound* pos = exit->get<NbtCompound>(); pos != nullptr) {
            fight.putIntArray("ExitPortalLocation", {pos->getInt("X"), pos->getInt("Y"), pos->getInt("Z")});
        }
    }
    level.set("DragonFight", NbtTag::ofCompound(std::move(fight)));
}

// parity: `LegacyWorldBorderFix`.
void legacyWorldBorder(NbtCompound& level, const Context&) {
    NbtCompound border = worldBorderOfLegacy(level);
    for (const std::string_view key :
         {"BorderCenterX", "BorderCenterZ", "BorderSize", "BorderSizeLerpTime", "BorderSizeLerpTarget",
          "BorderSafeZone", "BorderDamagePerBlock", "BorderWarningBlocks", "BorderWarningTime"}) {
        level.remove(key);
    }
    level.set("world_border", NbtTag::ofCompound(std::move(border)));
}

// parity: `WorldSpawnDataFix`.
void worldSpawn(NbtCompound& level, const Context&) {
    const std::int32_t x = intOf(level, "SpawnX", 0);
    const std::int32_t y = intOf(level, "SpawnY", 0);
    const std::int32_t z = intOf(level, "SpawnZ", 0);
    const float angle = static_cast<float>(real(level.find("SpawnAngle")).value_or(0.0));
    NbtCompound spawn;
    spawn.putString("dimension", "minecraft:overworld");
    spawn.putIntArray("pos", {x, y, z});
    spawn.putFloat("yaw", angle);
    spawn.putFloat("pitch", 0.0F);
    for (const std::string_view key : {"SpawnX", "SpawnY", "SpawnZ", "SpawnAngle"}) {
        level.remove(key);
    }
    level.set("spawn", NbtTag::ofCompound(std::move(spawn)));
}

// parity: `GameRuleRegistryFix`.
void gameRuleRegistry(NbtCompound& level, const Context&) {
    if (const NbtCompound* rules = compoundAt(level, "GameRules"); rules != nullptr) {
        NbtCompound converted = gameRulesOfLegacy(*rules);
        level.remove("GameRules");
        level.set("game_rules", NbtTag::ofCompound(std::move(converted)));
    }
}

// parity: `DayTimeToClockFix`.
void dayTimeToClock(NbtCompound& level, const Context&) {
    const std::int64_t gameTime = whole(level.find("Time")).value_or(0);
    const std::int64_t dayTime = whole(level.find("DayTime")).value_or(gameTime);
    level.remove("DayTime");
    NbtCompound clock;
    clock.putLong("total_ticks", dayTime);
    clock.putBoolean("paused", false);
    NbtCompound clocks;
    clocks.set("minecraft:overworld", NbtTag::ofCompound(std::move(clock)));
    level.set("world_clocks", NbtTag::ofCompound(std::move(clocks)));
}

// parity: `LevelDatDifficultyFix`.
void levelDifficulty(NbtCompound& level, const Context&) {
    static constexpr std::array<std::string_view, 4> kNames{"peaceful", "easy", "normal", "hard"};
    const std::int32_t difficulty = intOf(level, "Difficulty", 2);
    NbtCompound settings;
    settings.putString(
        "difficulty",
        std::string{difficulty >= 0 && difficulty < 4 ? kNames[static_cast<std::size_t>(difficulty)] : "normal"});
    settings.putBoolean("hardcore", boolOf(level, "hardcore", false));
    settings.putBoolean("locked", boolOf(level, "DifficultyLocked", false));
    level.set("difficulty_settings", NbtTag::ofCompound(std::move(settings)));
    level.remove("Difficulty");
    level.remove("hardcore");
    level.remove("DifficultyLocked");
}

// parity: `LevelDatToSavedDataPreparationFix`.
void levelSavedDataPreparation(NbtCompound& level, const Context&) {
    renameAndFix(level, "DragonFight", "dragon_fight", [](NbtTag& fight) {
        if (const NbtCompound* legacy = fight.get<NbtCompound>(); legacy != nullptr) {
            fight = NbtTag::ofCompound(dragonFightOfLegacy(*legacy));
        }
    });
    NbtCompound trader;
    trader.putInt("spawn_delay", intOf(level, "WanderingTraderSpawnDelay", 24000));
    trader.putInt("spawn_chance", intOf(level, "WanderingTraderSpawnChance", 25));
    level.set("wandering_trader_migration_data", NbtTag::ofCompound(std::move(trader)));
    level.remove("WanderingTraderSpawnDelay");
    level.remove("WanderingTraderSpawnChance");
    level.remove("WanderingTraderId");
    NbtCompound weather;
    weather.putInt("clear_weather_time", intOf(level, "clearWeatherTime", 0));
    weather.putInt("rain_time", intOf(level, "rainTime", 0));
    weather.putInt("thunder_time", intOf(level, "thunderTime", 0));
    weather.putBoolean("raining", boolOf(level, "raining", false));
    weather.putBoolean("thundering", boolOf(level, "thundering", false));
    for (const std::string_view key : {"clearWeatherTime", "rainTime", "thunderTime", "raining", "thundering"}) {
        level.remove(key);
    }
    level.set("weather_data", NbtTag::ofCompound(std::move(weather)));
    std::vector<NbtCompound> events;
    if (const NbtList* scheduled = level.getList("ScheduledEvents"); scheduled != nullptr) {
        for (const NbtTag& element : *scheduled) {
            if (const NbtCompound* held = element.get<NbtCompound>(); held != nullptr) {
                NbtCompound event = *held;
                rename(event, "Name", "id");
                rename(event, "TriggerTime", "trigger_time");
                renameAndFix(event, "Callback", "callback", [](NbtTag& callback) {
                    if (NbtCompound* fields = callback.get<NbtCompound>(); fields != nullptr) {
                        rename(*fields, "Type", "type");
                        rename(*fields, "Name", "id");
                    }
                });
                events.push_back(std::move(event));
            }
        }
    }
    NbtCompound wrapper;
    wrapper.set("events", compoundList(std::move(events)));
    level.remove("ScheduledEvents");
    level.set("scheduled_events", NbtTag::ofCompound(std::move(wrapper)));
    renameAndFix(level, "WorldGenSettings", "world_gen_settings", [](NbtTag& settings) {
        if (NbtCompound* fields = settings.get<NbtCompound>(); fields != nullptr) {
            rename(*fields, "generate_features", "generate_structures");
        }
    });
}

} // namespace

void addLevelFixes(std::vector<Fix>& fixes) {
    fixes.insert(fixes.end(),
                 {
                     {1451, Target::Root, {}, flatGeneratorInfo, "LevelFlatGeneratorInfoFix", kLevel},
                     {1506, Target::Root, {}, generatorOptions, "LevelDataGeneratorOptionsFix", kLevel},
                     {2514, Target::Root, {}, levelUuids, "LevelUUIDFix", kLevel},
                     {2538, Target::Root, {}, legacyWorldGenSettings, "LevelLegacyWorldGenSettingsFix", kLevel},
                     {3440, Target::Root, {}, removeUpdate120, "FeatureFlagRemoveFix", kLevel},
                     {3459, Target::Root, {}, legacyDragonFight, "LegacyDragonFightFix", kLevel},
                     {3939, Target::Root, {}, removeUpdate121, "FeatureFlagRemoveFix", kLevel},
                     {4067, Target::Root, {}, removeBundle, "FeatureFlagRemoveFix", kLevel},
                     {4180, Target::Root, {}, removeWinterDrop, "FeatureFlagRemoveFix", kLevel},
                     {4424, Target::Root, {}, removeLocatorBar, "FeatureFlagRemoveFix", kLevel},
                     {4544, Target::Root, {}, legacyWorldBorder, "LegacyWorldBorderFix", kLevel},
                     {4548, Target::Root, {}, worldSpawn, "WorldSpawnDataFix", kLevel},
                     {4658, Target::Root, {}, gameRuleRegistry, "GameRuleRegistryFix", kLevel},
                     {4766, Target::Root, {}, dayTimeToClock, "DayTimeToClockFix", kLevel},
                     {4771, Target::Root, {}, levelDifficulty, "LevelDatDifficultyFix", kLevel},
                     {4771, Target::Root, {}, levelSavedDataPreparation, "LevelDatToSavedDataPreparationFix", kLevel},
                 });
}

} // namespace basalt::metamorph::datafix
