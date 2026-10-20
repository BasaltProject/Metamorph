#include "FixSupport.hpp"
#include "LegacyIdTables.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>

namespace basalt::metamorph::datafix {
namespace {

using wire::nbt::NbtCompound;
using wire::nbt::NbtList;
using wire::nbt::NbtTag;
using wire::nbt::NbtType;

constexpr int kMaxDepth = 48;
constexpr int kSplashPotionBit = 16384;

[[nodiscard]] std::optional<std::int64_t> number(const NbtTag* tag) {
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
    if (const auto* v = tag->get<float>(); v != nullptr) {
        return static_cast<std::int64_t>(*v);
    }
    if (const auto* v = tag->get<double>(); v != nullptr) {
        return static_cast<std::int64_t>(*v);
    }
    return std::nullopt;
}

[[nodiscard]] std::int64_t whole(const NbtCompound& compound, std::string_view key, std::int64_t fallback = 0) {
    return number(compound.find(key)).value_or(fallback);
}

[[nodiscard]] NbtCompound* compoundIn(NbtCompound& parent, std::string_view key) {
    NbtTag* held = parent.find(key);
    return held == nullptr ? nullptr : held->get<NbtCompound>();
}

[[nodiscard]] NbtCompound& compoundOrCreate(NbtCompound& parent, std::string_view key) {
    if (NbtCompound* held = compoundIn(parent, key); held != nullptr) {
        return *held;
    }
    parent.set(key, NbtTag::ofCompound(NbtCompound{}));
    return *compoundIn(parent, key);
}

[[nodiscard]] std::string_view lookup(const auto& table, int key) {
    for (const auto& [id, name] : table) {
        if (id == key) {
            return name;
        }
    }
    return {};
}

[[nodiscard]] std::string_view lookup(const auto& table, std::string_view key) {
    for (const auto& [id, name] : table) {
        if (id == key) {
            return name;
        }
    }
    return {};
}

[[nodiscard]] NbtTag floatList(std::initializer_list<float> values) {
    std::vector<NbtTag> tags;
    for (const float value : values) {
        tags.push_back(NbtTag::ofFloat(value));
    }
    return NbtTag::ofList(NbtList{NbtType::Float, std::move(tags)});
}

[[nodiscard]] NbtTag compoundList(std::vector<NbtCompound> compounds) {
    std::vector<NbtTag> tags;
    tags.reserve(compounds.size());
    for (NbtCompound& compound : compounds) {
        tags.push_back(NbtTag::ofCompound(std::move(compound)));
    }
    return NbtTag::ofList(NbtList{NbtType::Compound, std::move(tags)});
}

void walkAll(NbtTag& tag, int depth, const std::function<void(NbtCompound&)>& edit);

void walkCompoundAll(NbtCompound& compound, int depth, const std::function<void(NbtCompound&)>& edit) {
    if (depth > kMaxDepth) {
        return;
    }
    edit(compound);
    const std::vector<std::string> names = compound.names();
    for (const std::string& name : names) {
        if (NbtTag* child = compound.find(name); child != nullptr) {
            walkAll(*child, depth + 1, edit);
        }
    }
}

void walkAll(NbtTag& tag, int depth, const std::function<void(NbtCompound&)>& edit) {
    if (NbtCompound* compound = tag.get<NbtCompound>(); compound != nullptr) {
        walkCompoundAll(*compound, depth, edit);
    } else if (NbtList* list = tag.get<NbtList>(); list != nullptr) {
        for (std::size_t i = 0; i < list->size(); ++i) {
            walkAll((*list)[i], depth + 1, edit);
        }
    }
}

[[nodiscard]] bool holdsNumericStack(const NbtCompound& compound) {
    const NbtTag* id = compound.find("id");
    const bool numericId = id != nullptr && (id->get<std::int8_t>() != nullptr || id->get<std::int16_t>() != nullptr ||
                                             id->get<std::int32_t>() != nullptr);
    return numericId && number(compound.find("Count")).has_value();
}

// parity: `ItemIdFix`.
void itemIds(NbtCompound& root, const Context&) {
    walkCompoundAll(root, 0, [](NbtCompound& compound) {
        if (holdsNumericStack(compound)) {
            const std::string_view name = lookup(legacyIds::kItemNames, static_cast<int>(whole(compound, "id")));
            compound.putString("id", name.empty() ? "minecraft:air" : std::string{name});
        }
    });
}

void forEachNamedStack(NbtCompound& root, const std::function<void(NbtCompound&)>& edit) {
    walkCompoundAll(root, 0, [&edit](NbtCompound& compound) {
        const NbtTag* id = compound.find("id");
        if (id != nullptr && id->get<std::string>() != nullptr && number(compound.find("Count")).has_value()) {
            edit(compound);
        }
    });
}

// parity: `ItemPotionFix`.
void itemPotions(NbtCompound& stack, const Context&) {
    if (stack.getString("id") != "minecraft:potion") {
        return;
    }
    const auto damage = static_cast<std::int16_t>(whole(stack, "Damage"));
    NbtCompound* tag = compoundIn(stack, "tag");
    if (tag == nullptr) {
        return;
    }
    if (!tag->contains("Potion")) {
        const std::string_view potion = legacyIds::kPotions[static_cast<std::size_t>(damage & 127)];
        tag->putString("Potion", potion.empty() ? "minecraft:water" : std::string{potion});
        if ((damage & kSplashPotionBit) == kSplashPotionBit) {
            stack.putString("id", "minecraft:splash_potion");
        }
    }
    if (damage != 0) {
        stack.putShort("Damage", 0);
    }
}

// parity: `ItemSpawnEggFix`.
void itemSpawnEggs(NbtCompound& stack, const Context&) {
    if (stack.getString("id") != "minecraft:spawn_egg") {
        return;
    }
    const auto damage = static_cast<std::int16_t>(whole(stack, "Damage"));
    const std::string_view entity = lookup(legacyIds::kSpawnEggEntities, damage & 255);
    if (!entity.empty()) {
        NbtCompound& tag = compoundOrCreate(stack, "tag");
        NbtCompound& entityTag = compoundOrCreate(tag, "EntityTag");
        if (!entityTag.contains("id") || entityTag.getString("id") != entity) {
            entityTag.putString("id", std::string{entity});
        }
    }
    if (damage != 0) {
        stack.putShort("Damage", 0);
    }
}

// parity: `ItemBannerColorFix`.
void bannerItems(NbtCompound& stack, const Context&) {
    if (stack.getString("id") != "minecraft:banner") {
        return;
    }
    NbtCompound* tag = compoundIn(stack, "tag");
    NbtCompound* blockEntity = tag == nullptr ? nullptr : compoundIn(*tag, "BlockEntityTag");
    if (blockEntity == nullptr || !number(blockEntity->find("Base")).has_value()) {
        return;
    }
    stack.putShort("Damage", static_cast<std::int16_t>(whole(*blockEntity, "Base") & 15));
    if (const NbtCompound* display = tag->getCompound("display"); display != nullptr && display->size() == 1) {
        const NbtList* lore = display->getList("Lore");
        if (lore != nullptr && lore->size() == 1 && (*lore)[0].get<std::string>() != nullptr &&
            *(*lore)[0].get<std::string>() == "(+NBT") {
            return;
        }
    }
    blockEntity->remove("Base");
}

// parity: `ItemWaterPotionFix`.
void waterPotions(NbtCompound& stack, const Context&) {
    const std::string_view id = stack.getString("id");
    if (id == "minecraft:potion" || id == "minecraft:splash_potion" || id == "minecraft:lingering_potion" ||
        id == "minecraft:tipped_arrow") {
        NbtCompound& tag = compoundOrCreate(stack, "tag");
        if (tag.getString("Potion").empty()) {
            tag.putString("Potion", "minecraft:water");
        }
    }
}

constexpr std::array<std::string_view, 16> kShulkerBoxes{
    "minecraft:white_shulker_box",      "minecraft:orange_shulker_box", "minecraft:magenta_shulker_box",
    "minecraft:light_blue_shulker_box", "minecraft:yellow_shulker_box", "minecraft:lime_shulker_box",
    "minecraft:pink_shulker_box",       "minecraft:gray_shulker_box",   "minecraft:silver_shulker_box",
    "minecraft:cyan_shulker_box",       "minecraft:purple_shulker_box", "minecraft:blue_shulker_box",
    "minecraft:brown_shulker_box",      "minecraft:green_shulker_box",  "minecraft:red_shulker_box",
    "minecraft:black_shulker_box"};

// parity: `ItemShulkerBoxColorFix`.
void shulkerBoxItems(NbtCompound& stack, const Context&) {
    if (stack.getString("id") != "minecraft:shulker_box") {
        return;
    }
    NbtCompound* tag = compoundIn(stack, "tag");
    NbtCompound* blockEntity = tag == nullptr ? nullptr : compoundIn(*tag, "BlockEntityTag");
    if (blockEntity == nullptr) {
        return;
    }
    const auto color = static_cast<std::size_t>(whole(*blockEntity, "Color") % 16);
    blockEntity->remove("Color");
    stack.putString("id", std::string{kShulkerBoxes[color]});
}

// parity: `BedItemColorFix`.
void bedItems(NbtCompound& stack, const Context&) {
    if (stack.getString("id") == "minecraft:bed" && whole(stack, "Damage") == 0) {
        stack.putShort("Damage", 14);
    }
}

// parity: `EntityEquipmentToArmorAndHandFix`.
void equipmentSlots(NbtCompound& entity, const Context&) {
    const NbtList* equipment = entity.getList("Equipment");
    std::vector<NbtCompound> items;
    if (equipment != nullptr) {
        for (const NbtTag& item : *equipment) {
            const NbtCompound* held = item.get<NbtCompound>();
            items.push_back(held != nullptr ? *held : NbtCompound{});
        }
    }
    entity.remove("Equipment");
    if (!items.empty()) {
        entity.set("HandItems", compoundList({items.front(), NbtCompound{}}));
    }
    if (items.size() > 1) {
        std::vector<NbtCompound> armor(4);
        for (std::size_t i = 1; i < std::min<std::size_t>(items.size(), 5); ++i) {
            armor[i - 1] = items[i];
        }
        entity.set("ArmorItems", compoundList(std::move(armor)));
    }
    const NbtList* chances = entity.getList("DropChances");
    if (chances == nullptr) {
        return;
    }
    std::vector<float> values;
    for (const NbtTag& value : *chances) {
        const auto* f = value.get<float>();
        values.push_back(f != nullptr ? *f : 0.0F);
    }
    values.resize(std::max<std::size_t>(values.size(), 5), 0.0F);
    entity.remove("DropChances");
    if (!entity.contains("HandDropChances")) {
        entity.set("HandDropChances", floatList({values[0], 0.0F}));
    }
    if (!entity.contains("ArmorDropChances")) {
        entity.set("ArmorDropChances", floatList({values[1], values[2], values[3], values[4]}));
    }
}

// parity: `VillagerSetCanPickUpLootFix`.
void villagerPicksUpLoot(NbtCompound& entity, const Context&) { entity.putBoolean("CanPickUpLoot", true); }

// parity: `MobSpawnerEntityIdentifiersFix`.
void spawnerEntityIds(NbtCompound& spawner, const Context&) {
    if (const NbtTag* entityId = spawner.find("EntityId");
        entityId != nullptr && entityId->get<std::string>() != nullptr) {
        const std::string id = *entityId->get<std::string>();
        NbtCompound& spawnData = compoundOrCreate(spawner, "SpawnData");
        spawnData.putString("id", id.empty() ? "Pig" : id);
        spawner.remove("EntityId");
    }
    forEachCompound(spawner, "SpawnPotentials", [](NbtCompound& potential) {
        const NbtTag* type = potential.find("Type");
        if (type == nullptr || type->get<std::string>() == nullptr) {
            return;
        }
        NbtCompound entity;
        if (const NbtCompound* properties = potential.getCompound("Properties"); properties != nullptr) {
            entity = *properties;
        }
        entity.putString("id", *type->get<std::string>());
        potential.set("Entity", NbtTag::ofCompound(std::move(entity)));
        potential.remove("Type");
        potential.remove("Properties");
    });
}

// parity: `EntityMinecartIdentifiersFix`.
void minecartIds(NbtCompound& entity, const Context&) {
    if (entity.getString("id") != "Minecart") {
        return;
    }
    const std::int64_t type = whole(entity, "Type");
    entity.putString("id", type == 1 ? "MinecartChest" : (type == 2 ? "MinecartFurnace" : "MinecartRideable"));
    entity.remove("Type");
}

// parity: `EntityStringUuidFix`.
void stringUuids(NbtCompound& entity, const Context&) {
    const NbtTag* held = entity.find("UUID");
    if (held == nullptr || held->get<std::string>() == nullptr) {
        return;
    }
    std::string hex;
    for (const char c : *held->get<std::string>()) {
        if (c != '-') {
            hex.push_back(c);
        }
    }
    if (hex.size() != 32) {
        return;
    }
    const auto most = std::strtoull(hex.substr(0, 16).c_str(), nullptr, 16);
    const auto least = std::strtoull(hex.substr(16).c_str(), nullptr, 16);
    entity.remove("UUID");
    entity.putLong("UUIDMost", static_cast<std::int64_t>(most));
    entity.putLong("UUIDLeast", static_cast<std::int64_t>(least));
}

// parity: `EntityHealthFix`.
void healthAsFloat(NbtCompound& entity, const Context&) {
    if (const auto healF = number(entity.find("HealF")); healF.has_value()) {
        const NbtTag* tag = entity.find("HealF");
        const float value = tag->get<float>() != nullptr
                                ? *tag->get<float>()
                                : (tag->get<double>() != nullptr ? static_cast<float>(*tag->get<double>())
                                                                 : static_cast<float>(*healF));
        entity.remove("HealF");
        entity.putFloat("Health", value);
    } else if (const NbtTag* health = entity.find("Health"); health != nullptr && number(health).has_value()) {
        const float value = health->get<float>() != nullptr
                                ? *health->get<float>()
                                : (health->get<double>() != nullptr ? static_cast<float>(*health->get<double>())
                                                                    : static_cast<float>(*number(health)));
        entity.putFloat("Health", value);
    }
}

// parity: `EntityHorseSaddleFix`.
void horseSaddle(NbtCompound& entity, const Context&) {
    if (entity.getString("id") != "EntityHorse" || entity.contains("SaddleItem") || !entity.getBoolean("Saddle")) {
        return;
    }
    NbtCompound saddle;
    saddle.putString("id", "minecraft:saddle");
    saddle.putByte("Count", 1);
    saddle.putShort("Damage", 0);
    entity.set("SaddleItem", NbtTag::ofCompound(std::move(saddle)));
}

// parity: `EntityPaintingItemFrameDirectionFix`.
void hangingDirections(NbtCompound& entity, const Context&) {
    static constexpr int kDirections[4][3] = {{0, 0, 1}, {-1, 0, 0}, {0, 0, -1}, {1, 0, 0}};
    const std::string_view id = entity.getString("id");
    const bool painting = id == "Painting";
    const bool itemFrame = id == "ItemFrame";
    if ((!painting && !itemFrame) || number(entity.find("Facing")).has_value()) {
        return;
    }
    std::int64_t direction = 0;
    if (number(entity.find("Direction")).has_value()) {
        direction = whole(entity, "Direction") % 4;
        const int* steps = kDirections[direction < 0 ? direction + 4 : direction];
        entity.putInt("TileX", static_cast<std::int32_t>(whole(entity, "TileX")) + steps[0]);
        entity.putInt("TileY", static_cast<std::int32_t>(whole(entity, "TileY")) + steps[1]);
        entity.putInt("TileZ", static_cast<std::int32_t>(whole(entity, "TileZ")) + steps[2]);
        entity.remove("Direction");
        if (itemFrame && number(entity.find("ItemRotation")).has_value()) {
            entity.putByte("ItemRotation", static_cast<std::int8_t>(whole(entity, "ItemRotation") * 2));
        }
    } else {
        direction = whole(entity, "Dir") % 4;
        entity.remove("Dir");
    }
    entity.putByte("Facing", static_cast<std::int8_t>(direction));
}

[[nodiscard]] bool zeroList(const NbtCompound& entity, std::string_view key, std::size_t size) {
    const NbtList* list = entity.getList(key);
    if (list == nullptr || list->size() != size) {
        return false;
    }
    return std::ranges::all_of(
        *list, [](const NbtTag& value) { return value.get<float>() != nullptr && *value.get<float>() == 0.0F; });
}

// parity: `EntityRedundantChanceTagsFix`.
void redundantChances(NbtCompound& entity, const Context&) {
    if (zeroList(entity, "HandDropChances", 2)) {
        entity.remove("HandDropChances");
    }
    if (zeroList(entity, "ArmorDropChances", 4)) {
        entity.remove("ArmorDropChances");
    }
}

// parity: `EntityRidingToPassengersFix`, the mount at the bottom of the chain becomes the root.
void ridingToPassengers(NbtCompound& root, const Context&) {
    std::function<void(NbtCompound&)> lift = [&lift](NbtCompound& entity) {
        NbtCompound* mount = compoundIn(entity, "Riding");
        if (mount == nullptr) {
            return;
        }
        NbtCompound rider = entity;
        NbtCompound top = *mount;
        rider.remove("Riding");
        NbtCompound* tail = &top;
        while (true) {
            NbtCompound* next = compoundIn(*tail, "Riding");
            if (next == nullptr) {
                break;
            }
            NbtCompound copy = *next;
            tail->remove("Riding");
            tail->set("Passengers", compoundList({std::move(copy)}));
            NbtList* list = tail->find("Passengers")->get<NbtList>();
            tail = (*list)[0].get<NbtCompound>();
        }
        tail->set("Passengers", compoundList({std::move(rider)}));
        top.remove("Riding");
        entity = std::move(top);
        lift(entity);
    };
    lift(root);
}

// parity: `EntityTippedArrowFix`.
void tippedArrow(NbtCompound& entity, const Context&) {
    if (entity.getString("id") == "TippedArrow") {
        entity.putString("id", "Arrow");
    }
}

// parity: `EntityArmorStandSilentFix`.
void armorStandSilent(NbtCompound& entity, const Context&) {
    if (entity.getString("id") == "ArmorStand" && entity.getBoolean("Silent") && !entity.getBoolean("Marker")) {
        entity.remove("Silent");
    }
}

[[nodiscard]] std::string jsonText(std::string_view text) {
    std::string out = "{\"text\":\"";
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char escaped[8];
                std::snprintf(escaped, sizeof escaped, "\\u%04x", static_cast<unsigned>(c));
                out += escaped;
            } else {
                out.push_back(c);
            }
        }
    }
    return out + "\"}";
}

