#include "BswFile.hpp"
#include "BswInternal.hpp"

#include <basalt/metamorph/region/BswRegion.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <map>
#include <shared_mutex>
#include <system_error>
#include <utility>
#include <zstd.h>

#if defined(__x86_64__)
#include <cpuid.h>
#include <nmmintrin.h>
#endif

namespace basalt::metamorph::region {
namespace {

constexpr std::array<char, 8> kMagic{'B', 'S', 'W', 'R', 'E', 'G', '0', '1'};
constexpr std::uint64_t kSlotMagic = 0x315453574C535742ULL;
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kPage = 4096;
constexpr std::size_t kEntrySize = 32;
constexpr std::size_t kSlotHeaderSize = 32;
constexpr std::size_t kSlotBytes = ((kSlotHeaderSize + BswRegion::kSlotCount * kEntrySize) + kPage - 1) / kPage * kPage;
constexpr std::uint64_t kSlotOffset[2] = {kPage, kPage + kSlotBytes};
constexpr std::uint64_t kDataStart = kPage + (2 * kSlotBytes);
constexpr std::uint64_t kBlobAlign = 64;

[[nodiscard]] std::uint64_t alignUp(std::uint64_t value) { return (value + kBlobAlign - 1) / kBlobAlign * kBlobAlign; }

void put32(std::uint8_t* at, std::uint32_t v) { std::memcpy(at, &v, 4); }

void put64(std::uint8_t* at, std::uint64_t v) { std::memcpy(at, &v, 8); }

[[nodiscard]] std::uint32_t get32(const std::uint8_t* at) {
    std::uint32_t v;
    std::memcpy(&v, at, 4);
    return v;
}

[[nodiscard]] std::uint64_t get64(const std::uint8_t* at) {
    std::uint64_t v;
    std::memcpy(&v, at, 8);
    return v;
}

struct Entry {
    std::uint64_t offset = 0;
    std::uint32_t compressedSize = 0;
    std::uint32_t rawSize = 0;
    std::uint32_t crc = 0;
    std::uint32_t timestamp = 0;
    std::uint8_t codec = 0;
    std::uint8_t level = 0;
    std::uint16_t mask = 0;
    std::uint32_t dictId = 0;

    [[nodiscard]] bool live() const noexcept { return mask != 0; }
};

void encodeEntry(std::uint8_t* at, const Entry& e) {
    std::memset(at, 0, kEntrySize);
    put64(at, e.offset);
    put32(at + 8, e.compressedSize);
    put32(at + 12, e.rawSize);
    put32(at + 16, e.crc);
    put32(at + 20, e.timestamp);
    at[24] = e.codec;
    at[25] = e.level;
    at[26] = static_cast<std::uint8_t>(e.mask);
    at[27] = static_cast<std::uint8_t>(e.mask >> 8);
    put32(at + 28, e.dictId);
}

[[nodiscard]] Entry decodeEntry(const std::uint8_t* at) {
    Entry e;
    e.offset = get64(at);
    e.compressedSize = get32(at + 8);
    e.rawSize = get32(at + 12);
    e.crc = get32(at + 16);
    e.timestamp = get32(at + 20);
    e.codec = at[24];
    e.level = at[25];
    e.mask = static_cast<std::uint16_t>(at[26] | (at[27] << 8));
    e.dictId = get32(at + 28);
    return e;
}

bool preadFully(int fd, std::uint8_t* into, std::size_t size, std::uint64_t offset) {
    std::size_t done = 0;
    while (done < size) {
        const std::int64_t got = bswio::readAt(fd, into + done, size - done, offset + done);
        if (got <= 0) {
            if (got < 0 && errno == EINTR) {
                continue;
            }
            return false;
        }
        done += static_cast<std::size_t>(got);
    }
    return true;
}

bool pwriteFully(int fd, const std::uint8_t* from, std::size_t size, std::uint64_t offset) {
    std::size_t done = 0;
    while (done < size) {
        const std::int64_t put = bswio::writeAt(fd, from + done, size - done, offset + done);
        if (put <= 0) {
            if (put < 0 && errno == EINTR) {
                continue;
            }
            return false;
        }
        done += static_cast<std::size_t>(put);
    }
    return true;
}

#if defined(__x86_64__)
__attribute__((target("sse4.2"))) std::uint32_t crc32cHardware(const std::uint8_t* p, std::size_t n,
                                                               std::uint32_t crc) {
    std::uint64_t c = ~crc;
    while (n >= 8) {
        std::uint64_t word;
        std::memcpy(&word, p, 8);
        c = _mm_crc32_u64(c, word);
        p += 8;
        n -= 8;
    }
    while (n-- > 0) {
        c = _mm_crc32_u8(static_cast<std::uint32_t>(c), *p++);
    }
    return ~static_cast<std::uint32_t>(c);
}
#endif

[[nodiscard]] std::uint32_t crc32cSoftware(const std::uint8_t* p, std::size_t n, std::uint32_t crc) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1U) != 0 ? (c >> 1) ^ 0x82F63B78U : c >> 1;
            }
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t c = ~crc;
    while (n-- > 0) {
        c = table[(c ^ *p++) & 0xFFU] ^ (c >> 8);
    }
    return ~c;
}

