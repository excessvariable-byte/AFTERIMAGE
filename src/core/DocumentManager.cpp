#include "afterimage/core/DocumentManager.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace afterimage {

DocumentId DocumentManager::addDocument(Document document) {
    if (document.title.empty()) {
        throw std::invalid_argument("A document must have a title.");
    }

    if (nextId_ == std::numeric_limits<DocumentId>::max()) {
        throw std::overflow_error("The document ID space has been exhausted.");
    }

    const DocumentId newId = nextId_;
    const std::size_t newPosition = documents_.size();
    document.id = newId;

    // Insert the lookup entry first. If adding to the vector throws, remove
    // the lookup entry so the two containers cannot become inconsistent.
    const auto [mapEntry, inserted] = indexById_.emplace(newId, newPosition);
    if (!inserted) {
        throw std::logic_error("Generated a duplicate document ID.");
    }

    try {
        documents_.push_back(std::move(document));
    } catch (...) {
        indexById_.erase(mapEntry);
        throw;
    }

    ++nextId_;
    return newId;
}

void DocumentManager::restoreDocument(Document document) {
    if (document.id == 0 || document.title.empty()) {
        throw std::invalid_argument("A restored document needs a non-zero ID and a title.");
    }
    if (indexById_.contains(document.id)) {
        throw std::invalid_argument("A document with this ID is already loaded.");
    }
    if (document.id >= nextId_) {
        if (document.id == std::numeric_limits<DocumentId>::max()) {
            throw std::overflow_error("The document ID space has been exhausted.");
        }
        nextId_ = document.id + 1;
    }
    const std::size_t position = documents_.size();
    const auto [entry, inserted] = indexById_.emplace(document.id, position);
    if (!inserted) throw std::logic_error("Duplicate restored document ID.");
    try { documents_.push_back(std::move(document)); }
    catch (...) { indexById_.erase(entry); throw; }
}

std::optional<Document> DocumentManager::findById(DocumentId id) const {
    const auto mapEntry = indexById_.find(id);
    if (mapEntry == indexById_.end()) {
        return std::nullopt;
    }

    return documents_.at(mapEntry->second);
}

bool DocumentManager::setExtractedText(DocumentId id, std::string text) {
    const auto mapEntry = indexById_.find(id);
    if (mapEntry == indexById_.end()) return false;
    documents_.at(mapEntry->second).extractedText = std::move(text);
    return true;
}

bool DocumentManager::updateFileMetadata(
    DocumentId id,
    std::optional<std::uintmax_t> sizeBytes,
    std::optional<std::int64_t> modifiedAtEpochSeconds
) {
    const auto mapEntry = indexById_.find(id);
    if (mapEntry == indexById_.end()) return false;

    Document& document = documents_.at(mapEntry->second);
    if (document.sizeBytes == sizeBytes &&
        document.modifiedAtEpochSeconds == modifiedAtEpochSeconds) {
        return false;
    }

    document.sizeBytes = sizeBytes;
    document.modifiedAtEpochSeconds = modifiedAtEpochSeconds;
    // The old index text no longer describes the current file.
    document.extractedText.clear();
    return true;
}

const std::vector<Document>& DocumentManager::allDocuments() const noexcept {
    return documents_;
}

std::size_t DocumentManager::size() const noexcept {
    return documents_.size();
}

bool DocumentManager::empty() const noexcept {
    return documents_.empty();
}

} // namespace afterimage