// parity: `LegacyComponentDataFixUtils.rewriteFromLenient`, without re-serialising JSON that is already valid.
[[nodiscard]] std::string strictComponent(std::string_view text) {
    if (text.empty() || text == "null") {
        return jsonText("");
    }
    const char first = text.front();
    const char last = text.back();
    if (text.size() > 1 && first == '"' && last == '"') {
        return jsonText(text.substr(1, text.size() - 2));
    }
    if ((first == '{' && last == '}') || (first == '[' && last == ']')) {
        return std::string{text};
    }
    return jsonText(text);
}

// parity: `SignTextStrictJsonFix`.
void signText(NbtCompound& sign, const Context&) {
    for (const char* key : {"Text1", "Text2", "Text3", "Text4"}) {
        if (const NbtTag* line = sign.find(key); line != nullptr && line->get<std::string>() != nullptr) {
            sign.putString(key, strictComponent(*line->get<std::string>()));
        }
    }
}

// parity: `WrittenBookPagesStrictJsonFix`.
void bookPages(NbtCompound& stack, const Context&) {
    if (stack.getString("id") != "minecraft:written_book") {
        return;
    }
    NbtCompound* tag = compoundIn(stack, "tag");
    NbtTag* pages = tag == nullptr ? nullptr : tag->find("pages");
    NbtList* list = pages == nullptr ? nullptr : pages->get<NbtList>();
    for (std::size_t i = 0; list != nullptr && i < list->size(); ++i) {
        if (const std::string* page = (*list)[i].get<std::string>(); page != nullptr) {
            (*list)[i] = NbtTag::ofString(strictComponent(*page));
        }
    }
}

