#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <basalt/metamorph/region/ChunkBlob.hpp>
#include <basalt/wire/nbt/NbtIO.hpp>

#include <doctest.h>

#include <cstdint>
#include <cstring>
#include <vector>

using namespace basalt::metamorph::region;
using namespace basalt::wire::nbt;

namespace {

constexpr std::int32_t kVersion = 5023;

[[nodiscard]] NbtTag blockEntry(std::string_view id, std::vector<std::pair<std::string, std::string>> properties = {}) {
    NbtCompound entry;
    entry.putString("id", std::string{id});
    if (!properties.empty()) {
        NbtCompound tag;
        for (const auto& [name, value] : properties) {
            tag.putString(name, value);
        }
        entry.set("properties", NbtTag::ofCompound(std::move(tag)));
    }
    return NbtTag::ofCompound(std::move(entry));
}

[[nodiscard]] NbtCompound container(NbtList palette, std::vector<std::uint32_t> indices, int bits) {
    NbtCompound tag;
    const std::size_t entries = palette.size();
    tag.set("palette", NbtTag::ofList(std::move(palette)));
    if (entries > 1) {
        const std::size_t perLong = 64 / static_cast<std::size_t>(bits);
        std::vector<std::int64_t> longs((indices.size() + perLong - 1) / perLong, 0);
        for (std::size_t i = 0; i < indices.size(); ++i) {
            longs[i / perLong] |= static_cast<std::int64_t>(static_cast<std::uint64_t>(indices[i])
                                                            << ((i % perLong) * static_cast<std::size_t>(bits)));
        }
        tag.putLongArray("data", std::move(longs));
    }
    return tag;
}

[[nodiscard]] NbtCompound sampleChunk() {
    std::vector<NbtTag> sections;
    NbtCompound air;
    air.putByte("Y", -4);
    air.set("block_states",
            NbtTag::ofCompound(container(NbtList{NbtType::Compound, {blockEntry("minecraft:air")}}, {}, 0)));
    air.set("biomes",
            NbtTag::ofCompound(container(NbtList{NbtType::String, {NbtTag::ofString("minecraft:plains")}}, {}, 0)));
    sections.push_back(NbtTag::ofCompound(std::move(air)));

    std::vector<std::uint32_t> blocks(4096);
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        blocks[i] = static_cast<std::uint32_t>((i * 7 + i / 16) % 5);
    }
    std::vector<std::uint32_t> biomes(64);
    for (std::size_t i = 0; i < biomes.size(); ++i) {
        biomes[i] = static_cast<std::uint32_t>(i % 2);
    }
    NbtCompound mixed;
    mixed.putByte("Y", -3);
    mixed.set("block_states",
              NbtTag::ofCompound(
                  container(NbtList{NbtType::Compound,
                                    {blockEntry("minecraft:stone"), blockEntry("minecraft:dirt"),
                                     blockEntry("minecraft:oak_stairs", {{"half", "bottom"}, {"facing", "north"}}),
                                     blockEntry("minecraft:air"), blockEntry("minecraft:water", {{"level", "0"}})}},
                            blocks, 4)));
    mixed.set("biomes", NbtTag::ofCompound(container(
                            NbtList{NbtType::String,
                                    {NbtTag::ofString("minecraft:plains"), NbtTag::ofString("minecraft:desert")}},
                            biomes, 1)));
    mixed.putByteArray("SkyLight", std::vector<std::int8_t>(2048, 15));
    sections.push_back(NbtTag::ofCompound(std::move(mixed)));

    NbtCompound root;
    root.putInt("DataVersion", kVersion);
    root.putInt("xPos", -7);
    root.putInt("yPos", -4);
    root.putInt("zPos", 12);
    root.putLong("InhabitedTime", 123456789012LL);
    root.putString("Status", "minecraft:full");
    root.set("sections", NbtTag::ofList(NbtList{NbtType::Compound, std::move(sections)}));
    root.set("block_entities", NbtTag::ofList(NbtList{}));
    NbtCompound heightmaps;
    heightmaps.putLongArray("MOTION_BLOCKING", {1, 2, 3});
    root.set("Heightmaps", NbtTag::ofCompound(std::move(heightmaps)));
    return root;
}

