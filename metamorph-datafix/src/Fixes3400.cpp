#include "FixSupport.hpp"

#include <algorithm>
#include <array>
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

constexpr std::string_view kEmptyComponent = R"({"text":""})";

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

[[maybe_unused, nodiscard]] NbtTag listOfStrings(const std::vector<std::string>& values) {
    std::vector<NbtTag> elements;
    for (const std::string& value : values) {
        elements.push_back(NbtTag::ofString(value));
    }
    return NbtTag::ofList(NbtList{elements.empty() ? NbtType::End : NbtType::String, std::move(elements)});
}

[[nodiscard]] NbtTag listOfCompounds(std::vector<NbtTag> elements) {
    return NbtTag::ofList(NbtList{elements.empty() ? NbtType::End : NbtType::Compound, std::move(elements)});
}

// parity: `BlockEntityRenameFix`, the suspicious sand that became the brushable block.
void brushableBlockEntity(NbtCompound& blockEntity, const Context&) {
    blockEntity.putString("id", "minecraft:brushable_block");
}

// parity: `EntityBrushableBlockFieldsRenameFix`.
void brushableFields(NbtCompound& blockEntity, const Context&) {
    rename(blockEntity, "loot_table", "LootTable");
    rename(blockEntity, "loot_table_seed", "LootTableSeed");
}

[[nodiscard]] std::string potteryRename(const std::string& id) {
    static const std::map<std::string, std::string> kFirst{
        {"minecraft:pottery_shard_archer", "minecraft:archer_pottery_shard"},
        {"minecraft:pottery_shard_prize", "minecraft:prize_pottery_shard"},
        {"minecraft:pottery_shard_arms_up", "minecraft:arms_up_pottery_shard"},
        {"minecraft:pottery_shard_skull", "minecraft:skull_pottery_shard"}};
    const auto found = kFirst.find(id);
    return found == kFirst.end() ? id : found->second;
}

[[nodiscard]] std::string sherdRename(const std::string& id) {
    static constexpr std::string_view kShard = "_pottery_shard";
    static const std::set<std::string_view> kNames{
        "angler",     "archer", "arms_up", "blade",   "brewer", "burn",  "danger", "explorer", "friend", "heart",
        "heartbreak", "howl",   "miner",   "mourner", "plenty", "prize", "sheaf",  "shelter",  "skull",  "snort"};
    if (id.starts_with("minecraft:") && id.ends_with(kShard)) {
        const std::string name = id.substr(10, id.size() - 10 - kShard.size());
        if (kNames.contains(name)) {
            return "minecraft:" + name + "_pottery_sherd";
        }
    }
    return id;
}

// parity: `ItemRenameFix`, the pottery shards of 1.20 and then their move to sherds.
void potteryShards(NbtCompound& stack, const Context&) {
    if (const NbtTag* id = stack.find("id"); id != nullptr && id->get<std::string>() != nullptr) {
        stack.putString("id", potteryRename(namespaced(*id->get<std::string>())));
    }
}

void potterySherds(NbtCompound& stack, const Context&) {
    if (const NbtTag* id = stack.find("id"); id != nullptr && id->get<std::string>() != nullptr) {
        stack.putString("id", sherdRename(namespaced(*id->get<std::string>())));
    }
}

// parity: `DecoratedPotFieldRenameFix`.
void decoratedPotFields(NbtCompound& blockEntity, const Context&) { rename(blockEntity, "shards", "sherds"); }

[[nodiscard]] std::optional<NbtTag> line(const NbtCompound& tag, std::string_view prefix, int number) {
    const NbtTag* held = tag.find(std::string{prefix} + std::to_string(number));
    return held == nullptr ? std::nullopt : std::optional{*held};
}

constexpr std::array<std::string_view, 10> kSignFieldsToDrop{
    "Text1",         "Text2",         "Text3",         "Text4", "FilteredText1",
    "FilteredText2", "FilteredText3", "FilteredText4", "Color", "GlowingText"};

