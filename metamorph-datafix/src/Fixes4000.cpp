#include "FixSupport.hpp"

#include <basalt/wire/common/json/Json.hpp>

#include <array>
#include <optional>
#include <string>

namespace basalt::metamorph::datafix {
namespace {

using wire::nbt::NbtCompound;
using wire::nbt::NbtList;
using wire::nbt::NbtTag;
using wire::nbt::NbtType;

/// parity: `LegacyComponentDataFixUtils.extractTranslationString`, over a text component kept as a JSON string.
[[nodiscard]] std::optional<std::string> jsonTranslationKey(const NbtTag* component) {
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

void ominousBannerRarity(NbtCompound& holder) {
    NbtTag* heldComponents = holder.find("components");
    NbtCompound* components = heldComponents == nullptr ? nullptr : heldComponents->get<NbtCompound>();
    if (components == nullptr || jsonTranslationKey(components->find("minecraft:item_name")) !=
                                     std::optional<std::string>{"block.minecraft.ominous_banner"}) {
        return;
    }
    components->putString("minecraft:item_name", R"({"translate":"block.minecraft.ominous_banner"})");
    components->putString("minecraft:rarity", "uncommon");
}

// parity: `OminousBannerRarityFix`, over the banner block entity.
void ominousBannerBlockEntity(NbtCompound& blockEntity, const Context&) { ominousBannerRarity(blockEntity); }

// parity: `OminousBannerRarityFix`, over the white banner item.
void ominousBannerItem(NbtCompound& stack, const Context&) {
    if (stack.getString("id") == "minecraft:white_banner") {
        ominousBannerRarity(stack);
    }
}

// parity: `AttributeIdPrefixFix`, over the item attribute modifiers.
void attributeIdPrefix(NbtCompound& components, const Context&) {
    NbtTag* held = components.find("minecraft:attribute_modifiers");
    NbtCompound* modifiers = held == nullptr ? nullptr : held->get<NbtCompound>();
    NbtTag* listTag = modifiers == nullptr ? nullptr : modifiers->find("modifiers");
    NbtList* list = listTag == nullptr ? nullptr : listTag->get<NbtList>();
    if (list == nullptr) {
        return;
    }
    static constexpr std::array<std::string_view, 4> kPrefixes{"minecraft:generic.", "minecraft:horse.",
                                                               "minecraft:player.", "minecraft:zombie."};
    for (std::size_t i = 0; i < list->size(); ++i) {
        NbtCompound* modifier = (*list)[i].get<NbtCompound>();
        if (modifier == nullptr || !modifier->contains("type")) {
            continue;
        }
        const std::string type = namespaced(modifier->getString("type"));
        for (const std::string_view prefix : kPrefixes) {
            if (type.starts_with(prefix)) {
                modifier->putString("type", "minecraft:" + type.substr(prefix.size()));
                break;
            }
        }
    }
}

// parity: `AttributeIdPrefixFix`, over the attributes of an entity or a player.
void attributeIdPrefixOfEntity(NbtCompound& entity, const Context&) {
    static constexpr std::array<std::string_view, 4> kPrefixes{"minecraft:generic.", "minecraft:horse.",
                                                               "minecraft:player.", "minecraft:zombie."};
    forEachCompound(entity, "attributes", [](NbtCompound& attribute) {
        if (!attribute.contains("id")) {
            return;
        }
        const std::string id = namespaced(attribute.getString("id"));
        for (const std::string_view prefix : kPrefixes) {
            if (id.starts_with(prefix)) {
                attribute.putString("id", "minecraft:" + id.substr(prefix.size()));
                break;
            }
        }
    });
}

// parity: `FoodToConsumableFix`.
void foodToConsumable(NbtCompound& components, const Context&) {
    NbtTag* held = components.find("minecraft:food");
    NbtCompound* food = held == nullptr ? nullptr : held->get<NbtCompound>();
    if (food == nullptr) {
        return;
    }
    const float eatSeconds = food->contains("eat_seconds") ? food->getFloat("eat_seconds", 1.6F) : 1.6F;
    std::vector<NbtTag> onConsume;
    if (const NbtTag* effects = food->find("effects"); effects != nullptr && effects->get<NbtList>() != nullptr) {
        for (const NbtTag& effect : *effects->get<NbtList>()) {
            const NbtCompound* source = effect.get<NbtCompound>();
            if (source == nullptr) {
                continue;
            }
            NbtCompound apply;
            apply.putString("type", "minecraft:apply_effects");
            std::vector<NbtTag> inner;
            if (const NbtTag* one = source->find("effect"); one != nullptr) {
                inner.push_back(*one);
            }
            apply.set("effects", NbtTag::ofList(NbtList{NbtType::Compound, std::move(inner)}));
            apply.putFloat("probability",
                           source->contains("probability") ? source->getFloat("probability", 1.0F) : 1.0F);
            onConsume.push_back(NbtTag::ofCompound(std::move(apply)));
        }
    }
    if (const NbtTag* remainder = food->find("using_converts_to"); remainder != nullptr) {
        components.set("minecraft:use_remainder", *remainder);
    }
    food->remove("eat_seconds");
    food->remove("effects");
    food->remove("using_converts_to");
    NbtCompound consumable;
    consumable.putFloat("consume_seconds", eatSeconds);
    consumable.set("on_consume_effects", NbtTag::ofList(NbtList{NbtType::Compound, std::move(onConsume)}));
    components.set("minecraft:consumable", NbtTag::ofCompound(std::move(consumable)));
}

// parity: `FireResistantToDamageResistantComponentFix`.
void fireResistantToDamageResistant(NbtCompound& components, const Context&) {
    if (!components.contains("minecraft:fire_resistant")) {
        return;
    }
    components.remove("minecraft:fire_resistant");
    NbtCompound resistant;
    resistant.putString("types", "#minecraft:is_fire");
    components.set("minecraft:damage_resistant", NbtTag::ofCompound(std::move(resistant)));
}

/// parity: `LockComponentPredicateFix.fixLock`, a plain key turned into a custom-name predicate; nothing to keep
/// otherwise.
[[nodiscard]] std::optional<NbtTag> lockPredicate(const NbtTag& lock) {
    const std::string* key = lock.get<std::string>();
    if (key == nullptr || key->empty()) {
        return std::nullopt;
    }
    std::string quoted = "\"";
    for (const char c : *key) {
        if (c == '"' || c == '\\') {
            quoted += '\\';
        }
        quoted += c;
    }
    quoted += '"';
    NbtCompound components;
    components.putString("minecraft:custom_name", quoted);
    NbtCompound predicate;
    predicate.set("components", NbtTag::ofCompound(std::move(components)));
    return NbtTag::ofCompound(std::move(predicate));
}

// parity: `LockComponentPredicateFix`.
void lockComponentPredicate(NbtCompound& components, const Context&) {
    const NbtTag* lock = components.find("minecraft:lock");
    if (lock == nullptr) {
        return;
    }
    const std::optional<NbtTag> fixed = lockPredicate(*lock);
    components.remove("minecraft:lock");
    if (fixed.has_value()) {
        components.set("minecraft:lock", *fixed);
    }
}

// parity: `ContainerBlockEntityLockPredicateFix`.
void lockBlockEntityPredicate(NbtCompound& blockEntity, const Context&) {
    const NbtTag* lock = blockEntity.find("Lock");
    if (lock == nullptr) {
        return;
    }
    const std::optional<NbtTag> fixed = lockPredicate(*lock);
    blockEntity.remove("Lock");
    if (fixed.has_value()) {
        blockEntity.set("lock", *fixed);
    }
}

/// parity: `InvalidLockComponentFix.isBrokenLock`, a predicate that is only an empty custom name.
[[nodiscard]] bool brokenLock(const NbtTag& lock) {
    const NbtCompound* predicate = lock.get<NbtCompound>();
    const NbtTag* components = predicate != nullptr && predicate->size() == 1 ? predicate->find("components") : nullptr;
    const NbtCompound* held = components == nullptr ? nullptr : components->get<NbtCompound>();
    const NbtTag* name = held != nullptr && held->size() == 1 ? held->find("minecraft:custom_name") : nullptr;
    const std::string* text = name == nullptr ? nullptr : name->get<std::string>();
    return text != nullptr && *text == "\"\"";
}

// parity: `InvalidLockComponentFix`.
void invalidLockComponent(NbtCompound& components, const Context&) {
    const NbtTag* lock = components.find("minecraft:lock");
    if (lock != nullptr && brokenLock(*lock)) {
        components.remove("minecraft:lock");
    }
}

// parity: `InvalidBlockEntityLockFix`.
void invalidBlockEntityLock(NbtCompound& blockEntity, const Context&) {
    const NbtTag* lock = blockEntity.find("lock");
    if (lock != nullptr && brokenLock(*lock)) {
        blockEntity.remove("lock");
    }
}

// parity: `EquippableAssetRenameFix`.
void equippableAssetRename(NbtCompound& components, const Context&) {
    NbtTag* held = components.find("minecraft:equippable");
    if (NbtCompound* equippable = held == nullptr ? nullptr : held->get<NbtCompound>(); equippable != nullptr) {
        rename(*equippable, "model", "asset_id");
    }
}

// parity: `CustomModelDataExpandFix`.
void customModelDataExpand(NbtCompound& components, const Context&) {
    const NbtTag* current = components.find("minecraft:custom_model_data");
    if (current == nullptr) {
        return;
    }
    float value = 0.0F;
    if (const auto* i = current->get<std::int32_t>(); i != nullptr) {
        value = static_cast<float>(*i);
    } else if (const auto* f = current->get<float>(); f != nullptr) {
        value = *f;
    } else if (const auto* d = current->get<double>(); d != nullptr) {
        value = static_cast<float>(*d);
    }
    NbtCompound expanded;
    expanded.set("floats", NbtTag::ofList(NbtList{NbtType::Float, {NbtTag::ofFloat(value)}}));
    components.set("minecraft:custom_model_data", NbtTag::ofCompound(std::move(expanded)));
}

// parity: `BlockEntityFurnaceBurnTimeFix`.
void furnaceBurnTime(NbtCompound& blockEntity, const Context&) {
    rename(blockEntity, "CookTime", "cooking_time_spent");
    rename(blockEntity, "CookTimeTotal", "cooking_total_time");
    rename(blockEntity, "BurnTime", "lit_time_remaining");
    if (const NbtTag* remaining = blockEntity.find("lit_time_remaining"); remaining != nullptr) {
        blockEntity.set("lit_total_time", *remaining);
    }
}

// parity: `LegacyBlockPropertyRenameAndFix` for `creaking_heart.active` to `creaking_heart_state`.
void creakingHeartState(NbtCompound& entry, const Context&) {
    if (namespaced(entry.getString("Name")) != "minecraft:creaking_heart" || !entry.contains("Name")) {
        return;
    }
    NbtTag* held = entry.find("Properties");
    NbtCompound* properties = held == nullptr ? nullptr : held->get<NbtCompound>();
    const NbtTag* active = properties == nullptr ? nullptr : properties->find("active");
    if (active == nullptr) {
        return;
    }
    const bool awake = properties->getString("active") == "true";
    properties->remove("active");
    properties->putString("creaking_heart_state", awake ? "awake" : "uprooted");
}

} // namespace

void addFixesFrom4000(std::vector<Fix>& fixes) {
    fixes.insert(fixes.end(), {
                                  {4054, Target::BlockEntity, "minecraft:banner", ominousBannerBlockEntity},
                                  {4054, Target::ItemStack, {}, ominousBannerItem},
                                  {4055, Target::Components, {}, attributeIdPrefix},
                                  {4055, Target::Entity, {}, attributeIdPrefixOfEntity},
                                  {4059, Target::Components, {}, foodToConsumable},
                                  {4064, Target::Components, {}, fireResistantToDamageResistant},
                                  {4068, Target::Components, {}, lockComponentPredicate},
                                  {4068, Target::BlockEntity, {}, lockBlockEntityPredicate},
                                  {4175, Target::Components, {}, equippableAssetRename},
                                  {4175, Target::Components, {}, customModelDataExpand},
                                  {4176, Target::BlockEntity, {}, invalidBlockEntityLock},
                                  {4176, Target::Components, {}, invalidLockComponent},
                                  {4181, Target::BlockEntity, "minecraft:furnace", furnaceBurnTime},
                                  {4181, Target::BlockEntity, "minecraft:smoker", furnaceBurnTime},
                                  {4181, Target::BlockEntity, "minecraft:blast_furnace", furnaceBurnTime},
                                  {4294, Target::Palette, {}, creakingHeartState},
                              });
}

} // namespace basalt::metamorph::datafix