// parity: `EntityZombieVillagerTypeFix`, whose random profession for an unset one is not reproducible and becomes 0.
void zombieVillagerType(NbtCompound& entity, const Context&) {
    if (entity.getString("id") != "Zombie" || !entity.getBoolean("IsVillager")) {
        return;
    }
    if (!entity.contains("ZombieType")) {
        const std::int64_t profession = whole(entity, "VillagerProfession", -1);
        entity.putInt("ZombieType", profession >= 0 && profession < 6 ? static_cast<std::int32_t>(profession) : 0);
    }
    entity.remove("IsVillager");
}

// parity: `EntityElderGuardianSplitFix`.
void elderGuardian(NbtCompound& entity, const Context&) {
    if (entity.getString("id") == "Guardian" && entity.getBoolean("Elder")) {
        entity.putString("id", "ElderGuardian");
    }
}

// parity: `EntitySkeletonSplitFix`.
void skeletonSplit(NbtCompound& entity, const Context&) {
    if (entity.getString("id") != "Skeleton") {
        return;
    }
    const std::int64_t type = whole(entity, "SkeletonType");
    if (type == 1) {
        entity.putString("id", "WitherSkeleton");
    } else if (type == 2) {
        entity.putString("id", "Stray");
    }
}

// parity: `EntityZombieSplitFix`.
void zombieSplit(NbtCompound& entity, const Context&) {
    if (entity.getString("id") != "Zombie") {
        return;
    }
    const std::int64_t type = whole(entity, "ZombieType");
    if (type >= 1 && type <= 5) {
        entity.putString("id", "ZombieVillager");
        entity.putInt("Profession", static_cast<std::int32_t>(type - 1));
    } else if (type == 6) {
        entity.putString("id", "Husk");
    }
    entity.remove("ZombieType");
}

