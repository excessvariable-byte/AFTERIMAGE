#pragma once

#include "afterimage/core/Document.h"
#include "afterimage/indexing/InvertedIndex.h"
#include "afterimage/ranking/RankedResult.h"
#include "afterimage/ranking/RankingOptions.h"
#include "afterimage/retrieval/RetrievalResult.h"

#include <string_view>
#include <vector>

namespace afterimage {

// Re-scores Module 6 candidates using BM25 and explainable metadata boosts.
class RankingEngine {
public:
    [[nodiscard]] std::vector<RankedResult> rank(
        std::string_view query,
        const std::vector<RetrievalResult>& candidates,
        const InvertedIndex& index,
        const std::vector<Document>& documents,
        const RankingOptions& options = {}) const;
};

} // namespace afterimage