void syncDirectory(const std::string& path) {
    bswio::syncDirectory(std::filesystem::path{path}.parent_path().string());
}

struct Extent {
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

} // namespace

std::uint32_t crc32c(std::span<const std::uint8_t> bytes, std::uint32_t seed) noexcept {
#if defined(__x86_64__)
    static const bool hardware = [] {
        unsigned eax = 0;
        unsigned ebx = 0;
        unsigned ecx = 0;
        unsigned edx = 0;
        return __get_cpuid(1, &eax, &ebx, &ecx, &edx) != 0 && (ecx & bit_SSE4_2) != 0;
    }();
    if (hardware) {
        return crc32cHardware(bytes.data(), bytes.size(), seed);
    }
#endif
    return crc32cSoftware(bytes.data(), bytes.size(), seed);
}

struct BswScratch::Impl {
    ZSTD_CCtx* cctx = ZSTD_createCCtx();
    ZSTD_DCtx* dctx = ZSTD_createDCtx();

    ~Impl() {
        ZSTD_freeCCtx(cctx);
        ZSTD_freeDCtx(dctx);
    }
};

BswScratch::BswScratch() : impl_(std::make_unique<Impl>()) {}

BswScratch::~BswScratch() = default;

struct BswRegion::State {
    std::string path;
    BswOptions options;
    int fd = -1;
    std::uint32_t dictId = 0;
    unsigned unitShift = 0;

    mutable std::shared_mutex indexMutex;
    std::mutex writeMutex;
    std::array<Entry, kSlotCount> entries{};
    std::uint64_t generation = 0;
    std::uint64_t dataEnd = kDataStart;
    int activeSlot = 0;

    const std::uint8_t* map = nullptr;
    std::size_t mapSize = 0;

    ~State() {
        unmap();
        if (fd >= 0) {
            bswio::closeFile(fd);
        }
    }

    void unmap() {
        if (map != nullptr) {
            bswio::unmap(map, mapSize);
            map = nullptr;
            mapSize = 0;
        }
    }

    void remap() {
        if (!options.mapReads) {
            return;
        }
        const std::int64_t size = bswio::sizeOf(fd);
        if (size < 0 || static_cast<std::uint64_t>(size) <= mapSize) {
            return;
        }
        unmap();
        map = bswio::mapReadOnly(fd, static_cast<std::size_t>(size));
        mapSize = map != nullptr ? static_cast<std::size_t>(size) : 0;
    }

    [[nodiscard]] std::size_t unitsPerSide() const { return kDimension >> unitShift; }

    [[nodiscard]] std::size_t unitOf(std::int32_t lx, std::int32_t lz) const {
        return (static_cast<std::size_t>(lz >> unitShift) * unitsPerSide()) + static_cast<std::size_t>(lx >> unitShift);
    }

