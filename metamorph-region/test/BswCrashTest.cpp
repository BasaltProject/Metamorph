#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <basalt/metamorph/region/BswRegion.hpp>

#include <doctest.h>

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

using namespace basalt::metamorph::region;

namespace {

constexpr std::int32_t kSide = 8;
constexpr std::uint64_t kSlotBytes = 36864;

using Model = std::map<std::pair<int, int>, std::vector<std::uint8_t>>;

[[nodiscard]] std::vector<std::uint8_t> payload(std::uint64_t seed, std::size_t size) {
    std::mt19937_64 random{seed};
    std::vector<std::uint8_t> bytes(size);
    for (std::size_t at = 0; at < size; ++at) {
        bytes[at] = static_cast<std::uint8_t>(at % 5 == 0 ? random() : at / 3);
    }
    return bytes;
}

[[nodiscard]] bool compactsAt(std::uint64_t generation) { return generation % 13 == 0; }

struct Step {
    std::vector<std::vector<std::uint8_t>> bytes;
    std::vector<BswPut> puts;
};

[[nodiscard]] Step stepFor(std::uint64_t generation) {
    std::mt19937_64 random{generation * 7919 + 1};
    Step step;
    const int count = 1 + static_cast<int>(random() % 6);
    step.bytes.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const auto x = static_cast<std::int32_t>(random() % kSide);
        const auto z = static_cast<std::int32_t>(random() % kSide);
        const bool remove = random() % 7 == 0;
        step.bytes.push_back(remove ? std::vector<std::uint8_t>{} : payload(random(), 200 + (random() % 40000)));
        step.puts.push_back({x, z, step.bytes.back(), static_cast<std::int32_t>(generation)});
    }
    return step;
}

void apply(Model& model, const Step& step) {
    for (const BswPut& put : step.puts) {
        if (put.nbt.empty()) {
            model.erase({put.localX, put.localZ});
        } else {
            model[{put.localX, put.localZ}].assign(put.nbt.begin(), put.nbt.end());
        }
    }
}

void expectModel(BswRegion& region, const Model& model, BswScratch& scratch) {
    std::vector<std::uint8_t> out;
    for (std::int32_t z = 0; z < kSide; ++z) {
        for (std::int32_t x = 0; x < kSide; ++x) {
            const auto found = model.find({x, z});
            if (found == model.end()) {
                REQUIRE_FALSE(region.hasChunk(x, z));
            } else {
                REQUIRE(region.read(x, z, scratch, out) == BswRead::Ok);
                REQUIRE(out == found->second);
            }
        }
    }
    REQUIRE(region.countCorrupt(scratch) == 0);
}

[[nodiscard]] std::string fresh(std::string_view name) {
    const std::filesystem::path directory = std::filesystem::path{BASALT_TEST_SCRATCH_DIR} / name;
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory);
    return (directory / "r.0.0.bsw").string();
}

[[noreturn]] void childLoop(const std::string& path) {
    BswOptions options;
    options.syncOnCommit = false;
    BswScratch scratch;
    auto region = BswRegion::open(path, options, true);
    if (region == nullptr) {
        _exit(3);
    }
    for (;;) {
        const std::uint64_t generation = region->stats().generation;
        if (compactsAt(generation)) {
            region->compact(scratch);
        } else {
            const Step step = stepFor(generation);
            region->write(step.puts, scratch);
        }
    }
}

} // namespace

TEST_CASE("bsw survives a process killed at random points of its commits") {
    const std::string path = fresh("crash");
    BswScratch scratch;
    std::mt19937 random{12345};
    std::uint64_t previous = 0;
    const int iterations = 600;
    for (int iteration = 0; iteration < iterations; ++iteration) {
        const pid_t child = fork();
        REQUIRE(child >= 0);
        if (child == 0) {
            childLoop(path);
        }
        timespec pause{0, static_cast<long>(random() % 6'000'000)};
        nanosleep(&pause, nullptr);
        kill(child, SIGKILL);
        int status = 0;
        waitpid(child, &status, 0);

        if (!std::filesystem::exists(path)) {
            REQUIRE(previous == 0);
            continue;
        }
        BswOptions options;
        auto region = BswRegion::open(path, options, false);
        REQUIRE(region != nullptr);
        const std::uint64_t generation = region->stats().generation;
        REQUIRE(generation >= previous);
        previous = generation;
        Model model;
        for (std::uint64_t at = 1; at < generation; ++at) {
            if (!compactsAt(at)) {
                apply(model, stepFor(at));
            }
        }
        expectModel(*region, model, scratch);
    }
    CHECK(previous > 10);
}

