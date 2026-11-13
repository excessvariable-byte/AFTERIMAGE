#pragma once
#include "afterimage/core/Document.h"
#include <cstddef>
#include <string>
#include <vector>

namespace afterimage {

struct RelatedDocument {
    DocumentId documentId{0};
    double score{0.0};
    std::size_t shortestPathHops{0};
    std::vector<std::string> connectingConcepts;
};

} // namespace afterimage