    [[nodiscard]] unsigned bitOf(std::int32_t lx, std::int32_t lz) const {
        const unsigned side = 1U << unitShift;
        return (static_cast<unsigned>(lz) & (side - 1)) * side + (static_cast<unsigned>(lx) & (side - 1));
    }

    static constexpr std::int32_t kDimension = BswRegion::kDimension;

    bool writeHeader() const {
        std::array<std::uint8_t, kPage> page{};
        std::memcpy(page.data(), kMagic.data(), 8);
        put32(page.data() + 8, kVersion);
        page[12] = static_cast<std::uint8_t>(unitShift);
        put32(page.data() + 16, dictId);
        put32(page.data() + 20, crc32c(std::span{page.data(), 20}));
        return pwriteFully(fd, page.data(), page.size(), 0);
    }

    bool writeSlot(int slot, std::uint64_t gen, std::uint64_t end, const std::array<Entry, kSlotCount>& source,
                   bool sync) const {
        std::vector<std::uint8_t> bytes(kSlotBytes, 0);
        for (std::size_t i = 0; i < kSlotCount; ++i) {
            encodeEntry(bytes.data() + kSlotHeaderSize + (i * kEntrySize), source[i]);
        }
        const std::uint32_t entriesCrc = crc32c(std::span{bytes.data() + kSlotHeaderSize, kSlotCount * kEntrySize});
        put64(bytes.data(), kSlotMagic);
        put64(bytes.data() + 8, gen);
        put64(bytes.data() + 16, end);
        put32(bytes.data() + 24, entriesCrc);
        put32(bytes.data() + 28, crc32c(std::span{bytes.data(), 28}));
        if (!pwriteFully(fd, bytes.data(), bytes.size(), kSlotOffset[slot])) {
            return false;
        }
        return !sync || bswio::flush(fd);
    }

    struct UnitUpdate {
        std::size_t unit = 0;
        std::vector<std::uint8_t> raw;
        std::uint16_t mask = 0;
        std::uint32_t timestamp = 0;
    };

    [[nodiscard]] bool loadBlob(const Entry& e, BswScratch& scratch, const std::uint8_t*& view) const {
        if (map != nullptr && e.offset + e.compressedSize <= mapSize) {
            view = map + e.offset;
            return true;
        }
        scratch.compressed.resize(e.compressedSize);
        view = scratch.compressed.data();
        return preadFully(fd, scratch.compressed.data(), e.compressedSize, e.offset);
    }

    [[nodiscard]] bool inflate(const Entry& e, BswScratch& scratch, std::vector<std::uint8_t>& raw) const {
        const std::uint8_t* view = nullptr;
        if (!loadBlob(e, scratch, view)) {
            return false;
        }
        if (crc32c(std::span{view, e.compressedSize}) != e.crc) {
            return false;
        }
        raw.resize(e.rawSize);
        if (e.codec == static_cast<std::uint8_t>(BswCodec::Store)) {
            std::memcpy(raw.data(), view, e.rawSize);
            return true;
        }
        std::size_t got = 0;
        if (e.codec == static_cast<std::uint8_t>(BswCodec::Zstd)) {
            got = ZSTD_decompressDCtx(scratch.impl_->dctx, raw.data(), raw.size(), view, e.compressedSize);
        } else if (options.dictionary != nullptr && options.dictionary->id() == e.dictId) {
            got = ZSTD_decompress_usingDDict(scratch.impl_->dctx, raw.data(), raw.size(), view, e.compressedSize,
                                             options.dictionary->impl_->ddict);
        } else {
            return false;
        }
        return !ZSTD_isError(got) && got == e.rawSize;
    }

