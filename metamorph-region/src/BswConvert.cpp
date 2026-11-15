#include <basalt/metamorph/datafix/DataFixer.hpp>
#include <basalt/metamorph/region/AnvilRegion.hpp>
#include <basalt/metamorph/region/BswConvert.hpp>
#include <basalt/metamorph/region/BswRegion.hpp>
#include <basalt/metamorph/region/ChunkBlob.hpp>
#include <basalt/wire/io/FileIO.hpp>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace basalt::metamorph::region {
namespace {

constexpr std::size_t kSampleChunks = 192;
constexpr std::size_t kDictionaryBytes = 112640;

struct Located {
    std::string path;
    std::int32_t regionX = 0;
    std::int32_t regionZ = 0;
};

[[nodiscard]] bool parseName(const std::string& name, std::int32_t& x, std::int32_t& z, std::string& extension) {
    if (!name.starts_with("r.")) {
        return false;
    }
    const std::size_t second = name.find('.', 2);
    const std::size_t third = second == std::string::npos ? second : name.find('.', second + 1);
    if (third == std::string::npos) {
        return false;
    }
    const auto number = [&](std::size_t from, std::size_t to, std::int32_t& out) {
        const auto [end, code] = std::from_chars(name.data() + from, name.data() + to, out);
        return code == std::errc{} && end == name.data() + to;
    };
    extension = name.substr(third);
    return number(2, second, x) && number(second + 1, third, z);
}

[[nodiscard]] RegionFormat formatOfExtension(const std::string& extension) {
    if (extension == ".mca") {
        return RegionFormat::Anvil;
    }
    if (extension == ".linear") {
        return RegionFormat::Linear;
    }
    if (extension == ".bsw") {
        return RegionFormat::Bsw;
    }
    return RegionFormat::Empty;
}

[[nodiscard]] std::vector<Located> listRegions(const std::string& folder, RegionFormat format) {
    std::vector<Located> found;
    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder, error)) {
        Located at;
        std::string extension;
        if (entry.is_regular_file() && parseName(entry.path().filename().string(), at.regionX, at.regionZ, extension) &&
            formatOfExtension(extension) == format) {
            at.path = entry.path().string();
            found.push_back(std::move(at));
        }
    }
    std::ranges::sort(found, {}, &Located::path);
    return found;
}

struct Loaded {
    bool ok = false;
    LinearRegion region;
};

[[nodiscard]] Loaded loadRegion(const Located& at, RegionFormat format,
                                const std::shared_ptr<const BswDictionary>& dictionary) {
    Loaded loaded;
    if (format == RegionFormat::Bsw) {
        BswOptions options;
        options.dictionary = dictionary;
        const std::unique_ptr<BswRegion> region = BswRegion::open(at.path, options, false);
        if (region == nullptr) {
            return loaded;
        }
        BswScratch scratch;
        std::vector<std::uint8_t> bytes;
        for (std::int32_t z = 0; z < LinearRegion::kDimension; ++z) {
            for (std::int32_t x = 0; x < LinearRegion::kDimension; ++x) {
                if (!region->hasChunk(x, z)) {
                    continue;
                }
                if (region->read(x, z, scratch, bytes) != BswRead::Ok) {
                    return loaded;
                }
                std::vector<std::uint8_t> nbt = chunkBlob::decode(bytes);
                if (nbt.empty()) {
                    return loaded;
                }
                loaded.region.setChunk(x, z, std::move(nbt), region->timestampOf(x, z));
            }
        }
        loaded.ok = true;
        return loaded;
    }
    const auto bytes = wire::io::fileIO::readBytes(at.path);
    if (!bytes.has_value()) {
        return loaded;
    }
    if (format == RegionFormat::Linear) {
        auto read = LinearRegion::read(*bytes);
        if (read.has_value()) {
            loaded.region = std::move(*read);
            loaded.ok = true;
        }
        return loaded;
    }
    auto read = anvilRegion::read(*bytes);
    if (read.has_value()) {
        loaded.region = std::move(*read);
        loaded.ok = true;
    }
    return loaded;
}