// parity: `EntityHorseSplitFix`.
void horseSplit(NbtCompound& entity, const Context&) {
    if (entity.getString("id") != "EntityHorse") {
        return;
    }
    switch (whole(entity, "Type")) {
    case 1:
        entity.putString("id", "Donkey");
        break;
    case 2:
        entity.putString("id", "Mule");
        break;
    case 3:
        entity.putString("id", "ZombieHorse");
        break;
    case 4:
        entity.putString("id", "SkeletonHorse");
        break;
    default:
        entity.putString("id", "Horse");
    }
    entity.remove("Type");
}

// parity: `BlockEntityIdFix`.
void blockEntityIds(NbtCompound& blockEntity, const Context&) {
    const std::string_view mapped = lookup(legacyIds::kBlockEntityIds, blockEntity.getString("id"));
    if (!mapped.empty()) {
        blockEntity.putString("id", std::string{mapped});
    }
}

// parity: `EntityIdFix`.
void entityIds(NbtCompound& entity, const Context&) {
    const std::string_view mapped = lookup(legacyIds::kEntityIds, entity.getString("id"));
    if (!mapped.empty()) {
        entity.putString("id", std::string{mapped});
    }
}

// parity: `EntityShulkerColorFix`.
void shulkerColor(NbtCompound& entity, const Context&) {
    if (!number(entity.find("Color")).has_value()) {
        entity.putByte("Color", 10);
    }
}