    [[nodiscard]] bool deflate(std::span<const std::uint8_t> raw, BswCodec codec, int level, BswScratch& scratch,
                               std::vector<std::uint8_t>& out, std::uint8_t& usedCodec) const {
        usedCodec = static_cast<std::uint8_t>(codec);
        if (codec == BswCodec::Store) {
            out.assign(raw.begin(), raw.end());
            return true;
        }
        out.resize(ZSTD_compressBound(raw.size()));
        std::size_t got = 0;
        if (codec == BswCodec::ZstdDict && options.dictionary != nullptr) {
            got = ZSTD_compress_usingCDict(scratch.impl_->cctx, out.data(), out.size(), raw.data(), raw.size(),
                                           options.dictionary->impl_->cdict);
        } else {
            usedCodec = static_cast<std::uint8_t>(BswCodec::Zstd);
            got = ZSTD_compressCCtx(scratch.impl_->cctx, out.data(), out.size(), raw.data(), raw.size(), level);
        }
        if (ZSTD_isError(got)) {
            return false;
        }
        out.resize(got);
        return true;
    }

    /// Free gaps of the committed index: the only space a commit may write into without risking the live slot.
    [[nodiscard]] std::vector<Extent> gaps() const {
        std::vector<Extent> live;
        for (const Entry& e : entries) {
            if (e.live()) {
                live.push_back({e.offset, alignUp(e.compressedSize)});
            }
        }
        std::sort(live.begin(), live.end(), [](const Extent& a, const Extent& b) { return a.offset < b.offset; });
        std::vector<Extent> free;
        std::uint64_t cursor = kDataStart;
        for (const Extent& e : live) {
            if (e.offset > cursor) {
                free.push_back({cursor, e.offset - cursor});
            }
            cursor = std::max(cursor, e.offset + e.size);
        }
        return free;
    }

    bool commit(std::vector<UnitUpdate>& updates, BswCodec codec, int level, BswScratch& scratch) {
        std::vector<Extent> free = gaps();
        std::uint64_t end = dataEnd;
        auto next = entries;
        std::vector<std::uint8_t> blob;
        for (UnitUpdate& update : updates) {
            if (update.mask == 0) {
                next[update.unit] = Entry{};
                continue;
            }
            std::uint8_t usedCodec = 0;
            if (!deflate(update.raw, codec, level, scratch, blob, usedCodec)) {
                return false;
            }
            const std::uint64_t need = alignUp(blob.size());
            std::uint64_t at = 0;
            bool placed = false;
            for (Extent& gap : free) {
                if (gap.size >= need) {
                    at = gap.offset;
                    gap.offset += need;
                    gap.size -= need;
                    placed = true;
                    break;
                }
            }
            if (!placed) {
                at = end;
                end += need;
            }
            if (!pwriteFully(fd, blob.data(), blob.size(), at)) {
                return false;
            }
            Entry e;
            e.offset = at;
            e.compressedSize = static_cast<std::uint32_t>(blob.size());
            e.rawSize = static_cast<std::uint32_t>(update.raw.size());
            e.crc = crc32c(blob);
            e.timestamp = update.timestamp;
            e.codec = usedCodec;
            e.level = static_cast<std::uint8_t>(level < 0 ? 0 : level);
            e.dictId = usedCodec == static_cast<std::uint8_t>(BswCodec::ZstdDict) ? options.dictionary->id() : 0;
            e.mask = update.mask;
            next[update.unit] = e;
        }
        if (options.syncOnCommit && !bswio::flush(fd)) {
            return false;
        }
        const int target = 1 - activeSlot;
        if (!writeSlot(target, generation + 1, end, next, options.syncOnCommit)) {
            return false;
        }
        const std::unique_lock lock{indexMutex};
        entries = next;
        generation += 1;
        dataEnd = end;
        activeSlot = target;
        remap();
        return true;
    }
};

BswRegion::BswRegion(std::unique_ptr<State> state) : state_(std::move(state)) {}

BswRegion::~BswRegion() = default;

