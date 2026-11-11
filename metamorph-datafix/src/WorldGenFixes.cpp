#include "DomainSupport.hpp"
#include "Tables.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace basalt::metamorph::datafix {
namespace {

using namespace support;
using wire::nbt::NbtCompound;
using wire::nbt::NbtList;
using wire::nbt::NbtTag;
using wire::nbt::NbtType;

constexpr std::uint32_t kWorldGen = domainBit(Domain::WorldGenSettings);

struct Spread {
    std::int32_t spacing;
    std::int32_t separation;
    std::int32_t salt;
};

const std::map<std::string, Spread>& structureDefaults() {
    static const std::map<std::string, Spread> defaults{
        {"minecraft:village", {32, 8, 10387312}},   {"minecraft:desert_pyramid", {32, 8, 14357617}},
        {"minecraft:igloo", {32, 8, 14357618}},     {"minecraft:jungle_pyramid", {32, 8, 14357619}},
        {"minecraft:swamp_hut", {32, 8, 14357620}}, {"minecraft:pillager_outpost", {32, 8, 165745296}},
        {"minecraft:monument", {32, 5, 10387313}},  {"minecraft:endcity", {20, 11, 10387313}},
        {"minecraft:mansion", {80, 20, 10387319}},
    };
    return defaults;
}

[[nodiscard]] std::int32_t parsedInt(const std::string& text, std::int32_t fallback, std::int32_t minimum) {
    char* end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    const std::int32_t value =
        !text.empty() && end == text.c_str() + text.size() ? static_cast<std::int32_t>(parsed) : fallback;
    return std::max(minimum, value);
}

[[nodiscard]] NbtCompound noise(std::int64_t seed, std::string settings, NbtCompound biomeSource) {
    NbtCompound generator;
    generator.putString("type", "minecraft:noise");
    generator.set("biome_source", NbtTag::ofCompound(std::move(biomeSource)));
    generator.putLong("seed", seed);
    generator.putString("settings", std::move(settings));
    return generator;
}

[[nodiscard]] NbtCompound vanillaBiomeSource(std::int64_t seed, bool legacyInitLayer, bool largeBiomes) {
    NbtCompound source;
    source.putString("type", "minecraft:vanilla_layered");
    source.putLong("seed", seed);
    source.putBoolean("large_biomes", largeBiomes);
    if (legacyInitLayer) {
        source.putBoolean("legacy_biome_init_layer", true);
    }
    return source;
}

[[nodiscard]] NbtCompound defaultOverworld(std::int64_t seed) {
    return noise(seed, "minecraft:overworld", vanillaBiomeSource(seed, false, false));
}

[[nodiscard]] NbtCompound vanillaLevels(std::int64_t seed, NbtCompound overworldGenerator, bool caves) {
    NbtCompound overworld;
    overworld.putString("type", caves ? "minecraft:overworld_caves" : "minecraft:overworld");
    overworld.set("generator", NbtTag::ofCompound(std::move(overworldGenerator)));
    NbtCompound netherSource;
    netherSource.putString("type", "minecraft:multi_noise");
    netherSource.putLong("seed", seed);
    netherSource.putString("preset", "minecraft:nether");
    NbtCompound nether;
    nether.putString("type", "minecraft:the_nether");
    nether.set("generator", NbtTag::ofCompound(noise(seed, "minecraft:nether", std::move(netherSource))));
    NbtCompound endSource;
    endSource.putString("type", "minecraft:the_end");
    endSource.putLong("seed", seed);
    NbtCompound end;
    end.putString("type", "minecraft:the_end");
    end.set("generator", NbtTag::ofCompound(noise(seed, "minecraft:end", std::move(endSource))));
    NbtCompound levels;
    levels.set("minecraft:overworld", NbtTag::ofCompound(std::move(overworld)));
    levels.set("minecraft:the_nether", NbtTag::ofCompound(std::move(nether)));
    levels.set("minecraft:the_end", NbtTag::ofCompound(std::move(end)));
    return levels;
}

[[nodiscard]] NbtCompound spreadTag(const Spread& spread) {
    NbtCompound tag;
    tag.putInt("spacing", spread.spacing);
    tag.putInt("separation", spread.separation);
    tag.putInt("salt", spread.salt);
    return tag;
}

class FlatStructures {
public:
    explicit FlatStructures(const NbtCompound* settings) : settings_(settings) {}

