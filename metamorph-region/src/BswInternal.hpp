#pragma once

#include <basalt/metamorph/region/BswRegion.hpp>

#include <zstd.h>

namespace basalt::metamorph::region {

struct BswDictionary::Impl {
    ZSTD_CDict* cdict = nullptr;
    ZSTD_DDict* ddict = nullptr;
};

} // namespace basalt::metamorph::region
