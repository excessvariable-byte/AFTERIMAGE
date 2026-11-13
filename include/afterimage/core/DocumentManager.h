#pragma once

#include "afterimage/core/Document.h"

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

namespace afterimage {

// Owns the in-memory document collection. Ingestion adds discovered files;
// extraction and persistence remain separate modules.
class DocumentManager {
public:
    DocumentId addDocument(Document document);

    // Rehydrates a document using its original persistent ID. Intended for storage loading.
    void restoreDocument(Document document);

    // Returns a copy so callers cannot hold a pointer into a vector that may
    // reallocate when another document is added. We can optimize this API later.
    std::optional<Document> findById(DocumentId id) const;
    bool setExtractedText(DocumentId id, std::string text);
    // Updates source metadata and invalidates extracted text when a file changed.
    bool updateFileMetadata(
        DocumentId id,
        std::optional<std::uintmax_t> sizeBytes,
        std::optional<std::int64_t> modifiedAtEpochSeconds
    );

    const std::vector<Document>& allDocuments() const noexcept;
    std::size_t size() const noexcept;
    bool empty() const noexcept;

private:
    std::vector<Document> documents_;
    std::unordered_map<DocumentId, std::size_t> indexById_;
    DocumentId nextId_{1};
};

} // namespace afterimage