    [[nodiscard]] NbtCompound build() {
        if (settings_ == nullptr) {
            hasStronghold_ = true;
            spreads_["minecraft:village"] = structureDefaults().at("minecraft:village");
        } else if (const NbtCompound* structures = settings_->getCompound("structures"); structures != nullptr) {
            for (std::size_t i = 0; i < structures->size(); ++i) {
                if (const NbtCompound* options = structures->values()[i].get<NbtCompound>(); options != nullptr) {
                    for (std::size_t o = 0; o < options->size(); ++o) {
                        visit(structures->names()[i], options->names()[o],
                              std::string{options->getString(options->names()[o])});
                    }
                }
            }
        }
        NbtCompound configured;
        for (const auto& [name, spread] : spreads_) {
            configured.set(name, NbtTag::ofCompound(spreadTag(spread)));
        }
        NbtCompound result;
        result.set("structures", NbtTag::ofCompound(std::move(configured)));
        if (hasStronghold_) {
            NbtCompound stronghold;
            stronghold.putInt("distance", distance_);
            stronghold.putInt("spread", spread_);
            stronghold.putInt("count", count_);
            result.set("stronghold", NbtTag::ofCompound(std::move(stronghold)));
        }
        return result;
    }

private:
    void spacing(const std::string& id, const std::string& value, std::int32_t minimum) {
        Spread config = current(id);
        config.spacing = parsedInt(value, config.spacing, minimum);
        spreads_[id] = config;
    }

    Spread current(const std::string& id) {
        const auto found = spreads_.find(id);
        return found != spreads_.end() ? found->second : structureDefaults().at(id);
    }

    void visit(const std::string& structure, const std::string& option, const std::string& value) {
        if (structure == "stronghold") {
            hasStronghold_ = true;
            if (option == "distance") {
                distance_ = parsedInt(value, distance_, 1);
            } else if (option == "spread") {
                spread_ = parsedInt(value, spread_, 1);
            } else if (option == "count") {
                count_ = parsedInt(value, count_, 1);
            }
        } else if (option == "distance") {
            if (structure == "village") {
                spacing("minecraft:village", value, 9);
            } else if (structure == "biome_1") {
                for (const char* id : {"minecraft:desert_pyramid", "minecraft:igloo", "minecraft:jungle_pyramid",
                                       "minecraft:swamp_hut", "minecraft:pillager_outpost"}) {
                    spacing(id, value, 9);
                }
            } else if (structure == "endcity") {
                spacing("minecraft:endcity", value, 1);
            } else if (structure == "mansion") {
                spacing("minecraft:mansion", value, 1);
            }
        } else if (option == "separation" && structure == "oceanmonument") {
            Spread config = current("minecraft:monument");
            config.spacing = parsedInt(value, config.separation, 1);
            spreads_["minecraft:monument"] = config;
        } else if (option == "spacing" && structure == "oceanmonument") {
            spacing("minecraft:monument", value, 1);
        }
    }

