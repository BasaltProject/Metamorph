#include "Componentization.hpp"
#include "FixSupport.hpp"

#include <basalt/wire/common/json/Json.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
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

constexpr float kDefaultDropChance = 0.085F;

[[nodiscard]] NbtCompound* compoundAt(NbtCompound& parent, std::string_view key) {
    NbtTag* held = parent.find(key);
    return held == nullptr ? nullptr : held->get<NbtCompound>();
}

[[nodiscard]] NbtList* listAt(NbtCompound& parent, std::string_view key) {
    NbtTag* held = parent.find(key);
    return held == nullptr ? nullptr : held->get<NbtList>();
}

void forEachInList(NbtCompound& parent, std::string_view key, const std::function<void(NbtCompound&)>& edit) {
    forEachCompound(parent, key, edit);
}

[[nodiscard]] std::optional<double> number(const NbtTag* tag) {
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
        return static_cast<double>(*v);
    }
    if (const auto* v = tag->get<float>(); v != nullptr) {
        return *v;
    }
    if (const auto* v = tag->get<double>(); v != nullptr) {
        return *v;
    }
    return std::nullopt;
}

/// parity: `ExtraDataFixUtils.fixBlockPos`, a `{X, Y, Z}` compound as an int array.
void fixBlockPos(NbtTag& tag) {
    const NbtCompound* held = tag.get<NbtCompound>();
    if (held == nullptr) {
        return;
    }
    const auto x = number(held->find("X"));
    const auto y = number(held->find("Y"));
    const auto z = number(held->find("Z"));
    if (x.has_value() && y.has_value() && z.has_value()) {
        tag = NbtTag::ofIntArray(
            {static_cast<std::int32_t>(*x), static_cast<std::int32_t>(*y), static_cast<std::int32_t>(*z)});
    }
}

void renameAndFixBlockPos(NbtCompound& compound, std::string_view from, std::string_view to) {
    rename(compound, from, to);
    if (NbtTag* moved = compound.find(to); moved != nullptr) {
        fixBlockPos(*moved);
    }
}

// parity: `ItemRenameFix` for the scute that became the turtle scute.
void renameScute(NbtCompound& stack, const Context&) {
    if (namespaced(stack.getString("id")) == "minecraft:scute") {
        stack.putString("id", "minecraft:turtle_scute");
    }
}

// parity: `RenameEnchantmentsFix`, over the enchantment lists in the tag of an item.
void renameSweeping(NbtCompound& stack, const Context&) {
    NbtCompound* tag = compoundAt(stack, "tag");
    if (tag == nullptr) {
        return;
    }
    for (const std::string_view field : {"Enchantments", "StoredEnchantments"}) {
        forEachInList(*tag, field, [](NbtCompound& enchantment) {
            if (namespaced(enchantment.getString("id")) == "minecraft:sweeping") {
                enchantment.putString("id", "minecraft:sweeping_edge");
            }
        });
    }
}

void armorBody(NbtCompound& entity, std::string_view previous, bool clearArmorItems) {
    const NbtTag* held = entity.find(previous);
    if (held == nullptr) {
        return;
    }
    const NbtTag bodyArmor = *held;
    entity.remove(previous);
    if (clearArmorItems) {
        if (NbtList* armor = listAt(entity, "ArmorItems"); armor != nullptr && armor->size() > 2) {
            (*armor)[2] = NbtTag::ofCompound(NbtCompound{});
        }
        if (NbtList* chances = listAt(entity, "ArmorDropChances"); chances != nullptr && chances->size() > 2) {
            (*chances)[2] = NbtTag::ofFloat(kDefaultDropChance);
        }
    }
    entity.set("body_armor_item", bodyArmor);
    entity.putFloat("body_armor_drop_chance", 2.0F);
}

// parity: `HorseBodyArmorItemFix`.
void horseBodyArmor(NbtCompound& entity, const Context&) {
    const std::string id = namespaced(entity.getString("id"));
    if (id == "minecraft:horse") {
        armorBody(entity, "ArmorItem", true);
    } else if (id == "minecraft:llama" || id == "minecraft:trader_llama") {
        armorBody(entity, "DecorItem", false);
    }
}