std::unique_ptr<BswRegion> BswRegion::open(const std::string& path, const BswOptions& options, bool create) {
    auto state = std::make_unique<State>();
    state->path = path;
    state->options = options;
    state->unitShift = options.unitShift;
    state->dictId = options.dictionary != nullptr ? options.dictionary->id() : 0;
    const bool exists = std::filesystem::exists(path);
    if (!exists && !create) {
        return nullptr;
    }
    const std::string creating = path + ".new";
    state->fd = exists ? bswio::openExisting(path) : bswio::openNew(creating);
    if (state->fd < 0) {
        return nullptr;
    }
    if (!exists) {
        std::error_code error;
        if (!state->writeHeader() || !state->writeSlot(0, 1, kDataStart, state->entries, true) ||
            (std::filesystem::rename(creating, path, error), error)) {
            std::filesystem::remove(creating, error);
            return nullptr;
        }
        syncDirectory(path);
        state->generation = 1;
        state->activeSlot = 0;
    } else {
        std::array<std::uint8_t, kPage> page{};
        if (!preadFully(state->fd, page.data(), page.size(), 0) || std::memcmp(page.data(), kMagic.data(), 8) != 0 ||
            get32(page.data() + 8) != kVersion || crc32c(std::span{page.data(), 20}) != get32(page.data() + 20)) {
            return nullptr;
        }
        state->unitShift = page[12];
        state->dictId = get32(page.data() + 16);
        bool found = false;
        std::vector<std::uint8_t> bytes(kSlotBytes);
        for (int slot = 0; slot < 2; ++slot) {
            if (!preadFully(state->fd, bytes.data(), bytes.size(), kSlotOffset[slot]) ||
                get64(bytes.data()) != kSlotMagic || crc32c(std::span{bytes.data(), 28}) != get32(bytes.data() + 28) ||
                crc32c(std::span{bytes.data() + kSlotHeaderSize, kSlotCount * kEntrySize}) !=
                    get32(bytes.data() + 24)) {
                continue;
            }
            const std::uint64_t gen = get64(bytes.data() + 8);
            if (found && gen <= state->generation) {
                continue;
            }
            found = true;
            state->generation = gen;
            state->dataEnd = get64(bytes.data() + 16);
            state->activeSlot = slot;
            for (std::size_t i = 0; i < kSlotCount; ++i) {
                state->entries[i] = decodeEntry(bytes.data() + kSlotHeaderSize + (i * kEntrySize));
            }
        }
        if (!found) {
            return nullptr;
        }
    }
    state->remap();
    return std::unique_ptr<BswRegion>(new BswRegion(std::move(state)));
}

bool BswRegion::hasChunk(std::int32_t localX, std::int32_t localZ) const {
    const std::shared_lock lock{state_->indexMutex};
    const Entry& e = state_->entries[state_->unitOf(localX, localZ)];
    return (e.mask >> state_->bitOf(localX, localZ) & 1U) != 0;
}

BswRead BswRegion::read(std::int32_t localX, std::int32_t localZ, BswScratch& scratch,
                        std::vector<std::uint8_t>& out) const {
    const unsigned bit = state_->bitOf(localX, localZ);
    const std::shared_lock lock{state_->indexMutex};
    const Entry& e = state_->entries[state_->unitOf(localX, localZ)];
    if ((e.mask >> bit & 1U) == 0) {
        return BswRead::Absent;
    }
    if (!state_->inflate(e, scratch, scratch.raw)) {
        return BswRead::Corrupt;
    }
    const std::uint8_t* table = scratch.raw.data();
    const unsigned count = static_cast<unsigned>(std::popcount(e.mask));
    std::size_t payload = static_cast<std::size_t>(count) * 8;
    const unsigned rank = static_cast<unsigned>(std::popcount(static_cast<unsigned>(e.mask) & ((1U << bit) - 1)));
    for (unsigned i = 0; i < rank; ++i) {
        payload += get32(table + (i * 8));
    }
    const std::uint32_t length = get32(table + (rank * 8));
    if (payload + length > scratch.raw.size()) {
        return BswRead::Corrupt;
    }
    out.assign(scratch.raw.begin() + static_cast<std::ptrdiff_t>(payload),
               scratch.raw.begin() + static_cast<std::ptrdiff_t>(payload + length));
    return BswRead::Ok;
}

std::int32_t BswRegion::timestampOf(std::int32_t localX, std::int32_t localZ) const {
    const std::shared_lock lock{state_->indexMutex};
    return static_cast<std::int32_t>(state_->entries[state_->unitOf(localX, localZ)].timestamp);
}

