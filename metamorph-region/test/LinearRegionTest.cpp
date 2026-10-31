#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <basalt/metamorph/region/LinearRegion.hpp>
#include <basalt/wire/io/Compression.hpp>
#include <basalt/wire/io/FileIO.hpp>

#include <doctest.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using namespace basalt::metamorph::region;
namespace fileIO = basalt::wire::io::fileIO;

namespace {

[[nodiscard]] std::vector<std::uint8_t> blob(std::size_t size, std::uint8_t seed) {
    std::vector<std::uint8_t> bytes(size);
    for (std::size_t at = 0; at < size; ++at) {
        bytes[at] = static_cast<std::uint8_t>((at * 31) + seed);
    }
    return bytes;
}

[[nodiscard]] std::string scratch(std::string_view name) {
    const std::filesystem::path directory = std::filesystem::path{BASALT_TEST_SCRATCH_DIR} / name;
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory, ignored);
    return directory.string();
}

[[nodiscard]] std::uint32_t readIntAt(std::span<const std::uint8_t> from, std::size_t offset) {
    return (static_cast<std::uint32_t>(from[offset]) << 24) | (static_cast<std::uint32_t>(from[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(from[offset + 2]) << 8) | static_cast<std::uint32_t>(from[offset + 3]);
}

[[nodiscard]] std::optional<LinearError> errorOf(std::span<const std::uint8_t> bytes) {
    const std::expected<LinearRegion, LinearError> read = LinearRegion::read(bytes);
    return read.has_value() ? std::nullopt : std::optional{read.error()};
}

[[nodiscard]] bool sameBytes(std::span<const std::uint8_t> left, std::span<const std::uint8_t> right) {
    return std::ranges::equal(left, right);
}

} // namespace

TEST_CASE("an empty region is a header, an empty table and a footer") {
    const LinearRegion region;
    CHECK(region.residentBytes() == 0);
    CHECK_FALSE(region.hasChunkAt(0, 0));
    CHECK(region.chunkAt(0, 0).empty());
    CHECK(region.timestampAt(0, 0) == 0);

    const std::vector<std::uint8_t> written = region.write();
    CHECK(written[0] == 0xC3);
    CHECK(written[7] == 0x9A);
    CHECK(written[8] == 1);
    CHECK(written[18] == 0);
    CHECK(written[19] == 0);

    const std::expected<LinearRegion, LinearError> read = LinearRegion::read(written);
    REQUIRE(read.has_value());
    CHECK(read->residentBytes() == 0);
    CHECK_FALSE(read->hasChunkAt(31, 31));
}

TEST_CASE("what goes into a region comes back out of it, at the same coordinates") {
    LinearRegion region;
    const std::vector<std::uint8_t> corner = blob(4096, 7);
    const std::vector<std::uint8_t> distant = blob(70000, 200);
    const std::vector<std::uint8_t> tiny = blob(1, 3);
    region.setChunk(0, 0, corner, 1000);
    region.setChunk(31, 31, distant, 2000);
    region.setChunk(5, 17, tiny, 3000);
    CHECK(region.residentBytes() == 4096 + 70000 + 1);

    const std::vector<std::uint8_t> written = region.write();
    CHECK(written.size() < 4096 + 70000 + 8192);

    const std::expected<LinearRegion, LinearError> read = LinearRegion::read(written);
    REQUIRE(read.has_value());
    CHECK(sameBytes(read->chunkAt(0, 0), corner));
    CHECK(sameBytes(read->chunkAt(31, 31), distant));
    CHECK(sameBytes(read->chunkAt(5, 17), tiny));
    CHECK(read->timestampAt(0, 0) == 1000);
    CHECK(read->timestampAt(31, 31) == 2000);
    CHECK(read->timestampAt(5, 17) == 3000);
    CHECK(read->residentBytes() == region.residentBytes());
    CHECK_FALSE(read->hasChunkAt(1, 0));
    CHECK_FALSE(read->hasChunkAt(17, 5));

    CHECK(written[18] == 0);
    CHECK(written[19] == 3);
}

TEST_CASE("a chunk written twice is the second one, and one taken out is gone") {
    LinearRegion region;
    region.setChunk(2, 3, blob(500, 1), 10);
    region.setChunk(2, 3, blob(800, 2), 20);
    CHECK(region.residentBytes() == 800);
    CHECK(region.chunkAt(2, 3).size() == 800);
    CHECK(region.timestampAt(2, 3) == 20);

    region.clearChunk(2, 3);
    CHECK(region.residentBytes() == 0);
    CHECK_FALSE(region.hasChunkAt(2, 3));
    const std::expected<LinearRegion, LinearError> read = LinearRegion::read(region.write());
    REQUIRE(read.has_value());
    CHECK_FALSE(read->hasChunkAt(2, 3));
}

TEST_CASE("a file written to disk reads back the same as one written to memory") {
    const std::string root = scratch("linear-region");
    const std::string path = root + "/r.0.0.linear";

    LinearRegion region;
    for (std::int32_t local = 0; local < 32; ++local) {
        region.setChunk(local, local,
                        blob(static_cast<std::size_t>(1000 + (local * 37)), static_cast<std::uint8_t>(local)),
                        local + 1);
    }
    CHECK(region.writeTo(path));

    const std::optional<std::vector<std::uint8_t>> onDisk = fileIO::readBytes(path);
    REQUIRE(onDisk.has_value());
    CHECK(*onDisk == region.write());

    const std::expected<LinearRegion, LinearError> read = LinearRegion::read(*onDisk);
    REQUIRE(read.has_value());
    for (std::int32_t local = 0; local < 32; ++local) {
        CAPTURE(local);
        CHECK(read->chunkAt(local, local).size() == static_cast<std::size_t>(1000 + (local * 37)));
        CHECK(read->timestampAt(local, local) == local + 1);
    }
}

TEST_CASE("a corrupt region says what is wrong with it rather than half-reading it") {
    LinearRegion region;
    region.setChunk(1, 1, blob(2048, 9), 5);
    const std::vector<std::uint8_t> good = region.write();

    CHECK(errorOf(std::vector<std::uint8_t>(20, 0)) == LinearError::TooShort);

    std::vector<std::uint8_t> badSignature = good;
    badSignature[1] = 0;
    CHECK(errorOf(badSignature) == LinearError::BadSignature);

    std::vector<std::uint8_t> badVersion = good;
    badVersion[8] = 3;
    CHECK(errorOf(badVersion) == LinearError::UnsupportedVersion);
    std::vector<std::uint8_t> versionTwo = good;
    versionTwo[8] = 2;
    CHECK(LinearRegion::read(versionTwo).has_value());

    std::vector<std::uint8_t> badFooter = good;
    badFooter[badFooter.size() - 1] = 0;
    CHECK(errorOf(badFooter) == LinearError::BadFooter);

    std::vector<std::uint8_t> badLength = good;
    badLength[23] = static_cast<std::uint8_t>(badLength[23] + 1);
    CHECK(errorOf(badLength) == LinearError::LengthMismatch);

    std::vector<std::uint8_t> badCount = good;
    badCount[19] = 7;
    CHECK(errorOf(badCount) == LinearError::ChunkCountMismatch);
}

TEST_CASE("a stream that ends before its own table does") {
    LinearRegion region;
    const std::vector<std::uint8_t> good = region.write();
    std::vector<std::uint8_t> truncated;
    truncated.insert(truncated.end(), good.begin(), good.begin() + 32);
    const std::vector<std::uint8_t> emptyFrame = basalt::wire::io::zstd::compress(std::vector<std::uint8_t>{}).value();
    truncated.insert(truncated.end(), emptyFrame.begin(), emptyFrame.end());
    truncated.insert(truncated.end(), good.end() - 8, good.end());
    truncated[20] = static_cast<std::uint8_t>(emptyFrame.size() >> 24);
    truncated[21] = static_cast<std::uint8_t>(emptyFrame.size() >> 16);
    truncated[22] = static_cast<std::uint8_t>(emptyFrame.size() >> 8);
    truncated[23] = static_cast<std::uint8_t>(emptyFrame.size());
    CHECK(errorOf(truncated) == LinearError::TruncatedTable);
}

TEST_CASE("the table is laid out the way linear.py reads it") {
    LinearRegion region;
    region.setChunk(1, 0, blob(64, 1), 111);
    region.setChunk(0, 1, blob(128, 2), 222);
    const std::vector<std::uint8_t> written = region.write();

    CHECK(readIntAt(written, 13) == 222);

    const std::size_t compressedLength = readIntAt(written, 20);
    const std::vector<std::uint8_t> body =
        basalt::wire::io::zstd::decompress(std::span{written}.subspan(32, compressedLength)).value();
    REQUIRE(body.size() > 8192);

    // Row-major, x fastest: (1,0) is slot one and (0,1) is slot thirty-two.
    CHECK(readIntAt(body, 1 * 8) == 64);
    CHECK(readIntAt(body, (1 * 8) + 4) == 111);
    CHECK(readIntAt(body, 32 * 8) == 128);
    CHECK(readIntAt(body, (32 * 8) + 4) == 222);
    CHECK(readIntAt(body, 0) == 0);

    CHECK(body.size() == 8192 + 64 + 128);
    CHECK(body[8192] == 1);
}

TEST_CASE("a stream holding more than its table describes is not a region") {
    std::vector<std::uint8_t> body(8192, 0);
    body[3] = 16;
    std::vector<std::uint8_t> payload(16 + 40, 0xAB);
    body.insert(body.end(), payload.begin(), payload.end());
    const std::vector<std::uint8_t> compressed = basalt::wire::io::zstd::compress(body).value();

    std::vector<std::uint8_t> file(32, 0);
    file[0] = 0xC3;
    file[1] = 0xFF;
    file[2] = 0x13;
    file[3] = 0x18;
    file[4] = 0x3C;
    file[5] = 0xCA;
    file[6] = 0x9D;
    file[7] = 0x9A;
    file[8] = 1;
    file[19] = 1;
    file[20] = static_cast<std::uint8_t>(compressed.size() >> 24);
    file[21] = static_cast<std::uint8_t>(compressed.size() >> 16);
    file[22] = static_cast<std::uint8_t>(compressed.size() >> 8);
    file[23] = static_cast<std::uint8_t>(compressed.size());
    file.insert(file.end(), compressed.begin(), compressed.end());
    for (std::size_t at = 0; at < 8; ++at) {
        file.push_back(file[at]);
    }

    CHECK(errorOf(file) == LinearError::TruncatedChunks);
}