    const NbtCompound* settings_;
    std::map<std::string, Spread> spreads_;
    bool hasStronghold_ = false;
    std::int32_t distance_ = 32;
    std::int32_t spread_ = 3;
    std::int32_t count_ = 128;
};

[[nodiscard]] NbtTag defaultFlatLayers() {
    std::vector<NbtCompound> layers(3);
    layers[0].putInt("height", 1);
    layers[0].putString("block", "minecraft:bedrock");
    layers[1].putInt("height", 2);
    layers[1].putString("block", "minecraft:dirt");
    layers[2].putInt("height", 1);
    layers[2].putString("block", "minecraft:grass_block");
    return compoundList(std::move(layers));
}

[[nodiscard]] std::string lowerCase(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// parity: `WorldGenSettingsFix`.
void buildWorldGenSettings(NbtCompound& input, const Context&) {
    const std::int64_t seed = whole(input.find("RandomSeed")).value_or(0);
    const std::optional<std::string> name = stringOf(input.find("generatorName"));
    const std::string generatorName = name.has_value() ? lowerCase(*name) : std::string{};
    std::optional<std::string> legacyCustom = stringOf(input.find("legacy_custom_options"));
    if (!legacyCustom.has_value() && generatorName == "customized") {
        legacyCustom = stringOf(input.find("generatorOptions"));
    }
    bool caves = false;
    NbtCompound generator;
    if (!name.has_value() || generatorName == "customized") {
        generator = defaultOverworld(seed);
    } else if (generatorName == "flat") {
        const NbtCompound* flat = input.getCompound("generatorOptions");
        NbtCompound settings;
        settings.set("structures", NbtTag::ofCompound(FlatStructures{flat}.build()));
        const NbtTag* layers = flat == nullptr ? nullptr : flat->find("layers");
        settings.set("layers", layers != nullptr ? *layers : defaultFlatLayers());
        settings.putString("biome", flat == nullptr ? "minecraft:plains"
                                                    : std::string{flat->getString("biome", "minecraft:plains")});
        generator.putString("type", "minecraft:flat");
        generator.set("settings", NbtTag::ofCompound(std::move(settings)));
    } else if (generatorName == "debug_all_block_states") {
        generator.putString("type", "minecraft:debug");
    } else if (generatorName == "buffet") {
        const NbtCompound* options = input.getCompound("generatorOptions");
        const NbtCompound* chunkGenerator = options == nullptr ? nullptr : options->getCompound("chunk_generator");
        const std::string_view type =
            chunkGenerator == nullptr ? std::string_view{} : chunkGenerator->getString("type");
        std::string settings = "minecraft:overworld";
        if (type == "minecraft:caves") {
            settings = "minecraft:caves";
            caves = true;
        } else if (type == "minecraft:floating_islands") {
            settings = "minecraft:floating_islands";
        }
        NbtCompound source;
        if (const NbtCompound* held = options == nullptr ? nullptr : options->getCompound("biome_source");
            held != nullptr) {
            source = *held;
        } else {
            source.putString("type", "minecraft:fixed");
        }
        if (source.getString("type") == "minecraft:fixed") {
            std::string biome = "minecraft:ocean";
            if (const NbtCompound* fixedOptions = source.getCompound("options"); fixedOptions != nullptr) {
                if (const NbtList* biomes = fixedOptions->getList("biomes"); biomes != nullptr && !biomes->empty()) {
                    if (const std::string* first = (*biomes)[0].get<std::string>(); first != nullptr) {
                        biome = *first;
                    }
                }
            }
            source.remove("options");
            source.putString("biome", biome);
        }
        generator = noise(seed, std::move(settings), std::move(source));
    } else {
        const bool normal = generatorName == "default";
        const bool legacyInit =
            generatorName == "default_1_1" || (normal && whole(input.find("generatorVersion")).value_or(0) == 0);
        generator = noise(seed, generatorName == "amplified" ? "minecraft:amplified" : "minecraft:overworld",
                          vanillaBiomeSource(seed, legacyInit, generatorName == "largebiomes"));
    }
    const bool mapFeatures = whole(input.find("MapFeatures")).value_or(1) != 0;
    const bool bonusChest = whole(input.find("BonusChest")).value_or(0) != 0;
    NbtCompound result;
    result.putLong("seed", seed);
    result.putBoolean("generate_features", mapFeatures);
    result.putBoolean("bonus_chest", bonusChest);
    result.set("dimensions", NbtTag::ofCompound(vanillaLevels(seed, std::move(generator), caves)));
    if (legacyCustom.has_value()) {
        result.putString("legacy_custom_options", *legacyCustom);
    }
    input = std::move(result);
}

// parity: `MissingDimensionFix`.
void missingDimension(NbtCompound& settings, const Context&) {
    const NbtCompound* dimensions = settings.getCompound("dimensions");
    if (dimensions != nullptr && !dimensions->empty()) {
        return;
    }
    const std::int64_t seed = whole(settings.find("seed")).value_or(0);
    settings.set("dimensions", NbtTag::ofCompound(vanillaLevels(seed, defaultOverworld(seed), false)));
}

void addFlag(NbtCompound& settings, bool value) {
    if (!settings.contains("has_increased_height_already")) {
        settings.putBoolean("has_increased_height_already", value);
    }
}

// parity: `AddFlagIfNotPresentFix` for the height flag defaulting to false.
void addHeightFlagOff(NbtCompound& settings, const Context&) { addFlag(settings, false); }

// parity: `AddFlagIfNotPresentFix` for the height flag defaulting to true.
void addHeightFlagOn(NbtCompound& settings, const Context&) { addFlag(settings, true); }

void eachDimension(NbtCompound& settings, const std::function<void(NbtCompound&)>& edit) {
    NbtCompound* dimensions = compoundAt(settings, "dimensions");
    for (std::size_t i = 0; dimensions != nullptr && i < dimensions->size(); ++i) {
        if (NbtCompound* dimension = childAt(*dimensions, i); dimension != nullptr) {
            edit(*dimension);
        }
    }
}

void flatLayers(NbtCompound& generator) {
    NbtCompound* settings = compoundAt(generator, "settings");
    NbtList* layers = settings == nullptr ? nullptr : listAt(*settings, "layers");
    if (layers == nullptr) {
        return;
    }
    std::vector<NbtTag> rebuilt;
    NbtCompound air;
    air.putInt("height", 64);
    air.putString("block", "minecraft:air");
    rebuilt.push_back(NbtTag::ofCompound(std::move(air)));
    rebuilt.insert(rebuilt.end(), layers->begin(), layers->end());
    *layers = NbtList{NbtType::Compound, std::move(rebuilt)};
}

// parity: `WorldGenSettingsHeightAndBiomeFix`.
void heightAndBiome(NbtCompound& settings, const Context&) {
    const NbtTag* flag = settings.find("has_increased_height_already");
    const bool wasExperimentalSnapshot = flag == nullptr;
    const bool previouslyIncreased = flag == nullptr ? true : whole(flag).value_or(1) != 0;
    settings.remove("has_increased_height_already");
    NbtCompound* dimensions = compoundAt(settings, "dimensions");
    NbtCompound* overworld = dimensions == nullptr ? nullptr : compoundAt(*dimensions, "minecraft:overworld");
    NbtCompound* generator = overworld == nullptr ? nullptr : compoundAt(*overworld, "generator");
    if (generator == nullptr) {
        return;
    }
    const std::string_view type = generator->getString("type");
    if (type == "minecraft:noise") {
        bool largeBiomes = false;
        if (NbtCompound* source = compoundAt(*generator, "biome_source"); source != nullptr) {
            const std::string_view sourceType = source->getString("type");
            if (sourceType == "minecraft:vanilla_layered" ||
                (wasExperimentalSnapshot && sourceType == "minecraft:multi_noise")) {
                largeBiomes = whole(source->find("large_biomes")).value_or(0) != 0;
                NbtCompound replaced;
                replaced.putString("preset", "minecraft:overworld");
                replaced.putString("type", "minecraft:multi_noise");
                *source = std::move(replaced);
            }
        }
        if (largeBiomes && generator->getString("settings") == "minecraft:overworld") {
            generator->putString("settings", "minecraft:large_biomes");
        }
    } else if (type == "minecraft:flat" && !previouslyIncreased) {
        flatLayers(*generator);
    }
}

// parity: `WorldGenSettingsDisallowOldCustomWorldsFix`.
void disallowOldCustomWorlds(NbtCompound& settings, const Context& context) {
    eachDimension(settings, [&context](NbtCompound& dimension) {
        if (!dimension.contains("type")) {
            refuse(context, "Unable load old custom worlds.");
        }
    });
}

// parity: `StructureSettingsFlattenFix`.
void flattenStructureSettings(NbtCompound& settings, const Context&) {
    eachDimension(settings, [](NbtCompound& dimension) {
        NbtCompound* generator = compoundAt(dimension, "generator");
        NbtCompound* generatorSettings = generator == nullptr ? nullptr : compoundAt(*generator, "settings");
        NbtCompound* input = generatorSettings == nullptr ? nullptr : compoundAt(*generatorSettings, "structures");
        if (input == nullptr) {
            return;
        }
        NbtCompound structures;
        if (const NbtCompound* held = input->getCompound("structures"); held != nullptr) {
            structures = *held;
        }
        for (std::size_t i = 0; i < structures.size(); ++i) {
            if (NbtCompound* entry = childAt(structures, i); entry != nullptr) {
                entry->putString("type", "minecraft:random_spread");
            }
        }
        if (const NbtCompound* stronghold = input->getCompound("stronghold"); stronghold != nullptr) {
            NbtCompound ring = *stronghold;
            ring.putString("type", "minecraft:concentric_rings");
            structures.set("minecraft:stronghold", NbtTag::ofCompound(std::move(ring)));
        }
        *input = std::move(structures);
    });
}

[[nodiscard]] std::string ensureNamespaced(std::string_view id) { return namespaced(id); }

void renameBiomes(NbtCompound& settings, const std::function<std::optional<std::string>(const std::string&)>& renamer) {
    const auto renameTag = [&renamer](NbtTag& tag) {
        if (const std::string* name = tag.get<std::string>(); name != nullptr) {
            if (const std::optional<std::string> renamed = renamer(ensureNamespaced(*name)); renamed.has_value()) {
                tag = NbtTag::ofString(*renamed);
            }
        }
    };
    const auto renameField = [&renameTag](NbtCompound& holder, std::string_view key) {
        if (NbtTag* tag = holder.find(key); tag != nullptr) {
            renameTag(*tag);
        }
    };
    eachDimension(settings, [&](NbtCompound& dimension) {
        NbtCompound* generator = compoundAt(dimension, "generator");
        if (generator == nullptr) {
            return;
        }
        if (NbtCompound* source = compoundAt(*generator, "biome_source"); source != nullptr) {
            renameField(*source, "biome");
            forEachCompound(*source, "biomes", [&](NbtCompound& entry) { renameField(entry, "biome"); });
            if (NbtList* biomes = listAt(*source, "biomes"); biomes != nullptr) {
                for (std::size_t i = 0; i < biomes->size(); ++i) {
                    renameTag((*biomes)[i]);
                }
            }
        }
        if (NbtCompound* flat = compoundAt(*generator, "settings"); flat != nullptr) {
            renameField(*flat, "biome");
        }
    });
}

constexpr std::array<std::pair<std::string_view, std::string_view>, 53> kBiomes1162{{
    {"minecraft:extreme_hills", "minecraft:mountains"},
    {"minecraft:swampland", "minecraft:swamp"},
    {"minecraft:hell", "minecraft:nether_wastes"},
    {"minecraft:sky", "minecraft:the_end"},
    {"minecraft:ice_flats", "minecraft:snowy_tundra"},
    {"minecraft:ice_mountains", "minecraft:snowy_mountains"},
    {"minecraft:mushroom_island", "minecraft:mushroom_fields"},
    {"minecraft:mushroom_island_shore", "minecraft:mushroom_field_shore"},
    {"minecraft:beaches", "minecraft:beach"},
    {"minecraft:forest_hills", "minecraft:wooded_hills"},
    {"minecraft:smaller_extreme_hills", "minecraft:mountain_edge"},
    {"minecraft:stone_beach", "minecraft:stone_shore"},
    {"minecraft:cold_beach", "minecraft:snowy_beach"},
    {"minecraft:roofed_forest", "minecraft:dark_forest"},
    {"minecraft:taiga_cold", "minecraft:snowy_taiga"},
    {"minecraft:taiga_cold_hills", "minecraft:snowy_taiga_hills"},
    {"minecraft:redwood_taiga", "minecraft:giant_tree_taiga"},
    {"minecraft:redwood_taiga_hills", "minecraft:giant_tree_taiga_hills"},
    {"minecraft:extreme_hills_with_trees", "minecraft:wooded_mountains"},
    {"minecraft:savanna_rock", "minecraft:savanna_plateau"},
    {"minecraft:mesa", "minecraft:badlands"},
    {"minecraft:mesa_rock", "minecraft:wooded_badlands_plateau"},
    {"minecraft:mesa_clear_rock", "minecraft:badlands_plateau"},
    {"minecraft:sky_island_low", "minecraft:small_end_islands"},
    {"minecraft:sky_island_medium", "minecraft:end_midlands"},
    {"minecraft:sky_island_high", "minecraft:end_highlands"},
    {"minecraft:sky_island_barren", "minecraft:end_barrens"},
    {"minecraft:void", "minecraft:the_void"},
    {"minecraft:mutated_plains", "minecraft:sunflower_plains"},
    {"minecraft:mutated_desert", "minecraft:desert_lakes"},
    {"minecraft:mutated_extreme_hills", "minecraft:gravelly_mountains"},
    {"minecraft:mutated_forest", "minecraft:flower_forest"},
    {"minecraft:mutated_taiga", "minecraft:taiga_mountains"},
    {"minecraft:mutated_swampland", "minecraft:swamp_hills"},
    {"minecraft:mutated_ice_flats", "minecraft:ice_spikes"},
    {"minecraft:mutated_jungle", "minecraft:modified_jungle"},
    {"minecraft:mutated_jungle_edge", "minecraft:modified_jungle_edge"},
    {"minecraft:mutated_birch_forest", "minecraft:tall_birch_forest"},
    {"minecraft:mutated_birch_forest_hills", "minecraft:tall_birch_hills"},
    {"minecraft:mutated_roofed_forest", "minecraft:dark_forest_hills"},
    {"minecraft:mutated_taiga_cold", "minecraft:snowy_taiga_mountains"},
    {"minecraft:mutated_redwood_taiga", "minecraft:giant_spruce_taiga"},
    {"minecraft:mutated_redwood_taiga_hills", "minecraft:giant_spruce_taiga_hills"},
    {"minecraft:mutated_extreme_hills_with_trees", "minecraft:modified_gravelly_mountains"},
    {"minecraft:mutated_savanna", "minecraft:shattered_savanna"},
    {"minecraft:mutated_savanna_rock", "minecraft:shattered_savanna_plateau"},
    {"minecraft:mutated_mesa", "minecraft:eroded_badlands"},
    {"minecraft:mutated_mesa_rock", "minecraft:modified_wooded_badlands_plateau"},
    {"minecraft:mutated_mesa_clear_rock", "minecraft:modified_badlands_plateau"},
    {"minecraft:warm_deep_ocean", "minecraft:deep_warm_ocean"},
    {"minecraft:lukewarm_deep_ocean", "minecraft:deep_lukewarm_ocean"},
    {"minecraft:cold_deep_ocean", "minecraft:deep_cold_ocean"},
    {"minecraft:frozen_deep_ocean", "minecraft:deep_frozen_ocean"},
}};

// parity: `NamespacedTypeRenameFix` over biomes, for the nether rename.
void netherWastesBiome(NbtCompound& settings, const Context&) {
    renameBiomes(settings, [](const std::string& name) -> std::optional<std::string> {
        return name == "minecraft:nether" ? std::optional<std::string>{"minecraft:nether_wastes"} : std::nullopt;
    });
}

// parity: `NamespacedTypeRenameFix` over biomes, with `BiomeFix.BIOMES`.
void renamedBiomes1162(NbtCompound& settings, const Context&) {
    renameBiomes(settings, [](const std::string& name) -> std::optional<std::string> {
        for (const auto& [from, to] : kBiomes1162) {
            if (from == name) {
                return std::string{to};
            }
        }
        return std::nullopt;
    });
}

// parity: `NamespacedTypeRenameFix` over biomes, for the caves and cliffs renames.
void cavesAndCliffsBiomes(NbtCompound& settings, const Context&) {
    renameBiomes(settings, [](const std::string& name) -> std::optional<std::string> {
        for (const tables::Pair& pair : tables::kCavesAndCliffsBiomes) {
            if (pair.from == name) {
                return std::string{pair.to};
            }
        }
        return std::nullopt;
    });
}

// parity: `NamespacedTypeRenameFix` over biomes, for the removed deep warm ocean.
void deepWarmOcean(NbtCompound& settings, const Context&) {
    renameBiomes(settings, [](const std::string& name) -> std::optional<std::string> {
        return name == "minecraft:deep_warm_ocean" ? std::optional<std::string>{"minecraft:warm_ocean"} : std::nullopt;
    });
}

// parity: `NamespacedTypeRenameFix` over multi noise presets, for the experimental 1.20 overworld.
void experimentalOverworldPreset(NbtCompound& settings, const Context&) {
    eachDimension(settings, [](NbtCompound& dimension) {
        NbtCompound* generator = compoundAt(dimension, "generator");
        NbtCompound* source = generator == nullptr ? nullptr : compoundAt(*generator, "biome_source");
        const NbtTag* preset = source == nullptr ? nullptr : source->find("preset");
        const std::string* name = preset == nullptr ? nullptr : preset->get<std::string>();
        if (name != nullptr && ensureNamespaced(*name) == "minecraft:overworld_update_1_20") {
            source->putString("preset", "minecraft:overworld");
        }
    });
}

} // namespace

void addWorldGenFixes(std::vector<Fix>& fixes) {
    fixes.insert(
        fixes.end(),
        {
            {2550, Target::Root, {}, buildWorldGenSettings, "WorldGenSettingsFix", kWorldGen},
            {2552, Target::Root, {}, netherWastesBiome, "NamespacedTypeRenameFix", kWorldGen},
            {2553, Target::Root, {}, renamedBiomes1162, "NamespacedTypeRenameFix", kWorldGen},
            {2558, Target::Root, {}, missingDimension, "MissingDimensionFix", kWorldGen},
            {2693, Target::Root, {}, addHeightFlagOff, "AddFlagIfNotPresentFix", kWorldGen},
            {2707, Target::Root, {}, addHeightFlagOn, "AddFlagIfNotPresentFix", kWorldGen},
            {2825, Target::Root, {}, addHeightFlagOff, "AddFlagIfNotPresentFix", kWorldGen},
            {2832, Target::Root, {}, heightAndBiome, "WorldGenSettingsHeightAndBiomeFix", kWorldGen},
            {2833, Target::Root, {}, disallowOldCustomWorlds, "WorldGenSettingsDisallowOldCustomWorldsFix", kWorldGen},
            {2838, Target::Root, {}, cavesAndCliffsBiomes, "NamespacedTypeRenameFix", kWorldGen},
            {2843, Target::Root, {}, deepWarmOcean, "NamespacedTypeRenameFix", kWorldGen},
            {2852, Target::Root, {}, disallowOldCustomWorlds, "WorldGenSettingsDisallowOldCustomWorldsFix", kWorldGen},
            {2967, Target::Root, {}, flattenStructureSettings, "StructureSettingsFlattenFix", kWorldGen},
            {3440, Target::Root, {}, experimentalOverworldPreset, "NamespacedTypeRenameFix", kWorldGen},
        });
}

} // namespace basalt::metamorph::datafix
