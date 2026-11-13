#pragma once

#include <cstddef>

namespace afterimage {

// Controls which retrieval paths are allowed to generate candidates.
// Weights are deliberately simple in Module 6; Module 7 will tune ranking.
struct RetrievalOptions {
    bool enableExact{true};
    bool enablePrefix{true};
    bool enableInfix{true};
    bool enableFuzzy{true};
    bool enablePhrase{true};

    // Preserve the previous --phrase behavior: return exact phrase matches only.
    bool phraseOnly{false};

    // Require every distinct query token to match through at least one route.
    bool requireAllFragments{false};

    std::size_t minimumInfixLength{3};
    std::size_t minimumFuzzyLength{4};
    std::size_t maxInfixExpansions{8};
    std::size_t maxFuzzyExpansions{8};
    std::size_t maxEditDistance{1};
    std::size_t maxPrefixExpansions{20};
    std::size_t maxResults{10};

    double exactWeight{1.0};
    double prefixWeight{0.85};
    double infixWeight{0.65};
    double fuzzyWeight{0.45};
    double phraseWeight{1.25};
};

} // namespace afterimage
