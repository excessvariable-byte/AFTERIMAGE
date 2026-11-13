#pragma once

#include "afterimage/core/Document.h"
#include "afterimage/indexing/SearchOptions.h"
#include "afterimage/indexing/SearchResult.h"

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace afterimage {

// An in-memory inverted index: each vocabulary term points to the documents
// containing it and the word positions where it occurs.
class InvertedIndex {
public:
    void addDocument(const Document& document);
    bool removeDocument(DocumentId id);
    void clear() noexcept;

    std::vector<SearchResult> search(
        std::string_view query,
        const SearchOptions& options = {}) const;

    std::vector<std::string> expandPrefix(
        std::string_view prefix,
        std::size_t limit = 20) const;

    // Secondary vocabulary scans used by fragment retrieval. Infix and fuzzy
    // expansion are intentionally bounded because they examine more terms.
    std::vector<std::string> expandContaining(
        std::string_view fragment,
        std::size_t limit = 8) const;

    std::vector<std::string> expandFuzzy(
        std::string_view word,
        std::size_t maxDistance = 1,
        std::size_t limit = 8) const;

    std::size_t documentCount() const noexcept;
    std::size_t vocabularySize() const noexcept;
    std::size_t postingCount() const noexcept;
    bool containsDocument(DocumentId id) const noexcept;

    // Collection statistics exposed to the ranking layer (BM25).
    std::size_t documentFrequency(std::string_view term) const noexcept;
    std::size_t termFrequency(DocumentId id, std::string_view term) const noexcept;
    std::size_t documentLength(DocumentId id) const noexcept;
    double averageDocumentLength() const noexcept;

private:
    struct Posting {
        DocumentId documentId{0};
        std::vector<std::size_t> positions;
    };

    // std::map keeps terms ordered, making prefix expansion efficient with lower_bound.
    std::map<std::string, std::vector<Posting>, std::less<>> postingsByTerm_;
    // Records the unique terms for each document so replacing/removing a document
    // only visits terms that actually belong to it.
    std::unordered_map<DocumentId, std::vector<std::string>> termsByDocument_;
    std::unordered_map<DocumentId, std::size_t> lengthByDocument_;
    std::size_t documentCount_{0};
    std::size_t totalDocumentLength_{0};

    bool phraseOccurs(DocumentId id, const std::vector<std::string>& terms) const;
};

} // namespace afterimage
