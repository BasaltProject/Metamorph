#include "FixSupport.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <functional>
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

[[nodiscard]] NbtCompound* compoundAt(NbtCompound& parent, std::string_view key) {
    NbtTag* held = parent.find(key);
    return held == nullptr ? nullptr : held->get<NbtCompound>();
}

/// parity: `BitStorageAlignFix.addPadding`, values packed across longs moved to packing that never splits one.
[[nodiscard]] std::vector<std::int64_t> addPadding(int size, int bits, const std::vector<std::int64_t>& data) {
    const std::size_t dataLength = data.size();
    if (dataLength == 0) {
        return data;
    }
    const std::uint64_t mask = (std::uint64_t{1} << bits) - 1;
    const int valuesPerLong = 64 / bits;
    const int requiredLength = (size + valuesPerLong - 1) / valuesPerLong;
    std::vector<std::int64_t> result(static_cast<std::size_t>(requiredLength), 0);
    int outputIndex = 0;
    int outputStart = 0;
    std::uint64_t outputData = 0;
    std::size_t currentIndex = 0;
    std::uint64_t current = static_cast<std::uint64_t>(data[0]);
    std::uint64_t next = dataLength > 1 ? static_cast<std::uint64_t>(data[1]) : 0;
    for (int index = 0; index < size; ++index) {
        const int position = index * bits;
        const std::size_t startData = static_cast<std::size_t>(position >> 6);
        const std::size_t endData = static_cast<std::size_t>(((index + 1) * bits - 1) >> 6);
        const int startBit = position ^ static_cast<int>(startData << 6);
        if (startData != currentIndex) {
            current = next;
            next = startData + 1 < dataLength ? static_cast<std::uint64_t>(data[startData + 1]) : 0;
            currentIndex = startData;
        }
        std::uint64_t value = 0;
        if (startData == endData) {
            value = (current >> startBit) & mask;
        } else {
            const int shift = 64 - startBit;
            value = ((current >> startBit) | (next << shift)) & mask;
        }
        const int outputEnd = outputStart + bits;
        if (outputEnd >= 64) {
            result[static_cast<std::size_t>(outputIndex++)] = static_cast<std::int64_t>(outputData);
            outputData = value;
            outputStart = bits;
        } else {
            outputData |= value << outputStart;
            outputStart = outputEnd;
        }
    }
    if (outputData != 0 && static_cast<std::size_t>(outputIndex) < result.size()) {
        result[static_cast<std::size_t>(outputIndex)] = static_cast<std::int64_t>(outputData);
    }
    return result;
}

[[nodiscard]] int ceilLog2(int value) {
    return value == 0 ? 0 : static_cast<int>(std::ceil(std::log(value) / std::log(2.0)));
}

