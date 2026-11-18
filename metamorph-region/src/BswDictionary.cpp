#include "BswFile.hpp"
#include "BswInternal.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <zdict.h>

namespace basalt::metamorph::region {

BswDictionary::BswDictionary(std::vector<std::uint8_t> bytes, int level)
    : bytes_(std::move(bytes)), id_(crc32c(bytes_)), impl_(std::make_unique<Impl>()) {
    impl_->cdict = ZSTD_createCDict(bytes_.data(), bytes_.size(), level);
    impl_->ddict = ZSTD_createDDict(bytes_.data(), bytes_.size());
}

BswDictionary::~BswDictionary() {
    ZSTD_freeCDict(impl_->cdict);
    ZSTD_freeDDict(impl_->ddict);
}

std::shared_ptr<const BswDictionary> BswDictionary::train(std::span<const std::span<const std::uint8_t>> samples,
                                                          std::size_t capacity, int level) {
    std::vector<std::uint8_t> joined;
    std::vector<std::size_t> sizes;
    for (const auto& sample : samples) {
        joined.insert(joined.end(), sample.begin(), sample.end());
        sizes.push_back(sample.size());
    }
    if (sizes.size() < 8) {
        return nullptr;
    }
    std::vector<std::uint8_t> bytes(capacity);
    const std::size_t got = ZDICT_trainFromBuffer(bytes.data(), bytes.size(), joined.data(), sizes.data(),
                                                  static_cast<unsigned>(sizes.size()));
    if (ZDICT_isError(got)) {
        return nullptr;
    }
    bytes.resize(got);
    return std::make_shared<BswDictionary>(std::move(bytes), level);
}

std::shared_ptr<const BswDictionary> BswDictionary::load(const std::string& path, int level) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return nullptr;
    }
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    if (bytes.size() < 16) {
        return nullptr;
    }
    return std::make_shared<BswDictionary>(std::move(bytes), level);
}

bool BswDictionary::save(const std::string& path) const {
    const std::string temporary = path + ".tmp";
    const int fd = bswio::openNew(temporary);
    if (fd < 0) {
        return false;
    }
    std::size_t done = 0;
    bool ok = true;
    while (ok && done < bytes_.size()) {
        const std::int64_t put = bswio::writeAt(fd, bytes_.data() + done, bytes_.size() - done, done);
        ok = put > 0;
        done += ok ? static_cast<std::size_t>(put) : 0;
    }
    ok = ok && bswio::flush(fd);
    bswio::closeFile(fd);
    std::error_code error;
    if (ok) {
        std::filesystem::rename(temporary, path, error);
    }
    if (!ok || error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

} // namespace basalt::metamorph::region