[[nodiscard]] std::vector<std::uint8_t> bytesOf(const NbtCompound& root) {
    return nbtIO::write(NbtTag::ofCompound(root));
}

} // namespace

TEST_CASE("a chunk survives NBT to blob to NBT with every section cell and extra key") {
    const NbtCompound root = sampleChunk();
    const std::optional<std::vector<std::uint8_t>> blob = chunkBlob::fromNbt(root, kVersion);
    REQUIRE(blob.has_value());
    CHECK((*blob)[0] == chunkBlob::kMagic);
    CHECK((*blob)[1] == chunkBlob::kVersion);
    std::uint16_t sections = 0;
    std::memcpy(&sections, blob->data() + 2, sizeof sections);
    CHECK(sections == 2);
    std::int32_t version = 0;
    std::memcpy(&version, blob->data() + 4, sizeof version);
    CHECK(version == kVersion);

    const std::optional<NbtCompound> back = chunkBlob::toNbt(*blob);
    REQUIRE(back.has_value());
    CHECK(chunkBlob::equivalent(bytesOf(root), bytesOf(*back)));
    CHECK(back->getInt("zPos") == 12);
    CHECK(back->getLong("InhabitedTime") == 123456789012LL);
    CHECK(back->getCompound("Heightmaps") != nullptr);
    const NbtCompound* second = (*back->getList("sections"))[1].get<NbtCompound>();
    CHECK(second->find("SkyLight") != nullptr);
}

TEST_CASE("equivalence notices a changed cell and ignores key order") {
    NbtCompound changed = sampleChunk();
    NbtList sections = *changed.getList("sections");
    NbtCompound mixed = *sections[1].get<NbtCompound>();
    NbtCompound states = *mixed.getCompound("block_states");
    std::vector<std::int64_t> longs(states.getLongArray("data").begin(), states.getLongArray("data").end());
    longs[3] ^= 1;
    states.putLongArray("data", longs);
    mixed.set("block_states", NbtTag::ofCompound(states));
    sections[1] = NbtTag::ofCompound(mixed);
    changed.set("sections", NbtTag::ofList(sections));
    CHECK_FALSE(chunkBlob::equivalent(bytesOf(sampleChunk()), bytesOf(changed)));

    NbtCompound reordered;
    const NbtCompound original = sampleChunk();
    for (std::size_t i = original.size(); i-- > 0;) {
        reordered.set(original.names()[i], original.values()[i]);
    }
    CHECK(chunkBlob::equivalent(bytesOf(original), bytesOf(reordered)));
}

TEST_CASE("a chunk of another version or with a wide palette is stored as NBT") {
    NbtCompound old = sampleChunk();
    old.putInt("DataVersion", kVersion - 1);
    CHECK_FALSE(chunkBlob::fromNbt(old, kVersion).has_value());
    const std::vector<std::uint8_t> oldBytes = bytesOf(old);
    CHECK(chunkBlob::encode(oldBytes, kVersion) == oldBytes);
    CHECK(chunkBlob::decode(oldBytes) == oldBytes);

    const std::vector<std::uint8_t> current = chunkBlob::encode(bytesOf(sampleChunk()), kVersion);
    CHECK(chunkBlob::isBlob(current));
    CHECK(chunkBlob::equivalent(bytesOf(sampleChunk()), chunkBlob::decode(current)));
}

TEST_CASE("a damaged blob is refused rather than half read") {
    std::vector<std::uint8_t> blob = *chunkBlob::fromNbt(sampleChunk(), kVersion);
    blob.resize(blob.size() - 5);
    CHECK_FALSE(chunkBlob::toNbt(blob).has_value());
    CHECK(chunkBlob::decode(blob).empty());
}