// parity: `ChestedHorsesInventoryZeroIndexingFix`.
void chestedHorsesZeroIndexing(NbtCompound& entity, const Context&) {
    static const std::set<std::string_view> kChested{"minecraft:llama", "minecraft:trader_llama", "minecraft:mule",
                                                     "minecraft:donkey"};
    if (!kChested.contains(namespaced(entity.getString("id")))) {
        return;
    }
    forEachInList(entity, "Items", [](NbtCompound& stack) {
        const auto slot = number(stack.find("Slot"));
        stack.putByte("Slot", static_cast<std::int8_t>(static_cast<std::int32_t>(slot.value_or(2.0)) - 2));
    });
}

// parity: `FixWolfHealth`.
void wolfHealth(NbtCompound& entity, const Context&) {
    if (namespaced(entity.getString("id")) != "minecraft:wolf") {
        return;
    }
    bool adjusted = false;
    forEachInList(entity, "Attributes", [&](NbtCompound& attribute) {
        if (namespaced(attribute.getString("Name")) != "minecraft:generic.max_health") {
            return;
        }
        if (number(attribute.find("Base")).value_or(0.0) == 20.0) {
            attribute.putDouble("Base", 40.0);
            adjusted = true;
        }
    });
    if (adjusted) {
        entity.putFloat("Health", static_cast<float>(number(entity.find("Health")).value_or(0.0)) * 2.0F);
    }
}

// parity: `BlockPosFormatAndRenamesFix`, over entities.
void blockPosOfEntities(NbtCompound& entity, const Context&) {
    const std::string id = namespaced(entity.getString("id"));
    if (id == "minecraft:bee") {
        renameAndFixBlockPos(entity, "HivePos", "hive_pos");
        renameAndFixBlockPos(entity, "FlowerPos", "flower_pos");
    } else if (id == "minecraft:end_crystal") {
        renameAndFixBlockPos(entity, "BeamTarget", "beam_target");
    } else if (id == "minecraft:wandering_trader") {
        renameAndFixBlockPos(entity, "WanderTarget", "wander_target");
    } else if (id == "minecraft:witch" || id == "minecraft:ravager" || id == "minecraft:pillager" ||
               id == "minecraft:illusioner" || id == "minecraft:evoker" || id == "minecraft:vindicator") {
        renameAndFixBlockPos(entity, "PatrolTarget", "patrol_target");
    }
    renameAndFixBlockPos(entity, "Leash", "leash");
}

// parity: `BlockPosFormatAndRenamesFix`, over block entities.
void blockPosOfBlockEntities(NbtCompound& blockEntity, const Context&) {
    const std::string id = namespaced(blockEntity.getString("id"));
    if (id == "minecraft:beehive") {
        renameAndFixBlockPos(blockEntity, "FlowerPos", "flower_pos");
    } else if (id == "minecraft:end_gateway") {
        renameAndFixBlockPos(blockEntity, "ExitPortal", "exit_portal");
    }
}

// parity: `BlockPosFormatAndRenamesFix`, over the lodestone of a compass.
void blockPosOfCompass(NbtCompound& stack, const Context&) {
    if (namespaced(stack.getString("id")) != "minecraft:compass") {
        return;
    }
    if (NbtCompound* tag = compoundAt(stack, "tag"); tag != nullptr) {
        if (NbtTag* position = tag->find("LodestonePos"); position != nullptr) {
            fixBlockPos(*position);
        }
    }
}

[[nodiscard]] std::string renamedJumpStrength(const std::string& name) {
    return namespaced(name) == "minecraft:horse.jump_strength" ? "minecraft:generic.jump_strength" : name;
}

// parity: `AttributesRenameLegacy`, over the modifiers in the tag of an item.
void renameJumpStrengthOfItem(NbtCompound& stack, const Context&) {
    NbtCompound* tag = compoundAt(stack, "tag");
    if (tag == nullptr) {
        return;
    }
    forEachInList(*tag, "AttributeModifiers", [](NbtCompound& modifier) {
        if (const NbtTag* held = modifier.find("AttributeName");
            held != nullptr && held->get<std::string>() != nullptr) {
            modifier.putString("AttributeName", renamedJumpStrength(*held->get<std::string>()));
        }
    });
}

// parity: `AttributesRenameLegacy`, over the attributes of an entity or a player.
void renameJumpStrengthOfEntity(NbtCompound& entity, const Context&) {
    forEachInList(entity, "Attributes", [](NbtCompound& attribute) {
        if (const NbtTag* held = attribute.find("Name"); held != nullptr && held->get<std::string>() != nullptr) {
            attribute.putString("Name", renamedJumpStrength(*held->get<std::string>()));
        }
    });
}

