#pragma once

#include "afterimage/core/Document.h"

#include <string>
#include <vector>

namespace afterimage {

enum class RetrievalPath {
    Exact,
    Prefix,
    Infix,
    Fuzzy,
    Phrase
};

struct RetrievalMatch {
    std::string queryFragment;
    std::string matchedTerm;
    RetrievalPath path{RetrievalPath::Exact};
    double contribution{0.0};
};

// One candidate document plus evidence explaining why it was retrieved.
struct RetrievalResult {
    DocumentId documentId{0};
    double score{0.0};
    std::vector<RetrievalMatch> matches;
    std::size_t matchedFragments{0};
    bool phraseMatched{false};

    std::string explanation() const;
};

const char* retrievalPathName(RetrievalPath path) noexcept;

} // namespace afterimage
