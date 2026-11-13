#pragma once

#include "afterimage/core/DocumentManager.h"
#include "afterimage/extraction/ContentExtractor.h"

#include <cstddef>
#include <vector>

namespace afterimage {

struct ExtractionReport {
    std::size_t attempted{0};
    std::size_t extracted{0};
    std::size_t partial{0};
    std::size_t empty{0};
    std::size_t unsupported{0};
    std::size_t dependencyMissing{0};
    std::size_t failed{0};
    std::size_t tooLarge{0};
    std::vector<std::string> messages;
};

class ExtractionService {
public:
    explicit ExtractionService(ExtractionOptions options = {});

    // Convenience operation for tests and explicit full re-extraction.
    [[nodiscard]] ExtractionReport extractAll(DocumentManager& manager) const;

    // Normal indexing path: only newly imported or changed files are extracted.
    // This prevents expensive PDF OCR from running every time AFTERIMAGE opens.
    [[nodiscard]] ExtractionReport extractDocuments(
        DocumentManager& manager,
        const std::vector<DocumentId>& documentIds
    ) const;

private:
    ContentExtractor extractor_;
};

} // namespace afterimage
