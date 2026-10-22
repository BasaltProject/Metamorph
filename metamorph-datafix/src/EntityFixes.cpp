#include "FixSupport.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <string>

namespace basalt::metamorph::datafix {
namespace {

using wire::nbt::NbtCompound;
using wire::nbt::NbtList;
using wire::nbt::NbtTag;
using wire::nbt::NbtType;

constexpr float kDefaultDropChance = 0.085F;

// parity: `BoatSplitFix`.
void splitBoat(NbtCompound& entity, const Context&) {
    static const std::map<std::string_view, std::string_view> kBoats{
        {"spruce", "spruce_boat"},     {"birch", "birch_boat"},   {"jungle", "jungle_boat"},
        {"acacia", "acacia_boat"},     {"cherry", "cherry_boat"}, {"dark_oak", "dark_oak_boat"},
        {"mangrove", "mangrove_boat"}, {"bamboo", "bamboo_raft"}};
    static const std::map<std::string_view, std::string_view> kChestBoats{
        {"spruce", "spruce_chest_boat"},     {"birch", "birch_chest_boat"},   {"jungle", "jungle_chest_boat"},
        {"acacia", "acacia_chest_boat"},     {"cherry", "cherry_chest_boat"}, {"dark_oak", "dark_oak_chest_boat"},
        {"mangrove", "mangrove_chest_boat"}, {"bamboo", "bamboo_chest_raft"}};
    const std::string id = namespaced(entity.getString("id"));
    const bool chest = id == "minecraft:chest_boat";
    if (id != "minecraft:boat" && !chest) {
        return;
    }
    const auto& names = chest ? kChestBoats : kBoats;
    const std::string_view variant = entity.getString("Type");
    const auto found = names.find(variant);
    entity.putString("id", "minecraft:" + std::string{found != names.end() ? found->second
                                                                           : (chest ? "oak_chest_boat" : "oak_boat")});
    entity.remove("Type");
}

// parity: `EntitySalmonSizeFix`.
void salmonSize(NbtCompound& entity, const Context&) {
    const std::string_view type = entity.contains("type") ? entity.getString("type") : "medium";
    if (type != "large") {
        entity.putString("type", "medium");
    }
}

// parity: `EntityFieldsRenameFix` for the TNT minecart fuse.
void tntMinecartFuse(NbtCompound& entity, const Context&) { rename(entity, "TNTFuse", "fuse"); }

// parity: `DropChancesFormatFix`.
void dropChances(NbtCompound& entity, const Context&) {
    const auto listOf = [&entity](std::string_view key) {
        std::vector<float> chances;
        if (const NbtTag* held = entity.find(key); held != nullptr && held->get<NbtList>() != nullptr) {
            for (const NbtTag& value : *held->get<NbtList>()) {
                const float* f = value.get<float>();
                chances.push_back(f != nullptr ? *f : kDefaultDropChance);
            }
        }
        return chances;
    };
    const std::vector<float> armor = listOf("ArmorDropChances");
    const std::vector<float> hands = listOf("HandDropChances");
    float body = kDefaultDropChance;
    if (const NbtTag* held = entity.find("body_armor_drop_chance"); held != nullptr) {
        if (const float* f = held->get<float>(); f != nullptr) {
            body = *f;
        } else if (const double* d = held->get<double>(); d != nullptr) {
            body = static_cast<float>(*d);
        }
    }
    entity.remove("ArmorDropChances");
    entity.remove("HandDropChances");
    entity.remove("body_armor_drop_chance");
    NbtCompound slots;
    static constexpr std::array<std::string_view, 4> kArmor{"feet", "legs", "chest", "head"};
    static constexpr std::array<std::string_view, 2> kHands{"mainhand", "offhand"};
    for (std::size_t i = 0; i < kArmor.size() && i < armor.size(); ++i) {
        if (armor[i] != kDefaultDropChance) {
            slots.putFloat(kArmor[i], armor[i]);
        }
    }
    for (std::size_t i = 0; i < kHands.size() && i < hands.size(); ++i) {
        if (hands[i] != kDefaultDropChance) {
            slots.putFloat(kHands[i], hands[i]);
        }
    }
    if (body != kDefaultDropChance) {
        slots.putFloat("body", body);
    }
    if (!slots.empty()) {
        entity.set("drop_chances", NbtTag::ofCompound(std::move(slots)));
    }
}

// parity: `AreaEffectCloudDurationScaleFix`.
void areaEffectCloudScale(NbtCompound& entity, const Context&) { entity.putFloat("potion_duration_scale", 0.25F); }

void saddleDropChance(NbtCompound& entity) {
    NbtCompound chances;
    if (const NbtTag* held = entity.find("drop_chances"); held != nullptr && held->get<NbtCompound>() != nullptr) {
        chances = *held->get<NbtCompound>();
    }
    chances.putFloat("saddle", 2.0F);
    entity.set("drop_chances", NbtTag::ofCompound(std::move(chances)));
}

// parity: `SaddleEquipmentSlotFix`.
void saddleSlot(NbtCompound& entity, const Context&) {
    static constexpr std::array<std::string_view, 8> kWithItem{
        "minecraft:horse", "minecraft:skeleton_horse", "minecraft:zombie_horse", "minecraft:donkey",
        "minecraft:mule",  "minecraft:camel",          "minecraft:llama",        "minecraft:trader_llama"};
    const std::string id = namespaced(entity.getString("id"));
    if (std::ranges::find(kWithItem, id) != kWithItem.end()) {
        if (entity.contains("SaddleItem")) {
            rename(entity, "SaddleItem", "saddle");
            saddleDropChance(entity);
        }
    } else if (id == "minecraft:pig" || id == "minecraft:strider") {
        const bool saddled = entity.getBoolean("Saddle");
        entity.remove("Saddle");
        if (saddled) {
            NbtCompound saddle;
            saddle.putString("id", "minecraft:saddle");
            saddle.putInt("count", 1);
            entity.set("saddle", NbtTag::ofCompound(std::move(saddle)));
            saddleDropChance(entity);
        }
    }
}

// parity: `EquipmentFormatFix`, where an empty compound is the placeholder for no item.
void equipmentFormat(NbtCompound& entity, const Context&) {
    const auto itemAt = [&entity](std::string_view key, std::size_t index) -> std::optional<NbtTag> {
        const NbtTag* held = entity.find(key);
        const NbtList* list = held == nullptr ? nullptr : held->get<NbtList>();
        if (list == nullptr || index >= list->size()) {
            return std::nullopt;
        }
        const NbtCompound* item = (*list)[index].get<NbtCompound>();
        return item == nullptr || !item->contains("id") ? std::nullopt : std::optional{(*list)[index]};
    };
    const auto single = [&entity](std::string_view key) -> std::optional<NbtTag> {
        const NbtTag* held = entity.find(key);
        const NbtCompound* item = held == nullptr ? nullptr : held->get<NbtCompound>();
        return item == nullptr || !item->contains("id") ? std::nullopt : std::optional{*held};
    };
    NbtCompound equipment;
    const auto put = [&equipment](std::string_view slot, const std::optional<NbtTag>& item) {
        if (item.has_value()) {
            equipment.set(slot, *item);
        }
    };
    put("mainhand", itemAt("HandItems", 0));
    put("offhand", itemAt("HandItems", 1));
    put("feet", itemAt("ArmorItems", 0));
    put("legs", itemAt("ArmorItems", 1));
    put("chest", itemAt("ArmorItems", 2));
    put("head", itemAt("ArmorItems", 3));
    put("body", single("body_armor_item"));
    put("saddle", single("saddle"));
    for (const std::string_view old : {"ArmorItems", "HandItems", "body_armor_item", "saddle"}) {
        entity.remove(old);
    }
    if (!equipment.empty()) {
        entity.set("equipment", NbtTag::ofCompound(std::move(equipment)));
    }
}

// parity: `EntityFallDistanceFloatToDoubleFix`.
void fallDistanceToDouble(NbtCompound& entity, const Context&) {
    const NbtTag* held = entity.find("FallDistance");
    if (held == nullptr) {
        return;
    }
    double value = 0.0;
    if (const float* f = held->get<float>(); f != nullptr) {
        value = static_cast<double>(*f);
    } else if (const double* d = held->get<double>(); d != nullptr) {
        value = *d;
    }
    entity.remove("FallDistance");
    entity.putDouble("fall_distance", value);
}

// parity: `ThrownPotionSplitFix`.
void splitThrownPotion(NbtCompound& entity, const Context&) {
    if (namespaced(entity.getString("id")) != "minecraft:potion") {
        return;
    }
    const NbtCompound* item = entity.getCompound("Item");
    const std::string itemId = item == nullptr ? std::string{} : namespaced(item->getString("id"));
    entity.putString("id",
                     itemId == "minecraft:lingering_potion" ? "minecraft:lingering_potion" : "minecraft:splash_potion");
}

/// parity: `ExtraDataFixUtils.fixInlineBlockPos`.
void inlineBlockPos(NbtCompound& tag, std::string_view fieldX, std::string_view fieldY, std::string_view fieldZ,
                    std::string_view newField) {
    const auto number = [&tag](std::string_view key) -> std::optional<std::int32_t> {
        const NbtTag* held = tag.find(key);
        if (held == nullptr) {
            return std::nullopt;
        }
        if (const auto* i = held->get<std::int32_t>(); i != nullptr) {
            return *i;
        }
        if (const auto* b = held->get<std::int8_t>(); b != nullptr) {
            return *b;
        }
        if (const auto* s = held->get<std::int16_t>(); s != nullptr) {
            return *s;
        }
        if (const auto* l = held->get<std::int64_t>(); l != nullptr) {
            return static_cast<std::int32_t>(*l);
        }
        if (const auto* d = held->get<double>(); d != nullptr) {
            return static_cast<std::int32_t>(*d);
        }
        if (const auto* f = held->get<float>(); f != nullptr) {
            return static_cast<std::int32_t>(*f);
        }
        return std::nullopt;
    };
    const std::optional<std::int32_t> x = number(fieldX);
    const std::optional<std::int32_t> y = number(fieldY);
    const std::optional<std::int32_t> z = number(fieldZ);
    if (!x.has_value() || !y.has_value() || !z.has_value()) {
        return;
    }
    tag.remove(fieldX);
    tag.remove(fieldY);
    tag.remove(fieldZ);
    tag.putIntArray(newField, {*x, *y, *z});
}

// parity: `InlineBlockPosFormatFix`, for the entities that kept a position as three loose numbers.
void inlineBlockPositions(NbtCompound& entity, const Context&) {
    const std::string id = namespaced(entity.getString("id"));
    inlineBlockPos(entity, "SleepingX", "SleepingY", "SleepingZ", "sleeping_pos");
    if (id == "minecraft:vex") {
        rename(entity, "LifeTicks", "life_ticks");
        inlineBlockPos(entity, "BoundX", "BoundY", "BoundZ", "bound_pos");
    } else if (id == "minecraft:phantom") {
        rename(entity, "Size", "size");
        inlineBlockPos(entity, "AX", "AY", "AZ", "anchor_pos");
    } else if (id == "minecraft:turtle") {
        entity.remove("TravelPosX");
        entity.remove("TravelPosY");
        entity.remove("TravelPosZ");
        inlineBlockPos(entity, "HomePosX", "HomePosY", "HomePosZ", "home_pos");
        rename(entity, "HasEgg", "has_egg");
    } else if (id == "minecraft:item_frame" || id == "minecraft:glow_item_frame" || id == "minecraft:painting" ||
               id == "minecraft:leash_knot") {
        inlineBlockPos(entity, "TileX", "TileY", "TileZ", "block_pos");
    }
}

void changeBaseValue(NbtCompound& entity, std::string_view attribute, double from, double to) {
    NbtTag* held = entity.find("attributes");
    NbtList* list = held == nullptr ? nullptr : held->get<NbtList>();
    if (list == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < list->size(); ++i) {
        NbtCompound* entry = (*list)[i].get<NbtCompound>();
        if (entry == nullptr || namespaced(entry->getString("id")) != attribute) {
            continue;
        }
        const NbtTag* base = entry->find("base");
        const double* value = base == nullptr ? nullptr : base->get<double>();
        if (value != nullptr && *value == from) {
            entry->putDouble("base", to);
        }
    }
}

// parity: `EntityAttributeBaseValueFix` for the follow range fixes of 4187.
void followRanges(NbtCompound& entity, const Context&) {
    const std::string id = namespaced(entity.getString("id"));
    constexpr std::string_view kRange = "minecraft:follow_range";
    if (id == "minecraft:villager" || id == "minecraft:bee" || id == "minecraft:allay") {
        changeBaseValue(entity, kRange, 48.0, 16.0);
    } else if (id == "minecraft:llama") {
        changeBaseValue(entity, kRange, 40.0, 16.0);
    } else if (id == "minecraft:piglin_brute") {
        changeBaseValue(entity, kRange, 16.0, 12.0);
    } else if (id == "minecraft:warden") {
        changeBaseValue(entity, kRange, 16.0, 24.0);
    }
}

// parity: `PlayerEquipmentFix`.
void playerEquipment(NbtCompound& player, const Context&) {
    NbtTag* held = player.find("Inventory");
    NbtList* inventory = held == nullptr ? nullptr : held->get<NbtList>();
    if (inventory == nullptr) {
        return;
    }
    NbtCompound equipment;
    NbtList kept{NbtType::Compound, {}};
    for (std::size_t i = 0; i < inventory->size(); ++i) {
        NbtCompound* item = (*inventory)[i].get<NbtCompound>();
        const NbtTag* slotTag = item == nullptr ? nullptr : item->find("Slot");
        const auto* slot = slotTag == nullptr ? nullptr : slotTag->get<std::int8_t>();
        std::string_view name;
        if (slot != nullptr) {
            name = *slot == 100    ? "feet"
                   : *slot == 101  ? "legs"
                   : *slot == 102  ? "chest"
                   : *slot == 103  ? "head"
                   : *slot == -106 ? "offhand"
                                   : "";
        }
        if (name.empty()) {
            kept.push((*inventory)[i]);
        } else {
            item->remove("Slot");
            equipment.set(name, (*inventory)[i]);
        }
    }
    player.set("Inventory", NbtTag::ofList(std::move(kept)));
    if (!equipment.empty()) {
        player.set("equipment", NbtTag::ofCompound(std::move(equipment)));
    }
}

// parity: `AddFieldFix` for the locator bar icon style, on the players and the entities that have one.
void locatorBarStyle(NbtCompound& entity, const Context&) {
    NbtTag* held = entity.find("locator_bar_icon");
    if (NbtCompound* icon = held == nullptr ? nullptr : held->get<NbtCompound>(); icon != nullptr) {
        icon->putString("style", "minecraft:default");
    }
}

// parity: `VillagerSetVillagerDataFinalized` and `ZombieVillagerSetVillagerDataFinalized`.
void villagerDataFinalized(NbtCompound& entity, const Context&) { entity.putBoolean("VillagerDataFinalized", true); }

std::string_view dyeName(std::int32_t id) {
    static constexpr std::array<std::string_view, 16> kNames{
        "white",      "orange", "magenta", "light_blue", "yellow", "lime",  "pink", "gray",
        "light_gray", "cyan",   "purple",  "blue",       "brown",  "green", "red",  "black"};
    return id >= 1 && id < 16 ? kNames[static_cast<std::size_t>(id)] : "white";
}

std::optional<std::int32_t> intOf(const NbtTag* held) {
    if (held == nullptr) {
        return std::nullopt;
    }
    if (const auto* i = held->get<std::int32_t>(); i != nullptr) {
        return *i;
    }
    if (const auto* s = held->get<std::int16_t>(); s != nullptr) {
        return *s;
    }
    if (const auto* b = held->get<std::int8_t>(); b != nullptr) {
        return *b;
    }
    return std::nullopt;
}

NbtCompound* mutableCompound(NbtCompound& parent, std::string_view key) {
    NbtTag* held = parent.find(key);
    return held == nullptr ? nullptr : held->get<NbtCompound>();
}

// parity: `EntitySpawnerItemVariantComponentFix`.
void bucketVariants(NbtCompound& stack, const Context&) {
    const std::string id = namespaced(stack.getString("id"));
    NbtCompound* components = mutableCompound(stack, "components");
    if (components == nullptr) {
        return;
    }
    if (id == "minecraft:painting") {
        NbtCompound* data = mutableCompound(*components, "minecraft:entity_data");
        if (data == nullptr || namespaced(data->getString("id")) != "minecraft:painting") {
            return;
        }
        const NbtTag* variant = data->find("variant");
        std::optional<NbtTag> moved = variant == nullptr ? std::nullopt : std::optional{*variant};
        data->remove("variant");
        if (data->size() == 1) {
            components->remove("minecraft:entity_data");
        }
        if (moved.has_value()) {
            components->set("minecraft:painting/variant", *moved);
        }
        return;
    }
    NbtCompound* bucket = mutableCompound(*components, "minecraft:bucket_entity_data");
    if (bucket == nullptr) {
        return;
    }
    if (id == "minecraft:salmon_bucket") {
        if (const NbtTag* type = bucket->find("type"); type != nullptr) {
            const NbtTag size = *type;
            bucket->remove("type");
            components->set("minecraft:salmon/size", size);
        }
    } else if (id == "minecraft:axolotl_bucket") {
        if (const auto variant = intOf(bucket->find("Variant")); variant.has_value()) {
            static constexpr std::array<std::string_view, 5> kNames{"lucy", "wild", "gold", "cyan", "blue"};
            const std::string_view name =
                *variant >= 1 && *variant <= 4 ? kNames[static_cast<std::size_t>(*variant)] : "lucy";
            bucket->remove("Variant");
            components->putString("minecraft:axolotl/variant", std::string{name});
        }
    } else if (id == "minecraft:tropical_fish_bucket") {
        if (const auto packed = intOf(bucket->find("BucketVariantTag")); packed.has_value()) {
            static const std::map<std::int32_t, std::string_view> kPatterns{
                {1, "flopper"},   {256, "sunstreak"}, {257, "stripey"},   {512, "snooper"},
                {513, "glitter"}, {768, "dasher"},    {769, "blockfish"}, {1024, "brinely"},
                {1025, "betty"},  {1280, "spotty"},   {1281, "clayfish"}};
            const auto pattern = kPatterns.find(*packed & 0xFFFF);
            bucket->remove("BucketVariantTag");
            components->putString("minecraft:tropical_fish/pattern",
                                  std::string{pattern != kPatterns.end() ? pattern->second : "kob"});
            components->putString("minecraft:tropical_fish/base_color", std::string{dyeName((*packed >> 16) & 255)});
            components->putString("minecraft:tropical_fish/pattern_color", std::string{dyeName((*packed >> 24) & 255)});
        }
    }
}

// parity: `CopperGolemWeatherStateFix`.
void copperGolemWeatherState(NbtCompound& entity, const Context&) {
    const NbtTag* held = entity.find("weather_state");
    if (held == nullptr) {
        return;
    }
    std::int32_t state = 0;
    if (const auto* i = held->get<std::int32_t>(); i != nullptr) {
        state = *i;
    } else if (const auto* b = held->get<std::int8_t>(); b != nullptr) {
        state = *b;
    }
    static constexpr std::array<std::string_view, 4> kNames{"unaffected", "exposed", "weathered", "oxidized"};
    entity.putString("weather_state",
                     std::string{kNames[static_cast<std::size_t>(state >= 0 && state < 4 ? state : 0)]});
}

} // namespace

void addEntityFixes(std::vector<Fix>& fixes) {
    fixes.push_back({4299, Target::ItemStack, {}, bucketVariants});
    fixes.insert(fixes.end(), {
                                  {4067, Target::Entity, {}, splitBoat},
                                  {4081, Target::Entity, "minecraft:salmon", salmonSize},
                                  {4173, Target::Entity, "minecraft:tnt_minecart", tntMinecartFuse},
                                  {4187, Target::Entity, {}, followRanges},
                                  {4293, Target::Entity, {}, dropChances},
                                  {4296, Target::Entity, "minecraft:area_effect_cloud", areaEffectCloudScale},
                                  {4300, Target::Entity, {}, saddleSlot},
                                  {4301, Target::Entity, {}, equipmentFormat},
                                  {4303, Target::Entity, {}, fallDistanceToDouble},
                                  {4306, Target::Entity, {}, splitThrownPotion},
                                  {4312, Target::Entity, {}, playerEquipment},
                                  {4314, Target::Entity, {}, inlineBlockPositions},
                                  {4424, Target::Entity, {}, locatorBarStyle},
                                  {4763, Target::Entity, "minecraft:villager", villagerDataFinalized},
                                  {4763, Target::Entity, "minecraft:zombie_villager", villagerDataFinalized},
                                  {4535, Target::Entity, "minecraft:copper_golem", copperGolemWeatherState},
                              });
}

} // namespace basalt::metamorph::datafix
