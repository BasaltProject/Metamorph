#include "FixSupport.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace basalt::metamorph::datafix {
namespace {

using wire::nbt::NbtCompound;
using wire::nbt::NbtList;
using wire::nbt::NbtTag;
using wire::nbt::NbtType;

/// parity: `PotDecorationsComponentUnflatteningFix.unpackList`.
[[nodiscard]] NbtTag unpackPotDecorations(const NbtTag& original) {
    const NbtList* list = original.get<NbtList>();
    if (list == nullptr) {
        return original;
    }
    static constexpr std::array<std::string_view, 4> kSides{"back", "left", "right", "front"};
    NbtCompound result;
    for (std::size_t side = 0; side < kSides.size(); ++side) {
        std::string decoration = "minecraft:brick";
        if (side < list->size()) {
            if (const std::string* named = (*list)[side].get<std::string>(); named != nullptr) {
                decoration = *named;
            }
        }
        if (decoration.empty()) {
            return original;
        }
        NbtCompound stack;
        stack.putString("id", decoration);
        result.set(kSides[side], NbtTag::ofCompound(std::move(stack)));
    }
    return NbtTag::ofCompound(std::move(result));
}

constexpr std::array<std::string_view, 2> kIntermediateStatuses{"minecraft:noise", "minecraft:surface"};

[[nodiscard]] bool intermediateStatus(std::string_view status) {
    return std::ranges::find(kIntermediateStatuses, status) != kIntermediateStatuses.end();
}

[[nodiscard]] std::string mergedStatus(std::string_view status) {
    if (status == "minecraft:carvers") {
        return "minecraft:terrain";
    }
    return intermediateStatus(status) ? "minecraft:biomes" : std::string{status};
}

// parity: `MergeTerrainChunkStatusFix`.
void mergeTerrainStatus(NbtCompound& chunk, const Context&) {
    const std::string oldStatus =
        namespaced(chunk.find("Status") != nullptr && !chunk.getString("Status").empty() ? chunk.getString("Status")
                                                                                         : "minecraft:empty");
    chunk.putString("Status", mergedStatus(oldStatus));
    std::optional<std::string> oldTarget;
    if (NbtTag* retrogen = chunk.find("below_zero_retrogen"); retrogen != nullptr) {
        if (NbtCompound* held = retrogen->get<NbtCompound>(); held != nullptr) {
            const std::string_view target = held->getString("target_status");
            oldTarget = namespaced(target.empty() ? "minecraft:empty" : target);
            held->putString("target_status", mergedStatus(*oldTarget));
        }
    }
    const bool targetIntermediate = oldTarget.has_value() && intermediateStatus(*oldTarget);
    if (!intermediateStatus(oldStatus) && !targetIntermediate) {
        return;
    }
    const bool onlyBelowZero = oldTarget.has_value() && !targetIntermediate;
    chunk.remove("Heightmaps");
    chunk.remove("blending_data");
    forEachCompound(chunk, "sections", [onlyBelowZero](NbtCompound& section) {
        if (section.getByte("Y") >= 0 && onlyBelowZero) {
            return;
        }
        section.remove("block_states");
    });
}

// parity: `BlendingDataFix`, which rebuilds the marker for overworld chunks that were generated.
void rebuildBlendingData(NbtCompound& chunk, const Context& context) {
    chunk.remove("blending_data");
    if (!context.overworld || chunk.find("Status") == nullptr) {
        return;
    }
    const auto skipped = [](std::string_view status) {
        return status == "minecraft:empty" || status == "minecraft:structure_starts" ||
               status == "minecraft:structure_references" || status == "minecraft:biomes";
    };
    const std::string_view raw = chunk.getString("Status");
    const std::string status = namespaced(raw.empty() ? "empty" : raw);
    const auto mark = [&chunk](std::int32_t height, std::int32_t minY) {
        NbtCompound blending;
        blending.putInt("min_section", minY >> 4);
        blending.putInt("max_section", (minY + height) >> 4);
        chunk.set("blending_data", NbtTag::ofCompound(std::move(blending)));
    };
    if (!skipped(status)) {
        mark(384, -64);
    } else if (const NbtCompound* retrogen = chunk.getCompound("below_zero_retrogen"); retrogen != nullptr) {
        const std::string_view rawTarget = retrogen->getString("target_status");
        if (!skipped(namespaced(rawTarget.empty() ? "empty" : rawTarget))) {
            mark(256, 0);
        }
    }
}

// parity: `BlendingDataNoValueHeightFix`.
void blendingHeightsToFloat(NbtCompound& chunk, const Context&) {
    NbtTag* blending = chunk.find("blending_data");
    NbtCompound* held = blending == nullptr ? nullptr : blending->get<NbtCompound>();
    NbtTag* heights = held == nullptr ? nullptr : held->find("heights");
    NbtList* list = heights == nullptr ? nullptr : heights->get<NbtList>();
    if (list == nullptr) {
        return;
    }
    std::vector<NbtTag> fixed;
    for (const NbtTag& height : *list) {
        float value = std::numeric_limits<float>::max();
        if (const double* wide = height.get<double>(); wide != nullptr && *wide != std::numeric_limits<double>::max()) {
            value = static_cast<float>(*wide);
        } else if (const float* narrow = height.get<float>(); narrow != nullptr) {
            value = *narrow;
        }
        fixed.push_back(NbtTag::ofFloat(value));
    }
    *list = NbtList{NbtType::Float, std::move(fixed)};
}

// parity: `BlockStateFieldNamesFix`.
void blockStateFieldNames(NbtCompound& chunk, const Context&) {
    forEachCompound(chunk, "sections", [](NbtCompound& section) {
        NbtTag* states = section.find("block_states");
        NbtCompound* held = states == nullptr ? nullptr : states->get<NbtCompound>();
        if (held == nullptr) {
            return;
        }
        forEachCompound(*held, "palette", [](NbtCompound& entry) {
            rename(entry, "Name", "id");
            rename(entry, "Properties", "properties");
        });
    });
}

// parity: `SignsOpFlagFix`.
void allowSignOpFeatures(NbtCompound& blockEntity, const Context&) {
    blockEntity.putBoolean("allow_op_features", true);
}

// parity: `PotDecorationsBlockEntityUnflatteningFix`.
void unflattenSherds(NbtCompound& blockEntity, const Context&) {
    const NbtTag* sherds = blockEntity.find("sherds");
    blockEntity.set("sherds",
                    unpackPotDecorations(sherds != nullptr ? *sherds : NbtTag::ofList(NbtList{NbtType::String, {}})));
}

// parity: `PotDecorationsComponentUnflatteningFix`.
void unflattenPotDecorations(NbtCompound& components, const Context&) {
    if (const NbtTag* decorations = components.find("minecraft:pot_decorations"); decorations != nullptr) {
        components.set("minecraft:pot_decorations", unpackPotDecorations(*decorations));
    }
}

// parity: `SwingAnimationComponentSplitFix`.
void splitSwingAnimation(NbtCompound& components, const Context&) {
    const NbtTag* swing = components.find("minecraft:swing_animation");
    if (swing == nullptr) {
        return;
    }
    const NbtTag copy = *swing;
    components.remove("minecraft:swing_animation");
    components.set("minecraft:attack_animation", copy);
    components.set("minecraft:interact_animation", copy);
}

// parity: `MapColorComponentRemovalFix`.
void removeMapColor(NbtCompound& components, const Context&) { components.remove("minecraft:map_color"); }

// parity: `RemoveBlockTransformerComponentFix`.
void removeBlockTransformer(NbtCompound& components, const Context&) {
    components.remove("minecraft:block_transformer");
}

// parity: `ExplorerMapItemFix`.
void explorerMapItem(NbtCompound& stack, const Context&) {
    static const std::map<std::string_view, std::pair<std::string_view, std::string_view>> kByDecoration{
        {"minecraft:mansion", {"minecraft:woodland_explorer_map", "filled_map.mansion"}},
        {"minecraft:monument", {"minecraft:ocean_explorer_map", "filled_map.monument"}},
        {"minecraft:trial_chambers", {"minecraft:trial_explorer_map", "filled_map.trial_chambers"}},
        {"minecraft:jungle_temple", {"minecraft:jungle_explorer_map", "filled_map.explorer_jungle"}},
        {"minecraft:swamp_hut", {"minecraft:swamp_explorer_map", "filled_map.explorer_swamp"}},
        {"minecraft:village_desert", {"minecraft:desert_village_map", "filled_map.village_desert"}},
        {"minecraft:village_plains", {"minecraft:plains_village_map", "filled_map.village_plains"}},
        {"minecraft:village_savanna", {"minecraft:savanna_village_map", "filled_map.village_savanna"}},
        {"minecraft:village_snowy", {"minecraft:snowy_village_map", "filled_map.village_snowy"}},
        {"minecraft:village_taiga", {"minecraft:taiga_village_map", "filled_map.village_taiga"}},
        {"minecraft:red_x", {"minecraft:buried_treasure_map", "filled_map.buried_treasure"}}};
    if (stack.getString("id") != "minecraft:filled_map") {
        return;
    }
    NbtTag* heldComponents = stack.find("components");
    NbtCompound* components = heldComponents == nullptr ? nullptr : heldComponents->get<NbtCompound>();
    const NbtCompound* decorations =
        components == nullptr ? nullptr : components->getCompound("minecraft:map_decorations");
    const NbtCompound* explorer = decorations == nullptr ? nullptr : decorations->getCompound("+");
    if (explorer == nullptr) {
        return;
    }
    const auto found = kByDecoration.find(namespaced(explorer->getString("type")));
    if (found == kByDecoration.end()) {
        return;
    }
    stack.putString("id", std::string{found->second.first});
    if (plainTranslationKey(components->find("minecraft:item_name")) == found->second.second) {
        components->remove("minecraft:item_name");
    }
}

// parity: `ItemRenameFix` over the explorer map ids.
void renameExplorerMaps(NbtCompound& stack, const Context&) {
    static const std::map<std::string_view, std::string_view> kRenames{
        {"minecraft:ocean_explorer_map", "minecraft:ocean_monument_map"},
        {"minecraft:swamp_explorer_map", "minecraft:swamp_hut_map"},
        {"minecraft:trial_explorer_map", "minecraft:buried_trial_chambers_map"},
        {"minecraft:woodland_explorer_map", "minecraft:woodland_mansion_map"},
        {"minecraft:jungle_explorer_map", "minecraft:jungle_pyramid_map"}};
    if (const auto found = kRenames.find(stack.getString("id")); found != kRenames.end()) {
        stack.putString("id", std::string{found->second});
    }
}

// parity: `UnfilledBuriedTreasureMapFix`.
void unfilledBuriedTreasureMap(NbtCompound& stack, const Context&) {
    if (stack.getString("id") != "minecraft:map") {
        return;
    }
    NbtTag* heldComponents = stack.find("components");
    NbtCompound* components = heldComponents == nullptr ? nullptr : heldComponents->get<NbtCompound>();
    NbtTag* name = components == nullptr ? nullptr : components->find("minecraft:item_name");
    if (plainTranslationKey(name) != "filled_map.buried_treasure") {
        return;
    }
    name->get<NbtCompound>()->putString("translate", "item.minecraft.buried_treasure_map");
}

} // namespace

void addFixesFrom4996(std::vector<Fix>& fixes) {
    fixes.insert(fixes.end(), {
                                  {4996, Target::Components, {}, unflattenPotDecorations},
                                  {4996, Target::BlockEntity, "minecraft:decorated_pot", unflattenSherds},
                                  {4997, Target::Chunk, {}, rebuildBlendingData},
                                  {5002, Target::BlockEntity, "minecraft:sign", allowSignOpFeatures},
                                  {5002, Target::BlockEntity, "minecraft:hanging_sign", allowSignOpFeatures},
                                  {5006, Target::Chunk, {}, blockStateFieldNames},
                                  {5007, Target::Components, {}, splitSwingAnimation},
                                  {5008, Target::ItemStack, {}, explorerMapItem},
                                  {5008, Target::Components, {}, removeMapColor},
                                  {5009, Target::Chunk, {}, blendingHeightsToFloat},
                                  {5012, Target::ItemStack, {}, renameExplorerMaps},
                                  {5013, Target::Chunk, {}, mergeTerrainStatus},
                                  {5014, Target::Components, {}, removeBlockTransformer},
                                  {5016, Target::ItemStack, {}, unfilledBuriedTreasureMap},
                              });
}

} // namespace basalt::metamorph::datafix