void BswRegion::useDictionary(std::shared_ptr<const BswDictionary> dictionary) {
    const std::lock_guard<std::mutex> writer{state_->writeMutex};
    const std::unique_lock lock{state_->indexMutex};
    state_->options.dictionary = std::move(dictionary);
    state_->options.codec = state_->options.dictionary != nullptr ? BswCodec::ZstdDict : BswCodec::Zstd;
}

bool BswRegion::write(std::span<const BswPut> puts, BswScratch& scratch) {
    const std::lock_guard<std::mutex> writer{state_->writeMutex};
    State& s = *state_;
    std::map<std::size_t, std::vector<const BswPut*>> byUnit;
    for (const BswPut& put : puts) {
        byUnit[s.unitOf(put.localX, put.localZ)].push_back(&put);
    }
    std::vector<State::UnitUpdate> updates;
    std::vector<std::uint8_t> oldRaw;
    for (auto& [unit, list] : byUnit) {
        const Entry old = s.entries[unit];
        const bool partial = old.live() && s.unitShift != 0;
        if (partial && !s.inflate(old, scratch, oldRaw)) {
            return false;
        }
        const unsigned side = 1U << s.unitShift;

        struct Piece {
            std::span<const std::uint8_t> bytes;
            std::uint32_t timestamp = 0;
        };

        std::array<Piece, 16> pieces{};
        std::uint16_t mask = 0;
        if (partial) {
            const unsigned count = static_cast<unsigned>(std::popcount(old.mask));
            std::size_t payload = static_cast<std::size_t>(count) * 8;
            unsigned rank = 0;
            for (unsigned bit = 0; bit < side * side; ++bit) {
                if ((old.mask >> bit & 1U) == 0) {
                    continue;
                }
                const std::uint32_t length = get32(oldRaw.data() + (rank * 8));
                pieces[bit] = {std::span{oldRaw.data() + payload, length}, get32(oldRaw.data() + (rank * 8) + 4)};
                payload += length;
                ++rank;
                mask = static_cast<std::uint16_t>(mask | (1U << bit));
            }
        }
        std::uint32_t newest = partial ? old.timestamp : 0;
        for (const BswPut* put : list) {
            const unsigned bit = s.bitOf(put->localX, put->localZ);
            if (put->nbt.empty()) {
                pieces[bit] = {};
                mask = static_cast<std::uint16_t>(mask & ~(1U << bit));
            } else {
                pieces[bit] = {put->nbt, static_cast<std::uint32_t>(put->timestamp)};
                mask = static_cast<std::uint16_t>(mask | (1U << bit));
                newest = std::max(newest, static_cast<std::uint32_t>(put->timestamp));
            }
        }
        State::UnitUpdate update;
        update.unit = unit;
        update.mask = mask;
        update.timestamp = newest;
        if (mask != 0) {
            std::size_t total = static_cast<std::size_t>(std::popcount(mask)) * 8;
            for (unsigned bit = 0; bit < side * side; ++bit) {
                if ((mask >> bit & 1U) != 0) {
                    total += pieces[bit].bytes.size();
                }
            }
            update.raw.resize(total);
            std::size_t tableAt = 0;
            std::size_t payloadAt = static_cast<std::size_t>(std::popcount(mask)) * 8;
            for (unsigned bit = 0; bit < side * side; ++bit) {
                if ((mask >> bit & 1U) == 0) {
                    continue;
                }
                put32(update.raw.data() + tableAt, static_cast<std::uint32_t>(pieces[bit].bytes.size()));
                put32(update.raw.data() + tableAt + 4, pieces[bit].timestamp);
                tableAt += 8;
                std::memcpy(update.raw.data() + payloadAt, pieces[bit].bytes.data(), pieces[bit].bytes.size());
                payloadAt += pieces[bit].bytes.size();
            }
        }
        updates.push_back(std::move(update));
    }
    return s.commit(updates, s.options.codec, s.options.level, scratch);
}