// parity: `BitStorageAlignFix`.
void alignBitStorage(NbtCompound& chunk, const Context&) {
    NbtCompound* level = compoundAt(chunk, "Level");
    if (level == nullptr) {
        return;
    }
    forEachCompound(*level, "Sections", [](NbtCompound& section) {
        const NbtTag* palette = section.find("Palette");
        const NbtList* list = palette == nullptr ? nullptr : palette->get<NbtList>();
        const int bits = list == nullptr ? 0 : std::max(4, ceilLog2(static_cast<int>(list->size())));
        if (bits == 0 || std::has_single_bit(static_cast<unsigned>(bits))) {
            return;
        }
        NbtTag* states = section.find("BlockStates");
        if (auto* longs = states == nullptr ? nullptr : states->get<std::vector<std::int64_t>>(); longs != nullptr) {
            *longs = addPadding(4096, bits, *longs);
        }
    });
    if (NbtCompound* maps = compoundAt(*level, "Heightmaps"); maps != nullptr) {
        for (std::size_t i = 0; i < maps->names().size(); ++i) {
            NbtTag* held = maps->find(maps->names()[i]);
            if (auto* longs = held == nullptr ? nullptr : held->get<std::vector<std::int64_t>>(); longs != nullptr) {
                *longs = addPadding(256, 9, *longs);
            }
        }
    }
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

[[nodiscard]] double decimal(const NbtTag* tag, double fallback) {
    if (tag == nullptr) {
        return fallback;
    }
    if (const auto* v = tag->get<double>(); v != nullptr) {
        return *v;
    }
    if (const auto* v = tag->get<float>(); v != nullptr) {
        return static_cast<double>(*v);
    }
    const auto number = whole(tag);
    return number.has_value() ? static_cast<double>(*number) : fallback;
}

void forEachPaletteEntry(NbtCompound& chunk, const std::function<void(NbtCompound&)>& edit) {
    NbtCompound* level = compoundAt(chunk, "Level");
    if (level == nullptr) {
        return;
    }
    forEachCompound(*level, "Sections", [&](NbtCompound& section) { forEachCompound(section, "Palette", edit); });
}

NbtTag uuidArray(std::int64_t most, std::int64_t least) {
    return NbtTag::ofIntArray({static_cast<std::int32_t>(most >> 32), static_cast<std::int32_t>(most),
                               static_cast<std::int32_t>(least >> 32), static_cast<std::int32_t>(least)});
}

/// parity: `AbstractUUIDFix.replaceUUIDLeastMost`, `<key>Most` and `<key>Least` as one int array.
bool replaceUuidLeastMost(NbtCompound& tag, std::string_view oldKey, std::string_view newKey) {
    const std::string most = std::string{oldKey} + "Most";
    const std::string least = std::string{oldKey} + "Least";
    const std::int64_t high = whole(tag.find(most)).value_or(0);
    const std::int64_t low = whole(tag.find(least)).value_or(0);
    if (high == 0 || low == 0) {
        return false;
    }
    tag.remove(most);
    tag.remove(least);
    tag.set(newKey, uuidArray(high, low));
    return true;
}

/// parity: `AbstractUUIDFix.replaceUUIDString`, a textual uuid as one int array.
bool replaceUuidString(NbtCompound& tag, std::string_view oldKey, std::string_view newKey) {
    const NbtTag* held = tag.find(oldKey);
    const std::string* text = held == nullptr ? nullptr : held->get<std::string>();
    if (text == nullptr) {
        return false;
    }
    std::string digits;
    for (const char c : *text) {
        if (c != '-') {
            digits += c;
        }
    }
    if (digits.size() != 32 || text->size() != 36 ||
        !std::ranges::all_of(digits, [](unsigned char c) { return std::isxdigit(c) != 0; })) {
        return false;
    }
    const std::uint64_t high = std::stoull(digits.substr(0, 16), nullptr, 16);
    const std::uint64_t low = std::stoull(digits.substr(16), nullptr, 16);
    tag.remove(oldKey);
    tag.set(newKey, uuidArray(static_cast<std::int64_t>(high), static_cast<std::int64_t>(low)));
    return true;
}

/// parity: `AbstractUUIDFix.replaceUUIDMLTag`, a `{M, L}` compound as one int array.
bool replaceUuidMl(NbtCompound& tag, std::string_view oldKey, std::string_view newKey) {
    const NbtTag* held = tag.find(oldKey);
    const NbtCompound* source = held == nullptr ? nullptr : held->get<NbtCompound>();
    if (source == nullptr) {
        return false;
    }
    const std::int64_t high = whole(source->find("M")).value_or(0);
    const std::int64_t low = whole(source->find("L")).value_or(0);
    if (high == 0 || low == 0) {
        return false;
    }
    tag.remove(oldKey);
    tag.set(newKey, uuidArray(high, low));
    return true;
}

void livingUuids(NbtCompound& tag) {
    forEachCompound(tag, "Attributes", [](NbtCompound& attribute) {
        forEachCompound(attribute, "Modifiers",
                        [](NbtCompound& modifier) { replaceUuidLeastMost(modifier, "UUID", "UUID"); });
    });
}

void mobUuids(NbtCompound& tag) {
    livingUuids(tag);
    if (NbtCompound* leash = compoundAt(tag, "Leash"); leash != nullptr) {
        replaceUuidLeastMost(*leash, "UUID", "UUID");
    }
}

void animalUuids(NbtCompound& tag) {
    mobUuids(tag);
    replaceUuidLeastMost(tag, "LoveCause", "LoveCause");
}

// parity: `EntityUUIDFix`.
void entityUuids(NbtCompound& entity, const Context&) {
    static const std::set<std::string_view> kHorses{
        "minecraft:donkey",         "minecraft:horse",        "minecraft:llama",       "minecraft:mule",
        "minecraft:skeleton_horse", "minecraft:trader_llama", "minecraft:zombie_horse"};
    static const std::set<std::string_view> kTameable{"minecraft:cat", "minecraft:parrot", "minecraft:wolf"};
    static const std::set<std::string_view> kAnimals{
        "minecraft:bee",    "minecraft:chicken", "minecraft:cow",   "minecraft:fox",        "minecraft:mooshroom",
        "minecraft:ocelot", "minecraft:panda",   "minecraft:pig",   "minecraft:polar_bear", "minecraft:rabbit",
        "minecraft:sheep",  "minecraft:turtle",  "minecraft:hoglin"};
    static const std::set<std::string_view> kMobs{"minecraft:bat",
                                                  "minecraft:blaze",
                                                  "minecraft:cave_spider",
                                                  "minecraft:cod",
                                                  "minecraft:creeper",
                                                  "minecraft:dolphin",
                                                  "minecraft:drowned",
                                                  "minecraft:elder_guardian",
                                                  "minecraft:ender_dragon",
                                                  "minecraft:enderman",
                                                  "minecraft:endermite",
                                                  "minecraft:evoker",
                                                  "minecraft:ghast",
                                                  "minecraft:giant",
                                                  "minecraft:guardian",
                                                  "minecraft:husk",
                                                  "minecraft:illusioner",
                                                  "minecraft:magma_cube",
                                                  "minecraft:pufferfish",
                                                  "minecraft:zombified_piglin",
                                                  "minecraft:salmon",
                                                  "minecraft:shulker",
                                                  "minecraft:silverfish",
                                                  "minecraft:skeleton",
                                                  "minecraft:slime",
                                                  "minecraft:snow_golem",
                                                  "minecraft:spider",
                                                  "minecraft:squid",
                                                  "minecraft:stray",
                                                  "minecraft:tropical_fish",
                                                  "minecraft:vex",
                                                  "minecraft:villager",
                                                  "minecraft:iron_golem",
                                                  "minecraft:vindicator",
                                                  "minecraft:pillager",
                                                  "minecraft:wandering_trader",
                                                  "minecraft:witch",
                                                  "minecraft:wither",
                                                  "minecraft:wither_skeleton",
                                                  "minecraft:zombie",
                                                  "minecraft:zombie_villager",
                                                  "minecraft:phantom",
                                                  "minecraft:ravager",
                                                  "minecraft:piglin"};
    static const std::set<std::string_view> kProjectiles{
        "minecraft:arrow",      "minecraft:dragon_fireball", "minecraft:firework_rocket",   "minecraft:fireball",
        "minecraft:llama_spit", "minecraft:small_fireball",  "minecraft:snowball",          "minecraft:spectral_arrow",
        "minecraft:egg",        "minecraft:ender_pearl",     "minecraft:experience_bottle", "minecraft:potion",
        "minecraft:trident",    "minecraft:wither_skull"};
    const std::string id = namespaced(entity.getString("id"));
    replaceUuidLeastMost(entity, "UUID", "UUID");
    if (kHorses.contains(id) || kTameable.contains(id)) {
        animalUuids(entity);
        replaceUuidString(entity, "OwnerUUID", "Owner");
    }
    if (kAnimals.contains(id)) {
        animalUuids(entity);
    }
    if (kMobs.contains(id)) {
        mobUuids(entity);
    }
    if (id == "minecraft:armor_stand") {
        livingUuids(entity);
    }
    if (kProjectiles.contains(id)) {
        rename(entity, "OwnerUUID", "Owner");
    }
    if (id == "minecraft:bee" || id == "minecraft:zombified_piglin") {
        replaceUuidString(entity, "HurtBy", "HurtBy");
    }
    if (id == "minecraft:fox") {
        if (NbtList* trusted = listAt(entity, "TrustedUUIDs"); trusted != nullptr) {
            std::vector<NbtTag> fixed;
            for (std::size_t i = 0; i < trusted->size(); ++i) {
                NbtCompound holder;
                holder.set("v", (*trusted)[i]);
                if (!replaceUuidMl(holder, "v", "v")) {
                    fixed.push_back((*trusted)[i]);
                } else {
                    fixed.push_back(*holder.find("v"));
                }
            }
            entity.remove("TrustedUUIDs");
            entity.set("Trusted",
                       NbtTag::ofList(NbtList{fixed.empty() ? NbtType::End : fixed.front().type(), std::move(fixed)}));
        }
    }
    if (id == "minecraft:item") {
        replaceUuidMl(entity, "Owner", "Owner");
        replaceUuidMl(entity, "Thrower", "Thrower");
    }
    if (id == "minecraft:shulker_bullet") {
        replaceUuidMl(entity, "Owner", "Owner");
        replaceUuidMl(entity, "Target", "Target");
    }
    if (id == "minecraft:area_effect_cloud") {
        replaceUuidLeastMost(entity, "OwnerUUID", "Owner");
    }
    if (id == "minecraft:zombie_villager") {
        replaceUuidLeastMost(entity, "ConversionPlayer", "ConversionPlayer");
    }
    if (id == "minecraft:evoker_fangs") {
        replaceUuidLeastMost(entity, "OwnerUUID", "Owner");
    }
    if (id == "minecraft:piglin") {
        if (NbtCompound* brain = compoundAt(entity, "Brain"); brain != nullptr) {
            if (NbtCompound* memories = compoundAt(*brain, "memories"); memories != nullptr) {
                if (NbtCompound* angryAt = compoundAt(*memories, "minecraft:angry_at"); angryAt != nullptr) {
                    replaceUuidString(*angryAt, "value", "value");
                }
            }
        }
    }
}

// parity: `BlockEntityUUIDFix`.
void blockEntityUuids(NbtCompound& blockEntity, const Context&) {
    const std::string id = namespaced(blockEntity.getString("id"));
    if (id == "minecraft:conduit") {
        replaceUuidMl(blockEntity, "target_uuid", "Target");
    } else if (id == "minecraft:skull") {
        if (NbtCompound* owner = compoundAt(blockEntity, "Owner"); owner != nullptr) {
            NbtCompound moved = *owner;
            replaceUuidString(moved, "Id", "Id");
            blockEntity.remove("Owner");
            blockEntity.set("SkullOwner", NbtTag::ofCompound(std::move(moved)));
        }
    }
}

// parity: `PlayerUUIDFix`.
void playerUuids(NbtCompound& player, const Context&) {
    if (NbtCompound* vehicle = compoundAt(player, "RootVehicle"); vehicle != nullptr) {
        replaceUuidLeastMost(*vehicle, "Attach", "Attach");
    }
    livingUuids(player);
    replaceUuidLeastMost(player, "UUID", "UUID");
}

// parity: `ItemStackUUIDFix`.
void itemStackUuids(NbtCompound& stack, const Context&) {
    NbtCompound* tag = compoundAt(stack, "tag");
    if (tag == nullptr) {
        return;
    }
    forEachCompound(*tag, "AttributeModifiers",
                    [](NbtCompound& modifier) { replaceUuidLeastMost(modifier, "UUID", "UUID"); });
    if (namespaced(stack.getString("id")) == "minecraft:player_head") {
        if (NbtCompound* owner = compoundAt(*tag, "SkullOwner"); owner != nullptr) {
            replaceUuidString(*owner, "Id", "Id");
        }
    }
}

// parity: `GossipUUIDFix`.
void gossipUuids(NbtCompound& entity, const Context&) {
    const std::string id = namespaced(entity.getString("id"));
    if (id == "minecraft:villager" || id == "minecraft:zombie_villager") {
        forEachCompound(entity, "Gossips",
                        [](NbtCompound& gossip) { replaceUuidLeastMost(gossip, "Target", "Target"); });
    }
}

// parity: `EntityProjectileOwnerFix`.
void projectileOwner(NbtCompound& entity, const Context&) {
    static const std::set<std::string_view> kThrowables{"minecraft:egg", "minecraft:ender_pearl",
                                                        "minecraft:experience_bottle", "minecraft:snowball",
                                                        "minecraft:potion"};
    static const std::set<std::string_view> kArrows{"minecraft:arrow", "minecraft:spectral_arrow", "minecraft:trident"};
    const std::string id = namespaced(entity.getString("id"));
    std::int64_t high = 0;
    std::int64_t low = 0;
    if (kThrowables.contains(id)) {
        const NbtCompound* owner = compoundAt(entity, "owner");
        high = owner == nullptr ? 0 : whole(owner->find("M")).value_or(0);
        low = owner == nullptr ? 0 : whole(owner->find("L")).value_or(0);
        entity.remove("owner");
    } else if (id == "minecraft:llama_spit") {
        const NbtCompound* owner = compoundAt(entity, "Owner");
        high = owner == nullptr ? 0 : whole(owner->find("OwnerUUIDMost")).value_or(0);
        low = owner == nullptr ? 0 : whole(owner->find("OwnerUUIDLeast")).value_or(0);
        entity.remove("Owner");
    } else if (kArrows.contains(id)) {
        high = whole(entity.find("OwnerUUIDMost")).value_or(0);
        low = whole(entity.find("OwnerUUIDLeast")).value_or(0);
        entity.remove("OwnerUUIDMost");
        entity.remove("OwnerUUIDLeast");
    } else {
        return;
    }
    if (high != 0 && low != 0) {
        entity.set("OwnerUUID", uuidArray(high, low));
    }
}

// parity: `JigsawPropertiesFix`.
void jigsawProperties(NbtCompound& blockEntity, const Context&) {
    if (namespaced(blockEntity.getString("id")) != "minecraft:jigsaw") {
        return;
    }
    const std::string name{blockEntity.contains("attachement_type") ? blockEntity.getString("attachement_type")
                                                                    : "minecraft:empty"};
    const std::string pool{blockEntity.contains("target_pool") ? blockEntity.getString("target_pool")
                                                               : "minecraft:empty"};
    blockEntity.putString("name", name);
    blockEntity.putString("target", name);
    blockEntity.remove("attachement_type");
    blockEntity.putString("pool", pool);
    blockEntity.remove("target_pool");
}

// parity: `LegacyJigsawRotationFix`.
void jigsawRotation(NbtCompound& chunk, const Context&) {
    static const std::map<std::string_view, std::string_view> kOrientations{
        {"down", "down_south"}, {"up", "up_north"},  {"north", "north_up"},
        {"south", "south_up"},  {"west", "west_up"}, {"east", "east_up"}};
    forEachPaletteEntry(chunk, [](NbtCompound& entry) {
        if (namespaced(entry.getString("Name")) != "minecraft:jigsaw") {
            return;
        }
        NbtCompound* properties = compoundAt(entry, "Properties");
        if (properties == nullptr) {
            return;
        }
        const std::string facing{properties->contains("facing") ? properties->getString("facing") : "north"};
        const auto found = kOrientations.find(facing);
        properties->remove("facing");
        properties->putString("orientation",
                              std::string{found == kOrientations.end() ? facing : std::string{found->second}});
    });
}

[[nodiscard]] std::string legacyAttributeName(const std::string& name) {
    static const std::map<std::string_view, std::string_view> kNames{
        {"generic.maxHealth", "minecraft:generic.max_health"},
        {"Max Health", "minecraft:generic.max_health"},
        {"zombie.spawnReinforcements", "minecraft:zombie.spawn_reinforcements"},
        {"Spawn Reinforcements Chance", "minecraft:zombie.spawn_reinforcements"},
        {"horse.jumpStrength", "minecraft:horse.jump_strength"},
        {"Jump Strength", "minecraft:horse.jump_strength"},
        {"generic.followRange", "minecraft:generic.follow_range"},
        {"Follow Range", "minecraft:generic.follow_range"},
        {"generic.knockbackResistance", "minecraft:generic.knockback_resistance"},
        {"Knockback Resistance", "minecraft:generic.knockback_resistance"},
        {"generic.movementSpeed", "minecraft:generic.movement_speed"},
        {"Movement Speed", "minecraft:generic.movement_speed"},
        {"generic.flyingSpeed", "minecraft:generic.flying_speed"},
        {"Flying Speed", "minecraft:generic.flying_speed"},
        {"generic.attackDamage", "minecraft:generic.attack_damage"},
        {"generic.attackKnockback", "minecraft:generic.attack_knockback"},
        {"generic.attackSpeed", "minecraft:generic.attack_speed"},
        {"generic.armorToughness", "minecraft:generic.armor_toughness"}};
    const auto found = kNames.find(name);
    return found == kNames.end() ? name : std::string{found->second};
}

// parity: `AttributesRenameLegacy`, the 1.16 attribute names, over entities and the modifiers of an item.
void renameLegacyAttributesOfEntity(NbtCompound& entity, const Context&) {
    forEachCompound(entity, "Attributes", [](NbtCompound& attribute) {
        if (const NbtTag* name = attribute.find("Name"); name != nullptr && name->get<std::string>() != nullptr) {
            attribute.putString("Name", legacyAttributeName(*name->get<std::string>()));
        }
    });
}

void renameLegacyAttributesOfItem(NbtCompound& stack, const Context&) {
    NbtCompound* tag = compoundAt(stack, "tag");
    if (tag == nullptr) {
        return;
    }
    forEachCompound(*tag, "AttributeModifiers", [](NbtCompound& modifier) {
        if (const NbtTag* name = modifier.find("AttributeName");
            name != nullptr && name->get<std::string>() != nullptr) {
            modifier.putString("AttributeName", legacyAttributeName(*name->get<std::string>()));
        }
    });
}

// parity: `WallPropertyFix`.
void wallProperties(NbtCompound& chunk, const Context&) {
    static const std::set<std::string_view> kWalls{"minecraft:andesite_wall",
                                                   "minecraft:brick_wall",
                                                   "minecraft:cobblestone_wall",
                                                   "minecraft:diorite_wall",
                                                   "minecraft:end_stone_brick_wall",
                                                   "minecraft:granite_wall",
                                                   "minecraft:mossy_cobblestone_wall",
                                                   "minecraft:mossy_stone_brick_wall",
                                                   "minecraft:nether_brick_wall",
                                                   "minecraft:prismarine_wall",
                                                   "minecraft:red_nether_brick_wall",
                                                   "minecraft:red_sandstone_wall",
                                                   "minecraft:sandstone_wall",
                                                   "minecraft:stone_brick_wall",
                                                   "minecraft:blackstone_wall",
                                                   "minecraft:polished_blackstone_wall",
                                                   "minecraft:polished_blackstone_brick_wall"};
    forEachPaletteEntry(chunk, [](NbtCompound& entry) {
        if (!kWalls.contains(entry.getString("Name"))) {
            return;
        }
        NbtCompound* properties = compoundAt(entry, "Properties");
        if (properties == nullptr) {
            return;
        }
        for (const std::string_view side : {"east", "west", "north", "south"}) {
            if (const NbtTag* value = properties->find(side);
                value != nullptr && value->get<std::string>() != nullptr) {
                properties->putString(side, *value->get<std::string>() == "true" ? "low" : "none");
            }
        }
    });
}

// parity: `MemoryExpiryDataFix`, every memory wrapped as `{value: ...}`.
void memoryExpiry(NbtCompound& entity, const Context&) {
    if (namespaced(entity.getString("id")) != "minecraft:villager") {
        return;
    }
    NbtCompound* brain = compoundAt(entity, "Brain");
    NbtCompound* memories = brain == nullptr ? nullptr : compoundAt(*brain, "memories");
    if (memories == nullptr) {
        return;
    }
    NbtCompound wrapped;
    for (std::size_t i = 0; i < memories->names().size(); ++i) {
        NbtCompound holder;
        holder.set("value", memories->values()[i]);
        wrapped.set(memories->names()[i], NbtTag::ofCompound(std::move(holder)));
    }
    brain->set("memories", NbtTag::ofCompound(std::move(wrapped)));
}

// parity: `RedstoneWireConnectionsFix`.
void redstoneConnections(NbtCompound& chunk, const Context&) {
    forEachPaletteEntry(chunk, [](NbtCompound& entry) {
        if (namespaced(entry.getString("Name")) != "minecraft:redstone_wire") {
            return;
        }
        NbtCompound* properties = compoundAt(entry, "Properties");
        if (properties == nullptr) {
            return;
        }
        const auto side = [&](std::string_view key) {
            return std::string{properties->contains(key) ? properties->getString(key) : "none"};
        };
        const std::string east = side("east");
        const std::string west = side("west");
        const std::string north = side("north");
        const std::string south = side("south");
        const auto connected = [](const std::string& value) { return value != "none"; };
        const bool eastWest = connected(east) || connected(west);
        const bool northSouth = connected(north) || connected(south);
        properties->putString("east", !connected(east) && !northSouth ? "side" : east);
        properties->putString("west", !connected(west) && !northSouth ? "side" : west);
        properties->putString("north", !connected(north) && !eastWest ? "side" : north);
        properties->putString("south", !connected(south) && !eastWest ? "side" : south);
    });
}

// parity: `StriderGravityFix`.
void striderGravity(NbtCompound& entity, const Context&) {
    if (namespaced(entity.getString("id")) == "minecraft:strider" && entity.getBoolean("NoGravity")) {
        entity.putBoolean("NoGravity", false);
    }
}

// parity: `VillagerFollowRangeFix`, which compares the name before the 1.16 rename could have made it namespaced.
void villagerFollowRange(NbtCompound& entity, const Context&) {
    if (namespaced(entity.getString("id")) != "minecraft:villager") {
        return;
    }
    forEachCompound(entity, "Attributes", [](NbtCompound& attribute) {
        if (attribute.getString("Name") == "generic.follow_range" && decimal(attribute.find("Base"), 0.0) == 16.0) {
            attribute.putDouble("Base", 48.0);
        }
    });
}

// parity: `EntityShulkerRotationFix`.
void shulkerRotation(NbtCompound& entity, const Context&) {
    if (namespaced(entity.getString("id")) != "minecraft:shulker") {
        return;
    }
    NbtList* rotation = listAt(entity, "Rotation");
    if (rotation == nullptr || rotation->empty()) {
        return;
    }
    std::vector<NbtTag> fixed;
    for (std::size_t i = 0; i < rotation->size(); ++i) {
        const double value = decimal(&(*rotation)[i], 180.0);
        fixed.push_back(NbtTag::ofDouble(i == 0 ? value - 180.0 : value));
    }
    entity.set("Rotation", NbtTag::ofList(NbtList{NbtType::Double, std::move(fixed)}));
}

[[nodiscard]] std::optional<std::string> fungusName(const std::string& name) {
    if (name == "minecraft:warped_fungi") {
        return "minecraft:warped_fungus";
    }
    if (name == "minecraft:crimson_fungi") {
        return "minecraft:crimson_fungus";
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> soulFireName(const std::string& name) {
    static const std::map<std::string_view, std::string_view> kNames{
        {"minecraft:soul_fire_torch", "minecraft:soul_torch"},
        {"minecraft:soul_fire_wall_torch", "minecraft:soul_wall_torch"},
        {"minecraft:soul_fire_lantern", "minecraft:soul_lantern"}};
    const auto found = kNames.find(name);
    return found == kNames.end() ? std::nullopt : std::optional{std::string{found->second}};
}

void renameOldBlocks(NbtCompound& chunk, std::optional<std::string> (*renamer)(const std::string&)) {
    forEachPaletteEntry(chunk, [renamer](NbtCompound& entry) {
        if (const auto renamed = renamer(namespaced(entry.getString("Name"))); renamed.has_value()) {
            entry.putString("Name", *renamed);
        }
    });
}

void renameLegacyItems(NbtCompound& stack, std::optional<std::string> (*renamer)(const std::string&)) {
    if (const auto renamed = renamer(namespaced(stack.getString("id"))); renamed.has_value()) {
        stack.putString("id", *renamed);
    }
}

[[nodiscard]] std::optional<std::string> zombiePigmanEgg(const std::string& name) {
    return name == "minecraft:zombie_pigman_spawn_egg"
               ? std::optional<std::string>{"minecraft:zombified_piglin_spawn_egg"}
               : std::nullopt;
}

// parity: the namespaced-string type the structure piece ids have since 1.14, which lowercases legacy ids.
void structurePieceIds(NbtCompound& chunk, const Context&) {
    NbtCompound* level = compoundAt(chunk, "Level");
    NbtCompound* structures = level == nullptr ? nullptr : compoundAt(*level, "Structures");
    NbtCompound* starts = structures == nullptr ? nullptr : compoundAt(*structures, "Starts");
    if (starts == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < starts->names().size(); ++i) {
        NbtCompound* start = starts->find(starts->names()[i])->get<NbtCompound>();
        if (start == nullptr) {
            continue;
        }
        forEachCompound(*start, "Children", [](NbtCompound& piece) {
            const NbtTag* id = piece.find("id");
            if (id == nullptr || id->get<std::string>() == nullptr) {
                return;
            }
            std::string lowered = *id->get<std::string>();
            std::ranges::transform(lowered, lowered.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            piece.putString("id", namespaced(lowered));
        });
    }
}

// parity: `EntityZombifiedPiglinRenameFix`.
void zombifiedPiglin(NbtCompound& entity, const Context&) {
    if (namespaced(entity.getString("id")) == "minecraft:zombie_pigman") {
        entity.putString("id", "minecraft:zombified_piglin");
    }
}

} // namespace

void addFixesFrom1400(std::vector<Fix>& fixes) {
    fixes.insert(
        fixes.end(),
        {
            {1800, Target::Chunk, {}, structurePieceIds},
            {2503, Target::Chunk, {}, wallProperties},
            {2505, Target::Entity, {}, memoryExpiry},
            {2508, Target::Chunk, {}, [](NbtCompound& c, const Context&) { renameOldBlocks(c, fungusName); }},
            {2508, Target::LegacyStack, {}, [](NbtCompound& s, const Context&) { renameLegacyItems(s, fungusName); }},
            {2509, Target::Entity, {}, zombifiedPiglin},
            {2509,
             Target::LegacyStack,
             {},
             [](NbtCompound& s, const Context&) { renameLegacyItems(s, zombiePigmanEgg); }},
            {2511, Target::Entity, {}, projectileOwner},
            {2514, Target::Entity, {}, entityUuids},
            {2514, Target::BlockEntity, {}, blockEntityUuids},
            {2514, Target::Entity, {}, playerUuids},
            {2514, Target::LegacyStack, {}, itemStackUuids},
            {2516, Target::Entity, {}, gossipUuids},
            {2518, Target::BlockEntity, {}, jigsawProperties},
            {2518, Target::Chunk, {}, jigsawRotation},
            {2523, Target::Entity, {}, renameLegacyAttributesOfEntity},
            {2523, Target::LegacyStack, {}, renameLegacyAttributesOfItem},
            {2527, Target::Chunk, {}, alignBitStorage},
            {2528, Target::Chunk, {}, [](NbtCompound& c, const Context&) { renameOldBlocks(c, soulFireName); }},
            {2528, Target::LegacyStack, {}, [](NbtCompound& s, const Context&) { renameLegacyItems(s, soulFireName); }},
            {2529, Target::Entity, {}, striderGravity},
            {2531, Target::Chunk, {}, redstoneConnections},
            {2533, Target::Entity, {}, villagerFollowRange},
            {2535, Target::Entity, {}, shulkerRotation},
        });
}

} // namespace basalt::metamorph::datafix
