#include "FixSupport.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <string>

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

// parity: `ChunkDeleteIgnoredLightDataFix`, light kept by a chunk that says its light is off.
void deleteIgnoredLight(NbtCompound& chunk, const Context&) {
    if (chunk.getBoolean("isLightOn")) {
        return;
    }
    forEachCompound(chunk, "sections", [](NbtCompound& section) {
        section.remove("BlockLight");
        section.remove("SkyLight");
    });
}

// parity: `EntityVariantFix`, the cat type and then the frog variant as names.
void variantByNumber(NbtCompound& entity, std::string_view field, std::span<const std::string_view> names,
                     std::string_view fallback) {
    const NbtTag* held = entity.find(field);
    const auto number = whole(held);
    if (!number.has_value()) {
        return;
    }
    const bool known = *number >= 0 && *number < static_cast<std::int64_t>(names.size());
    entity.putString("variant",
                     "minecraft:" + std::string{known ? names[static_cast<std::size_t>(*number)] : fallback});
    entity.remove(field);
}

void catVariant(NbtCompound& entity, const Context&) {
    if (namespaced(entity.getString("id")) != "minecraft:cat") {
        return;
    }
    static constexpr std::array<std::string_view, 11> kCats{
        "tabby", "black", "red", "siamese", "british", "calico", "persian", "ragdoll", "white", "jellie", "all_black"};
    variantByNumber(entity, "CatType", kCats, "tabby");
}

void frogVariant(NbtCompound& entity, const Context&) {
    if (namespaced(entity.getString("id")) != "minecraft:frog") {
        return;
    }
    static constexpr std::array<std::string_view, 3> kFrogs{"temperate", "warm", "cold"};
    const auto number = whole(entity.find("Variant"));
    if (number.has_value() && *number >= 0 && *number < 3) {
        entity.putString("variant", "minecraft:" + std::string{kFrogs[static_cast<std::size_t>(*number)]});
    }
    entity.remove("Variant");
}

// parity: `EntityFieldsRenameFix` for the painting.
void paintingFields(NbtCompound& entity, const Context&) {
    if (namespaced(entity.getString("id")) != "minecraft:painting") {
        return;
    }
    rename(entity, "Motive", "variant");
    rename(entity, "Facing", "facing");
}

// parity: `EntityGoatMissingStateFix`.
void goatHorns(NbtCompound& entity, const Context&) {
    if (namespaced(entity.getString("id")) == "minecraft:goat") {
        entity.putBoolean("HasLeftHorn", true);
        entity.putBoolean("HasRightHorn", true);
    }
}

// parity: `GoatHornIdFix`.
void goatHornId(NbtCompound& stack, const Context&) {
    if (namespaced(stack.getString("id")) != "minecraft:goat_horn") {
        return;
    }
    NbtCompound* tag = compoundAt(stack, "tag");
    if (tag == nullptr) {
        return;
    }
    static constexpr std::array<std::string_view, 8> kInstruments{
        "minecraft:ponder_goat_horn", "minecraft:sing_goat_horn",   "minecraft:seek_goat_horn",
        "minecraft:feel_goat_horn",   "minecraft:admire_goat_horn", "minecraft:call_goat_horn",
        "minecraft:yearn_goat_horn",  "minecraft:dream_goat_horn"};
    const auto variant = whole(tag->find("SoundVariant")).value_or(0);
    tag->remove("SoundVariant");
    tag->putString("instrument",
                   std::string{kInstruments[variant >= 0 && variant < 8 ? static_cast<std::size_t>(variant) : 0]});
}

// parity: `FilteredBooksFix`.
void filteredBooks(NbtCompound& stack, const Context&) {
    const std::string id = namespaced(stack.getString("id"));
    if (id != "minecraft:writable_book" && id != "minecraft:written_book") {
        return;
    }
    if (NbtCompound* tag = compoundAt(stack, "tag"); tag != nullptr) {
        tag->remove("filtered_title");
        tag->remove("filtered_pages");
    }
}

