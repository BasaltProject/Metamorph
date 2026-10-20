#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <basalt/metamorph/datafix/DataFixer.hpp>
#include <basalt/metamorph/datafix/LevelData.hpp>
#include <basalt/wire/nbt/NbtIO.hpp>

#include <doctest.h>

using namespace basalt::wire::nbt;
using basalt::metamorph::datafix::Domain;

namespace {

NbtCompound stack(std::string_view id, std::int32_t count) {
    NbtCompound tag;
    tag.putString("id", std::string{id});
    tag.putInt("count", count);
    return tag;
}

} // namespace

TEST_CASE("a 26.2 chunk is brought to 26.3") {
    NbtCompound components;
    NbtCompound explorer;
    explorer.putString("type", "minecraft:monument");
    NbtCompound decorations;
    decorations.set("+", NbtTag::ofCompound(explorer));
    components.set("minecraft:map_decorations", NbtTag::ofCompound(decorations));
    NbtCompound name;
    name.putString("translate", "filled_map.monument");
    components.set("minecraft:item_name", NbtTag::ofCompound(name));
    components.putInt("minecraft:map_color", 5);
    NbtCompound map = stack("minecraft:filled_map", 1);
    map.set("components", NbtTag::ofCompound(components));
    NbtCompound chest;
    chest.putString("id", "minecraft:chest");
    NbtList items{NbtType::Compound, {}};
    items.push(NbtTag::ofCompound(map));
    chest.set("Items", NbtTag::ofList(items));
    NbtCompound sign;
    sign.putString("id", "minecraft:sign");
    NbtCompound pot;
    pot.putString("id", "minecraft:decorated_pot");
    NbtList sherds{NbtType::String, {}};
    sherds.push(NbtTag::ofString("minecraft:angler_pottery_sherd"));
    pot.set("sherds", NbtTag::ofList(sherds));
    NbtList blockEntities{NbtType::Compound, {}};
    blockEntities.push(NbtTag::ofCompound(chest));
    blockEntities.push(NbtTag::ofCompound(sign));
    blockEntities.push(NbtTag::ofCompound(pot));

    NbtCompound chunk;
    chunk.putString("Status", "minecraft:carvers");
    chunk.set("block_entities", NbtTag::ofList(blockEntities));

    basalt::metamorph::datafix::upgrade(Domain::Chunk, chunk, 4903);

    CHECK(chunk.getString("Status") == "minecraft:terrain");
    const NbtCompound* fixedMap = chunk.getList("block_entities")
                                      ->elements()[0]
                                      .get<NbtCompound>()
                                      ->getList("Items")
                                      ->elements()[0]
                                      .get<NbtCompound>();
    CHECK(fixedMap->getString("id") == "minecraft:ocean_monument_map");
    const NbtCompound* fixedComponents = fixedMap->getCompound("components");
    CHECK_FALSE(fixedComponents->contains("minecraft:map_color"));
    CHECK_FALSE(fixedComponents->contains("minecraft:item_name"));
    const NbtCompound* fixedSign = chunk.getList("block_entities")->elements()[1].get<NbtCompound>();
    CHECK(fixedSign->getBoolean("allow_op_features"));
    const NbtCompound* fixedSherds =
        chunk.getList("block_entities")->elements()[2].get<NbtCompound>()->getCompound("sherds");
    REQUIRE(fixedSherds != nullptr);
    CHECK(fixedSherds->getCompound("back")->getString("id") == "minecraft:angler_pottery_sherd");
    CHECK(fixedSherds->getCompound("front")->getString("id") == "minecraft:brick");
}