// parity: `BeehiveFieldRenameFix`, with the field renames its output schema carries.
void beehiveFields(NbtCompound& blockEntity, const Context&) {
    if (namespaced(blockEntity.getString("id")) != "minecraft:beehive") {
        return;
    }
    rename(blockEntity, "Bees", "bees");
    forEachInList(blockEntity, "bees", [](NbtCompound& bee) {
        rename(bee, "EntityData", "entity_data");
        rename(bee, "TicksInHive", "ticks_in_hive");
        rename(bee, "MinOccupationTicks", "min_ticks_in_hive");
    });
}

[[nodiscard]] std::string_view dyeName(std::int32_t id) {
    static constexpr std::array<std::string_view, 16> kNames{
        "white",      "orange", "magenta", "light_blue", "yellow", "lime",  "pink", "gray",
        "light_gray", "cyan",   "purple",  "blue",       "brown",  "green", "red",  "black"};
    return id >= 1 && id < 16 ? kNames[static_cast<std::size_t>(id)] : "white";
}

// parity: `BannerPatternFormatFix`.
void bannerPatterns(NbtCompound& blockEntity, const Context&) {
    static const std::map<std::string_view, std::string_view> kPatterns{{"b", "minecraft:base"},
                                                                        {"bl", "minecraft:square_bottom_left"},
                                                                        {"br", "minecraft:square_bottom_right"},
                                                                        {"tl", "minecraft:square_top_left"},
                                                                        {"tr", "minecraft:square_top_right"},
                                                                        {"bs", "minecraft:stripe_bottom"},
                                                                        {"ts", "minecraft:stripe_top"},
                                                                        {"ls", "minecraft:stripe_left"},
                                                                        {"rs", "minecraft:stripe_right"},
                                                                        {"cs", "minecraft:stripe_center"},
                                                                        {"ms", "minecraft:stripe_middle"},
                                                                        {"drs", "minecraft:stripe_downright"},
                                                                        {"dls", "minecraft:stripe_downleft"},
                                                                        {"ss", "minecraft:small_stripes"},
                                                                        {"cr", "minecraft:cross"},
                                                                        {"sc", "minecraft:straight_cross"},
                                                                        {"bt", "minecraft:triangle_bottom"},
                                                                        {"tt", "minecraft:triangle_top"},
                                                                        {"bts", "minecraft:triangles_bottom"},
                                                                        {"tts", "minecraft:triangles_top"},
                                                                        {"ld", "minecraft:diagonal_left"},
                                                                        {"rd", "minecraft:diagonal_up_right"},
                                                                        {"lud", "minecraft:diagonal_up_left"},
                                                                        {"rud", "minecraft:diagonal_right"},
                                                                        {"mc", "minecraft:circle"},
                                                                        {"mr", "minecraft:rhombus"},
                                                                        {"vh", "minecraft:half_vertical"},
                                                                        {"hh", "minecraft:half_horizontal"},
                                                                        {"vhr", "minecraft:half_vertical_right"},
                                                                        {"hhb", "minecraft:half_horizontal_bottom"},
                                                                        {"bo", "minecraft:border"},
                                                                        {"cbo", "minecraft:curly_border"},
                                                                        {"gra", "minecraft:gradient"},
                                                                        {"gru", "minecraft:gradient_up"},
                                                                        {"bri", "minecraft:bricks"},
                                                                        {"glb", "minecraft:globe"},
                                                                        {"cre", "minecraft:creeper"},
                                                                        {"sku", "minecraft:skull"},
                                                                        {"flo", "minecraft:flower"},
                                                                        {"moj", "minecraft:mojang"},
                                                                        {"pig", "minecraft:piglin"}};
    if (namespaced(blockEntity.getString("id")) != "minecraft:banner") {
        return;
    }
    rename(blockEntity, "Patterns", "patterns");
    forEachInList(blockEntity, "patterns", [](NbtCompound& layer) {
        rename(layer, "Pattern", "pattern");
        if (const NbtTag* held = layer.find("pattern"); held != nullptr && held->get<std::string>() != nullptr) {
            const auto found = kPatterns.find(*held->get<std::string>());
            if (found != kPatterns.end()) {
                layer.putString("pattern", std::string{found->second});
            }
        }
        layer.putString("color",
                        std::string{dyeName(static_cast<std::int32_t>(number(layer.find("Color")).value_or(0.0)))});
        layer.remove("Color");
    });
}

