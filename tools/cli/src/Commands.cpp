#include "Commands.hpp"

#include <basalt/wire/nbt/NbtIO.hpp>

#include <iostream>

namespace fs = std::filesystem;

namespace basalt::metamorph::cli {
namespace {

struct Counts {
    int chunks = 0;
    int upgraded = 0;
    int unreadable = 0;
};

void upgradeChunks(region::LinearRegion& region, datafix::Domain domain, Counts& counts) {
    for (std::int32_t z = 0; z < region::LinearRegion::kDimension; ++z) {
        for (std::int32_t x = 0; x < region::LinearRegion::kDimension; ++x) {
            if (!region.hasChunkAt(x, z)) {
                continue;
            }
            ++counts.chunks;
            const auto blob = region.chunkAt(x, z);
            auto parsed = wire::nbt::nbtIO::read(blob);
            wire::nbt::NbtCompound* root = parsed.has_value() ? parsed->tag.get<wire::nbt::NbtCompound>() : nullptr;
            if (root == nullptr) {
                ++counts.unreadable;
                continue;
            }
            const std::int32_t version = root->getInt("DataVersion");
            if (version <= 0 || version >= datafix::kCurrentVersion) {
                continue;
            }
            datafix::upgrade(domain, *root, version);
            root->putInt("DataVersion", datafix::kCurrentVersion);
            region.setChunk(x, z, wire::nbt::nbtIO::write(parsed->tag, parsed->name), region.timestampAt(x, z));
            ++counts.upgraded;
        }
    }
}

} // namespace

int convert(const fs::path& world, Format format, const fs::path& output) {
    int failed = 0;
    int written = 0;
    for (const RegionFile& file : regionFilesIn(world)) {
        auto region = load(file);
        if (!region.has_value()) {
            std::cerr << region.error() << '\n';
            ++failed;
            continue;
        }
        fs::path target = output / file.relative;
        target.replace_extension(extensionOf(format));
        if (const auto stored = store(*region, format, target); !stored.has_value()) {
            std::cerr << stored.error() << '\n';
            ++failed;
            continue;
        }
        ++written;
    }
    std::cout << written << " region(s) written, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}

int upgrade(const fs::path& world, const fs::path& output) {
    int failed = 0;
    Counts counts;
    for (const RegionFile& file : regionFilesIn(world)) {
        auto region = load(file);
        if (!region.has_value()) {
            std::cerr << region.error() << '\n';
            ++failed;
            continue;
        }
        if (file.domain.has_value()) {
            upgradeChunks(*region, *file.domain, counts);
        }
        if (const auto stored = store(*region, file.format, output / file.relative); !stored.has_value()) {
            std::cerr << stored.error() << '\n';
            ++failed;
        }
    }
    std::cout << counts.chunks << " chunk(s) read, " << counts.upgraded << " upgraded to data version "
              << datafix::kCurrentVersion << ", " << counts.unreadable << " unreadable, " << failed
              << " region(s) failed\n";
    return failed == 0 && counts.unreadable == 0 ? 0 : 1;
}

} // namespace basalt::metamorph::cli