TEST_CASE("bsw opens as the old or the new state when the newest index slot is torn at any byte") {
    const std::string path = fresh("torn-any");
    BswOptions options;
    options.syncOnCommit = false;
    BswScratch scratch;
    Model oldModel;
    Model newModel;
    std::string before;
    std::uint64_t newest = 0;
    {
        auto region = BswRegion::open(path, options, true);
        REQUIRE(region != nullptr);
        for (std::uint64_t generation = 1; generation <= 6; ++generation) {
            const Step step = stepFor(generation);
            if (generation == 6) {
                region.reset();
                std::ifstream in{path, std::ios::binary};
                before.assign(std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{});
                oldModel = newModel;
                region = BswRegion::open(path, options, false);
            }
            REQUIRE(region->write(step.puts, scratch));
            apply(newModel, step);
        }
        newest = region->stats().generation;
    }
    std::string after;
    {
        std::ifstream in{path, std::ios::binary};
        after.assign(std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{});
    }
    const std::uint64_t slotAt = 4096 + ((newest % 2 == 1) ? 0 : kSlotBytes);
    std::mt19937 random{99};
    for (int attempt = 0; attempt < 300; ++attempt) {
        std::string torn = after;
        const std::size_t cut = random() % kSlotBytes;
        torn.replace(slotAt + cut, kSlotBytes - cut, before, slotAt + cut, kSlotBytes - cut);
        const std::string copy = fresh("torn-any-copy");
        {
            std::ofstream out{copy, std::ios::binary};
            out.write(torn.data(), static_cast<std::streamsize>(torn.size()));
        }
        auto region = BswRegion::open(copy, options, false);
        REQUIRE(region != nullptr);
        const bool isNew = region->stats().generation == newest;
        expectModel(*region, isNew ? newModel : oldModel, scratch);
    }
}

TEST_CASE("bsw reports a flipped bit in a blob as corruption and keeps the other chunks") {
    const std::string path = fresh("flip");
    BswOptions options;
    BswScratch scratch;
    const auto a = payload(1, 9000);
    const auto b = payload(2, 9000);
    {
        auto region = BswRegion::open(path, options, true);
        REQUIRE(region != nullptr);
        REQUIRE(region->write(std::vector<BswPut>{{0, 0, a, 1}, {1, 0, b, 1}}, scratch));
    }
    {
        std::fstream file{path, std::ios::in | std::ios::out | std::ios::binary};
        file.seekg(0, std::ios::end);
        file.seekp(77824 + 5);
        char byte = 0;
        file.seekg(77824 + 5);
        file.read(&byte, 1);
        byte = static_cast<char>(byte ^ 0x04);
        file.seekp(77824 + 5);
        file.write(&byte, 1);
    }
    auto region = BswRegion::open(path, options, false);
    REQUIRE(region != nullptr);
    std::vector<std::uint8_t> out;
    CHECK(region->read(0, 0, scratch, out) == BswRead::Corrupt);
    CHECK(region->read(1, 0, scratch, out) == BswRead::Ok);
    CHECK(out == b);
    CHECK(region->countCorrupt(scratch) == 1);
}

TEST_CASE("bsw reuses freed space and compaction keeps every chunk") {
    const std::string path = fresh("compact");
    BswOptions options;
    options.syncOnCommit = false;
    BswScratch scratch;
    auto region = BswRegion::open(path, options, true);
    REQUIRE(region != nullptr);
    Model model;
    for (std::uint64_t generation = 1; generation <= 200; ++generation) {
        const Step step = stepFor(generation * 3 + 1);
        REQUIRE(region->write(step.puts, scratch));
        apply(model, step);
    }
    expectModel(*region, model, scratch);
    const BswStats before = region->stats();
    CHECK(before.freeBytes > 0);
    REQUIRE(region->compact(scratch));
    const BswStats after = region->stats();
    CHECK(after.freeBytes == 0);
    CHECK(after.fileBytes < before.fileBytes);
    expectModel(*region, model, scratch);
    region.reset();
    region = BswRegion::open(path, options, false);
    REQUIRE(region != nullptr);
    expectModel(*region, model, scratch);
}
