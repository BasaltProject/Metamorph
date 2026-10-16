#include "Worlds.hpp"

#include <basalt/wire/io/FileIO.hpp>

#include <algorithm>

namespace fs = std::filesystem;

namespace basalt::metamorph::cli {
namespace {

bool isRegionName(const std::string& name) { return name.starts_with("r.") && name.find('.', 2) != std::string::npos; }

} // namespace

const char* extensionOf(Format format) { return format == Format::Anvil ? ".mca" : ".linear"; }

std::vector<RegionFile> regionFilesIn(const fs::path& world) {
    std::vector<RegionFile> files;
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(world)) {
        const fs::path& path = entry.path();
        const std::string extension = path.extension().string();
        if (!entry.is_regular_file() || !isRegionName(path.filename().string()) ||
            (extension != ".mca" && extension != ".linear")) {
            continue;
        }
        const std::string folder = path.parent_path().filename().string();
        std::optional<datafix::Domain> domain;
        if (folder == "region") {
            domain = datafix::Domain::Chunk;
        } else if (folder == "entities") {
            domain = datafix::Domain::Entities;
        }
        files.push_back(
            {path, fs::relative(path, world), extension == ".mca" ? Format::Anvil : Format::Linear, domain});
    }
    std::ranges::sort(files, {}, &RegionFile::relative);
    return files;
}

std::expected<region::LinearRegion, std::string> load(const RegionFile& file) {
    const auto bytes = wire::io::fileIO::readBytes(file.path.string());
    if (!bytes.has_value()) {
        return std::unexpected("cannot read " + file.path.string());
    }
    if (file.format == Format::Linear) {
        auto read = region::LinearRegion::read(*bytes);
        if (!read.has_value()) {
            return std::unexpected(file.path.string() + ": " + std::string{describe(read.error())});
        }
        return std::move(*read);
    }
    auto read = region::anvilRegion::read(*bytes);
    if (!read.has_value()) {
        return std::unexpected(file.path.string() + ": " + std::string{describe(read.error())});
    }
    return std::move(*read);
}

std::expected<void, std::string> store(const region::LinearRegion& region, Format format, const fs::path& path) {
    fs::create_directories(path.parent_path());
    if (format == Format::Linear) {
        if (!region.writeTo(path.string())) {
            return std::unexpected("cannot write " + path.string());
        }
        return {};
    }
    const auto bytes = region::anvilRegion::write(region);
    if (!bytes.has_value()) {
        return std::unexpected(path.string() + ": " + std::string{describe(bytes.error())});
    }
    if (!wire::io::fileIO::writeBytes(path.string(), *bytes)) {
        return std::unexpected("cannot write " + path.string());
    }
    return {};
}

} // namespace basalt::metamorph::cli
