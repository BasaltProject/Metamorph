#include "FixSupport.hpp"

#include <array>
#include <set>
#include <string>
#include <vector>

namespace basalt::metamorph::datafix {
namespace {

using wire::nbt::NbtCompound;
using wire::nbt::NbtList;
using wire::nbt::NbtTag;
using wire::nbt::NbtType;

/// Strips `show_in_tooltip` from a component and notes it as hidden when it was false.
void takeShowInTooltip(NbtCompound& component, std::string_view id, std::set<std::string>& hidden) {
    if (!component.getBoolean("show_in_tooltip", true)) {
        hidden.emplace(id);
    }
    component.remove("show_in_tooltip");
}

void fixTooltipComponent(NbtCompound& components, std::string_view id, std::string_view unwrap,
                         std::set<std::string>& hidden) {
    NbtTag* held = components.find(id);
    NbtCompound* component = held == nullptr ? nullptr : held->get<NbtCompound>();
    if (component == nullptr) {
        return;
    }
    takeShowInTooltip(*component, id, hidden);
    if (!unwrap.empty()) {
        if (const NbtTag* inner = component->find(unwrap); inner != nullptr) {
            const NbtTag value = *inner;
            *held = value;
        }
    }
}

void fixAdventurePredicate(NbtCompound& components, std::string_view id, std::set<std::string>& hidden) {
    NbtTag* held = components.find(id);
    NbtCompound* component = held == nullptr ? nullptr : held->get<NbtCompound>();
    const NbtTag* predicates = component == nullptr ? nullptr : component->find("predicates");
    if (predicates == nullptr) {
        return;
    }
    if (!component->getBoolean("show_in_tooltip", true)) {
        hidden.emplace(id);
    }
    const NbtTag value = *predicates;
    *held = value;
}

// parity: `TooltipDisplayComponentFix`.
void tooltipDisplay(NbtCompound& components, const Context&) {
    static constexpr std::array<std::string_view, 17> kAdditional{
        "minecraft:banner_patterns",     "minecraft:bees",
        "minecraft:block_entity_data",   "minecraft:block_state",
        "minecraft:bundle_contents",     "minecraft:charged_projectiles",
        "minecraft:container",           "minecraft:container_loot",
        "minecraft:firework_explosion",  "minecraft:fireworks",
        "minecraft:instrument",          "minecraft:map_id",
        "minecraft:painting/variant",    "minecraft:pot_decorations",
        "minecraft:potion_contents",     "minecraft:tropical_fish/pattern",
        "minecraft:written_book_content"};
    std::set<std::string> hidden;
    fixAdventurePredicate(components, "minecraft:can_place_on", hidden);
    fixAdventurePredicate(components, "minecraft:can_break", hidden);
    fixTooltipComponent(components, "minecraft:trim", {}, hidden);
    fixTooltipComponent(components, "minecraft:unbreakable", {}, hidden);
    fixTooltipComponent(components, "minecraft:dyed_color", "rgb", hidden);
    fixTooltipComponent(components, "minecraft:attribute_modifiers", "modifiers", hidden);
    fixTooltipComponent(components, "minecraft:enchantments", "levels", hidden);
    fixTooltipComponent(components, "minecraft:stored_enchantments", "levels", hidden);
    fixTooltipComponent(components, "minecraft:jukebox_playable", "song", hidden);
    const bool hideTooltip = components.contains("minecraft:hide_tooltip");
    components.remove("minecraft:hide_tooltip");
    const bool hideAdditional = components.contains("minecraft:hide_additional_tooltip");
    components.remove("minecraft:hide_additional_tooltip");
    if (hideAdditional) {
        for (const std::string_view id : kAdditional) {
            if (components.contains(id)) {
                hidden.emplace(id);
            }
        }
    }
    if (hidden.empty() && !hideTooltip) {
        return;
    }
    NbtCompound display;
    display.putBoolean("hide_tooltip", hideTooltip);
    std::vector<NbtTag> ids;
    for (const std::string& id : hidden) {
        ids.push_back(NbtTag::ofString(id));
    }
    display.set("hidden_components", NbtTag::ofList(NbtList{NbtType::String, std::move(ids)}));
    components.set("minecraft:tooltip_display", NbtTag::ofCompound(std::move(display)));
}

// parity: `ChunkDeleteLightFix`.
void deleteLight(NbtCompound& chunk, const Context&) {
    chunk.remove("isLightOn");
    forEachCompound(chunk, "sections", [](NbtCompound& section) {
        section.remove("BlockLight");
        section.remove("SkyLight");
    });
}

// parity: `LegacyBlockRenameFix` for chain to iron_chain, over the palette and the scheduled block ticks.
void renameChainBlock(NbtCompound& chunk, const Context&) {
    forEachCompound(chunk, "sections", [](NbtCompound& section) {
        NbtTag* states = section.find("block_states");
        NbtCompound* held = states == nullptr ? nullptr : states->get<NbtCompound>();
        if (held == nullptr) {
            return;
        }
        forEachCompound(*held, "palette", [](NbtCompound& entry) {
            if (namespaced(entry.getString("Name")) == "minecraft:chain" && entry.contains("Name")) {
                entry.putString("Name", "minecraft:iron_chain");
            }
        });
    });
    forEachCompound(chunk, "block_ticks", [](NbtCompound& tick) {
        if (tick.contains("i") && namespaced(tick.getString("i")) == "minecraft:chain") {
            tick.putString("i", "minecraft:iron_chain");
        }
    });
}

// parity: `ItemRenameFix` for chain to iron_chain.
void renameChainItem(NbtCompound& stack, const Context&) {
    if (stack.getString("id") == "minecraft:chain") {
        stack.putString("id", "minecraft:iron_chain");
    }
}

// parity: `TridentAnimationFix`.
void tridentAnimation(NbtCompound& components, const Context&) {
    NbtTag* held = components.find("minecraft:consumable");
    NbtCompound* consumable = held == nullptr ? nullptr : held->get<NbtCompound>();
    if (consumable != nullptr && consumable->getString("animation") == "spear") {
        consumable->putString("animation", "trident");
    }
}

} // namespace

void addFixesFrom4300(std::vector<Fix>& fixes) {
    fixes.insert(fixes.end(), {
                                  {4307, Target::Components, {}, tooltipDisplay},
                                  {4537, Target::Chunk, {}, deleteLight},
                                  {4541, Target::Chunk, {}, renameChainBlock},
                                  {4541, Target::ItemStack, {}, renameChainItem},
                                  {4649, Target::Components, {}, tridentAnimation},
                              });
}

} // namespace basalt::metamorph::datafix