TEST_CASE("a 1.21.4 chunk loses its light and gains iron chains and tooltip display") {
    NbtCompound levels;
    levels.putInt("minecraft:sharpness", 5);
    NbtCompound enchantments;
    enchantments.set("levels", NbtTag::ofCompound(levels));
    enchantments.putBoolean("show_in_tooltip", false);
    NbtCompound components;
    components.set("minecraft:enchantments", NbtTag::ofCompound(enchantments));
    NbtCompound sword = stack("minecraft:chain", 1);
    sword.set("components", NbtTag::ofCompound(components));
    NbtCompound chest;
    chest.putString("id", "minecraft:chest");
    NbtList items{NbtType::Compound, {}};
    items.push(NbtTag::ofCompound(sword));
    chest.set("Items", NbtTag::ofList(items));
    NbtList blockEntities{NbtType::Compound, {}};
    blockEntities.push(NbtTag::ofCompound(chest));

    NbtCompound entry;
    entry.putString("Name", "minecraft:chain");
    NbtList palette{NbtType::Compound, {}};
    palette.push(NbtTag::ofCompound(entry));
    NbtCompound states;
    states.set("palette", NbtTag::ofList(palette));
    NbtCompound section;
    section.set("block_states", NbtTag::ofCompound(states));
    section.putByteArray("SkyLight", {1, 2});
    NbtList sections{NbtType::Compound, {}};
    sections.push(NbtTag::ofCompound(section));

    NbtCompound chunk;
    chunk.putBoolean("isLightOn", true);
    chunk.set("sections", NbtTag::ofList(sections));
    chunk.set("block_entities", NbtTag::ofList(blockEntities));
    basalt::metamorph::datafix::upgrade(Domain::Chunk, chunk, 4189);

    CHECK_FALSE(chunk.contains("isLightOn"));
    const NbtCompound* fixedSection = chunk.getList("sections")->elements()[0].get<NbtCompound>();
    CHECK_FALSE(fixedSection->contains("SkyLight"));
    const NbtCompound* fixedEntry =
        fixedSection->getCompound("block_states")->getList("palette")->elements()[0].get<NbtCompound>();
    CHECK(fixedEntry->getString("id") == "minecraft:iron_chain");
    const NbtCompound* fixedStack = chunk.getList("block_entities")
                                        ->elements()[0]
                                        .get<NbtCompound>()
                                        ->getList("Items")
                                        ->elements()[0]
                                        .get<NbtCompound>();
    CHECK(fixedStack->getString("id") == "minecraft:iron_chain");
    const NbtCompound* fixedComponents = fixedStack->getCompound("components");
    CHECK(fixedComponents->getCompound("minecraft:enchantments")->getInt("minecraft:sharpness") == 5);
    const NbtList* hidden = fixedComponents->getCompound("minecraft:tooltip_display")->getList("hidden_components");
    REQUIRE(hidden->size() == 1);
    CHECK(*hidden->elements()[0].get<std::string>() == "minecraft:enchantments");
}

TEST_CASE("sign text saved as JSON becomes components with the new events") {
    NbtList messages{NbtType::String, {}};
    messages.push(
        NbtTag::ofString(R"({"text":"hi","bold":"true","clickEvent":{"action":"run_command","value":"/say hi"},)"
                         R"("hoverEvent":{"action":"show_text","contents":{"text":"tip"}}})"));
    messages.push(NbtTag::ofString(R"("plain")"));
    NbtCompound front;
    front.set("messages", NbtTag::ofList(messages));
    NbtCompound sign;
    sign.putString("id", "minecraft:sign");
    sign.set("front_text", NbtTag::ofCompound(front));
    NbtList blockEntities{NbtType::Compound, {}};
    blockEntities.push(NbtTag::ofCompound(sign));
    NbtCompound chunk;
    chunk.set("block_entities", NbtTag::ofList(blockEntities));

    basalt::metamorph::datafix::upgrade(Domain::Chunk, chunk, 4189);

    const NbtCompound* fixed =
        chunk.getList("block_entities")->elements()[0].get<NbtCompound>()->getCompound("front_text");
    const NbtCompound* first = fixed->getList("messages")->elements()[0].get<NbtCompound>();
    REQUIRE(first != nullptr);
    CHECK(first->getString("text") == "hi");
    CHECK(first->getBoolean("bold"));
    CHECK(first->getCompound("click_event")->getString("command") == "/say hi");
    CHECK_FALSE(first->contains("clickEvent"));
    CHECK(first->getCompound("hover_event")->getCompound("value")->getString("text") == "tip");
    CHECK(*fixed->getList("messages")->elements()[1].get<std::string>() == "plain");
}