[[nodiscard]] bool storeRegion(const LinearRegion& region, RegionFormat format, const std::string& path,
                               const std::shared_ptr<const BswDictionary>& dictionary, int level) {
    if (format == RegionFormat::Linear) {
        return region.writeTo(path);
    }
    if (format == RegionFormat::Anvil) {
        const auto bytes = anvilRegion::write(region);
        return bytes.has_value() && wire::io::fileIO::writeBytes(path, *bytes);
    }
    BswOptions options;
    options.level = level;
    options.dictionary = dictionary;
    options.codec = dictionary != nullptr ? BswCodec::ZstdDict : BswCodec::Zstd;
    std::error_code ignored;
    fs::remove(path, ignored);
    const std::unique_ptr<BswRegion> out = BswRegion::open(path, options, true);
    if (out == nullptr) {
        return false;
    }
    std::vector<BswPut> puts;
    std::vector<std::vector<std::uint8_t>> stored;
    stored.reserve(LinearRegion::kSlotCount);
    for (std::int32_t z = 0; z < LinearRegion::kDimension; ++z) {
        for (std::int32_t x = 0; x < LinearRegion::kDimension; ++x) {
            if (region.hasChunkAt(x, z)) {
                stored.push_back(chunkBlob::encode(region.chunkAt(x, z), datafix::kCurrentVersion));
                puts.push_back({x, z, stored.back(), region.timestampAt(x, z)});
            }
        }
    }
    BswScratch scratch;
    return puts.empty() || out->write(puts, scratch);
}

[[nodiscard]] std::size_t countMismatches(const LinearRegion& expected, const LinearRegion& actual) {
    std::size_t bad = 0;
    for (std::int32_t z = 0; z < LinearRegion::kDimension; ++z) {
        for (std::int32_t x = 0; x < LinearRegion::kDimension; ++x) {
            const bool has = expected.hasChunkAt(x, z);
            if (has != actual.hasChunkAt(x, z)) {
                ++bad;
            } else if (has) {
                if (!chunkBlob::equivalent(expected.chunkAt(x, z), actual.chunkAt(x, z)) ||
                    expected.timestampAt(x, z) != actual.timestampAt(x, z)) {
                    ++bad;
                }
            }
        }
    }
    return bad;
}

[[nodiscard]] std::shared_ptr<const BswDictionary> trainFrom(const std::vector<Located>& sources, RegionFormat from,
                                                             const ConvertOptions& options) {
    std::vector<std::vector<std::uint8_t>> owned;
    for (const Located& at : sources) {
        const Loaded loaded = loadRegion(at, from, nullptr);
        if (!loaded.ok) {
            continue;
        }
        for (std::int32_t z = 0; z < LinearRegion::kDimension && owned.size() < kSampleChunks; ++z) {
            for (std::int32_t x = 0; x < LinearRegion::kDimension && owned.size() < kSampleChunks; ++x) {
                if (loaded.region.hasChunkAt(x, z)) {
                    owned.push_back(chunkBlob::encode(loaded.region.chunkAt(x, z), datafix::kCurrentVersion));
                }
            }
        }
        if (owned.size() >= kSampleChunks) {
            break;
        }
    }
    std::vector<std::span<const std::uint8_t>> views(owned.begin(), owned.end());
    return BswDictionary::train(views, kDictionaryBytes, options.level);
}

} // namespace

std::string_view nameOf(RegionFormat format) {
    switch (format) {
    case RegionFormat::Anvil:
        return "anvil";
    case RegionFormat::Linear:
        return "linear";
    case RegionFormat::Bsw:
        return "bsw";
    case RegionFormat::Mixed:
        return "mixed";
    case RegionFormat::Empty:
        break;
    }
    return "empty";
}

const char* extensionOf(RegionFormat format) {
    return format == RegionFormat::Anvil ? ".mca" : format == RegionFormat::Linear ? ".linear" : ".bsw";
}

bool hasRegionFiles(const std::string& regionFolder, RegionFormat format) {
    return !listRegions(regionFolder, format).empty();
}

