#pragma once

#include "afterimage/core/Document.h"

#include <cstddef>

namespace afterimage {

struct SearchResult {
    DocumentId documentId{0};
    double score{0.0};
    std::size_t matchedTerms{0};
    bool phraseMatched{false};
};

} // namespace afterimage
