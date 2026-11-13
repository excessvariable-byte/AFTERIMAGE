#pragma once

#include "afterimage/core/Document.h"
#include "afterimage/graph/GraphOptions.h"
#include "afterimage/graph/RelatedDocument.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace afterimage {

// A rebuildable bipartite memory graph. Document nodes connect to concept
// nodes; two documents are related when they share informative concepts.
// SQLite remains the source of truth, so this graph is rebuilt from documents.
class MemoryGraph {
public:
    void build(const std::vector<Document>& documents, const GraphOptions& options = {});
    void clear() noexcept;

    [[nodiscard]] std::vector<RelatedDocument> relatedDocuments(
        const std::vector<DocumentId>& seedDocuments,
        const GraphOptions& options = {}) const;

    [[nodiscard]] std::size_t documentCount() const noexcept;
    [[nodiscard]] std::size_t conceptCount() const noexcept;
    [[nodiscard]] std::size_t edgeCount() const noexcept;
    [[nodiscard]] bool containsDocument(DocumentId id) const noexcept;
    [[nodiscard]] const std::vector<std::string>& conceptsFor(DocumentId id) const;

private:
    std::unordered_map<DocumentId, std::vector<std::string>> conceptsByDocument_;
    std::unordered_map<std::string, std::vector<DocumentId>> documentsByConcept_;
    std::unordered_set<DocumentId> documentIds_;
    std::size_t edgeCount_{0};
};

} // namespace afterimage
