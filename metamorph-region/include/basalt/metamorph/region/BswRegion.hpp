#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace basalt::metamorph::region {

enum class BswCodec : std::uint8_t { Store = 0, Zstd = 1, ZstdDict = 2 };

/// A trained zstd dictionary shared by every region of a world; its id is the CRC32C of its bytes.
class BswDictionary {
public:
    BswDictionary(std::vector<std::uint8_t> bytes, int level);
    ~BswDictionary();
    BswDictionary(const BswDictionary&) = delete;
    BswDictionary& operator=(const BswDictionary&) = delete;

    [[nodiscard]] std::uint32_t id() const noexcept { return id_; }

    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return bytes_; }

    /// Null when the samples are too few or too uniform to train on.
    [[nodiscard]] static std::shared_ptr<const BswDictionary>
    train(std::span<const std::span<const std::uint8_t>> samples, std::size_t capacity, int level);

    [[nodiscard]] static std::shared_ptr<const BswDictionary> load(const std::string& path, int level);

    /// Written to a temporary file and renamed, so a crash leaves the old dictionary or the new one.
    [[nodiscard]] bool save(const std::string& path) const;

private:
    friend class BswRegion;
    struct Impl;
    std::vector<std::uint8_t> bytes_;
    std::uint32_t id_ = 0;
    std::unique_ptr<Impl> impl_;
};

/// Per-reader buffers: reuse one per thread and the read path allocates nothing after warm-up.
class BswScratch {
public:
    BswScratch();
    ~BswScratch();
    BswScratch(const BswScratch&) = delete;
    BswScratch& operator=(const BswScratch&) = delete;

    std::vector<std::uint8_t> compressed;
    std::vector<std::uint8_t> raw;

private:
    friend class BswRegion;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct BswOptions {
    /// 0, 1 or 2: a unit is 1x1, 2x2 or 4x4 chunks and is the smallest thing compressed or read.
    unsigned unitShift = 0;
    BswCodec codec = BswCodec::Zstd;
    int level = 3;
    std::shared_ptr<const BswDictionary> dictionary;
    /// Reads go through a read-only mapping instead of pread.
    bool mapReads = false;
    /// Two fdatasync calls per commit (blobs, then the index slot); off leaves them to `sync()`.
    bool syncOnCommit = true;
};

struct BswPut {
    std::int32_t localX = 0;
    std::int32_t localZ = 0;
    /// Empty removes the chunk.
    std::span<const std::uint8_t> nbt;
    std::int32_t timestamp = 0;
};

struct BswStats {
    std::uint64_t fileBytes = 0;
    std::uint64_t liveBytes = 0;
    std::uint64_t freeBytes = 0;
    std::uint32_t chunks = 0;
    std::uint32_t units = 0;
    std::uint64_t generation = 0;
};

enum class BswRead : std::uint8_t { Ok, Absent, Corrupt };

/// One region file: two alternating index slots, copy-on-write blobs, every blob checked by CRC32C.
class BswRegion {
public:
    static constexpr std::int32_t kDimension = 32;
    static constexpr std::size_t kSlotCount = 1024;

    ~BswRegion();
    BswRegion(const BswRegion&) = delete;
    BswRegion& operator=(const BswRegion&) = delete;

    /// Opens or, when `create` is set, makes the file. Null when it is missing, torn beyond both slots, or foreign.
    [[nodiscard]] static std::unique_ptr<BswRegion> open(const std::string& path, const BswOptions& options,
                                                         bool create);

    [[nodiscard]] bool hasChunk(std::int32_t localX, std::int32_t localZ) const;

    /// Fills `out` with the chunk's NBT; `Corrupt` when a checksum, a codec or the dictionary does not match.
    [[nodiscard]] BswRead read(std::int32_t localX, std::int32_t localZ, BswScratch& scratch,
                               std::vector<std::uint8_t>& out) const;

    [[nodiscard]] std::int32_t timestampOf(std::int32_t localX, std::int32_t localZ) const;

    /// Later commits compress with this dictionary; chunks already stored keep theirs.
    void useDictionary(std::shared_ptr<const BswDictionary> dictionary);

    /// One atomic commit for all the puts: crash safe, either every change is visible afterwards or none.
    bool write(std::span<const BswPut> puts, BswScratch& scratch);

    /// Flushes what a commit without `syncOnCommit` left in the page cache.
    bool sync();

    /// Rewrites every unit stored with a different codec or level into the given one, in one commit.
    bool recompress(BswCodec codec, int level, BswScratch& scratch);

    /// Rebuilds the file without holes, then renames it over the old one.
    bool compact(BswScratch& scratch);

    /// How many stored chunks fail their checksum; reads every blob.
    [[nodiscard]] std::size_t countCorrupt(BswScratch& scratch) const;

    [[nodiscard]] BswStats stats() const;

    [[nodiscard]] std::uint64_t indexBytesResident() const noexcept;

private:
    struct State;
    explicit BswRegion(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};

[[nodiscard]] std::uint32_t crc32c(std::span<const std::uint8_t> bytes, std::uint32_t seed = 0) noexcept;

} // namespace basalt::metamorph::region