// parity: `TippedArrowPotionToItemFix`.
void tippedArrowPotion(NbtCompound& entity, const Context&) {
    if (namespaced(entity.getString("id")) != "minecraft:arrow") {
        return;
    }
    const NbtTag* potion = entity.find("Potion");
    const NbtTag* effects = entity.find("custom_potion_effects");
    const NbtTag* color = entity.find("Color");
    if (potion == nullptr && effects == nullptr && color == nullptr) {
        return;
    }
    const std::optional<NbtTag> p = potion == nullptr ? std::nullopt : std::optional{*potion};
    const std::optional<NbtTag> e = effects == nullptr ? std::nullopt : std::optional{*effects};
    const std::optional<NbtTag> c = color == nullptr ? std::nullopt : std::optional{*color};
    entity.remove("Potion");
    entity.remove("custom_potion_effects");
    entity.remove("Color");
    NbtCompound item;
    if (const NbtCompound* held = compoundAt(entity, "item"); held != nullptr) {
        item = *held;
    }
    NbtCompound tag;
    if (const NbtCompound* held = compoundAt(item, "tag"); held != nullptr) {
        tag = *held;
    }
    if (p.has_value()) {
        tag.set("Potion", *p);
    }
    if (e.has_value()) {
        tag.set("custom_potion_effects", *e);
    }
    if (c.has_value()) {
        tag.set("CustomPotionColor", *c);
    }
    item.set("tag", NbtTag::ofCompound(std::move(tag)));
    entity.set("item", NbtTag::ofCompound(std::move(item)));
}

// parity: `ItemStackComponentizationFix`.
void componentizeStack(NbtCompound& stack, const Context&) { componentize(stack); }

// parity: `AreaEffectCloudPotionFix`.
void areaEffectCloudPotion(NbtCompound& entity, const Context&) {
    if (namespaced(entity.getString("id")) != "minecraft:area_effect_cloud") {
        return;
    }
    const NbtTag* color = entity.find("Color");
    const NbtTag* effects = entity.find("effects");
    const NbtTag* potion = entity.find("Potion");
    NbtCompound contents;
    if (color != nullptr) {
        contents.set("custom_color", *color);
    }
    if (effects != nullptr) {
        contents.set("custom_effects", *effects);
    }
    if (potion != nullptr) {
        contents.set("potion", *potion);
    }
    entity.remove("Color");
    entity.remove("effects");
    entity.remove("Potion");
    if (!contents.empty()) {
        entity.set("potion_contents", NbtTag::ofCompound(std::move(contents)));
    }
}

// parity: `PlayerHeadBlockProfileFix`.
void playerHeadProfile(NbtCompound& blockEntity, const Context&) {
    if (namespaced(blockEntity.getString("id")) != "minecraft:skull") {
        return;
    }
    const NbtTag* owner = blockEntity.find("SkullOwner");
    const NbtTag* extra = blockEntity.find("ExtraType");
    const NbtTag* profile = owner != nullptr ? owner : extra;
    if (profile == nullptr) {
        return;
    }
    const NbtTag fixed = fixProfile(*profile);
    blockEntity.remove("SkullOwner");
    blockEntity.remove("ExtraType");
    blockEntity.set("profile", fixed);
}

[[nodiscard]] std::optional<std::string> translationKey(const NbtTag* component) {
    const std::string* text = component == nullptr ? nullptr : component->get<std::string>();
    if (text == nullptr) {
        return std::nullopt;
    }
    const auto parsed = wire::common::parseJson(*text);
    const auto* object = parsed.has_value() ? parsed->get<wire::common::JsonObject>() : nullptr;
    const wire::common::JsonValue* key = object == nullptr ? nullptr : object->find("translate");
    const std::string* name = key == nullptr ? nullptr : key->get<std::string>();
    return name == nullptr ? std::nullopt : std::optional{*name};
}