// parity: `BlockEntityShulkerBoxColorFix`.
void shulkerBoxColor(NbtCompound& blockEntity, const Context&) { blockEntity.remove("Color"); }

// parity: `ChunkBedBlockEntityInjecterFix`, the bed block id being 26 in the chunk format of the time.
void injectBedBlockEntities(NbtCompound& chunk, const Context&) {
    NbtCompound* level = compoundIn(chunk, "Level");
    if (level == nullptr) {
        return;
    }
    const std::int32_t chunkX = level->getInt("xPos");
    const std::int32_t chunkZ = level->getInt("zPos");
    std::vector<NbtCompound> beds;
    forEachCompound(*level, "Sections", [&](NbtCompound& section) {
        const std::int32_t sectionY = section.getByte("Y");
        const auto blocks = section.getByteArray("Blocks");
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            if ((blocks[i] & 255) != 26) {
                continue;
            }
            NbtCompound bed;
            bed.putString("id", "minecraft:bed");
            bed.putInt("x", static_cast<std::int32_t>(i & 15) + (chunkX << 4));
            bed.putInt("y", static_cast<std::int32_t>((i >> 8) & 15) + (sectionY << 4));
            bed.putInt("z", static_cast<std::int32_t>((i >> 4) & 15) + (chunkZ << 4));
            bed.putShort("color", 14);
            beds.push_back(std::move(bed));
        }
    });
    if (beds.empty()) {
        return;
    }
    std::vector<NbtTag> tiles;
    if (const NbtList* existing = level->getList("TileEntities"); existing != nullptr) {
        tiles.assign(existing->begin(), existing->end());
    }
    for (NbtCompound& bed : beds) {
        tiles.push_back(NbtTag::ofCompound(std::move(bed)));
    }
    level->set("TileEntities", NbtTag::ofList(NbtList{NbtType::Compound, std::move(tiles)}));
}

} // namespace