RegionFormat detectRegionFormat(const std::string& regionFolder) {
    RegionFormat found = RegionFormat::Empty;
    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(regionFolder, error)) {
        std::int32_t x = 0;
        std::int32_t z = 0;
        std::string extension;
        if (!entry.is_regular_file() || !parseName(entry.path().filename().string(), x, z, extension)) {
            continue;
        }
        const RegionFormat format = formatOfExtension(extension);
        if (format == RegionFormat::Empty) {
            continue;
        }
        if (found != RegionFormat::Empty && found != format) {
            return RegionFormat::Mixed;
        }
        found = format;
    }
    return found;
}

ConvertReport convertRegionFolder(const std::string& source, const std::string& target, RegionFormat to,
                                  const ConvertOptions& options) {
    ConvertReport report;
    const RegionFormat from = detectRegionFormat(source);
    if (from == RegionFormat::Mixed || from == RegionFormat::Empty || to == RegionFormat::Mixed ||
        to == RegionFormat::Empty) {
        report.error = from == RegionFormat::Mixed ? "the source folder mixes region formats" : "nothing to convert";
        return report;
    }
    if (from == to) {
        report.error = "the source is already " + std::string{nameOf(to)};
        return report;
    }
    std::error_code error;
    fs::create_directories(target, error);
    if (error || !fs::is_empty(target, error)) {
        report.error = target + " is not an empty folder";
        return report;
    }
    const std::vector<Located> sources = listRegions(source, from);

    std::shared_ptr<const BswDictionary> sourceDictionary;
    if (from == RegionFormat::Bsw) {
        sourceDictionary = BswDictionary::load(source + "/" + kBswDictionaryName, options.level);
    }
    std::shared_ptr<const BswDictionary> targetDictionary;
    if (to == RegionFormat::Bsw && options.dictionary) {
        targetDictionary = trainFrom(sources, from, options);
        if (targetDictionary != nullptr && !targetDictionary->save(target + "/" + kBswDictionaryName)) {
            report.error = "cannot write the dictionary";
            return report;
        }
    }

    std::atomic<std::size_t> next{0};
    std::atomic<std::size_t> done{0};
    std::atomic<std::size_t> chunks{0};
    std::atomic<std::size_t> mismatches{0};
    std::atomic<std::size_t> failed{0};
    std::mutex callbackMutex;
    const auto work = [&] {
        for (std::size_t at = next++; at < sources.size(); at = next++) {
            const Located& from_ = sources[at];
            Loaded loaded = loadRegion(from_, from, sourceDictionary);
            const std::string out =
                target + "/r." + std::to_string(from_.regionX) + "." + std::to_string(from_.regionZ) + extensionOf(to);
            bool good = loaded.ok;
            if (good) {
                if (options.prepare) {
                    const std::lock_guard<std::mutex> lock{callbackMutex};
                    options.prepare(from_.regionX, from_.regionZ, loaded.region);
                }
                good = storeRegion(loaded.region, to, out, targetDictionary, options.level);
            }
            if (good) {
                const Loaded again = loadRegion({out, from_.regionX, from_.regionZ}, to, targetDictionary);
                good = again.ok;
                if (good) {
                    mismatches += countMismatches(loaded.region, again.region);
                    for (std::int32_t i = 0; i < LinearRegion::kDimension * LinearRegion::kDimension; ++i) {
                        chunks += loaded.region.hasChunkAt(i & 31, i >> 5) ? 1 : 0;
                    }
                }
            }
            if (!good) {
                ++failed;
            }
            const std::size_t finished = ++done;
            if (options.progress) {
                const std::lock_guard<std::mutex> lock{callbackMutex};
                options.progress(finished, sources.size());
            }
        }
    };
    const unsigned count =
        std::max(1U, options.threads != 0 ? options.threads : std::thread::hardware_concurrency() / 2);
    {
        std::vector<std::jthread> pool;
        for (unsigned i = 0; i < count; ++i) {
            pool.emplace_back(work);
        }
    }
    report.regions = sources.size();
    report.chunks = chunks;
    report.mismatches = mismatches;
    report.failed = failed;
    return report;
}

} // namespace basalt::metamorph::region