// parity: `BlockEntitySignDoubleSidedEditableTextFix`.
void doubleSidedSign(NbtCompound& blockEntity, const Context&) {
    const std::string id = namespaced(blockEntity.getString("id"));
    if (id != "minecraft:sign" && id != "minecraft:hanging_sign") {
        return;
    }
    const NbtTag empty = NbtTag::ofString(std::string{kEmptyComponent});
    std::vector<NbtTag> lines;
    for (int i = 1; i <= 4; ++i) {
        lines.push_back(line(blockEntity, "Text", i).value_or(empty));
    }
    NbtCompound front;
    front.set("messages", NbtTag::ofList(NbtList{NbtType::String, lines}));
    front.set("color", blockEntity.find("Color") != nullptr ? *blockEntity.find("Color") : NbtTag::ofString("black"));
    front.set("has_glowing_text",
              blockEntity.find("GlowingText") != nullptr ? *blockEntity.find("GlowingText") : NbtTag::ofBoolean(false));
    std::vector<std::optional<NbtTag>> filtered;
    for (int i = 1; i <= 4; ++i) {
        filtered.push_back(line(blockEntity, "FilteredText", i));
    }
    if (std::ranges::any_of(filtered, [](const std::optional<NbtTag>& value) { return value.has_value(); })) {
        std::vector<NbtTag> messages;
        for (std::size_t i = 0; i < filtered.size(); ++i) {
            messages.push_back(filtered[i].value_or(lines[i]));
        }
        front.set("filtered_messages", NbtTag::ofList(NbtList{NbtType::String, std::move(messages)}));
    }
    NbtCompound back;
    back.set("messages", NbtTag::ofList(NbtList{NbtType::String, {empty, empty, empty, empty}}));
    back.putString("color", "black");
    back.putBoolean("has_glowing_text", false);
    blockEntity.set("front_text", NbtTag::ofCompound(std::move(front)));
    blockEntity.set("back_text", NbtTag::ofCompound(std::move(back)));
    blockEntity.putBoolean("is_waxed", false);
    blockEntity.putBoolean("_filtered_correct", true);
    for (const std::string_view field : kSignFieldsToDrop) {
        blockEntity.remove(field);
    }
}

void dropInvalidFilteredText(NbtCompound& side) {
    NbtList* filtered = listAt(side, "filtered_messages");
    if (filtered == nullptr) {
        return;
    }
    const NbtTag empty = NbtTag::ofString(std::string{kEmptyComponent});
    std::vector<NbtTag> lines;
    if (const NbtList* messages = listAt(side, "messages"); messages != nullptr) {
        lines.assign(messages->begin(), messages->end());
    }
    std::vector<NbtTag> fixed;
    for (std::size_t i = 0; i < filtered->size(); ++i) {
        const NbtTag fallback = i < lines.size() ? lines[i] : empty;
        fixed.push_back((*filtered)[i] == empty ? fallback : (*filtered)[i]);
    }
    if (fixed == lines) {
        side.remove("filtered_messages");
    } else {
        side.set("filtered_messages", NbtTag::ofList(NbtList{NbtType::String, std::move(fixed)}));
    }
}

// parity: `DropInvalidSignDataFix`.
void dropInvalidSignData(NbtCompound& blockEntity, const Context&) {
    const std::string id = namespaced(blockEntity.getString("id"));
    if (id != "minecraft:sign" && id != "minecraft:hanging_sign") {
        return;
    }
    if (blockEntity.getBoolean("_filtered_correct")) {
        blockEntity.remove("_filtered_correct");
        return;
    }
    for (const std::string_view side : {"front_text", "back_text"}) {
        if (NbtCompound* held = compoundAt(blockEntity, side); held != nullptr) {
            dropInvalidFilteredText(*held);
        }
    }
    for (const std::string_view field : kSignFieldsToDrop) {
        blockEntity.remove(field);
    }
}