// parity: `ItemStackCustomNameToOverrideComponentFix`.
void customNameToItemName(NbtCompound& stack, const Context&) {
    static const std::set<std::string_view> kMaps{
        "filled_map.buried_treasure", "filled_map.explorer_jungle", "filled_map.explorer_swamp",
        "filled_map.mansion",         "filled_map.monument",        "filled_map.trial_chambers",
        "filled_map.village_desert",  "filled_map.village_plains",  "filled_map.village_savanna",
        "filled_map.village_snowy",   "filled_map.village_taiga"};
    const std::string id = namespaced(stack.getString("id"));
    NbtCompound* components = compoundAt(stack, "components");
    if (components == nullptr) {
        return;
    }
    const auto key = translationKey(components->find("minecraft:custom_name"));
    const bool expected =
        key.has_value() && ((id == "minecraft:white_banner" && *key == "block.minecraft.ominous_banner") ||
                            (id == "minecraft:filled_map" && kMaps.contains(*key)));
    if (expected) {
        rename(*components, "minecraft:custom_name", "minecraft:item_name");
    }
}

// parity: `BannerEntityCustomNameToOverrideComponentFix`.
void bannerCustomName(NbtCompound& blockEntity, const Context&) {
    if (namespaced(blockEntity.getString("id")) != "minecraft:banner") {
        return;
    }
    const NbtTag* name = blockEntity.find("CustomName");
    const auto key = translationKey(name);
    if (!key.has_value() || *key != "block.minecraft.ominous_banner") {
        return;
    }
    NbtCompound components;
    components.set("minecraft:item_name", *name);
    components.set("minecraft:hide_additional_tooltip", NbtTag::ofCompound(NbtCompound{}));
    blockEntity.set("components", NbtTag::ofCompound(std::move(components)));
    blockEntity.remove("CustomName");
}

// parity: `TrialSpawnerConfigFix`.
void trialSpawnerConfig(NbtCompound& blockEntity, const Context&) {
    if (namespaced(blockEntity.getString("id")) != "minecraft:trial_spawner") {
        return;
    }
    static constexpr std::array<std::string_view, 9> kKeys{"spawn_range",
                                                           "total_mobs",
                                                           "simultaneous_mobs",
                                                           "total_mobs_added_per_player",
                                                           "simultaneous_mobs_added_per_player",
                                                           "ticks_between_spawn",
                                                           "spawn_potentials",
                                                           "loot_tables_to_eject",
                                                           "items_to_drop_when_ominous"};
    NbtCompound config;
    for (const std::string_view key : kKeys) {
        if (const NbtTag* held = blockEntity.find(key); held != nullptr) {
            config.set(key, *held);
            blockEntity.remove(key);
        }
    }
    if (!config.empty()) {
        blockEntity.set("normal_config", NbtTag::ofCompound(std::move(config)));
    }
}

[[nodiscard]] bool emptyStack(const NbtCompound& item) {
    const std::string id = namespaced(item.contains("id") ? item.getString("id") : "minecraft:air");
    return id == "minecraft:air" || static_cast<std::int32_t>(number(item.find("count")).value_or(0.0)) == 0;
}

// parity: `EmptyItemInVillagerTradeFix`.
void emptyItemInTrade(NbtCompound& entity, const Context&) {
    NbtCompound* offers = compoundAt(entity, "Offers");
    if (offers == nullptr) {
        return;
    }
    forEachInList(*offers, "Recipes", [](NbtCompound& trade) {
        const NbtCompound* buyB = compoundAt(trade, "buyB");
        const std::string id = buyB == nullptr
                                   ? "minecraft:air"
                                   : namespaced(buyB->contains("id") ? buyB->getString("id") : "minecraft:air");
        const std::int32_t count =
            buyB == nullptr ? 0 : static_cast<std::int32_t>(number(buyB->find("count")).value_or(0.0));
        if (id == "minecraft:air" || count == 0) {
            trade.remove("buyB");
        }
    });
}

// parity: `RemoveEmptyItemInBrushableBlockFix`.
void emptyItemInBrushable(NbtCompound& blockEntity, const Context&) {
    if (namespaced(blockEntity.getString("id")) != "minecraft:brushable_block") {
        return;
    }
    const NbtCompound* item = compoundAt(blockEntity, "item");
    if (item != nullptr && emptyStack(*item)) {
        blockEntity.remove("item");
    }
}

