#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <basalt/metamorph/region/BswRegion.hpp>

#include <doctest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace basalt::metamorph::region;

namespace {

[[nodiscard]] std::vector<std::uint8_t> blob(std::size_t size, std::uint8_t seed) {
    std::vector<std::uint8_t> bytes(size);
    for (std::size_t at = 0; at < size; ++at) {
        bytes[at] = static_cast<std::uint8_t>((at / 7) + seed);
    }
    return bytes;
}

[[nodiscard]] std::string fresh(std::string_view name) {
    const std::filesystem::path directory = std::filesystem::path{BASALT_TEST_SCRATCH_DIR} / name;
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory);
    return (directory / "r.0.0.bsw").string();
}

} // namespace

TEST_CASE("bsw round trips chunks in every unit size and reuses space") {
    for (unsigned shift = 0; shift <= 2; ++shift) {
        BswOptions options;
        options.unitShift = shift;
        options.syncOnCommit = false;
        const std::string path = fresh("roundtrip" + std::to_string(shift));
        BswScratch scratch;
        auto region = BswRegion::open(path, options, true);
        REQUIRE(region != nullptr);
        const auto a = blob(5000, 1);
        const auto b = blob(9000, 2);
        const std::vector<BswPut> puts{{3, 4, a, 10}, {4, 4, b, 11}};
        REQUIRE(region->write(puts, scratch));
        const auto bigger = blob(20000, 3);
        const std::vector<BswPut> edit{{3, 4, bigger, 12}};
        REQUIRE(region->write(edit, scratch));
        const std::vector<BswPut> back{{3, 4, a, 13}};
        REQUIRE(region->write(back, scratch));
        std::vector<std::uint8_t> out;
        REQUIRE(region->read(3, 4, scratch, out));
        CHECK(out == a);
        REQUIRE(region->read(4, 4, scratch, out));
        CHECK(out == b);
        CHECK_FALSE(region->hasChunk(0, 0));
        const std::vector<BswPut> remove{{4, 4, {}, 0}};
        REQUIRE(region->write(remove, scratch));
        CHECK_FALSE(region->hasChunk(4, 4));
        REQUIRE(region->compact(scratch));
        region.reset();
        region = BswRegion::open(path, options, false);
        REQUIRE(region != nullptr);
        REQUIRE(region->read(3, 4, scratch, out));
        CHECK(out == a);
    }
}

TEST_CASE("bsw falls back to the previous index when the newest slot is torn") {
    BswOptions options;
    const std::string path = fresh("torn");
    BswScratch scratch;
    auto region = BswRegion::open(path, options, true);
    REQUIRE(region != nullptr);
    const auto a = blob(4000, 5);
    const auto b = blob(4000, 6);
    REQUIRE(region->write(std::vector<BswPut>{{1, 1, a, 1}}, scratch));
    REQUIRE(region->write(std::vector<BswPut>{{1, 1, b, 2}}, scratch));
    const std::uint64_t generation = region->stats().generation;
    region.reset();
    {
        std::fstream file{path, std::ios::in | std::ios::out | std::ios::binary};
        const std::size_t newestSlot = generation % 2 == 1 ? 4096 : 4096 + 36864;
        file.seekp(static_cast<std::streamoff>(newestSlot + 100));
        const char junk = 0x5A;
        file.write(&junk, 1);
    }
    region = BswRegion::open(path, options, false);
    REQUIRE(region != nullptr);
    std::vector<std::uint8_t> out;
    REQUIRE(region->read(1, 1, scratch, out));
    CHECK(out == a);
}