TEST_CASE("entities saved before 1.21.5 get their equipment, drop chances and the splits") {
    const auto item = [](std::string_view id) {
        NbtCompound tag;
        tag.putString("id", std::string{id});
        tag.putInt("count", 1);
        return tag;
    };
    NbtCompound horse;
    horse.putString("id", "minecraft:horse");
    horse.set("SaddleItem", NbtTag::ofCompound(item("minecraft:saddle")));
    NbtList armor{NbtType::Compound, {}};
    armor.push(NbtTag::ofCompound(NbtCompound{}));
    armor.push(NbtTag::ofCompound(NbtCompound{}));
    armor.push(NbtTag::ofCompound(item("minecraft:iron_chestplate")));
    armor.push(NbtTag::ofCompound(NbtCompound{}));
    horse.set("ArmorItems", NbtTag::ofList(armor));
    NbtList chances{NbtType::Float, {}};
    chances.push(NbtTag::ofFloat(0.085F));
    chances.push(NbtTag::ofFloat(0.085F));
    chances.push(NbtTag::ofFloat(1.0F));
    chances.push(NbtTag::ofFloat(0.085F));
    horse.set("ArmorDropChances", NbtTag::ofList(chances));
    horse.putFloat("FallDistance", 2.5F);

    NbtCompound boat;
    boat.putString("id", "minecraft:boat");
    boat.putString("Type", "birch");

    NbtCompound potion;
    potion.putString("id", "minecraft:potion");
    potion.set("Item", NbtTag::ofCompound(item("minecraft:lingering_potion")));

    NbtList entities{NbtType::Compound, {}};
    entities.push(NbtTag::ofCompound(horse));
    entities.push(NbtTag::ofCompound(boat));
    entities.push(NbtTag::ofCompound(potion));
    NbtCompound file;
    file.set("Entities", NbtTag::ofList(entities));
    basalt::metamorph::datafix::upgrade(Domain::Entities, file, 3955);

    const NbtCompound* fixedHorse = file.getList("Entities")->elements()[0].get<NbtCompound>();
    const NbtCompound* equipment = fixedHorse->getCompound("equipment");
    REQUIRE(equipment != nullptr);
    CHECK(equipment->getCompound("chest")->getString("id") == "minecraft:iron_chestplate");
    CHECK(equipment->getCompound("saddle")->getString("id") == "minecraft:saddle");
    CHECK_FALSE(fixedHorse->contains("ArmorItems"));
    CHECK(fixedHorse->getCompound("drop_chances")->getFloat("chest") == 1.0F);
    CHECK(fixedHorse->getCompound("drop_chances")->getFloat("saddle") == 2.0F);
    CHECK(fixedHorse->getDouble("fall_distance") == 2.5);
    CHECK(file.getList("Entities")->elements()[1].get<NbtCompound>()->getString("id") == "minecraft:birch_boat");
    CHECK(file.getList("Entities")->elements()[2].get<NbtCompound>()->getString("id") == "minecraft:lingering_potion");
}

TEST_CASE("an item frame and a turtle keep their positions as block positions") {
    NbtCompound frame;
    frame.putString("id", "minecraft:item_frame");
    frame.putInt("TileX", 10);
    frame.putInt("TileY", 64);
    frame.putInt("TileZ", -3);
    NbtCompound turtle;
    turtle.putString("id", "minecraft:turtle");
    turtle.putInt("HomePosX", 1);
    turtle.putInt("HomePosY", 2);
    turtle.putInt("HomePosZ", 3);
    turtle.putBoolean("HasEgg", true);
    turtle.putInt("TravelPosX", 9);
    NbtList entities{NbtType::Compound, {}};
    entities.push(NbtTag::ofCompound(frame));
    entities.push(NbtTag::ofCompound(turtle));
    NbtCompound file;
    file.set("Entities", NbtTag::ofList(entities));
    basalt::metamorph::datafix::upgrade(Domain::Entities, file, 4189);

    const NbtCompound* fixedFrame = file.getList("Entities")->elements()[0].get<NbtCompound>();
    CHECK(fixedFrame->getIntArray("block_pos").size() == 3);
    CHECK(fixedFrame->getIntArray("block_pos")[2] == -3);
    CHECK_FALSE(fixedFrame->contains("TileX"));
    const NbtCompound* fixedTurtle = file.getList("Entities")->elements()[1].get<NbtCompound>();
    CHECK(fixedTurtle->getIntArray("home_pos")[1] == 2);
    CHECK(fixedTurtle->getBoolean("has_egg"));
    CHECK_FALSE(fixedTurtle->contains("TravelPosX"));
}

TEST_CASE("an item saved before components keeps its enchantments, name and damage") {
    NbtCompound enchantment;
    enchantment.putString("id", "minecraft:sharpness");
    enchantment.putShort("lvl", 3);
    NbtList enchantments{NbtType::Compound, {}};
    enchantments.push(NbtTag::ofCompound(enchantment));
    NbtCompound display;
    display.putString("Name", R"({"text":"Blade"})");
    NbtCompound tag;
    tag.set("Enchantments", NbtTag::ofList(enchantments));
    tag.set("display", NbtTag::ofCompound(display));
    tag.putInt("Damage", 7);
    NbtCompound sword;
    sword.putString("id", "minecraft:diamond_sword");
    sword.putByte("Count", 1);
    sword.set("tag", NbtTag::ofCompound(tag));
    NbtList items{NbtType::Compound, {}};
    items.push(NbtTag::ofCompound(sword));
    NbtCompound entity;
    entity.putString("id", "minecraft:chest_minecart");
    entity.set("Items", NbtTag::ofList(items));
    NbtList entities{NbtType::Compound, {}};
    entities.push(NbtTag::ofCompound(entity));
    NbtCompound file;
    file.set("Entities", NbtTag::ofList(entities));
    basalt::metamorph::datafix::upgrade(Domain::Entities, file, 3700);

    const NbtCompound* fixed = file.getList("Entities")->elements()[0].get<NbtCompound>();
    REQUIRE(fixed != nullptr);
    const NbtList* fixedItems = fixed->getList("Items");
    REQUIRE(fixedItems != nullptr);
    const NbtCompound* stack = fixedItems->elements()[0].get<NbtCompound>();
    REQUIRE(stack != nullptr);
    CHECK(stack->getInt("count") == 1);
    CHECK_FALSE(stack->contains("Count"));
    const NbtCompound* components = stack->getCompound("components");
    REQUIRE(components != nullptr);
    CHECK(components->getInt("minecraft:damage") == 7);
    REQUIRE(components->getCompound("minecraft:enchantments") != nullptr);
    CHECK(components->getCompound("minecraft:enchantments")->getInt("minecraft:sharpness") == 3);
    CHECK(components->contains("minecraft:custom_name"));
}

