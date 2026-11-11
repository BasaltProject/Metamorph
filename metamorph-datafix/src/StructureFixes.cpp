#include "DomainSupport.hpp"
#include "LegacyStates.hpp"
#include "StructureRenames.hpp"

#include <algorithm>
#include <iterator>
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

constexpr std::string_view kBlockMarker = "__basalt_block";
constexpr std::string_view kEntityMarker = "__basalt_entity";
constexpr std::uint32_t kStructure = domainBit(Domain::Structure);
constexpr std::uint32_t kPoi = domainBit(Domain::PoiChunk);
constexpr std::uint32_t kStartDomains = domainBit(Domain::Chunk) | domainBit(Domain::SavedData);

[[nodiscard]] NbtTag compoundTagList(std::vector<NbtTag> elements) {
    return NbtTag::ofList(NbtList{NbtType::Compound, std::move(elements)});
}

[[nodiscard]] NbtCompound paletteSection(const NbtTag& palette) {
    NbtCompound states;
    states.set("palette", palette);
    NbtCompound section;
    section.set("block_states", NbtTag::ofCompound(std::move(states)));
    return section;
}

/// The nbt of each block or entity of a template that has one, tagged with where it came from.
[[nodiscard]] NbtTag taggedNbt(const NbtCompound& structure, std::string_view listKey, std::string_view marker) {
    std::vector<NbtTag> found;
    const NbtList* list = structure.getList(listKey);
    for (std::size_t i = 0; list != nullptr && i < list->size(); ++i) {
        const NbtCompound* element = (*list)[i].get<NbtCompound>();
        const NbtCompound* nbt = element == nullptr ? nullptr : element->getCompound("nbt");
        if (nbt != nullptr) {
            NbtCompound copy = *nbt;
            copy.putInt(marker, static_cast<std::int32_t>(i));
            found.push_back(NbtTag::ofCompound(std::move(copy)));
        }
    }
    return compoundTagList(std::move(found));
}

void putBackNbt(NbtCompound& structure, std::string_view listKey, std::string_view marker, const NbtList* fixed) {
    NbtList* list = listAt(structure, listKey);
    if (list == nullptr) {
        return;
    }
    std::map<std::int32_t, NbtCompound> byIndex;
    for (std::size_t i = 0; fixed != nullptr && i < fixed->size(); ++i) {
        if (const NbtCompound* nbt = (*fixed)[i].get<NbtCompound>(); nbt != nullptr) {
            NbtCompound copy = *nbt;
            if (const std::optional<std::int64_t> index = whole(copy.find(marker)); index.has_value()) {
                copy.remove(marker);
                byIndex[static_cast<std::int32_t>(*index)] = std::move(copy);
            }
        }
    }
    for (std::size_t i = 0; i < list->size(); ++i) {
        NbtCompound* element = (*list)[i].get<NbtCompound>();
        if (element == nullptr || !element->contains("nbt")) {
            continue;
        }
        if (const auto found = byIndex.find(static_cast<std::int32_t>(i)); found != byIndex.end()) {
            element->set("nbt", NbtTag::ofCompound(found->second));
        } else {
            element->remove("nbt");
        }
    }
}

} // namespace

NbtCompound structureAsChunk(const NbtCompound& structure) {
    std::vector<NbtTag> sections;
    if (const NbtTag* palette = structure.find("palette"); palette != nullptr) {
        sections.push_back(NbtTag::ofCompound(paletteSection(*palette)));
    }
    if (const NbtList* palettes = structure.getList("palettes"); palettes != nullptr) {
        for (const NbtTag& palette : *palettes) {
            sections.push_back(NbtTag::ofCompound(paletteSection(palette)));
        }
    }
    NbtCompound chunk;
    chunk.set("sections", compoundTagList(std::move(sections)));
    chunk.set("block_entities", taggedNbt(structure, "blocks", kBlockMarker));
    chunk.set("entities", taggedNbt(structure, "entities", kEntityMarker));
    return chunk;
}