// parity: `JukeboxTicksSinceSongStartedFix`.
void jukeboxTicks(NbtCompound& blockEntity, const Context&) {
    if (namespaced(blockEntity.getString("id")) != "minecraft:jukebox") {
        return;
    }
    const std::int64_t ticks = static_cast<std::int64_t>(number(blockEntity.find("TickCount")).value_or(0.0)) -
                               static_cast<std::int64_t>(number(blockEntity.find("RecordStartTick")).value_or(0.0));
    blockEntity.remove("IsPlaying");
    blockEntity.remove("TickCount");
    blockEntity.remove("RecordStartTick");
    if (ticks > 0) {
        blockEntity.putLong("ticks_since_song_started", ticks);
    }
}

[[nodiscard]] std::optional<std::string> uuidText(std::span<const std::int32_t> parts) {
    if (parts.size() != 4) {
        return std::nullopt;
    }
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%08x-%04x-%04x-%04x-%04x%08x", static_cast<std::uint32_t>(parts[0]),
                  static_cast<std::uint32_t>(parts[1]) >> 16, static_cast<std::uint32_t>(parts[1]) & 0xFFFFU,
                  static_cast<std::uint32_t>(parts[2]) >> 16, static_cast<std::uint32_t>(parts[2]) & 0xFFFFU,
                  static_cast<std::uint32_t>(parts[3]));
    return std::string{buffer};
}

const std::map<std::string_view, std::string_view>& modifierIdsByUuid() {
    static const std::map<std::string_view, std::string_view> kIds{
        {"736565d2-e1a7-403d-a3f8-1aeb3e302542", "minecraft:creative_mode_block_range"},
        {"98491ef6-97b1-4584-ae82-71a8cc85cf73", "minecraft:creative_mode_entity_range"},
        {"91aeaa56-376b-4498-935b-2f7f68070635", "minecraft:effect.speed"},
        {"7107de5e-7ce8-4030-940e-514c1f160890", "minecraft:effect.slowness"},
        {"af8b6e3f-3328-4c0a-aa36-5ba2bb9dbef3", "minecraft:effect.haste"},
        {"55fced67-e92a-486e-9800-b47f202c4386", "minecraft:effect.mining_fatigue"},
        {"648d7064-6a60-4f59-8abe-c2c23a6dd7a9", "minecraft:effect.strength"},
        {"c0105bf3-aef8-46b0-9ebc-92943757ccbe", "minecraft:effect.jump_boost"},
        {"22653b89-116e-49dc-9b6b-9971489b5be5", "minecraft:effect.weakness"},
        {"5d6f0ba2-1186-46ac-b896-c61c5cee99cc", "minecraft:effect.health_boost"},
        {"eae29cf0-701e-4ed6-883a-96f798f3dab5", "minecraft:effect.absorption"},
        {"03c3c89d-7037-4b42-869f-b146bcb64d2e", "minecraft:effect.luck"},
        {"cc5af142-2bd2-4215-b636-2605aed11727", "minecraft:effect.unluck"},
        {"6555be74-63b3-41f1-a245-77833b3c2562", "minecraft:evil"},
        {"1eaf83ff-7207-4596-b37a-d7a07b3ec4ce", "minecraft:powder_snow"},
        {"662a6b8d-da3e-4c1c-8813-96ea6097278d", "minecraft:sprinting"},
        {"020e0dfb-87ae-4653-9556-831010e291a0", "minecraft:attacking"},
        {"766bfa64-11f3-11ea-8d71-362b9e155667", "minecraft:baby"},
        {"7e0292f2-9434-48d5-a29f-9583af7df27f", "minecraft:covered"},
        {"9e362924-01de-4ddd-a2b2-d0f7a405a174", "minecraft:suffocating"},
        {"5cd17e52-a79a-43d3-a529-90fde04b181e", "minecraft:drinking"},
        {"b9766b59-9566-4402-bc1f-2ee2a276d836", "minecraft:baby"},
        {"49455a49-7ec5-45ba-b886-3b90b23a1718", "minecraft:attacking"},
        {"845db27c-c624-495f-8c9f-6020a9a58b6b", "minecraft:armor.boots"},
        {"d8499b04-0e66-4726-ab29-64469d734e0d", "minecraft:armor.leggings"},
        {"9f3d476d-c118-4544-8365-64846904b48e", "minecraft:armor.chestplate"},
        {"2ad3f246-fee1-4e67-b886-69fd380bb150", "minecraft:armor.helmet"},
        {"c1c72771-8b8e-ba4a-ace0-81a93c8928b2", "minecraft:armor.body"},
        {"b572ecd2-ac0c-4071-abde-9594af072a37", "minecraft:enchantment.fire_protection"},
        {"40a9968f-5c66-4e2f-b7f4-2ec2f4b3e450", "minecraft:enchantment.blast_protection"},
        {"07a65791-f64d-4e79-86c7-f83932f007ec", "minecraft:enchantment.respiration"},
        {"60b1b7db-fffd-4ad0-817c-d6c6a93d8a45", "minecraft:enchantment.aqua_affinity"},
        {"11dc269a-4476-46c0-aff3-9e17d7eb6801", "minecraft:enchantment.depth_strider"},
        {"87f46a96-686f-4796-b035-22e16ee9e038", "minecraft:enchantment.soul_speed"},
        {"b9716dbd-50df-4080-850e-70347d24e687", "minecraft:enchantment.soul_speed"},
        {"92437d00-c3a7-4f2e-8f6c-1f21585d5dd0", "minecraft:enchantment.swift_sneak"},
        {"5d3d087b-debe-4037-b53e-d84f3ff51f17", "minecraft:enchantment.sweeping_edge"},
        {"3ceb37c0-db62-46b5-bd02-785457b01d96", "minecraft:enchantment.efficiency"},
        {"cb3f55d3-645c-4f38-a497-9c13a33db5cf", "minecraft:base_attack_damage"},
        {"fa233e1c-4180-4865-b01b-bcce9785aca3", "minecraft:base_attack_speed"}};
    return kIds;
}