[[nodiscard]] std::string remapStatus(const std::string& status) {
    const std::string id = namespaced(status);
    return id == "minecraft:liquid_carvers" ? "minecraft:carvers"
           : id == "minecraft:heightmaps"   ? "minecraft:spawn"
                                            : id;
}

// parity: `RemapChunkStatusFix`.
void remapChunkStatus(NbtCompound& chunk, const Context&) {
    if (const NbtTag* status = chunk.find("Status"); status != nullptr && status->get<std::string>() != nullptr) {
        chunk.putString("Status", remapStatus(*status->get<std::string>()));
    }
    if (NbtCompound* retrogen = compoundAt(chunk, "below_zero_retrogen"); retrogen != nullptr) {
        if (const NbtTag* target = retrogen->find("target_status");
            target != nullptr && target->get<std::string>() != nullptr) {
            retrogen->putString("target_status", remapStatus(*target->get<std::string>()));
        }
    }
}

// parity: `ChunkDeleteLightFix`.
void deleteLight(NbtCompound& chunk, const Context&) {
    chunk.remove("isLightOn");
    forEachCompound(chunk, "sections", [](NbtCompound& section) {
        section.remove("BlockLight");
        section.remove("SkyLight");
    });
}

[[nodiscard]] std::optional<std::string> effectName(const NbtTag* id) {
    static const std::array<std::string_view, 34> kNames{"",
                                                         "speed",
                                                         "slowness",
                                                         "haste",
                                                         "mining_fatigue",
                                                         "strength",
                                                         "instant_health",
                                                         "instant_damage",
                                                         "jump_boost",
                                                         "nausea",
                                                         "regeneration",
                                                         "resistance",
                                                         "fire_resistance",
                                                         "water_breathing",
                                                         "invisibility",
                                                         "blindness",
                                                         "night_vision",
                                                         "hunger",
                                                         "weakness",
                                                         "poison",
                                                         "wither",
                                                         "health_boost",
                                                         "absorption",
                                                         "saturation",
                                                         "glowing",
                                                         "levitation",
                                                         "luck",
                                                         "unluck",
                                                         "slow_falling",
                                                         "conduit_power",
                                                         "dolphins_grace",
                                                         "bad_omen",
                                                         "hero_of_the_village",
                                                         "darkness"};
    const auto value = whole(id);
    if (!value.has_value() || *value < 1 || *value > 33) {
        return std::nullopt;
    }
    return "minecraft:" + std::string{kNames[static_cast<std::size_t>(*value)]};
}

/// parity: `Dynamic.replaceField`, which drops the old field and sets the new one only when there is a value.
void replaceField(NbtCompound& holder, std::string_view from, std::string_view to, std::optional<NbtTag> value) {
    holder.remove(from);
    if (value.has_value()) {
        holder.set(to, std::move(*value));
    }
}

void effectIdField(NbtCompound& holder, std::string_view from, std::string_view to) {
    const auto name = effectName(holder.find(from));
    replaceField(holder, from, to, name.has_value() ? std::optional{NbtTag::ofString(*name)} : std::nullopt);
}

void mobEffectInstance(NbtCompound& instance) {
    effectIdField(instance, "Id", "id");
    rename(instance, "Ambient", "ambient");
    rename(instance, "Amplifier", "amplifier");
    rename(instance, "Duration", "duration");
    rename(instance, "ShowParticles", "show_particles");
    rename(instance, "ShowIcon", "show_icon");
    if (NbtCompound* hidden = compoundAt(instance, "HiddenEffect"); hidden != nullptr) {
        mobEffectInstance(*hidden);
        rename(instance, "HiddenEffect", "hidden_effect");
    }
}

