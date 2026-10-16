#include "Componentization.hpp"

#include "FixSupport.hpp"

#include <basalt/wire/common/json/Json.hpp>

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

constexpr int kHideEnchantments = 1;
constexpr int kHideModifiers = 2;
constexpr int kHideUnbreakable = 4;
constexpr int kHideCanDestroy = 8;
constexpr int kHideCanPlace = 16;
constexpr int kHideAdditional = 32;
constexpr int kHideDye = 64;
constexpr int kHideUpgrades = 128;

const std::set<std::string_view> kPotionHolders{"minecraft:potion", "minecraft:splash_potion",
                                                "minecraft:lingering_potion", "minecraft:tipped_arrow"};
const std::set<std::string_view> kBucketedMobs{"minecraft:pufferfish_bucket", "minecraft:salmon_bucket",
                                               "minecraft:cod_bucket",        "minecraft:tropical_fish_bucket",
                                               "minecraft:axolotl_bucket",    "minecraft:tadpole_bucket"};
constexpr std::array<std::string_view, 10> kBucketedMobTags{
    "NoAI",   "Silent", "NoGravity", "Glowing",         "Invulnerable",
    "Health", "Age",    "Variant",   "HuntingCooldown", "BucketVariantTag"};
const std::set<std::string_view> kBooleanProperties{"attached",
                                                    "bottom",
                                                    "conditional",
                                                    "disarmed",
                                                    "drag",
                                                    "enabled",
                                                    "extended",
                                                    "eye",
                                                    "falling",
                                                    "hanging",
                                                    "has_bottle_0",
                                                    "has_bottle_1",
                                                    "has_bottle_2",
                                                    "has_record",
                                                    "has_book",
                                                    "inverted",
                                                    "in_wall",
                                                    "lit",
                                                    "locked",
                                                    "occupied",
                                                    "open",
                                                    "persistent",
                                                    "powered",
                                                    "short",
                                                    "signal_fire",
                                                    "snowy",
                                                    "triggered",
                                                    "unstable",
                                                    "waterlogged",
                                                    "berries",
                                                    "bloom",
                                                    "shrieking",
                                                    "can_summon",
                                                    "up",
                                                    "down",
                                                    "north",
                                                    "east",
                                                    "south",
                                                    "west",
                                                    "slot_0_occupied",
                                                    "slot_1_occupied",
                                                    "slot_2_occupied",
                                                    "slot_3_occupied",
                                                    "slot_4_occupied",
                                                    "slot_5_occupied",
                                                    "cracked",
                                                    "crafting"};

