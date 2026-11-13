#include "afterimage/extraction/ExtractionService.h"

#include <exception>
#include <unordered_set>

namespace afterimage {
namespace {

void applyResult(
    const Document& document,
    const ExtractionResult& result,
    DocumentManager& manager,
    ExtractionReport& report
) {
    switch (result.status) {
        case ExtractionStatus::Success:
            manager.setExtractedText(document.id, result.text);
            ++report.extracted;
            break;
        case ExtractionStatus::Partial:
            manager.setExtractedText(document.id, result.text);
            ++report.extracted;
            ++report.partial;
            break;
        case ExtractionStatus::Empty:
            manager.setExtractedText(document.id, {});
            ++report.empty;
            break;
        case ExtractionStatus::Unsupported:
            ++report.unsupported;
            break;
        case ExtractionStatus::DependencyMissing:
            ++report.dependencyMissing;
            break;
        case ExtractionStatus::Failed:
            ++report.failed;
            break;
        case ExtractionStatus::TooLarge:
            ++report.tooLarge;
            break;
    }
    if (!result.message.empty()) {
        report.messages.push_back(document.title + ": " + result.message);
    }
}

} // namespace

ExtractionService::ExtractionService(ExtractionOptions options)
    : extractor_(std::move(options)) {}

ExtractionReport ExtractionService::extractAll(DocumentManager& manager) const {
    std::vector<DocumentId> ids;
    ids.reserve(manager.size());
    for (const Document& document : manager.allDocuments()) ids.push_back(document.id);
    return extractDocuments(manager, ids);
}

ExtractionReport ExtractionService::extractDocuments(
    DocumentManager& manager,
    const std::vector<DocumentId>& documentIds
) const {
    ExtractionReport report;
    std::unordered_set<DocumentId> processed;
    processed.reserve(documentIds.size());

    for (const DocumentId id : documentIds) {
        if (!processed.insert(id).second) continue;
        const auto document = manager.findById(id);
        if (!document) {
            ++report.failed;
            report.messages.push_back("Document ID " + std::to_string(id) + ": no longer exists in the document library.");
            continue;
        }
        ++report.attempted;
        try {
            const ExtractionResult result = extractor_.extract(*document);
            applyResult(*document, result, manager, report);
        } catch (const std::exception& error) {
            // A single malformed/unreadable document must not abort an entire
            // folder indexing run or prevent unrelated files from being saved.
            ++report.failed;
            report.messages.push_back(document->title + ": unexpected extraction error: " + error.what());
        } catch (...) {
            ++report.failed;
            report.messages.push_back(document->title + ": unknown extraction error.");
        }
    }
    return report;
}

} // namespace afterimage
