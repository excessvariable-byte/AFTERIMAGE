#pragma once

#include <cstddef>
#include <cstdint>

namespace afterimage {

// BM25 and small explainable boosts used after Module 6 candidate generation.
struct RankingOptions {
    double k1{1.2};
    double b{0.75};

    double titleBoostWeight{1.25};
    double pathBoostWeight{0.25};
    double phraseBoostWeight{0.75};
    double coverageBoostWeight{0.50};

    bool enableRecencyBoost{true};
    double recencyBoostWeight{0.20};
    double recencyHalfLifeDays{90.0};
    // Set a fixed Unix timestamp for deterministic tests. Zero means current time.
    std::int64_t referenceTimeEpochSeconds{0};

    std::size_t maxResults{10};
};

} // namespace afterimage