void addFixesFrom0100(std::vector<Fix>& fixes) {
    fixes.insert(fixes.end(), {
                                  {100, Target::Entity, {}, equipmentSlots},
                                  {101,
                                   Target::Entity,
                                   {},
                                   [](NbtCompound& e, const Context& c) {
                                       if (e.getString("id") == "Villager") {
                                           villagerPicksUpLoot(e, c);
                                       }
                                   }},
                                  {102, Target::Root, {}, itemIds},
                                  {102,
                                   Target::Root,
                                   {},
                                   [](NbtCompound& root, const Context& c) {
                                       forEachNamedStack(root, [&c](NbtCompound& s) { itemPotions(s, c); });
                                   }},
                                  {105,
                                   Target::Root,
                                   {},
                                   [](NbtCompound& root, const Context& c) {
                                       forEachNamedStack(root, [&c](NbtCompound& s) { itemSpawnEggs(s, c); });
                                   }},
                                  {106, Target::BlockEntity, "MobSpawner", spawnerEntityIds},
                                  {107, Target::Entity, {}, minecartIds},
                                  {108, Target::Entity, {}, stringUuids},
                                  {109, Target::Entity, {}, healthAsFloat},
                                  {110, Target::Entity, {}, horseSaddle},
                                  {111, Target::Entity, {}, hangingDirections},
                                  {113, Target::Entity, {}, redundantChances},
                                  {135, Target::Entity, {}, ridingToPassengers},
                                  {143, Target::Entity, {}, tippedArrow},
                                  {147, Target::Entity, {}, armorStandSilent},
                                  {165, Target::BlockEntity, "Sign", signText},
                                  {165,
                                   Target::Root,
                                   {},
                                   [](NbtCompound& root, const Context& c) {
                                       forEachNamedStack(root, [&c](NbtCompound& s) { bookPages(s, c); });
                                   }},
                                  {502, Target::Entity, {}, zombieVillagerType},
                                  {700, Target::Entity, {}, elderGuardian},
                                  {701, Target::Entity, {}, skeletonSplit},
                                  {702, Target::Entity, {}, zombieSplit},
                                  {703, Target::Entity, {}, horseSplit},
                                  {704, Target::BlockEntity, {}, blockEntityIds},
                                  {705, Target::Entity, {}, entityIds},
                                  {804,
                                   Target::Root,
                                   {},
                                   [](NbtCompound& root, const Context& c) {
                                       forEachNamedStack(root, [&c](NbtCompound& s) { bannerItems(s, c); });
                                   }},
                                  {806,
                                   Target::Root,
                                   {},
                                   [](NbtCompound& root, const Context& c) {
                                       forEachNamedStack(root, [&c](NbtCompound& s) { waterPotions(s, c); });
                                   }},
                                  {808, Target::Entity, "minecraft:shulker", shulkerColor},
                                  {813,
                                   Target::Root,
                                   {},
                                   [](NbtCompound& root, const Context& c) {
                                       forEachNamedStack(root, [&c](NbtCompound& s) { shulkerBoxItems(s, c); });
                                   }},
                                  {813, Target::BlockEntity, "minecraft:shulker_box", shulkerBoxColor},
                                  {1125, Target::Chunk, {}, injectBedBlockEntities},
                                  {1125,
                                   Target::Root,
                                   {},
                                   [](NbtCompound& root, const Context& c) {
                                       forEachNamedStack(root, [&c](NbtCompound& s) { bedItems(s, c); });
                                   }},
                              });
}

} // namespace basalt::metamorph::datafix