TEST_CASE("an upgraded sign still writes and reads back, with its neighbours") {
    NbtCompound sign;
    sign.putString("id", "minecraft:sign");
    sign.putString("Text1", R"({"text":"a"})");
    NbtCompound chest;
    chest.putString("id", "minecraft:chest");
    NbtList entities{NbtType::Compound, {}};
    entities.push(NbtTag::ofCompound(sign));
    entities.push(NbtTag::ofCompound(chest));
    NbtCompound chunk;
    chunk.set("block_entities", NbtTag::ofList(entities));
    basalt::metamorph::datafix::upgrade(Domain::Chunk, chunk, 3337);

    const auto bytes = basalt::wire::nbt::nbtIO::write(NbtTag::ofCompound(chunk), "");
    const auto back = basalt::wire::nbt::nbtIO::read(bytes);
    REQUIRE(back.has_value());
    const NbtList* list = back->tag.get<NbtCompound>()->getList("block_entities");
    REQUIRE(list != nullptr);
    CHECK(list->size() == 2);
    CHECK((*list)[1].get<NbtCompound>()->getString("id") == "minecraft:chest");
}

TEST_CASE("a world saved before 1.9 gets its mounts, mob splits and numeric items") {
    NbtCompound mount;
    mount.putString("id", "Pig");
    NbtCompound rider;
    rider.putString("id", "Skeleton");
    rider.putInt("SkeletonType", 1);
    rider.set("Riding", NbtTag::ofCompound(mount));
    NbtList entities{NbtType::Compound, {}};
    entities.push(NbtTag::ofCompound(rider));
    NbtCompound item;
    item.putShort("id", 276);
    item.putByte("Count", 1);
    NbtList items{NbtType::Compound, {}};
    items.push(NbtTag::ofCompound(item));
    NbtCompound chest;
    chest.putString("id", "Chest");
    chest.set("Items", NbtTag::ofList(items));
    NbtList tiles{NbtType::Compound, {}};
    tiles.push(NbtTag::ofCompound(chest));
    NbtCompound level;
    level.set("Entities", NbtTag::ofList(entities));
    level.set("TileEntities", NbtTag::ofList(tiles));
    NbtCompound chunk;
    chunk.set("Level", NbtTag::ofCompound(level));
    basalt::metamorph::datafix::upgrade(Domain::Chunk, chunk, 0, {}, 1343);
    const NbtCompound* upgraded = chunk.getCompound("Level");
    REQUIRE(upgraded != nullptr);
    const NbtCompound* root = (*upgraded->getList("Entities"))[0].get<NbtCompound>();
    CHECK(root->getString("id") == "minecraft:pig");
    const NbtCompound* passenger = (*root->getList("Passengers"))[0].get<NbtCompound>();
    CHECK(passenger->getString("id") == "minecraft:wither_skeleton");
    const NbtCompound* tile = (*upgraded->getList("TileEntities"))[0].get<NbtCompound>();
    CHECK(tile->getString("id") == "minecraft:chest");
    CHECK((*tile->getList("Items"))[0].get<NbtCompound>()->getString("id") == "minecraft:diamond_sword");
}

TEST_CASE("a map saved before 1.20.5 gets a named dimension and block positions") {
    NbtCompound pos;
    pos.putInt("X", 3);
    pos.putInt("Y", 64);
    pos.putInt("Z", -5);
    NbtCompound banner;
    banner.set("Pos", NbtTag::ofCompound(pos));
    banner.putString("Color", "red");
    NbtList banners{NbtType::Compound, {}};
    banners.push(NbtTag::ofCompound(banner));
    NbtCompound data;
    data.putByte("dimension", -1);
    data.set("banners", NbtTag::ofList(banners));
    basalt::metamorph::datafix::upgradeSavedData("map", data, 1343);
    CHECK(data.getString("dimension") == "minecraft:the_nether");
    const NbtCompound* upgraded = (*data.getList("banners"))[0].get<NbtCompound>();
    CHECK(upgraded->getIntArray("pos").size() == 3);
    CHECK(upgraded->getString("color") == "red");
}
