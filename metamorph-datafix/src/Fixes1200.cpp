#include "FixSupport.hpp"
#include "LegacyStates.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace basalt::metamorph::datafix {
namespace {

using legacy::State;
using wire::nbt::NbtCompound;
using wire::nbt::NbtList;
using wire::nbt::NbtTag;
using wire::nbt::NbtType;

constexpr int kNorthWestMask = 128;
constexpr int kWestMask = 64;
constexpr int kSouthWestMask = 32;
constexpr int kSouthMask = 16;
constexpr int kSouthEastMask = 8;
constexpr int kEastMask = 4;
constexpr int kNorthEastMask = 2;
constexpr int kNorthMask = 1;

[[nodiscard]] NbtCompound* compoundAt(NbtCompound& parent, std::string_view key) {
    NbtTag* held = parent.find(key);
    return held == nullptr ? nullptr : held->get<NbtCompound>();
}

[[nodiscard]] std::int64_t whole(const NbtTag* tag, std::int64_t fallback) {
    if (tag == nullptr) {
        return fallback;
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
    return fallback;
}

[[nodiscard]] int ceilLog2(int value) {
    return value == 0 ? 0 : static_cast<int>(std::ceil(std::log(value) / std::log(2.0)));
}

[[nodiscard]] State make(std::string name, std::map<std::string, std::string> properties = {}) {
    return State{std::move(name), std::move(properties)};
}

constexpr std::string_view kFilterMe = "%%FILTER_ME%%";

/// The state constants of `ChunkPalettedStorageFix.MappingConstants`, distinct objects as the palette compares
/// identity.
struct Constants {
    State air = make("minecraft:air");
    State pumpkin = make("minecraft:pumpkin");
    State snowyPodzol = make("minecraft:podzol", {{"snowy", "true"}});
    State snowyGrass = make("minecraft:grass_block", {{"snowy", "true"}});
    State snowyMycelium = make("minecraft:mycelium", {{"snowy", "true"}});
    State upperSunflower = make("minecraft:sunflower", {{"half", "upper"}});
    State upperLilac = make("minecraft:lilac", {{"half", "upper"}});
    State upperTallGrass = make("minecraft:tall_grass", {{"half", "upper"}});
    State upperLargeFern = make("minecraft:large_fern", {{"half", "upper"}});
    State upperRoseBush = make("minecraft:rose_bush", {{"half", "upper"}});
    State upperPeony = make("minecraft:peony", {{"half", "upper"}});
    std::map<std::string, State> flowerPots;
    std::map<std::string, State> skulls;
    std::map<std::string, State> doors;
    std::map<std::string, State> noteBlocks;
    std::map<std::string, State> beds;
    std::map<std::string, State> banners;
    std::set<int> fix;
    std::set<int> virtualBlocks;

    Constants() {
        const std::array<std::pair<const char*, const char*>, 22> pots{
            {{"minecraft:air0", "minecraft:flower_pot"},
             {"minecraft:red_flower0", "minecraft:potted_poppy"},
             {"minecraft:red_flower1", "minecraft:potted_blue_orchid"},
             {"minecraft:red_flower2", "minecraft:potted_allium"},
             {"minecraft:red_flower3", "minecraft:potted_azure_bluet"},
             {"minecraft:red_flower4", "minecraft:potted_red_tulip"},
             {"minecraft:red_flower5", "minecraft:potted_orange_tulip"},
             {"minecraft:red_flower6", "minecraft:potted_white_tulip"},
             {"minecraft:red_flower7", "minecraft:potted_pink_tulip"},
             {"minecraft:red_flower8", "minecraft:potted_oxeye_daisy"},
             {"minecraft:yellow_flower0", "minecraft:potted_dandelion"},
             {"minecraft:sapling0", "minecraft:potted_oak_sapling"},
             {"minecraft:sapling1", "minecraft:potted_spruce_sapling"},
             {"minecraft:sapling2", "minecraft:potted_birch_sapling"},
             {"minecraft:sapling3", "minecraft:potted_jungle_sapling"},
             {"minecraft:sapling4", "minecraft:potted_acacia_sapling"},
             {"minecraft:sapling5", "minecraft:potted_dark_oak_sapling"},
             {"minecraft:red_mushroom0", "minecraft:potted_red_mushroom"},
             {"minecraft:brown_mushroom0", "minecraft:potted_brown_mushroom"},
             {"minecraft:deadbush0", "minecraft:potted_dead_bush"},
             {"minecraft:tallgrass2", "minecraft:potted_fern"},
             {"minecraft:cactus0", "minecraft:potted_cactus"}}};
        for (const auto& [key, name] : pots) {
            flowerPots.emplace(key, make(name));
        }
        const std::array<std::pair<int, const char*>, 6> skullKinds{
            {{0, "skeleton"}, {1, "wither_skeleton"}, {2, "zombie"}, {3, "player"}, {4, "creeper"}, {5, "dragon"}}};
        for (std::size_t i = 0; i < skullKinds.size(); ++i) {
            const std::string type = i < 2 ? "skull" : "head";
            const std::string name = skullKinds[i].second;
            for (const char* facing : {"north", "east", "south", "west"}) {
                skulls.emplace(std::to_string(skullKinds[i].first) + facing,
                               make("minecraft:" + name + "_wall_" + type, {{"facing", facing}}));
            }
            for (int rotation = 0; rotation < 16; ++rotation) {
                skulls.emplace(std::to_string(skullKinds[i].first) + std::to_string(rotation),
                               make("minecraft:" + name + "_" + type, {{"rotation", std::to_string(rotation)}}));
            }
        }
        for (const char* door :
             {"oak_door", "iron_door", "spruce_door", "birch_door", "jungle_door", "acacia_door", "dark_oak_door"}) {
            const std::string id = std::string{"minecraft:"} + door;
            for (const char* facing : {"east", "north", "south", "west"}) {
                for (const char* half : {"lower", "upper"}) {
                    for (const char* hinge : {"left", "right"}) {
                        for (const char* open : {"false", "true"}) {
                            for (const char* powered : {"false", "true"}) {
                                doors.emplace(id + facing + half + hinge + open + powered,
                                              make(id, {{"facing", facing},
                                                        {"half", half},
                                                        {"hinge", hinge},
                                                        {"open", open},
                                                        {"powered", powered}}));
                            }
                        }
                    }
                }
            }
        }
        for (int i = 0; i < 26; ++i) {
            noteBlocks.emplace("true" + std::to_string(i),
                               make("minecraft:note_block", {{"powered", "true"}, {"note", std::to_string(i)}}));
            noteBlocks.emplace("false" + std::to_string(i),
                               make("minecraft:note_block", {{"powered", "false"}, {"note", std::to_string(i)}}));
        }
        const std::array<const char*, 16> colors{"white", "orange", "magenta",    "light_blue", "yellow", "lime",
                                                 "pink",  "gray",   "light_gray", "cyan",       "purple", "blue",
                                                 "brown", "green",  "red",        "black"};
        for (int id = 0; id < 16; ++id) {
            const std::string color = colors[static_cast<std::size_t>(id)];
            if (color != "red") {
                const std::string bed = "minecraft:" + color + "_bed";
                for (const char* facing : {"south", "west", "north", "east"}) {
                    beds.emplace(std::string{facing} + "falsefoot" + std::to_string(id),
                                 make(bed, {{"facing", facing}, {"occupied", "false"}, {"part", "foot"}}));
                    beds.emplace(std::string{facing} + "falsehead" + std::to_string(id),
                                 make(bed, {{"facing", facing}, {"occupied", "false"}, {"part", "head"}}));
                    beds.emplace(std::string{facing} + "truehead" + std::to_string(id),
                                 make(bed, {{"facing", facing}, {"occupied", "true"}, {"part", "head"}}));
                }
            }
            if (color != "white") {
                const int banner = 15 - id;
                for (int rotation = 0; rotation < 16; ++rotation) {
                    banners.emplace(std::to_string(rotation) + "_" + std::to_string(banner),
                                    make("minecraft:" + color + "_banner", {{"rotation", std::to_string(rotation)}}));
                }
                for (const char* facing : {"north", "south", "west", "east"}) {
                    banners.emplace(std::string{facing} + "_" + std::to_string(banner),
                                    make("minecraft:" + color + "_wall_banner", {{"facing", facing}}));
                }
            }
        }
        fix = {2, 3, 110, 140, 144, 25, 86, 26, 176, 177, 175, 64, 71, 193, 194, 195, 196, 197};
        virtualBlocks = {54,  146, 25,  26,  51,  53,  67,  108, 109, 114, 128, 134, 135, 136,
                         156, 163, 164, 180, 203, 55,  85,  113, 188, 189, 190, 191, 192, 93,
                         94,  101, 102, 160, 106, 107, 183, 184, 185, 186, 187, 132, 139, 199};
    }
};

const Constants& constants() {
    static const Constants kConstants;
    return kConstants;
}

[[nodiscard]] std::string propertyOf(const State& state, const std::string& key) {
    const auto found = state.properties.find(key);
    return found == state.properties.end() ? std::string{} : found->second;
}

[[nodiscard]] NbtTag toTag(const State& state) {
    NbtCompound tag;
    tag.putString("Name", state.name);
    if (!state.properties.empty()) {
        NbtCompound properties;
        for (const auto& [key, value] : state.properties) {
            properties.putString(key, value);
        }
        tag.set("Properties", NbtTag::ofCompound(std::move(properties)));
    }
    return NbtTag::ofCompound(std::move(tag));
}

[[nodiscard]] int sideMask(bool west, bool east, bool north, bool south) {
    int s = 0;
    if (north) {
        s |= east ? kNorthEastMask : (west ? kNorthWestMask : kNorthMask);
    } else if (south) {
        s |= west ? kSouthWestMask : (east ? kSouthEastMask : kSouthMask);
    } else if (east) {
        s |= kEastMask;
    } else if (west) {
        s |= kWestMask;
    }
    return s;
}

[[nodiscard]] int nibble(const std::vector<std::int8_t>& layer, int x, int y, int z) {
    if (layer.size() != 2048) {
        return 0;
    }
    const int position = y << 8 | z << 4 | x;
    const std::int8_t value = layer[static_cast<std::size_t>(position >> 1)];
    return (position & 1) == 0 ? value & 15 : (value >> 4) & 15;
}

enum class Direction { Down, Up, North, South, West, East };

/// parity: `UpgradeChunk.relative`, the position next to `pos`, or -1 outside the chunk column.
[[nodiscard]] int relative(int pos, Direction direction) {
    switch (direction) {
    case Direction::West:
    case Direction::East: {
        const int x = (pos & 15) + (direction == Direction::East ? 1 : -1);
        return x >= 0 && x <= 15 ? (pos & -16) | x : -1;
    }
    case Direction::Down:
    case Direction::Up: {
        const int y = (pos >> 8) + (direction == Direction::Up ? 1 : -1);
        return y >= 0 && y <= 255 ? (pos & 255) | (y << 8) : -1;
    }
    case Direction::North:
    case Direction::South: {
        const int z = ((pos >> 4) & 15) + (direction == Direction::South ? 1 : -1);
        return z >= 0 && z <= 15 ? (pos & -241) | (z << 4) : -1;
    }
    }
    return -1;
}

class Section {
public:
    explicit Section(NbtCompound source) : source_(std::move(source)) {
        y = static_cast<int>(whole(source_.find("Y"), 0));
        hasData = source_.find("Blocks") != nullptr;
    }

    const State* blockAt(int index) const {
        if (index < 0 || index > 4095 ||
            buffer_[static_cast<std::size_t>(index)] >= static_cast<int>(palette_.size())) {
            return &constants().air;
        }
        return palette_[static_cast<std::size_t>(buffer_[static_cast<std::size_t>(index)])];
    }

    void setBlock(int index, const State* state) {
        int id = -1;
        for (std::size_t i = 0; i < palette_.size(); ++i) {
            if (palette_[i] == state) {
                id = static_cast<int>(i);
                break;
            }
        }
        if (id < 0) {
            id = static_cast<int>(palette_.size());
            palette_.push_back(state);
            list_.push_back(state->name == kFilterMe ? &constants().air : state);
        }
        buffer_[static_cast<std::size_t>(index)] = id;
    }

    int upgrade(int sides) {
        if (!hasData) {
            return sides;
        }
        const NbtTag* blocksTag = source_.find("Blocks");
        const auto* blocks = blocksTag == nullptr ? nullptr : blocksTag->get<std::vector<std::int8_t>>();
        if (blocks == nullptr || blocks->size() < 4096) {
            hasData = false;
            return sides;
        }
        const auto layer = [this](std::string_view key) {
            const NbtTag* held = source_.find(key);
            const auto* bytes = held == nullptr ? nullptr : held->get<std::vector<std::int8_t>>();
            return bytes == nullptr ? std::vector<std::int8_t>{} : *bytes;
        };
        const std::vector<std::int8_t> data = layer("Data");
        const std::vector<std::int8_t> add = layer("Add");
        palette_.push_back(&constants().air);
        list_.push_back(&constants().air);
        for (int index = 0; index < 4096; ++index) {
            const int x = index & 15;
            const int row = (index >> 8) & 15;
            const int z = (index >> 4) & 15;
            const int id = nibble(add, x, row, z) << 12 |
                           (static_cast<std::uint8_t>((*blocks)[static_cast<std::size_t>(index)])) << 4 |
                           nibble(data, x, row, z);
            if (constants().fix.contains(id >> 4)) {
                addFix(id >> 4, index);
            }
            if (constants().virtualBlocks.contains(id >> 4)) {
                const int s = sideMask(x == 0, x == 15, z == 0, z == 15);
                if (s == 0) {
                    update.push_back(index);
                } else {
                    sides |= s;
                }
            }
            setBlock(index, &legacy::stateOf(id));
        }
        return sides;
    }

    NbtCompound write() {
        if (!hasData) {
            return source_;
        }
        std::vector<NbtTag> entries;
        for (const State* state : list_) {
            entries.push_back(toTag(*state));
        }
        source_.set("Palette", NbtTag::ofList(NbtList{NbtType::Compound, std::move(entries)}));
        const int bits = std::max(4, ceilLog2(static_cast<int>(palette_.size())));
        std::vector<std::int64_t> storage(static_cast<std::size_t>((4096 * bits + 63) / 64), 0);
        for (int i = 0; i < 4096; ++i) {
            const auto value = static_cast<std::uint64_t>(buffer_[static_cast<std::size_t>(i)]);
            const int bitIndex = i * bits;
            const int startLong = bitIndex >> 6;
            const int endLong = ((i + 1) * bits - 1) >> 6;
            const int startBit = bitIndex & 63;
            storage[static_cast<std::size_t>(startLong)] |= static_cast<std::int64_t>(value << startBit);
            if (startLong != endLong) {
                storage[static_cast<std::size_t>(endLong)] |= static_cast<std::int64_t>(value >> (64 - startBit));
            }
        }
        source_.set("BlockStates", NbtTag::ofLongArray(std::move(storage)));
        source_.remove("Blocks");
        source_.remove("Data");
        source_.remove("Add");
        return source_;
    }

    int y = 0;
    bool hasData = false;
    std::vector<std::pair<int, std::vector<int>>> toFix;
    std::vector<int> update;

private:
    void addFix(int id, int position) {
        for (auto& [existing, positions] : toFix) {
            if (existing == id) {
                positions.push_back(position);
                return;
            }
        }
        toFix.push_back({id, {position}});
    }

    NbtCompound source_;
    std::vector<const State*> palette_;
    std::vector<const State*> list_;
    std::array<int, 4096> buffer_{};
};

class UpgradeChunk {
public:
    explicit UpgradeChunk(NbtCompound& level) : level_(level) {
        x_ = static_cast<int>(whole(level.find("xPos"), 0)) << 4;
        z_ = static_cast<int>(whole(level.find("zPos"), 0)) << 4;
        if (const NbtTag* held = level.find("TileEntities"); held != nullptr && held->get<NbtList>() != nullptr) {
            for (const NbtTag& entry : *held->get<NbtList>()) {
                const NbtCompound* entity = entry.get<NbtCompound>();
                if (entity == nullptr) {
                    continue;
                }
                const int x = static_cast<int>(whole(entity->find("x"), 0) - x_) & 15;
                const int y = static_cast<int>(whole(entity->find("y"), 0));
                const int z = static_cast<int>(whole(entity->find("z"), 0) - z_) & 15;
                blockEntities_[y << 8 | z << 4 | x] = *entity;
                if (std::ranges::find(blockEntityOrder_, y << 8 | z << 4 | x) == blockEntityOrder_.end()) {
                    blockEntityOrder_.push_back(y << 8 | z << 4 | x);
                }
            }
        }
        convertedFromAlpha_ = level.getBoolean("convertedFromAlphaFormat");
        if (const NbtTag* held = level.find("Sections"); held != nullptr && held->get<NbtList>() != nullptr) {
            for (const NbtTag& entry : *held->get<NbtList>()) {
                if (const NbtCompound* source = entry.get<NbtCompound>(); source != nullptr) {
                    auto section = std::make_unique<Section>(*source);
                    sides_ = section->upgrade(sides_);
                    if (section->y >= 0 && section->y < 16) {
                        sections_[static_cast<std::size_t>(section->y)] = std::move(section);
                    }
                }
            }
        }
        fixSections();
    }

    void write() {
        if (blockEntityOrder_.empty() || blockEntities_.empty()) {
            level_.remove("TileEntities");
        } else {
            std::vector<NbtTag> entities;
            for (const int key : blockEntityOrder_) {
                if (const auto found = blockEntities_.find(key); found != blockEntities_.end()) {
                    entities.push_back(NbtTag::ofCompound(found->second));
                }
            }
            level_.set("TileEntities", NbtTag::ofList(NbtList{entities.empty() ? NbtType::End : NbtType::Compound,
                                                              std::move(entities)}));
        }
        NbtCompound indices;
        std::vector<NbtTag> sections;
        for (const auto& section : sections_) {
            if (section == nullptr) {
                continue;
            }
            sections.push_back(NbtTag::ofCompound(section->write()));
            std::vector<std::int32_t> positions(section->update.begin(), section->update.end());
            indices.set(std::to_string(section->y), NbtTag::ofIntArray(std::move(positions)));
        }
        NbtCompound upgrade;
        upgrade.putByte("Sides", static_cast<std::int8_t>(sides_));
        upgrade.set("Indices", NbtTag::ofCompound(std::move(indices)));
        level_.set("UpgradeData", NbtTag::ofCompound(std::move(upgrade)));
        level_.set("Sections",
                   NbtTag::ofList(NbtList{sections.empty() ? NbtType::End : NbtType::Compound, std::move(sections)}));
    }

private:
    NbtCompound* blockEntityAt(int pos) {
        const auto found = blockEntities_.find(pos);
        return found == blockEntities_.end() ? nullptr : &found->second;
    }

    std::optional<NbtCompound> removeBlockEntity(int pos) {
        const auto found = blockEntities_.find(pos);
        if (found == blockEntities_.end()) {
            return std::nullopt;
        }
        NbtCompound entity = std::move(found->second);
        blockEntities_.erase(found);
        return entity;
    }

    Section* sectionOf(int pos) {
        const int index = pos >> 12;
        return index >= 0 && index < 16 ? sections_[static_cast<std::size_t>(index)].get() : nullptr;
    }

    const State* blockAt(int pos) {
        if (pos < 0 || pos > 65535) {
            return &constants().air;
        }
        Section* section = sectionOf(pos);
        return section == nullptr ? &constants().air : section->blockAt(pos & 4095);
    }

    void setBlock(int pos, const State* state) {
        if (pos >= 0 && pos <= 65535) {
            if (Section* section = sectionOf(pos); section != nullptr) {
                section->setBlock(pos & 4095, state);
            }
        }
    }

    static bool isSnow(const std::string& name) { return name == "minecraft:snow" || name == "minecraft:snow_layer"; }

    void fixSections() {
        const Constants& c = constants();
        for (const auto& section : sections_) {
            if (section == nullptr) {
                continue;
            }
            for (const auto& [blockId, positions] : section->toFix) {
                const int dy = section->y << 12;
                for (int rawPos : positions) {
                    const int pos = rawPos | dy;
                    const State* state = blockAt(pos);
                    switch (blockId) {
                    case 2:
                        if (state->name == "minecraft:grass_block" &&
                            isSnow(blockAt(relative(pos, Direction::Up))->name)) {
                            setBlock(pos, &c.snowyGrass);
                        }
                        break;
                    case 3:
                        if (state->name == "minecraft:podzol" && isSnow(blockAt(relative(pos, Direction::Up))->name)) {
                            setBlock(pos, &c.snowyPodzol);
                        }
                        break;
                    case 25:
                        if (auto entity = removeBlockEntity(pos); entity.has_value()) {
                            const int note =
                                static_cast<int>(std::clamp<std::int64_t>(whole(entity->find("note"), 0), 0, 24));
                            const std::string key =
                                std::string{entity->getBoolean("powered") ? "true" : "false"} + std::to_string(note);
                            const auto found = c.noteBlocks.find(key);
                            setBlock(pos, found == c.noteBlocks.end() ? &c.noteBlocks.at("false0") : &found->second);
                        }
                        break;
                    case 26:
                        if (NbtCompound* entity = blockEntityAt(pos); entity != nullptr) {
                            const int color = static_cast<int>(whole(entity->find("color"), 0));
                            if (color != 14 && color >= 0 && color < 16) {
                                const std::string key = propertyOf(*state, "facing") + propertyOf(*state, "occupied") +
                                                        propertyOf(*state, "part") + std::to_string(color);
                                if (const auto found = c.beds.find(key); found != c.beds.end()) {
                                    setBlock(pos, &found->second);
                                }
                            }
                        }
                        break;
                    case 64:
                    case 71:
                    case 193:
                    case 194:
                    case 195:
                    case 196:
                    case 197:
                        if (state->name.ends_with("_door") && propertyOf(*state, "half") == "lower") {
                            const int above = relative(pos, Direction::Up);
                            const State* upper = blockAt(above);
                            if (state->name == upper->name) {
                                const std::string facing = propertyOf(*state, "facing");
                                const std::string open = propertyOf(*state, "open");
                                const std::string hinge = convertedFromAlpha_ ? "left" : propertyOf(*upper, "hinge");
                                const std::string powered =
                                    convertedFromAlpha_ ? "false" : propertyOf(*upper, "powered");
                                const auto lowerFound =
                                    c.doors.find(state->name + facing + "lower" + hinge + open + powered);
                                const auto upperFound =
                                    c.doors.find(state->name + facing + "upper" + hinge + open + powered);
                                if (lowerFound != c.doors.end()) {
                                    setBlock(pos, &lowerFound->second);
                                }
                                if (upperFound != c.doors.end()) {
                                    setBlock(above, &upperFound->second);
                                }
                            }
                        }
                        break;
                    case 86:
                        if (state->name == "minecraft:carved_pumpkin") {
                            const std::string below = blockAt(relative(pos, Direction::Down))->name;
                            if (below == "minecraft:grass_block" || below == "minecraft:dirt") {
                                setBlock(pos, &c.pumpkin);
                            }
                        }
                        break;
                    case 110:
                        if (state->name == "minecraft:mycelium" &&
                            isSnow(blockAt(relative(pos, Direction::Up))->name)) {
                            setBlock(pos, &c.snowyMycelium);
                        }
                        break;
                    case 140:
                        if (auto entity = removeBlockEntity(pos); entity.has_value()) {
                            const std::string key =
                                std::string{entity->getString("Item")} + std::to_string(whole(entity->find("Data"), 0));
                            const auto found = c.flowerPots.find(key);
                            setBlock(pos,
                                     found == c.flowerPots.end() ? &c.flowerPots.at("minecraft:air0") : &found->second);
                        }
                        break;
                    case 144:
                        if (NbtCompound* entity = blockEntityAt(pos); entity != nullptr) {
                            const std::string type = std::to_string(whole(entity->find("SkullType"), 0));
                            const std::string facing = propertyOf(*state, "facing");
                            const std::string key = facing == "up" || facing == "down"
                                                        ? type + std::to_string(whole(entity->find("Rot"), 0))
                                                        : type + facing;
                            entity->remove("SkullType");
                            entity->remove("facing");
                            entity->remove("Rot");
                            const auto found = c.skulls.find(key);
                            setBlock(pos, found == c.skulls.end() ? &c.skulls.at("0north") : &found->second);
                        }
                        break;
                    case 175:
                        if (propertyOf(*state, "half") == "upper") {
                            const std::string below = blockAt(relative(pos, Direction::Down))->name;
                            if (below == "minecraft:sunflower") {
                                setBlock(pos, &c.upperSunflower);
                            } else if (below == "minecraft:lilac") {
                                setBlock(pos, &c.upperLilac);
                            } else if (below == "minecraft:tall_grass") {
                                setBlock(pos, &c.upperTallGrass);
                            } else if (below == "minecraft:large_fern") {
                                setBlock(pos, &c.upperLargeFern);
                            } else if (below == "minecraft:rose_bush") {
                                setBlock(pos, &c.upperRoseBush);
                            } else if (below == "minecraft:peony") {
                                setBlock(pos, &c.upperPeony);
                            }
                        }
                        break;
                    case 176:
                    case 177:
                        if (NbtCompound* entity = blockEntityAt(pos); entity != nullptr) {
                            const int color = static_cast<int>(whole(entity->find("Base"), 0));
                            if (color != 15 && color >= 0 && color < 16) {
                                const std::string key = propertyOf(*state, blockId == 176 ? "rotation" : "facing") +
                                                        "_" + std::to_string(color);
                                if (const auto found = c.banners.find(key); found != c.banners.end()) {
                                    setBlock(pos, &found->second);
                                }
                            }
                        }
                        break;
                    default:
                        break;
                    }
                }
            }
        }
    }

    NbtCompound& level_;
    int x_ = 0;
    int z_ = 0;
    int sides_ = 0;
    bool convertedFromAlpha_ = false;
    std::array<std::unique_ptr<Section>, 16> sections_{};
    std::map<int, NbtCompound> blockEntities_;
    std::vector<int> blockEntityOrder_;
};

// parity: `ChunkPalettedStorageFix`, blocks by id and data to palettes, with the fixes for blocks that look at others.
void palettedStorage(NbtCompound& chunk, const Context&) {
    NbtCompound* level = compoundAt(chunk, "Level");
    if (level == nullptr) {
        return;
    }
    const NbtTag* sections = level->find("Sections");
    if (sections == nullptr || sections->get<NbtList>() == nullptr) {
        return;
    }
    UpgradeChunk upgrade{*level};
    upgrade.write();
}

} // namespace

void addFixesFrom1200(std::vector<Fix>& fixes) {
    fixes.insert(fixes.end(), {
                                  {1451, Target::Chunk, {}, palettedStorage},
                              });
}

} // namespace basalt::metamorph::datafix
