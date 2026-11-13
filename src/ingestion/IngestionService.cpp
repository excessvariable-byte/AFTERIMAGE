#include "afterimage/ingestion/IngestionService.h"

#include <filesystem>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace afterimage {
namespace {

std::string pathIdentity(const std::string& pathText) {
    std::error_code error;
    const std::filesystem::path path(pathText);
    auto normalized = std::filesystem::weakly_canonical(path, error);

    if (error) {
        error.clear();
        normalized = std::filesystem::absolute(path, error);
    }

    if (error) {
        return path.lexically_normal().generic_string();
    }

    return normalized.lexically_normal().generic_string();
}

} // namespace

IngestionReport IngestionService::ingestFolder(
    const std::filesystem::path& root,
    DocumentManager& documentManager,
    const IngestionOptions& options
) const {
    IngestionReport report;
    report.scan = scanner_.scan(root, options);

    if (!report.scan.rootValid) {
        return report;
    }

    // Keep path -> ID so a re-scan can detect changed files, not only duplicates.
    std::unordered_map<std::string, DocumentId> existingPaths;
    existingPaths.reserve(documentManager.size() + report.scan.files.size());

    for (const Document& document : documentManager.allDocuments()) {
        if (!document.filePath.empty()) {
            existingPaths.emplace(pathIdentity(document.filePath), document.id);
        }
    }

    for (const DiscoveredFile& discovered : report.scan.files) {
        const std::string storedPath = discovered.path.string();
        const std::string identity = pathIdentity(storedPath);
        const auto known = existingPaths.find(identity);

        if (known != existingPaths.end()) {
            ++report.skippedAlreadyImported;
            if (documentManager.updateFileMetadata(
                    known->second, discovered.sizeBytes, discovered.modifiedAtEpochSeconds)) {
                report.updatedDocumentIds.push_back(known->second);
                ++report.changedFilesDetected;
            }
            continue;
        }

        Document document;
        document.title = discovered.title.empty()
            ? discovered.path.filename().string()
            : discovered.title;
        document.filePath = storedPath;
        document.fileType = discovered.extension;
        document.sizeBytes = discovered.sizeBytes;
        document.modifiedAtEpochSeconds = discovered.modifiedAtEpochSeconds;

        const DocumentId id = documentManager.addDocument(std::move(document));
        existingPaths.emplace(identity, id);
        report.importedDocumentIds.push_back(id);
    }

    return report;
}

} // namespace afterimage