void mobEffectInstanceList(NbtCompound& holder, std::string_view from, std::string_view to) {
    NbtList* list = listAt(holder, from);
    if (list == nullptr) {
        holder.remove(from);
        return;
    }
    std::vector<NbtTag> fixed;
    for (std::size_t i = 0; i < list->size(); ++i) {
        if (NbtCompound* instance = (*list)[i].get<NbtCompound>(); instance != nullptr) {
            mobEffectInstance(*instance);
        }
        fixed.push_back((*list)[i]);
    }
    replaceField(holder, from, to, listOfCompounds(std::move(fixed)));
}

[[maybe_unused]] void stewEntry(NbtCompound& input, NbtCompound& output) {
    effectIdField(input, "EffectId", "id");
    if (const NbtTag* id = input.find("id"); id != nullptr && &input != &output) {
        output.set("id", *id);
    }
    const NbtTag* duration = input.find("EffectDuration");
    if (duration != nullptr) {
        output.set("duration", *duration);
    }
    input.remove("EffectDuration");
    if (&input != &output) {
        input.remove("id");
    }
}

// parity: `MobEffectIdFix`, over entities and players.
void mobEffectIdOfEntity(NbtCompound& entity, const Context&) {
    const std::string id = namespaced(entity.getString("id"));
    if (id == "minecraft:mooshroom") {
        NbtCompound entry;
        const auto name = effectName(entity.find("EffectId"));
        if (name.has_value()) {
            entry.putString("id", *name);
        }
        if (const NbtTag* duration = entity.find("EffectDuration"); duration != nullptr) {
            entry.set("duration", *duration);
        }
        if (!entry.empty()) {
            entity.set("stew_effects", listOfCompounds({NbtTag::ofCompound(std::move(entry))}));
        }
        entity.remove("EffectId");
        entity.remove("EffectDuration");
    } else if (id == "minecraft:arrow") {
        mobEffectInstanceList(entity, "CustomPotionEffects", "custom_potion_effects");
    } else if (id == "minecraft:area_effect_cloud") {
        mobEffectInstanceList(entity, "Effects", "effects");
    }
    mobEffectInstanceList(entity, "ActiveEffects", "active_effects");
}

// parity: `MobEffectIdFix`, over beacons.
void mobEffectIdOfBeacon(NbtCompound& blockEntity, const Context&) {
    effectIdField(blockEntity, "Primary", "primary_effect");
    effectIdField(blockEntity, "Secondary", "secondary_effect");
}

// parity: `MobEffectIdFix`, over the tag of potions and suspicious stew.
void mobEffectIdOfItem(NbtCompound& stack, const Context&) {
    NbtCompound* tag = compoundAt(stack, "tag");
    if (tag == nullptr) {
        return;
    }
    const std::string id = namespaced(stack.getString("id"));
    if (id == "minecraft:suspicious_stew") {
        if (NbtList* effects = listAt(*tag, "Effects"); effects != nullptr) {
            std::vector<NbtTag> fixed;
            for (std::size_t i = 0; i < effects->size(); ++i) {
                const NbtTag& entry = (*effects)[i];
                if (const NbtCompound* source = entry.get<NbtCompound>(); source != nullptr) {
                    NbtCompound out = *source;
                    NbtCompound entryOut;
                    effectIdField(out, "EffectId", "id");
                    if (const NbtTag* duration = out.find("EffectDuration"); duration != nullptr) {
                        out.set("duration", *duration);
                        out.remove("EffectDuration");
                    }
                    fixed.push_back(NbtTag::ofCompound(std::move(out)));
                } else {
                    fixed.push_back(entry);
                }
            }
            replaceField(*tag, "Effects", "effects", listOfCompounds(std::move(fixed)));
        }
    } else if (id == "minecraft:potion" || id == "minecraft:splash_potion" || id == "minecraft:lingering_potion" ||
               id == "minecraft:tipped_arrow") {
        mobEffectInstanceList(*tag, "CustomPotionEffects", "custom_potion_effects");
    }
}