// parity: `FilteredSignsFix`.
void filteredSigns(NbtCompound& blockEntity, const Context&) {
    if (namespaced(blockEntity.getString("id")) != "minecraft:sign") {
        return;
    }
    for (const std::string_view field : {"FilteredText1", "FilteredText2", "FilteredText3", "FilteredText4"}) {
        blockEntity.remove(field);
    }
}

// parity: `VariantRenameFix` for the british shorthair.
void britishShorthair(NbtCompound& entity, const Context&) {
    if (namespaced(entity.getString("id")) == "minecraft:cat" && entity.getString("variant") == "minecraft:british") {
        entity.putString("variant", "minecraft:british_shorthair");
    }
}

// parity: `BlendingDataRemoveFromNetherEndFix`.
void blendingDataOutsideOverworld(NbtCompound& chunk, const Context& context) {
    if (!context.overworld) {
        chunk.remove("blending_data");
    }
}

// parity: `ItemStackSpawnEggFix`, the pig egg that carries another mob's id.
void spawnEgg(NbtCompound& stack, const Context&) {
    static const std::map<std::string, std::string> kEggs{
        {"minecraft:bat", "minecraft:bat_spawn_egg"},
        {"minecraft:blaze", "minecraft:blaze_spawn_egg"},
        {"minecraft:cave_spider", "minecraft:cave_spider_spawn_egg"},
        {"minecraft:chicken", "minecraft:chicken_spawn_egg"},
        {"minecraft:cow", "minecraft:cow_spawn_egg"},
        {"minecraft:creeper", "minecraft:creeper_spawn_egg"},
        {"minecraft:donkey", "minecraft:donkey_spawn_egg"},
        {"minecraft:elder_guardian", "minecraft:elder_guardian_spawn_egg"},
        {"minecraft:ender_dragon", "minecraft:ender_dragon_spawn_egg"},
        {"minecraft:enderman", "minecraft:enderman_spawn_egg"},
        {"minecraft:endermite", "minecraft:endermite_spawn_egg"},
        {"minecraft:evocation_illager", "minecraft:evocation_illager_spawn_egg"},
        {"minecraft:ghast", "minecraft:ghast_spawn_egg"},
        {"minecraft:guardian", "minecraft:guardian_spawn_egg"},
        {"minecraft:horse", "minecraft:horse_spawn_egg"},
        {"minecraft:husk", "minecraft:husk_spawn_egg"},
        {"minecraft:iron_golem", "minecraft:iron_golem_spawn_egg"},
        {"minecraft:llama", "minecraft:llama_spawn_egg"},
        {"minecraft:magma_cube", "minecraft:magma_cube_spawn_egg"},
        {"minecraft:mooshroom", "minecraft:mooshroom_spawn_egg"},
        {"minecraft:mule", "minecraft:mule_spawn_egg"},
        {"minecraft:ocelot", "minecraft:ocelot_spawn_egg"},
        {"minecraft:pufferfish", "minecraft:pufferfish_spawn_egg"},
        {"minecraft:parrot", "minecraft:parrot_spawn_egg"},
        {"minecraft:pig", "minecraft:pig_spawn_egg"},
        {"minecraft:polar_bear", "minecraft:polar_bear_spawn_egg"},
        {"minecraft:rabbit", "minecraft:rabbit_spawn_egg"},
        {"minecraft:sheep", "minecraft:sheep_spawn_egg"},
        {"minecraft:shulker", "minecraft:shulker_spawn_egg"},
        {"minecraft:silverfish", "minecraft:silverfish_spawn_egg"},
        {"minecraft:skeleton", "minecraft:skeleton_spawn_egg"},
        {"minecraft:skeleton_horse", "minecraft:skeleton_horse_spawn_egg"},
        {"minecraft:slime", "minecraft:slime_spawn_egg"},
        {"minecraft:snow_golem", "minecraft:snow_golem_spawn_egg"},
        {"minecraft:spider", "minecraft:spider_spawn_egg"},
        {"minecraft:squid", "minecraft:squid_spawn_egg"},
        {"minecraft:stray", "minecraft:stray_spawn_egg"},
        {"minecraft:turtle", "minecraft:turtle_spawn_egg"},
        {"minecraft:vex", "minecraft:vex_spawn_egg"},
        {"minecraft:villager", "minecraft:villager_spawn_egg"},
        {"minecraft:vindication_illager", "minecraft:vindication_illager_spawn_egg"},
        {"minecraft:witch", "minecraft:witch_spawn_egg"},
        {"minecraft:wither", "minecraft:wither_spawn_egg"},
        {"minecraft:wither_skeleton", "minecraft:wither_skeleton_spawn_egg"},
        {"minecraft:wolf", "minecraft:wolf_spawn_egg"},
        {"minecraft:zombie", "minecraft:zombie_spawn_egg"},
        {"minecraft:zombie_horse", "minecraft:zombie_horse_spawn_egg"},
        {"minecraft:zombie_pigman", "minecraft:zombie_pigman_spawn_egg"},
        {"minecraft:zombie_villager", "minecraft:zombie_villager_spawn_egg"}};
    if (namespaced(stack.getString("id")) != "minecraft:pig_spawn_egg") {
        return;
    }
    NbtCompound* tag = compoundAt(stack, "tag");
    NbtCompound* entity = tag == nullptr ? nullptr : compoundAt(*tag, "EntityTag");
    if (entity == nullptr || !entity->contains("id")) {
        return;
    }
    const auto found = kEggs.find(namespaced(entity->getString("id")));
    stack.putString("id", found == kEggs.end() ? "minecraft:pig_spawn_egg" : found->second);
}