void chunkIntoStructure(const NbtCompound& chunk, NbtCompound& structure) {
    const NbtList* sections = chunk.getList("sections");
    std::size_t next = 0;
    const auto paletteOf = [&]() -> const NbtTag* {
        const NbtCompound* section =
            sections != nullptr && next < sections->size() ? (*sections)[next].get<NbtCompound>() : nullptr;
        ++next;
        const NbtCompound* states = section == nullptr ? nullptr : section->getCompound("block_states");
        return states == nullptr ? nullptr : states->find("palette");
    };
    if (structure.contains("palette")) {
        if (const NbtTag* palette = paletteOf(); palette != nullptr) {
            structure.set("palette", *palette);
        }
    }
    if (NbtList* palettes = listAt(structure, "palettes"); palettes != nullptr) {
        for (std::size_t i = 0; i < palettes->size(); ++i) {
            if (const NbtTag* palette = paletteOf(); palette != nullptr) {
                (*palettes)[i] = *palette;
            }
        }
    }
    putBackNbt(structure, "blocks", kBlockMarker, chunk.getList("block_entities"));
    putBackNbt(structure, "entities", kEntityMarker, chunk.getList("entities"));
}

namespace {

[[nodiscard]] std::map<std::string, std::string> propertiesOf(const NbtCompound& entry) {
    std::map<std::string, std::string> properties;
    if (const NbtCompound* held = entry.getCompound("Properties"); held != nullptr) {
        for (std::size_t i = 0; i < held->size(); ++i) {
            if (const std::string* value = held->values()[i].get<std::string>(); value != nullptr) {
                properties.emplace(held->names()[i], *value);
            }
        }
    }
    return properties;
}

// parity: `BlockStateStructureTemplateFix`, through `BlockStateData.upgradeBlockStateTag`.
void blockStateOfTemplate(NbtCompound& chunk, const Context&) {
    forEachCompound(chunk, "sections", [](NbtCompound& section) {
        NbtCompound* states = compoundAt(section, "block_states");
        NbtList* palette = states == nullptr ? nullptr : listAt(*states, "palette");
        for (std::size_t i = 0; palette != nullptr && i < palette->size(); ++i) {
            NbtCompound* entry = (*palette)[i].get<NbtCompound>();
            if (entry == nullptr || !entry->contains("Name")) {
                continue;
            }
            const bool onlyKnownKeys = entry->size() == (entry->contains("Properties") ? 2U : 1U);
            const std::optional<legacy::State> state =
                onlyKnownKeys ? legacy::upgradeState(entry->getString("Name"), propertiesOf(*entry)) : std::nullopt;
            if (!state.has_value()) {
                continue;
            }
            NbtCompound upgraded;
            upgraded.putString("Name", state->name);
            if (!state->properties.empty()) {
                NbtCompound properties;
                for (const auto& [key, value] : state->properties) {
                    properties.putString(key, value);
                }
                upgraded.set("Properties", NbtTag::ofCompound(std::move(properties)));
            }
            *entry = std::move(upgraded);
        }
    });
}

void eachStart(NbtCompound& root, const Context& context, const std::function<void(NbtCompound&)>& edit) {
    NbtCompound* starts = nullptr;
    if (!context.savedData.empty()) {
        NbtCompound* data = compoundAt(root, "data");
        starts = data == nullptr ? nullptr : compoundAt(*data, "Features");
    } else if (NbtCompound* level = compoundAt(root, "Level"); level != nullptr) {
        NbtCompound* structures = compoundAt(*level, "Structures");
        starts = structures == nullptr ? nullptr : compoundAt(*structures, "Starts");
    }
    for (std::size_t i = 0; starts != nullptr && i < starts->size(); ++i) {
        if (NbtCompound* start = childAt(*starts, i); start != nullptr) {
            edit(*start);
        }
    }
}

[[nodiscard]] bool isIglooPiece(const NbtTag& piece) {
    const NbtCompound* fields = piece.get<NbtCompound>();
    return fields != nullptr && fields->getString("id") == "Iglu";
}

// parity: `IglooMetadataRemovalFix`.
void iglooMetadata(NbtCompound& root, const Context& context) {
    eachStart(root, context, [](NbtCompound& start) {
        NbtList* children = listAt(start, "Children");
        if (children == nullptr) {
            return;
        }
        if (std::ranges::all_of(*children, isIglooPiece)) {
            start.putString("id", "Igloo");
            start.remove("Children");
            return;
        }
        std::vector<NbtTag> kept;
        std::ranges::copy_if(*children, std::back_inserter(kept),
                             [](const NbtTag& piece) { return !isIglooPiece(piece); });
        *children = NbtList{kept.empty() ? NbtType::End : NbtType::Compound, std::move(kept)};
    });
}

// parity: `ChunkStructuresTemplateRenameFix`.
void structureTemplateRenames(NbtCompound& root, const Context& context) {
    eachStart(root, context, [](NbtCompound& start) {
        for (const structures::PieceRenames& rule : structures::kPieceRenames) {
            if (start.getString("id") != rule.structure) {
                continue;
            }
            forEachCompound(start, "Children", [&rule](NbtCompound& child) {
                if (child.getString("id") != rule.piece) {
                    return;
                }
                const std::string_view current = child.getString("Template");
                for (std::size_t i = 0; i < rule.count; ++i) {
                    if (rule.renames[i].from == current) {
                        child.putString("Template", std::string{rule.renames[i].to});
                        break;
                    }
                }
            });
        }
    });
}

// parity: `NewVillageFix`.
void newVillage(NbtCompound& root, const Context& context) {
    if (context.savedData.empty()) {
        NbtCompound* level = compoundAt(root, "Level");
        NbtCompound* structures = level == nullptr ? nullptr : compoundAt(*level, "Structures");
        if (structures == nullptr) {
            return;
        }
        if (NbtCompound* starts = compoundAt(*structures, "Starts"); starts != nullptr) {
            starts->remove("Village");
            rename(*starts, "New_Village", "Village");
        }
        if (NbtCompound* references = compoundAt(*structures, "References"); references != nullptr) {
            references->remove("New_Village");
            references->remove("Village");
        }
    }
    eachStart(root, context, [](NbtCompound& start) {
        if (namespaced(start.getString("id")) == "minecraft:new_village") {
            start.putString("id", "minecraft:village");
        }
    });
}

// parity: `StructureReferenceCountFix`.
void structureReferenceCount(NbtCompound& root, const Context& context) {
    eachStart(root, context, [](NbtCompound& start) {
        if (const NbtTag* references = start.find("references"); references != nullptr) {
            const std::optional<double> count = real(references);
            start.putInt("references", count.has_value() && *count > 0 ? static_cast<std::int32_t>(*count) : 1);
        }
    });
}

[[nodiscard]] std::string pathString(const NbtCompound& root, std::initializer_list<std::string_view> path) {
    const NbtCompound* current = &root;
    std::size_t index = 0;
    for (const std::string_view key : path) {
        ++index;
        const NbtTag* next = current->find(key);
        if (next == nullptr) {
            return {};
        }
        if (index == path.size()) {
            return stringOf(next).value_or("");
        }
        current = next->get<NbtCompound>();
        if (current == nullptr) {
            return {};
        }
    }
    return {};
}

[[nodiscard]] const NbtCompound* firstOfList(const NbtCompound& holder, std::string_view key) {
    const NbtList* list = holder.getList(key);
    return list == nullptr || list->empty() ? nullptr : (*list)[0].get<NbtCompound>();
}

[[nodiscard]] std::string weighedFirst(const NbtCompound& feature) {
    const NbtCompound* config = feature.getCompound("config");
    const NbtCompound* provider = config == nullptr ? nullptr : config->getCompound("state_provider");
    const NbtCompound* first = provider == nullptr ? nullptr : firstOfList(*provider, "entries");
    const NbtCompound* data = first == nullptr ? nullptr : first->getCompound("data");
    return data == nullptr ? std::string{} : std::string{data->getString("Name")};
}

[[nodiscard]] std::optional<std::string> featureReplacement(const NbtCompound& feature) {
    const std::string typeField = pathString(feature, {"type"});
    const std::string nameField = pathString(feature, {"name"});
    std::string kind = typeField;
    if (kind.empty()) {
        if (nameField.empty()) {
            return std::nullopt;
        }
        kind = nameField == "minecraft:normal_tree" ? "minecraft:tree" : nameField;
    }
    const std::string providerType = pathString(feature, {"config", "state_provider", "type"});
    const std::string providerState = pathString(feature, {"config", "state_provider", "state", "Name"});
    const std::string weighedState = weighedFirst(feature);
    const std::string foliage = pathString(feature, {"config", "foliage_placer", "type"});
    const std::string leaves = pathString(feature, {"config", "leaves_provider", "state", "Name"});
    if (kind == "minecraft:random_patch") {
        if (providerType == "minecraft:simple_state_provider") {
            if (providerState == "minecraft:sweet_berry_bush") {
                return "minecraft:patch_berry_bush";
            }
            if (providerState == "minecraft:cactus") {
                return "minecraft:patch_cactus";
            }
        } else if (providerType == "minecraft:weighted_state_provider" &&
                   (weighedState == "minecraft:grass" || weighedState == "minecraft:fern")) {
            return "minecraft:patch_taiga_grass";
        }
    } else if (kind == "minecraft:block_pile") {
        if (providerType == "minecraft:simple_state_provider" || providerType == "minecraft:rotated_block_provider") {
            if (providerState == "minecraft:hay_block") {
                return "minecraft:pile_hay";
            }
            if (providerState == "minecraft:melon") {
                return "minecraft:pile_melon";
            }
            if (providerState == "minecraft:snow") {
                return "minecraft:pile_snow";
            }
        } else if (providerType == "minecraft:weighted_state_provider") {
            if (weighedState == "minecraft:packed_ice" || weighedState == "minecraft:blue_ice") {
                return "minecraft:pile_ice";
            }
            if (weighedState == "minecraft:jack_o_lantern" || weighedState == "minecraft:pumpkin") {
                return "minecraft:pile_pumpkin";
            }
        }
    } else if (kind == "minecraft:flower") {
        return "minecraft:flower_plain";
    } else if (kind == "minecraft:tree") {
        if (foliage == "minecraft:acacia_foliage_placer") {
            return "minecraft:acacia";
        }
        if (foliage == "minecraft:blob_foliage_placer" && leaves == "minecraft:oak_leaves") {
            return "minecraft:oak";
        }
        if (foliage == "minecraft:pine_foliage_placer") {
            return "minecraft:pine";
        }
        if (foliage == "minecraft:spruce_foliage_placer") {
            return "minecraft:spruce";
        }
    }
    return std::nullopt;
}

// parity: `SavedDataFeaturePoolElementFix`.
void featurePoolElements(NbtCompound& root, const Context& context) {
    static constexpr std::array<std::string_view, 5> kPieces{"minecraft:jigsaw", "minecraft:nvi", "minecraft:pcp",
                                                             "minecraft:bastionremnant", "minecraft:runtime"};
    eachStart(root, context, [](NbtCompound& start) {
        forEachCompound(start, "Children", [](NbtCompound& child) {
            if (std::ranges::find(kPieces, child.getString("id")) == kPieces.end()) {
                return;
            }
            NbtCompound* pool = compoundAt(child, "pool_element");
            if (pool == nullptr || pool->getString("element_type") != "minecraft:feature_pool_element") {
                return;
            }
            NbtTag* feature = pool->find("feature");
            const NbtCompound* fields = feature == nullptr ? nullptr : feature->get<NbtCompound>();
            if (const std::optional<std::string> replacement =
                    fields == nullptr ? std::nullopt : featureReplacement(*fields);
                replacement.has_value()) {
                *feature = NbtTag::ofString(*replacement);
            }
        });
    });
}

void eachPoiSection(NbtCompound& poi, const std::function<void(NbtCompound&)>& edit) {
    NbtCompound* sections = compoundAt(poi, "Sections");
    for (std::size_t i = 0; sections != nullptr && i < sections->size(); ++i) {
        if (NbtCompound* section = childAt(*sections, i); section != nullptr) {
            edit(*section);
        }
    }
}

// parity: `ReorganizePoi`.
void reorganizePoi(NbtCompound& poi, const Context&) {
    NbtCompound sections;
    for (int i = 0; i < 16; ++i) {
        const std::string key = std::to_string(i);
        if (const NbtTag* records = poi.find(key); records != nullptr) {
            NbtCompound section;
            section.set("Records", *records);
            sections.set(key, NbtTag::ofCompound(std::move(section)));
            poi.remove(key);
        }
    }
    poi.set("Sections", NbtTag::ofCompound(std::move(sections)));
}

// parity: `PoiTypeRenameFix` for the bee hive.
void renameBeeHivePoi(NbtCompound& poi, const Context&) {
    eachPoiSection(poi, [](NbtCompound& section) {
        forEachCompound(section, "Records", [](NbtCompound& record) {
            if (const std::optional<std::string> type = stringOf(record.find("type"));
                type.has_value() && namespaced(*type) == "minecraft:bee_hive") {
                record.putString("type", "minecraft:beehive");
            }
        });
    });
}

// parity: `ForcePoiRebuild`.
void forcePoiRebuild(NbtCompound& poi, const Context&) {
    eachPoiSection(poi, [](NbtCompound& section) { section.remove("Valid"); });
}

// parity: `PoiTypeRemoveFix` for the villager types nobody holds.
void removeUnpopulatedPoi(NbtCompound& poi, const Context&) {
    eachPoiSection(poi, [](NbtCompound& section) {
        NbtList* records = listAt(section, "Records");
        if (records == nullptr) {
            return;
        }
        std::vector<NbtTag> kept;
        for (const NbtTag& record : *records) {
            const NbtCompound* fields = record.get<NbtCompound>();
            const std::string_view type = fields == nullptr ? std::string_view{} : fields->getString("type");
            if (fields != nullptr && fields->find("type") != nullptr &&
                fields->find("type")->get<std::string>() != nullptr && type != "minecraft:unemployed" &&
                type != "minecraft:nitwit") {
                kept.push_back(record);
            }
        }
        *records = NbtList{kept.empty() ? NbtType::End : NbtType::Compound, std::move(kept)};
    });
}

} // namespace

void addStructureFixes(std::vector<Fix>& fixes) {
    fixes.insert(
        fixes.end(),
        {
            {1450, Target::Root, {}, blockStateOfTemplate, "BlockStateStructureTemplateFix", kStructure},
            {1488, Target::Root, {}, iglooMetadata, "IglooMetadataRemovalFix", kStartDomains},
            {1492, Target::Root, {}, structureTemplateRenames, "ChunkStructuresTemplateRenameFix", kStartDomains},
            {1920, Target::Root, {}, newVillage, "NewVillageFix", kStartDomains},
            {1946, Target::Root, {}, reorganizePoi, "ReorganizePoi", kPoi},
            {2209, Target::Root, {}, renameBeeHivePoi, "PoiTypeRenameFix", kPoi},
            {2211, Target::Root, {}, structureReferenceCount, "StructureReferenceCountFix", kStartDomains},
            {2218, Target::Root, {}, forcePoiRebuild, "ForcePoiRebuild", kPoi},
            {2701, Target::Root, {}, featurePoolElements, "SavedDataFeaturePoolElementFix", kStartDomains},
            {3097, Target::Root, {}, removeUnpopulatedPoi, "PoiTypeRemoveFix", kPoi},
        });
}

} // namespace basalt::metamorph::datafix
