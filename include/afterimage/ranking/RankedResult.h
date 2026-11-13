#pragma once

#include "afterimage/retrieval/RetrievalResult.h"

#include <cstddef>
#include <string>

namespace afterimage {

struct RankingBreakdown {
    double bm25Score{0.0};
    double titleBoost{0.0};
    double pathBoost{0.0};
    double phraseBoost{0.0};
    double coverageBoost{0.0};
    double recencyBoost{0.0};
    double finalScore{0.0};
    std::size_t titleTermHits{0};
    std::size_t pathTermHits{0};
    double queryCoverage{0.0};

    [[nodiscard]] std::string explanation() const;
};

struct RankedResult {
    RetrievalResult retrieval;
    RankingBreakdown breakdown;
};

} // namespace afterimage
