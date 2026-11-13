#pragma once

#include "afterimage/indexing/InvertedIndex.h"
#include "afterimage/retrieval/RetrievalOptions.h"
#include "afterimage/retrieval/RetrievalResult.h"

#include <string_view>
#include <vector>

namespace afterimage {

// Generates candidate documents through independent retrieval paths, merges
// duplicate candidates, and keeps evidence so results can explain themselves.
class FragmentRetrievalEngine {
public:
    std::vector<RetrievalResult> retrieve(
        std::string_view query,
        const InvertedIndex& index,
        const RetrievalOptions& options = {}) const;
};

} // namespace afterimage