[[nodiscard]] std::optional<std::int64_t> integerOf(const NbtTag* tag) {
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

[[nodiscard]] std::optional<double> numberOf(const NbtTag* tag) {
    if (const auto whole = integerOf(tag); whole.has_value()) {
        return static_cast<double>(*whole);
    }
    if (tag == nullptr) {
        return std::nullopt;
    }
    if (const auto* v = tag->get<float>(); v != nullptr) {
        return static_cast<double>(*v);
    }
    if (const auto* v = tag->get<double>(); v != nullptr) {
        return *v;
    }
    return std::nullopt;
}

[[nodiscard]] std::int32_t intOr(const NbtTag* tag, std::int32_t fallback) {
    const auto number = numberOf(tag);
    return number.has_value() ? static_cast<std::int32_t>(*number) : fallback;
}

[[nodiscard]] std::string_view stringOr(const NbtTag* tag, std::string_view fallback) {
    const auto* text = tag == nullptr ? nullptr : tag->get<std::string>();
    return text == nullptr ? fallback : std::string_view{*text};
}

[[nodiscard]] NbtTag compound(NbtCompound value) { return NbtTag::ofCompound(std::move(value)); }

[[nodiscard]] NbtTag emptyList() { return NbtTag::ofList(NbtList{NbtType::End, {}}); }

[[nodiscard]] NbtTag listOf(std::vector<NbtTag> elements) {
    if (elements.empty()) {
        return emptyList();
    }
    const NbtType type = elements.front().type();
    return NbtTag::ofList(NbtList{type, std::move(elements)});
}

[[nodiscard]] bool isEmptyCompound(const NbtTag& tag) {
    const NbtCompound* held = tag.get<NbtCompound>();
    return held != nullptr && held->empty();
}

[[nodiscard]] std::string_view dyeName(std::int32_t id) {
    static constexpr std::array<std::string_view, 16> kNames{
        "white",      "orange", "magenta", "light_blue", "yellow", "lime",  "pink", "gray",
        "light_gray", "cyan",   "purple",  "blue",       "brown",  "green", "red",  "black"};
    return id >= 1 && id < 16 ? kNames[static_cast<std::size_t>(id)] : "white";
}

/// parity: `LegacyComponentDataFixUtils.isStrictlyValidJson`.
[[nodiscard]] bool strictlyValidJson(const NbtTag& component) {
    const auto* text = component.get<std::string>();
    return text != nullptr && wire::common::parseJson(*text).has_value();
}

[[nodiscard]] std::string quoted(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    return out + "\"";
}

/// parity: `LegacyComponentDataFixUtils.createTranslatableComponent`.
[[nodiscard]] NbtTag translatable(std::string_view key) {
    return NbtTag::ofString("{\"translate\":" + quoted(key) + "}");
}

[[nodiscard]] std::string javaNumber(const NbtTag& tag) {
    if (const auto whole = integerOf(&tag); whole.has_value()) {
        return std::to_string(*whole);
    }
    const auto number = numberOf(&tag);
    return std::to_string(number.value_or(0.0));
}

/// parity: `ItemStackData`, the stack split into the pieces the fix moves around.
class Stack {
public:
    [[nodiscard]] static std::optional<Stack> read(const NbtCompound& legacy) {
        const NbtTag* id = legacy.find("id");
        const auto count = numberOf(legacy.find("Count"));
        if (id == nullptr || id->get<std::string>() == nullptr || !count.has_value()) {
            return std::nullopt;
        }
        Stack stack;
        stack.item_ = namespaced(*id->get<std::string>());
        stack.count_ = static_cast<std::int32_t>(*count);
        stack.remainder_ = legacy;
        stack.remainder_.remove("id");
        stack.remainder_.remove("Count");
        if (const NbtTag* tag = stack.remainder_.find("tag"); tag != nullptr && tag->get<NbtCompound>() != nullptr) {
            stack.tag_ = *tag->get<NbtCompound>();
        }
        stack.remainder_.remove("tag");
        return stack;
    }

    [[nodiscard]] std::optional<NbtTag> removeTag(std::string_view key) {
        const NbtTag* held = tag_.find(key);
        if (held == nullptr) {
            return std::nullopt;
        }
        NbtTag value = *held;
        tag_.remove(key);
        return value;
    }

    void setComponent(std::string_view type, NbtTag value) { components_.set(type, std::move(value)); }

    void setComponent(std::string_view type, const NbtTag* value) {
        if (value != nullptr) {
            components_.set(type, *value);
        }
    }

    void moveTagToComponent(std::string_view key, std::string_view type) {
        if (auto value = removeTag(key); value.has_value()) {
            setComponent(type, std::move(*value));
        }
    }

    void moveTagToComponent(std::string_view key, std::string_view type, const NbtTag& defaultValue) {
        auto value = removeTag(key);
        if (value.has_value() && !(*value == defaultValue)) {
            setComponent(type, std::move(*value));
        }
    }

    /// Moves `fromKey` of the tag into `target[toKey]`.
    void moveTagInto(std::string_view fromKey, NbtCompound& target, std::string_view toKey) {
        if (auto value = removeTag(fromKey); value.has_value()) {
            target.set(toKey, std::move(*value));
        }
    }

    template <typename Fix>
    void fixSubTag(std::string_view key, bool skipWhenMissing, Fix&& fix) {
        const NbtTag* held = tag_.find(key);
        if (skipWhenMissing && held == nullptr) {
            return;
        }
        NbtCompound value;
        if (held != nullptr && held->get<NbtCompound>() != nullptr) {
            value = *held->get<NbtCompound>();
        }
        value = fix(std::move(value));
        if (value.empty()) {
            tag_.remove(key);
        } else {
            tag_.set(key, compound(std::move(value)));
        }
    }

    [[nodiscard]] bool is(std::string_view id) const { return item_ == id; }

    [[nodiscard]] bool isOneOf(const std::set<std::string_view>& ids) const { return ids.contains(item_); }

    [[nodiscard]] NbtCompound write() {
        NbtCompound result;
        result.putString("id", item_);
        result.putInt("count", count_);
        if (!tag_.empty()) {
            components_.set("minecraft:custom_data", compound(tag_));
        }
        if (!components_.empty()) {
            result.set("components", compound(components_));
        }
        NbtCompound merged = remainder_;
        for (std::size_t i = 0; i < result.names().size(); ++i) {
            merged.set(result.names()[i], result.values()[i]);
        }
        return merged;
    }

    NbtCompound tag_;
    NbtCompound components_;

private:
    std::string item_;
    std::int32_t count_ = 0;
    NbtCompound remainder_;
};

[[nodiscard]] NbtCompound fixBlockStateTag(const NbtCompound& blockState) {
    NbtCompound out;
    for (std::size_t i = 0; i < blockState.names().size(); ++i) {
        const std::string& key = blockState.names()[i];
        const NbtTag& value = blockState.values()[i];
        const bool numeric = numberOf(&value).has_value();
        if (numeric && kBooleanProperties.contains(key)) {
            out.putString(key, numberOf(&value).value() != 0.0 ? "true" : "false");
        } else if (numeric) {
            out.putString(key, javaNumber(value));
        } else {
            out.set(key, value);
        }
    }
    return out;
}

[[nodiscard]] NbtCompound fixDisplay(Stack& stack, NbtCompound display, int hideFlags) {
    if (const NbtTag* name = display.find("Name"); name != nullptr && strictlyValidJson(*name)) {
        stack.setComponent("minecraft:custom_name", *name);
    }
    if (const NbtTag* lore = display.find("Lore"); lore != nullptr) {
        std::vector<NbtTag> lines;
        if (const NbtList* list = lore->get<NbtList>(); list != nullptr) {
            for (const NbtTag& line : *list) {
                if (strictlyValidJson(line)) {
                    lines.push_back(line);
                }
            }
        }
        stack.setComponent("minecraft:lore", listOf(std::move(lines)));
    }
    const auto color = numberOf(display.find("color"));
    const bool hideDye = (hideFlags & kHideDye) != 0;
    if (color.has_value() || hideDye) {
        NbtCompound dyed;
        dyed.putInt("rgb", color.has_value() ? static_cast<std::int32_t>(*color) : 10511680);
        if (hideDye) {
            dyed.putBoolean("show_in_tooltip", false);
        }
        stack.setComponent("minecraft:dyed_color", compound(std::move(dyed)));
    }
    if (const NbtTag* locName = display.find("LocName"); locName != nullptr && locName->get<std::string>() != nullptr) {
        stack.setComponent("minecraft:item_name", translatable(*locName->get<std::string>()));
    }
    if (stack.is("minecraft:filled_map")) {
        stack.setComponent("minecraft:map_color", display.find("MapColor"));
        display.remove("MapColor");
    }
    for (const std::string_view key : {"Name", "Lore", "color", "LocName"}) {
        display.remove(key);
    }
    return display;
}

/// parity: `fixBlockStatePredicate`, the `block[state]{nbt}` string as a predicate.
[[nodiscard]] NbtTag fixBlockStatePredicate(const std::string& text) {
    const std::size_t startProperties = text.find('[');
    const std::size_t startNbt = text.find('{');
    std::size_t nameEnd = text.size();
    if (startProperties != std::string::npos) {
        nameEnd = startProperties;
    }
    if (startNbt != std::string::npos) {
        nameEnd = std::min(nameEnd, startNbt);
    }
    const auto trim = [](std::string value) {
        const std::size_t first = value.find_first_not_of(' ');
        const std::size_t last = value.find_last_not_of(' ');
        return first == std::string::npos ? std::string{} : value.substr(first, last - first + 1);
    };
    NbtCompound predicate;
    predicate.putString("blocks", trim(text.substr(0, nameEnd)));
    const std::size_t endProperties = text.find(']');
    if (startProperties != std::string::npos && endProperties != std::string::npos) {
        NbtCompound properties;
        const std::string inside = text.substr(startProperties + 1, endProperties - startProperties - 1);
        std::size_t start = 0;
        while (start <= inside.size()) {
            const std::size_t comma = inside.find(',', start);
            const std::string property =
                inside.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            if (const std::size_t assignment = property.find('='); assignment != std::string::npos) {
                properties.putString(trim(property.substr(0, assignment)), trim(property.substr(assignment + 1)));
            }
            if (comma == std::string::npos) {
                break;
            }
            start = comma + 1;
        }
        predicate.set("state", compound(std::move(properties)));
    }
    const std::size_t endNbt = text.find('}');
    if (startNbt != std::string::npos && endNbt != std::string::npos) {
        predicate.putString("nbt", text.substr(startNbt, endNbt - startNbt + 1));
    }
    return compound(std::move(predicate));
}

void fixBlockStatePredicates(Stack& stack, std::string_view tag, std::string_view componentId, bool hide) {
    const auto old = stack.removeTag(tag);
    if (!old.has_value()) {
        return;
    }
    std::vector<NbtTag> predicates;
    if (const NbtList* list = old->get<NbtList>(); list != nullptr) {
        for (const NbtTag& value : *list) {
            const auto* text = value.get<std::string>();
            predicates.push_back(text == nullptr ? value : fixBlockStatePredicate(*text));
        }
    }
    NbtCompound component;
    component.set("predicates", listOf(std::move(predicates)));
    if (hide) {
        component.putBoolean("show_in_tooltip", false);
    }
    stack.setComponent(componentId, compound(std::move(component)));
}

[[nodiscard]] NbtTag fixAttributeModifier(const NbtCompound& input) {
    NbtCompound result;
    result.putString("name", "");
    result.putDouble("amount", 0.0);
    result.putString("operation", "add_value");
    const auto copy = [&](std::string_view from, std::string_view to) {
        if (const NbtTag* held = input.find(from); held != nullptr) {
            result.set(to, *held);
        }
    };
    copy("AttributeName", "type");
    copy("Slot", "slot");
    copy("UUID", "uuid");
    copy("Name", "name");
    copy("Amount", "amount");
    if (const NbtTag* operation = input.find("Operation"); operation != nullptr) {
        const std::int32_t value = intOr(operation, 0);
        result.putString("operation", value == 1   ? "add_multiplied_base"
                                      : value == 2 ? "add_multiplied_total"
                                                   : "add_value");
    }
    return compound(std::move(result));
}

[[nodiscard]] std::string_view mapDecorationType(std::int32_t id) {
    static constexpr std::array<std::string_view, 34> kTypes{
        "player",        "frame",          "red_marker",        "blue_marker",       "target_x",
        "target_point",  "player_off_map", "player_off_limits", "mansion",           "monument",
        "banner_white",  "banner_orange",  "banner_magenta",    "banner_light_blue", "banner_yellow",
        "banner_lime",   "banner_pink",    "banner_gray",       "banner_light_gray", "banner_cyan",
        "banner_purple", "banner_blue",    "banner_brown",      "banner_green",      "banner_red",
        "banner_black",  "red_x",          "village_desert",    "village_plains",    "village_savanna",
        "village_snowy", "village_taiga",  "jungle_temple",     "swamp_hut"};
    return id >= 1 && id < 34 ? kTypes[static_cast<std::size_t>(id)] : "player";
}

void fixEnchantments(Stack& stack, std::string_view key, std::string_view componentType, bool hide) {
    const auto raw = stack.removeTag(key);
    std::vector<std::pair<std::string, std::int32_t>> enchantments;
    if (raw.has_value()) {
        if (const NbtList* list = raw->get<NbtList>(); list != nullptr) {
            for (const NbtTag& entry : *list) {
                const NbtCompound* held = entry.get<NbtCompound>();
                const NbtTag* id = held == nullptr ? nullptr : held->find("id");
                const auto level = held == nullptr ? std::nullopt : numberOf(held->find("lvl"));
                if (id == nullptr || id->get<std::string>() == nullptr || !level.has_value()) {
                    continue;
                }
                const std::int32_t clamped = std::clamp(static_cast<std::int32_t>(*level), 0, 255);
                if (clamped > 0) {
                    enchantments.emplace_back(*id->get<std::string>(), clamped);
                }
            }
        }
    }
    if (!enchantments.empty() || hide) {
        NbtCompound levels;
        for (const auto& [id, level] : enchantments) {
            levels.putInt(id, level);
        }
        NbtCompound component;
        component.set("levels", compound(std::move(levels)));
        if (hide) {
            component.putBoolean("show_in_tooltip", false);
        }
        stack.setComponent(componentType, compound(std::move(component)));
    }
    if (raw.has_value() && enchantments.empty()) {
        stack.setComponent("minecraft:enchantment_glint_override", NbtTag::ofBoolean(true));
    }
}

[[nodiscard]] NbtCompound fixBlockEntityTag(Stack& stack, NbtCompound blockEntity, const std::string& id) {
    stack.setComponent("minecraft:lock", blockEntity.find("Lock"));
    blockEntity.remove("Lock");
    if (const NbtTag* lootTable = blockEntity.find("LootTable"); lootTable != nullptr) {
        NbtCompound containerLoot;
        containerLoot.set("loot_table", *lootTable);
        const auto seed = integerOf(blockEntity.find("LootTableSeed"));
        if (seed.value_or(0) != 0) {
            containerLoot.putLong("seed", *seed);
        }
        stack.setComponent("minecraft:container_loot", compound(std::move(containerLoot)));
        blockEntity.remove("LootTable");
        blockEntity.remove("LootTableSeed");
    }
    static const std::set<std::string_view> kContainers{"minecraft:shulker_box",   "minecraft:chest",
                                                        "minecraft:trapped_chest", "minecraft:furnace",
                                                        "minecraft:ender_chest",   "minecraft:dispenser",
                                                        "minecraft:dropper",       "minecraft:brewing_stand",
                                                        "minecraft:hopper",        "minecraft:barrel",
                                                        "minecraft:smoker",        "minecraft:blast_furnace",
                                                        "minecraft:campfire",      "minecraft:chiseled_bookshelf",
                                                        "minecraft:crafter"};
    if (id == "minecraft:skull") {
        stack.setComponent("minecraft:note_block_sound", blockEntity.find("note_block_sound"));
        blockEntity.remove("note_block_sound");
    } else if (id == "minecraft:decorated_pot") {
        stack.setComponent("minecraft:pot_decorations", blockEntity.find("sherds"));
        if (const NbtTag* item = blockEntity.find("item"); item != nullptr) {
            NbtCompound slot;
            slot.putInt("slot", 0);
            slot.set("item", *item);
            stack.setComponent("minecraft:container", listOf({compound(std::move(slot))}));
        }
        blockEntity.remove("sherds");
        blockEntity.remove("item");
    } else if (id == "minecraft:banner") {
        stack.setComponent("minecraft:banner_patterns", blockEntity.find("patterns"));
        if (const auto base = numberOf(blockEntity.find("Base")); base.has_value()) {
            stack.setComponent("minecraft:base_color",
                               NbtTag::ofString(std::string{dyeName(static_cast<std::int32_t>(*base))}));
        }
        blockEntity.remove("patterns");
        blockEntity.remove("Base");
    } else if (kContainers.contains(id)) {
        std::vector<NbtTag> items;
        if (const NbtTag* held = blockEntity.find("Items"); held != nullptr && held->get<NbtList>() != nullptr) {
            for (const NbtTag& entry : *held->get<NbtList>()) {
                const NbtCompound* source = entry.get<NbtCompound>();
                if (source == nullptr) {
                    continue;
                }
                NbtCompound slot;
                slot.putInt("slot", static_cast<std::int32_t>(intOr(source->find("Slot"), 0) & 255));
                NbtCompound item = *source;
                item.remove("Slot");
                slot.set("item", compound(std::move(item)));
                items.push_back(compound(std::move(slot)));
            }
        }
        if (!items.empty()) {
            stack.setComponent("minecraft:container", listOf(std::move(items)));
        }
        blockEntity.remove("Items");
    } else if (id == "minecraft:beehive") {
        stack.setComponent("minecraft:bees", blockEntity.find("bees"));
        blockEntity.remove("bees");
    }
    return blockEntity;
}

[[nodiscard]] NbtTag fixFireworkExplosion(NbtCompound explosion) {
    const std::int32_t type = intOr(explosion.find("Type"), 0);
    explosion.putString("shape", type == 1   ? "large_ball"
                                 : type == 2 ? "star"
                                 : type == 3 ? "creeper"
                                 : type == 4 ? "burst"
                                             : "small_ball");
    explosion.remove("Type");
    rename(explosion, "Colors", "colors");
    rename(explosion, "FadeColors", "fade_colors");
    rename(explosion, "Trail", "has_trail");
    rename(explosion, "Flicker", "has_twinkle");
    return compound(std::move(explosion));
}

[[nodiscard]] bool validPlayerName(std::string_view name) {
    return name.size() <= 16 && std::ranges::all_of(name, [](char c) {
               const auto code = static_cast<unsigned char>(c);
               return code > 32 && code < 127;
           });
}

[[nodiscard]] std::optional<NbtTag> fixProfileProperties(const NbtTag* held) {
    const NbtCompound* properties = held == nullptr ? nullptr : held->get<NbtCompound>();
    if (properties == nullptr || properties->empty()) {
        return std::nullopt;
    }
    std::vector<NbtTag> out;
    for (std::size_t i = 0; i < properties->names().size(); ++i) {
        const NbtList* list = properties->values()[i].get<NbtList>();
        if (list == nullptr) {
            continue;
        }
        for (const NbtTag& entry : *list) {
            const NbtCompound* source = entry.get<NbtCompound>();
            if (source == nullptr) {
                continue;
            }
            NbtCompound property;
            property.putString("name", properties->names()[i]);
            property.putString("value", std::string{stringOr(source->find("Value"), "")});
            if (const NbtTag* signature = source->find("Signature");
                signature != nullptr && signature->get<std::string>() != nullptr) {
                property.set("signature", *signature);
            }
            out.push_back(compound(std::move(property)));
        }
    }
    if (out.empty()) {
        return std::nullopt;
    }
    return listOf(std::move(out));
}

void fixPotionContents(Stack& stack) {
    NbtCompound component;
    if (const auto potion = stack.removeTag("Potion"); potion.has_value()) {
        const auto* id = potion->get<std::string>();
        if (id != nullptr && *id != "minecraft:empty") {
            component.putString("potion", *id);
        }
    }
    stack.moveTagInto("CustomPotionColor", component, "custom_color");
    stack.moveTagInto("custom_potion_effects", component, "custom_effects");
    if (!component.empty()) {
        stack.setComponent("minecraft:potion_contents", compound(std::move(component)));
    }
}

[[nodiscard]] NbtTag filteredText(std::string_view text, const std::optional<std::string>& filtered) {
    NbtCompound page;
    page.putString("raw", std::string{text});
    if (filtered.has_value()) {
        page.putString("filtered", *filtered);
    }
    return compound(std::move(page));
}

[[nodiscard]] std::optional<NbtTag> fixBookPages(Stack& stack) {
    std::vector<std::string> pages;
    if (const auto held = stack.removeTag("pages"); held.has_value() && held->get<NbtList>() != nullptr) {
        for (const NbtTag& page : *held->get<NbtList>()) {
            pages.emplace_back(stringOr(&page, ""));
        }
    }
    std::map<std::string, std::string> filtered;
    if (const auto held = stack.removeTag("filtered_pages"); held.has_value() && held->get<NbtCompound>() != nullptr) {
        const NbtCompound& source = *held->get<NbtCompound>();
        for (std::size_t i = 0; i < source.names().size(); ++i) {
            filtered[source.names()[i]] = std::string{stringOr(&source.values()[i], "")};
        }
    }
    if (pages.empty()) {
        return std::nullopt;
    }
    std::vector<NbtTag> fixed;
    for (std::size_t i = 0; i < pages.size(); ++i) {
        const auto found = filtered.find(std::to_string(i));
        fixed.push_back(filteredText(pages[i], found == filtered.end() ? std::nullopt : std::optional{found->second}));
    }
    return listOf(std::move(fixed));
}

void fixItemStack(Stack& stack) {
    const int hideFlags = [&] {
        const auto flags = stack.removeTag("HideFlags");
        return flags.has_value() ? intOr(&*flags, 0) : 0;
    }();
    stack.moveTagToComponent("Damage", "minecraft:damage", NbtTag::ofInt(0));
    stack.moveTagToComponent("RepairCost", "minecraft:repair_cost", NbtTag::ofInt(0));
    stack.moveTagToComponent("CustomModelData", "minecraft:custom_model_data");
    if (const auto blockState = stack.removeTag("BlockStateTag"); blockState.has_value()) {
        const NbtCompound* held = blockState->get<NbtCompound>();
        stack.setComponent("minecraft:block_state", held == nullptr ? *blockState : compound(fixBlockStateTag(*held)));
    }
    stack.moveTagToComponent("EntityTag", "minecraft:entity_data");
    stack.fixSubTag("BlockEntityTag", false, [&](NbtCompound blockEntityTag) {
        const std::string id = namespaced(stringOr(blockEntityTag.find("id"), ""));
        blockEntityTag = fixBlockEntityTag(stack, std::move(blockEntityTag), id);
        NbtCompound withoutId = blockEntityTag;
        withoutId.remove("id");
        return withoutId.empty() ? withoutId : blockEntityTag;
    });
    stack.moveTagToComponent("BlockEntityTag", "minecraft:block_entity_data");
    if (const auto unbreakable = stack.removeTag("Unbreakable"); unbreakable.has_value()) {
        if (numberOf(&*unbreakable).value_or(0.0) != 0.0) {
            NbtCompound component;
            if ((hideFlags & kHideUnbreakable) != 0) {
                component.putBoolean("show_in_tooltip", false);
            }
            stack.setComponent("minecraft:unbreakable", compound(std::move(component)));
        }
    }
    fixEnchantments(stack, "Enchantments", "minecraft:enchantments", (hideFlags & kHideEnchantments) != 0);
    if (stack.is("minecraft:enchanted_book")) {
        fixEnchantments(stack, "StoredEnchantments", "minecraft:stored_enchantments",
                        (hideFlags & kHideAdditional) != 0);
    }
    stack.fixSubTag("display", false,
                    [&](NbtCompound display) { return fixDisplay(stack, std::move(display), hideFlags); });
    fixBlockStatePredicates(stack, "CanDestroy", "minecraft:can_break", (hideFlags & kHideCanDestroy) != 0);
    fixBlockStatePredicates(stack, "CanPlaceOn", "minecraft:can_place_on", (hideFlags & kHideCanPlace) != 0);
    if (const auto modifiers = stack.removeTag("AttributeModifiers"); modifiers.has_value()) {
        std::vector<NbtTag> fixed;
        if (const NbtList* list = modifiers->get<NbtList>(); list != nullptr) {
            for (const NbtTag& entry : *list) {
                if (const NbtCompound* source = entry.get<NbtCompound>(); source != nullptr) {
                    fixed.push_back(fixAttributeModifier(*source));
                }
            }
        }
        NbtCompound component;
        component.set("modifiers", listOf(std::move(fixed)));
        if ((hideFlags & kHideModifiers) != 0) {
            component.putBoolean("show_in_tooltip", false);
        }
        stack.setComponent("minecraft:attribute_modifiers", compound(std::move(component)));
    }
    if (auto trim = stack.removeTag("Trim"); trim.has_value()) {
        if (NbtCompound* held = trim->get<NbtCompound>(); held != nullptr && (hideFlags & kHideUpgrades) != 0) {
            held->putBoolean("show_in_tooltip", false);
        }
        stack.setComponent("minecraft:trim", std::move(*trim));
    }
    if ((hideFlags & kHideAdditional) != 0) {
        stack.setComponent("minecraft:hide_additional_tooltip", compound(NbtCompound{}));
    }
    if (stack.is("minecraft:crossbow")) {
        stack.removeTag("Charged");
        const auto empty = emptyList();
        auto projectiles = stack.removeTag("ChargedProjectiles");
        const NbtList* list = projectiles.has_value() ? projectiles->get<NbtList>() : nullptr;
        if (projectiles.has_value() && !(list != nullptr && list->empty())) {
            stack.setComponent("minecraft:charged_projectiles", std::move(*projectiles));
        }
    }
    if (stack.is("minecraft:bundle")) {
        auto items = stack.removeTag("Items");
        const NbtList* list = items.has_value() ? items->get<NbtList>() : nullptr;
        if (items.has_value() && !(list != nullptr && list->empty())) {
            stack.setComponent("minecraft:bundle_contents", std::move(*items));
        }
    }
    if (stack.is("minecraft:filled_map")) {
        stack.moveTagToComponent("map", "minecraft:map_id");
        NbtCompound decorations;
        if (const auto held = stack.removeTag("Decorations"); held.has_value() && held->get<NbtList>() != nullptr) {
            for (const NbtTag& entry : *held->get<NbtList>()) {
                const NbtCompound* source = entry.get<NbtCompound>();
                if (source == nullptr) {
                    continue;
                }
                const std::string id{stringOr(source->find("id"), "")};
                if (decorations.contains(id)) {
                    continue;
                }
                NbtCompound value;
                value.putString("type", std::string{mapDecorationType(intOr(source->find("type"), 0))});
                value.putDouble("x", numberOf(source->find("x")).value_or(0.0));
                value.putDouble("z", numberOf(source->find("z")).value_or(0.0));
                value.putFloat("rotation", static_cast<float>(numberOf(source->find("rot")).value_or(0.0)));
                decorations.set(id, compound(std::move(value)));
            }
        }
        if (!decorations.empty()) {
            stack.setComponent("minecraft:map_decorations", compound(std::move(decorations)));
        }
    }
    if (stack.isOneOf(kPotionHolders)) {
        fixPotionContents(stack);
    }
    if (stack.is("minecraft:writable_book")) {
        if (auto pages = fixBookPages(stack); pages.has_value()) {
            NbtCompound content;
            content.set("pages", std::move(*pages));
            stack.setComponent("minecraft:writable_book_content", compound(std::move(content)));
        }
    }
    if (stack.is("minecraft:written_book")) {
        auto pages = fixBookPages(stack);
        const auto titleTag = stack.removeTag("title");
        const std::string title{stringOr(titleTag.has_value() ? &*titleTag : nullptr, "")};
        const auto filteredTag = stack.removeTag("filtered_title");
        std::optional<std::string> filteredTitle;
        if (filteredTag.has_value() && filteredTag->get<std::string>() != nullptr) {
            filteredTitle = *filteredTag->get<std::string>();
        }
        NbtCompound component;
        component.set("title", filteredText(title, filteredTitle));
        stack.moveTagInto("author", component, "author");
        stack.moveTagInto("resolved", component, "resolved");
        stack.moveTagInto("generation", component, "generation");
        if (pages.has_value()) {
            component.set("pages", std::move(*pages));
        }
        stack.setComponent("minecraft:written_book_content", compound(std::move(component)));
    }
    if (stack.is("minecraft:suspicious_stew")) {
        stack.moveTagToComponent("effects", "minecraft:suspicious_stew_effects");
    }
    if (stack.is("minecraft:debug_stick")) {
        stack.moveTagToComponent("DebugProperty", "minecraft:debug_stick_state");
    }
    if (stack.isOneOf(kBucketedMobs)) {
        NbtCompound data;
        for (const std::string_view key : kBucketedMobTags) {
            stack.moveTagInto(key, data, key);
        }
        if (!data.empty()) {
            stack.setComponent("minecraft:bucket_entity_data", compound(std::move(data)));
        }
    }
    if (stack.is("minecraft:goat_horn")) {
        stack.moveTagToComponent("instrument", "minecraft:instrument");
    }
    if (stack.is("minecraft:knowledge_book")) {
        stack.moveTagToComponent("Recipes", "minecraft:recipes");
    }
    if (stack.is("minecraft:compass")) {
        const auto position = stack.removeTag("LodestonePos");
        const auto dimension = stack.removeTag("LodestoneDimension");
        if (position.has_value() || dimension.has_value()) {
            const auto trackedTag = stack.removeTag("LodestoneTracked");
            const bool tracked = trackedTag.has_value() ? numberOf(&*trackedTag).value_or(1.0) != 0.0 : true;
            NbtCompound component;
            if (position.has_value() && dimension.has_value()) {
                NbtCompound target;
                target.set("pos", *position);
                target.set("dimension", *dimension);
                component.set("target", compound(std::move(target)));
            }
            if (!tracked) {
                component.putBoolean("tracked", false);
            }
            stack.setComponent("minecraft:lodestone_tracker", compound(std::move(component)));
        }
    }
    if (stack.is("minecraft:firework_rocket")) {
        stack.fixSubTag("Fireworks", true, [&](NbtCompound fireworks) {
            std::vector<NbtTag> explosions;
            if (const NbtTag* held = fireworks.find("Explosions"); held != nullptr && held->get<NbtList>() != nullptr) {
                for (const NbtTag& entry : *held->get<NbtList>()) {
                    if (const NbtCompound* source = entry.get<NbtCompound>(); source != nullptr) {
                        explosions.push_back(fixFireworkExplosion(*source));
                    }
                }
            }
            NbtCompound component;
            component.set("explosions", listOf(std::move(explosions)));
            component.putByte("flight_duration", static_cast<std::int8_t>(intOr(fireworks.find("Flight"), 0)));
            stack.setComponent("minecraft:fireworks", compound(std::move(component)));
            fireworks.remove("Explosions");
            fireworks.remove("Flight");
            return fireworks;
        });
    }
    if (stack.is("minecraft:firework_star")) {
        stack.fixSubTag("Explosion", true, [&](NbtCompound explosion) {
            stack.setComponent("minecraft:firework_explosion", fixFireworkExplosion(explosion));
            for (const std::string_view key : {"Type", "Colors", "FadeColors", "Trail", "Flicker"}) {
                explosion.remove(key);
            }
            return explosion;
        });
    }
    if (stack.is("minecraft:player_head")) {
        if (const auto owner = stack.removeTag("SkullOwner"); owner.has_value()) {
            stack.setComponent("minecraft:profile", fixProfile(*owner));
        }
    }
}

} // namespace

NbtTag fixProfile(const NbtTag& owner) {
    if (const auto* simple = owner.get<std::string>(); simple != nullptr) {
        NbtCompound profile;
        if (validPlayerName(*simple)) {
            profile.putString("name", *simple);
        }
        return compound(std::move(profile));
    }
    const NbtCompound* source = owner.get<NbtCompound>();
    NbtCompound profile;
    if (source == nullptr) {
        return compound(std::move(profile));
    }
    const std::string name{stringOr(source->find("Name"), "")};
    if (validPlayerName(name)) {
        profile.putString("name", name);
    }
    if (const NbtTag* id = source->find("Id"); id != nullptr) {
        profile.set("id", *id);
    }
    if (auto properties = fixProfileProperties(source->find("Properties")); properties.has_value()) {
        profile.set("properties", std::move(*properties));
    }
    return compound(std::move(profile));
}

void componentize(NbtCompound& stack) {
    auto data = Stack::read(stack);
    if (!data.has_value()) {
        return;
    }
    fixItemStack(*data);
    stack = data->write();
}

} // namespace basalt::metamorph::datafix
