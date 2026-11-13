#pragma once

#include "afterimage/core/DocumentManager.h"
#include "afterimage/ingestion/FileScanner.h"

#include <cstddef>
#include <vector>

namespace afterimage {

struct IngestionReport {
    ScanResult scan;
    std::vector<DocumentId> importedDocumentIds;
    std::vector<DocumentId> updatedDocumentIds;
    std::size_t skippedAlreadyImported{0};
    std::size_t changedFilesDetected{0};
};

// Connects filesystem discovery to the existing in-memory document library.
// Re-scanning the same path during one application session will not create a
// second Document record. Durable duplicate detection comes with Storage later.
class IngestionService {
public:
    IngestionReport ingestFolder(
        const std::filesystem::path& root,
        DocumentManager& documentManager,
        const IngestionOptions& options = {}
    ) const;

private:
    FileScanner scanner_;
};

} // namespace afterimage