const std::map<std::string_view, std::string_view>& modifierIdsByName() {
    static const std::map<std::string_view, std::string_view> kIds{
        {"Random spawn bonus", "minecraft:random_spawn_bonus"},
        {"Random zombie-spawn bonus", "minecraft:zombie_random_spawn_bonus"},
        {"Leader zombie bonus", "minecraft:leader_zombie_bonus"},
        {"Zombie reinforcement callee charge", "minecraft:reinforcement_callee_charge"},
        {"Zombie reinforcement caller charge", "minecraft:reinforcement_caller_charge"}};
    return kIds;
}

/// parity: `AttributeModifierIdFix.fixModifiers`, the modifiers keyed by an id instead of a uuid and a name.
[[nodiscard]] std::vector<NbtTag> fixModifiers(const NbtList& modifiers) {
    std::vector<std::pair<std::string, NbtCompound>> converted;
    const auto put = [&converted](const std::string& id, NbtCompound modifier) {
        for (auto& [existing, value] : converted) {
            if (existing == id) {
                value = std::move(modifier);
                return;
            }
        }
        converted.emplace_back(id, std::move(modifier));
    };
    for (const NbtTag& entry : modifiers) {
        const NbtCompound* source = entry.get<NbtCompound>();
        if (source == nullptr) {
            continue;
        }
        NbtCompound modifier = *source;
        const auto uuid = uuidText(modifier.getIntArray("uuid"));
        const std::string name{modifier.getString("name")};
        const auto byUuid = uuid.has_value() ? modifierIdsByUuid().find(*uuid) : modifierIdsByUuid().end();
        const auto byName = modifierIdsByName().find(name);
        if (byUuid != modifierIdsByUuid().end()) {
            const std::string id{byUuid->second};
            modifier.putString("id", id);
            modifier.remove("uuid");
            modifier.remove("name");
            put(id, std::move(modifier));
        } else if (byName != modifierIdsByName().end()) {
            const std::string id{byName->second};
            const auto existing = std::ranges::find(converted, id, &std::pair<std::string, NbtCompound>::first);
            if (existing == converted.end()) {
                modifier.putString("id", id);
                modifier.remove("uuid");
                modifier.remove("name");
                put(id, std::move(modifier));
            } else {
                const double amount = number(existing->second.find("amount")).value_or(0.0);
                const double added = number(modifier.find("amount")).value_or(0.0);
                existing->second.putDouble("amount", amount + added);
            }
        } else {
            const std::string id = "minecraft:" + (uuid.has_value() ? *uuid : std::string{"unknown"});
            modifier.putString("id", id);
            modifier.remove("uuid");
            modifier.remove("name");
            put(id, std::move(modifier));
        }
    }
    std::vector<NbtTag> out;
    for (auto& [id, modifier] : converted) {
        out.push_back(NbtTag::ofCompound(std::move(modifier)));
    }
    return out;
}

