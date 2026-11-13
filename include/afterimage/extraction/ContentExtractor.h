#pragma once

#include "afterimage/core/Document.h"
#include "afterimage/extraction/ExtractionOptions.h"
#include "afterimage/extraction/ExtractionResult.h"

namespace afterimage {

class ContentExtractor {
public:
    explicit ContentExtractor(ExtractionOptions options = {});

    [[nodiscard]] ExtractionResult extract(const Document& document) const;

private:
    ExtractionOptions options_;
};

} // namespace afterimage
