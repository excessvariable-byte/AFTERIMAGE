#pragma once
#include <cstddef>

namespace afterimage {

struct GraphOptions {
    std::size_t minimumConceptLength{4};
    std::size_t maximumConceptsPerDocument{120};
    // Ignore concepts appearing in more than this fraction of the library.
    // This prevents generic words from connecting almost every document.
    double maximumDocumentFrequencyRatio{0.25};
    std::size_t maximumHops{2};
    std::size_t maximumResults{8};
    double minimumScore{0.0};
};

} // namespace afterimage