bool BswRegion::sync() { return bswio::flush(state_->fd); }

bool BswRegion::recompress(BswCodec codec, int level, BswScratch& scratch) {
    const std::lock_guard<std::mutex> writer{state_->writeMutex};
    State& s = *state_;
    std::vector<State::UnitUpdate> updates;
    for (std::size_t unit = 0; unit < kSlotCount; ++unit) {
        const Entry e = s.entries[unit];
        if (!e.live() || (e.codec == static_cast<std::uint8_t>(codec) && e.level == level)) {
            continue;
        }
        State::UnitUpdate update;
        update.unit = unit;
        update.mask = e.mask;
        update.timestamp = e.timestamp;
        if (!s.inflate(e, scratch, update.raw)) {
            return false;
        }
        updates.push_back(std::move(update));
    }
    if (updates.empty()) {
        return true;
    }
    return s.commit(updates, codec, level, scratch);
}

bool BswRegion::compact(BswScratch& scratch) {
    const std::lock_guard<std::mutex> writer{state_->writeMutex};
    State& s = *state_;
    const std::string temporary = s.path + ".tmp";
    const int fd = bswio::openNew(temporary);
    if (fd < 0) {
        return false;
    }
    State fresh;
    fresh.fd = fd;
    fresh.unitShift = s.unitShift;
    fresh.dictId = s.dictId;
    fresh.options = s.options;
    bool ok = fresh.writeHeader();
    std::uint64_t cursor = kDataStart;
    std::vector<std::uint8_t> bytes;
    for (std::size_t unit = 0; ok && unit < kSlotCount; ++unit) {
        Entry e = s.entries[unit];
        if (!e.live()) {
            continue;
        }
        const std::uint8_t* view = nullptr;
        ok = s.loadBlob(e, scratch, view) && pwriteFully(fd, view, e.compressedSize, cursor);
        e.offset = cursor;
        cursor += alignUp(e.compressedSize);
        fresh.entries[unit] = e;
    }
    ok = ok && fresh.writeSlot(0, s.generation + 1, cursor, fresh.entries, true);
    if (ok) {
        ok = bswio::truncateTo(fd, cursor) && bswio::flush(fd);
    }
    fresh.fd = -1;
    bswio::closeFile(fd);
    std::error_code error;
    if (!ok) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    const std::unique_lock lock{s.indexMutex};
    s.unmap();
    std::filesystem::rename(temporary, s.path, error);
    if (error) {
        s.remap();
        return false;
    }
    syncDirectory(s.path);
    const int reopened = bswio::openExisting(s.path);
    if (reopened < 0) {
        return false;
    }
    bswio::closeFile(s.fd);
    s.fd = reopened;
    s.entries = fresh.entries;
    s.generation += 1;
    s.dataEnd = cursor;
    s.activeSlot = 0;
    s.remap();
    return true;
}

std::size_t BswRegion::countCorrupt(BswScratch& scratch) const {
    const std::shared_lock lock{state_->indexMutex};
    std::size_t bad = 0;
    for (const Entry& e : state_->entries) {
        if (e.live() && !state_->inflate(e, scratch, scratch.raw)) {
            bad += static_cast<std::size_t>(std::popcount(e.mask));
        }
    }
    return bad;
}

BswStats BswRegion::stats() const {
    const std::shared_lock lock{state_->indexMutex};
    BswStats out;
    out.generation = state_->generation;
    if (const std::int64_t size = bswio::sizeOf(state_->fd); size >= 0) {
        out.fileBytes = static_cast<std::uint64_t>(size);
    }
    for (const Entry& e : state_->entries) {
        if (e.live()) {
            out.liveBytes += alignUp(e.compressedSize);
            out.chunks += static_cast<std::uint32_t>(std::popcount(e.mask));
            ++out.units;
        }
    }
    out.freeBytes = state_->dataEnd - kDataStart - out.liveBytes;
    return out;
}

std::uint64_t BswRegion::indexBytesResident() const noexcept { return kSlotCount * sizeof(Entry); }

} // namespace basalt::metamorph::region
