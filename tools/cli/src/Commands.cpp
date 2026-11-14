#include "Commands.hpp"

#include <basalt/metamorph/region/BswConvert.hpp>
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
            if (const std::string refusal = datafix::upgrade(domain, *root, version); !refusal.empty()) {
                std::cerr << "chunk " << x << ' ' << z << ": " << refusal << '\n';
                ++counts.unreadable;
                continue;
            }
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

int convertFolders(const fs::path& world, std::string_view to, const fs::path& output) {
    namespace region = metamorph::region;
    const region::RegionFormat target = to == "anvil"    ? region::RegionFormat::Anvil
                                        : to == "linear" ? region::RegionFormat::Linear
                                                         : region::RegionFormat::Bsw;
    std::vector<fs::path> folders;
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(world)) {
        const bool chunkFolder = entry.path().filename() == "region";
        if (entry.is_directory() && (chunkFolder || to != "bsw") &&
            region::detectRegionFormat(entry.path().string()) != region::RegionFormat::Empty) {
            folders.push_back(entry.path());
        }
    }
    int status = 0;
    for (const fs::path& folder : folders) {
        const region::RegionFormat current = region::detectRegionFormat(folder.string());
        if (current == region::RegionFormat::Mixed || current == target) {
            std::cerr << folder.string() << ": " << region::nameOf(current) << ", skipped\n";
            status = current == region::RegionFormat::Mixed ? 1 : status;
            continue;
        }
        region::ConvertOptions options;
        options.progress = [](std::size_t done, std::size_t total) {
            if (done % 8 == 0 || done == total) {
                std::cout << "  " << done << "/" << total << " regions\r" << std::flush;
            }
        };
        const region::ConvertReport report = region::convertRegionFolder(
            folder.string(), (output / fs::relative(folder, world)).string(), target, options);
        std::cout << folder.string() << ": " << report.regions << " region(s), " << report.chunks
                  << " chunk(s) verified, " << report.mismatches << " mismatch(es), " << report.failed << " failed "
                  << report.error << '\n';
        status = report.ok() ? status : 1;
    }
    return status;
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