void effectDurationOf(NbtCompound& holder, std::string_view field) {
    forEachCompound(holder, field, [](NbtCompound& effect) {
        NbtCompound* factor = compoundAt(effect, "FactorCalculationData");
        if (factor == nullptr) {
            return;
        }
        const auto timestamp = static_cast<std::int32_t>(whole(factor->find("effect_changed_timestamp")).value_or(-1));
        factor->remove("effect_changed_timestamp");
        const auto duration = static_cast<std::int32_t>(whole(effect.find("Duration")).value_or(-1));
        factor->putInt("ticks_active", timestamp - duration);
    });
}

// parity: `EffectDurationFix`, over entities and players.
void effectDurationOfEntity(NbtCompound& entity, const Context&) {
    for (const std::string_view field : {"Effects", "ActiveEffects", "CustomPotionEffects"}) {
        effectDurationOf(entity, field);
    }
}

// parity: `EffectDurationFix`, over the potions an item carries.
void effectDurationOfItem(NbtCompound& stack, const Context&) {
    const std::string id = namespaced(stack.getString("id"));
    if (id != "minecraft:potion" && id != "minecraft:splash_potion" && id != "minecraft:lingering_potion" &&
        id != "minecraft:tipped_arrow") {
        return;
    }
    if (NbtCompound* tag = compoundAt(stack, "tag"); tag != nullptr) {
        effectDurationOf(*tag, "CustomPotionEffects");
    }
}

} // namespace

void addFixesFrom3000(std::vector<Fix>& fixes) {
    fixes.insert(fixes.end(), {
                                  {3077, Target::Chunk, {}, deleteIgnoredLight},
                                  {3086, Target::Entity, {}, catVariant},
                                  {3087, Target::Entity, {}, frogVariant},
                                  {3090, Target::Entity, {}, paintingFields},
                                  {3093, Target::Entity, {}, goatHorns},
                                  {3094, Target::LegacyStack, {}, goatHornId},
                                  {3097, Target::LegacyStack, {}, filteredBooks},
                                  {3097, Target::BlockEntity, {}, filteredSigns},
                                  {3097, Target::Entity, {}, britishShorthair},
                                  {3108, Target::Chunk, {}, blendingDataOutsideOverworld},
                                  {3209, Target::LegacyStack, {}, spawnEgg},
                                  {3322, Target::Entity, {}, effectDurationOfEntity},
                                  {3322, Target::LegacyStack, {}, effectDurationOfItem},
                              });
}

} // namespace basalt::metamorph::datafix