[[nodiscard]] NbtTag listOfCompounds(std::vector<NbtTag> elements) {
    return NbtTag::ofList(NbtList{elements.empty() ? NbtType::End : NbtType::Compound, std::move(elements)});
}

// parity: `AttributeModifierIdFix`, over the attribute modifiers of an item.
void modifierIdsOfItem(NbtCompound& components, const Context&) {
    NbtCompound* attributes = compoundAt(components, "minecraft:attribute_modifiers");
    NbtList* modifiers = attributes == nullptr ? nullptr : listAt(*attributes, "modifiers");
    if (modifiers != nullptr) {
        attributes->set("modifiers", listOfCompounds(fixModifiers(*modifiers)));
    }
}

// parity: `AttributeModifierIdFix`, over the attributes of an entity or a player.
void modifierIdsOfEntity(NbtCompound& entity, const Context&) {
    rename(entity, "Attributes", "attributes");
    forEachInList(entity, "attributes", [](NbtCompound& attribute) {
        rename(attribute, "Name", "id");
        rename(attribute, "Base", "base");
        rename(attribute, "Modifiers", "modifiers");
        NbtList* modifiers = listAt(attribute, "modifiers");
        if (modifiers == nullptr) {
            return;
        }
        for (std::size_t i = 0; i < modifiers->size(); ++i) {
            NbtCompound* modifier = (*modifiers)[i].get<NbtCompound>();
            if (modifier == nullptr) {
                continue;
            }
            rename(*modifier, "UUID", "uuid");
            rename(*modifier, "Name", "name");
            rename(*modifier, "Amount", "amount");
            rename(*modifier, "Operation", "operation");
            if (const NbtTag* operation = modifier->find("operation"); operation != nullptr) {
                const auto value = static_cast<std::int32_t>(number(operation).value_or(0.0));
                modifier->putString("operation", value == 0   ? "add_value"
                                                 : value == 1 ? "add_multiplied_base"
                                                 : value == 2 ? "add_multiplied_total"
                                                              : "invalid");
            }
        }
        attribute.set("modifiers", listOfCompounds(fixModifiers(*modifiers)));
    });
}

} // namespace

void addFixesFrom3800(std::vector<Fix>& fixes) {
    fixes.insert(fixes.end(), {
                                  {3800, Target::LegacyStack, {}, renameScute},
                                  {3800, Target::ItemStack, {}, renameScute},
                                  {3803, Target::LegacyStack, {}, renameSweeping},
                                  {3808, Target::Entity, {}, horseBodyArmor},
                                  {3809, Target::Entity, {}, chestedHorsesZeroIndexing},
                                  {3812, Target::Entity, {}, wolfHealth},
                                  {3813, Target::Entity, {}, blockPosOfEntities},
                                  {3813, Target::BlockEntity, {}, blockPosOfBlockEntities},
                                  {3813, Target::LegacyStack, {}, blockPosOfCompass},
                                  {3814, Target::LegacyStack, {}, renameJumpStrengthOfItem},
                                  {3814, Target::Entity, {}, renameJumpStrengthOfEntity},
                                  {3818, Target::BlockEntity, {}, beehiveFields},
                                  {3818, Target::BlockEntity, {}, bannerPatterns},
                                  {3818, Target::Entity, {}, tippedArrowPotion},
                                  {3818, Target::LegacyStack, {}, componentizeStack},
                                  {3818, Target::Entity, {}, areaEffectCloudPotion},
                                  {3820, Target::BlockEntity, {}, playerHeadProfile},
                                  {3825, Target::ItemStack, {}, customNameToItemName},
                                  {3825, Target::BlockEntity, {}, bannerCustomName},
                                  {3825, Target::BlockEntity, {}, trialSpawnerConfig},
                                  {3828, Target::Entity, {}, emptyItemInTrade},
                                  {3833, Target::BlockEntity, {}, emptyItemInBrushable},
                                  {3945, Target::Components, {}, modifierIdsOfItem},
                                  {3945, Target::Entity, {}, modifierIdsOfEntity},
                                  {3945, Target::BlockEntity, {}, jukeboxTicks},
                              });
}

} // namespace basalt::metamorph::datafix
