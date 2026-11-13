#pragma once

#include <cstddef>

namespace afterimage {

struct SearchOptions {
    // When true, a query token such as "travers" can match "traversal".
    bool prefixMatch{true};

    // When true, a result must contain at least one match for every query token.
    bool requireAllTerms{false};

    // When true, the complete query must occur as consecutive words.
    // Phrase matching is exact; it does not use prefix expansion.
    bool phraseMatch{false};

    // Maximum vocabulary entries considered for each prefix query token.
    std::size_t maxPrefixExpansions{20};
};

} // namespace afterimage