// parity: `PrimedTntBlockStateFixer`.
void primedTntBlockState(NbtCompound& entity, const Context&) {
    if (namespaced(entity.getString("id")) != "minecraft:tnt") {
        return;
    }
    NbtCompound state;
    state.putString("Name", "minecraft:tnt");
    entity.set("block_state", NbtTag::ofCompound(std::move(state)));
    rename(entity, "Fuse", "fuse");
}

// parity: `FixProjectileStoredItem`.
void projectileStoredItem(NbtCompound& entity, const Context&) {
    const std::string id = namespaced(entity.getString("id"));
    std::string item;
    if (id == "minecraft:arrow") {
        item = entity.getString("Potion").empty() || entity.getString("Potion") == "minecraft:empty"
                   ? "minecraft:arrow"
                   : "minecraft:tipped_arrow";
    } else if (id == "minecraft:spectral_arrow") {
        item = "minecraft:spectral_arrow";
    } else {
        return;
    }
    NbtCompound stack;
    stack.putString("id", item);
    stack.putInt("Count", 1);
    entity.set("item", NbtTag::ofCompound(std::move(stack)));
}

// parity: `LegacyBlockRenameFix`, the grass block that became short grass, over a block state.
void grassBlockState(NbtCompound& state) {
    if (namespaced(state.getString("Name")) == "minecraft:grass") {
        state.putString("Name", "minecraft:short_grass");
    }
}

void grassOfPalette(NbtCompound& entry, const Context&) { grassBlockState(entry); }

void grassOfEntity(NbtCompound& entity, const Context&) {
    for (const std::string_view key :
         {"inBlockState", "carriedBlockState", "BlockState", "DisplayState", "block_state"}) {
        if (NbtCompound* state = compoundAt(entity, key); state != nullptr) {
            grassBlockState(*state);
        }
    }
}

void grassOfPiston(NbtCompound& blockEntity, const Context&) {
    if (NbtCompound* state = compoundAt(blockEntity, "blockState"); state != nullptr) {
        grassBlockState(*state);
    }
}

// parity: `ItemRenameFix`, the grass item that became short grass.
void grassItem(NbtCompound& stack, const Context&) {
    if (namespaced(stack.getString("id")) == "minecraft:grass") {
        stack.putString("id", "minecraft:short_grass");
    }
}

} // namespace

void addFixesFrom3400(std::vector<Fix>& fixes) {
    fixes.insert(fixes.end(), {
                                  {3438, Target::BlockEntity, "minecraft:suspicious_sand", brushableBlockEntity},
                                  {3438, Target::BlockEntity, "minecraft:brushable_block", brushableFields},
                                  {3438, Target::LegacyStack, {}, potteryShards},
                                  {3439, Target::BlockEntity, {}, doubleSidedSign},
                                  {3447, Target::LegacyStack, {}, potterySherds},
                                  {3448, Target::BlockEntity, "minecraft:decorated_pot", decoratedPotFields},
                                  {3450, Target::Chunk, {}, remapChunkStatus},
                                  {3451, Target::Chunk, {}, deleteLight},
                                  {3564, Target::BlockEntity, {}, dropInvalidSignData},
                                  {3568, Target::BlockEntity, "minecraft:beacon", mobEffectIdOfBeacon},
                                  {3568, Target::Entity, {}, mobEffectIdOfEntity},
                                  {3568, Target::LegacyStack, {}, mobEffectIdOfItem},
                                  {3683, Target::Entity, {}, primedTntBlockState},
                                  {3685, Target::Entity, {}, projectileStoredItem},
                                  {3692, Target::Palette, {}, grassOfPalette},
                                  {3692, Target::Entity, {}, grassOfEntity},
                                  {3692, Target::BlockEntity, "minecraft:piston", grassOfPiston},
                                  {3692, Target::LegacyStack, {}, grassItem},
                              });
}

} // namespace basalt::metamorph::datafix
