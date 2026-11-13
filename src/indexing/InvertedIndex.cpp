#include "afterimage/indexing/InvertedIndex.h"
#include "afterimage/indexing/Tokeniser.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace afterimage {

void InvertedIndex::addDocument(const Document& document) {
    if (document.id == 0) {
        throw std::invalid_argument("Cannot index a document with ID 0.");
    }

    // Re-indexing the same ID replaces its old postings instead of duplicating them.
    removeDocument(document.id);

    const auto tokens = Tokeniser::tokenize(document.extractedText);
    std::map<std::string, std::vector<std::size_t>, std::less<>> positionsByTerm;
    for (const auto& token : tokens) {
        positionsByTerm[token.text].push_back(token.position);
    }

    std::vector<std::string> documentTerms;
    documentTerms.reserve(positionsByTerm.size());
    for (auto& [term, positions] : positionsByTerm) {
        postingsByTerm_[term].push_back(Posting{document.id, std::move(positions)});
        documentTerms.push_back(term);
    }

    termsByDocument_.emplace(document.id, std::move(documentTerms));
    lengthByDocument_[document.id] = tokens.size();
    totalDocumentLength_ += tokens.size();
    ++documentCount_;
}

bool InvertedIndex::removeDocument(DocumentId id) {
    const auto documentEntry = termsByDocument_.find(id);
    if (documentEntry == termsByDocument_.end()) {
        return false;
    }

    for (const auto& term : documentEntry->second) {
        const auto termEntry = postingsByTerm_.find(term);
        if (termEntry == postingsByTerm_.end()) {
            continue;
        }

        auto& postings = termEntry->second;
        postings.erase(
            std::remove_if(postings.begin(), postings.end(),
                [id](const Posting& posting) { return posting.documentId == id; }),
            postings.end());

        if (postings.empty()) {
            postingsByTerm_.erase(termEntry);
        }
    }

    termsByDocument_.erase(documentEntry);
    const auto lengthEntry = lengthByDocument_.find(id);
    if (lengthEntry != lengthByDocument_.end()) {
        totalDocumentLength_ -= lengthEntry->second;
        lengthByDocument_.erase(lengthEntry);
    }
    --documentCount_;
    return true;
}

void InvertedIndex::clear() noexcept {
    postingsByTerm_.clear();
    termsByDocument_.clear();
    lengthByDocument_.clear();
    documentCount_ = 0;
    totalDocumentLength_ = 0;
}

std::vector<std::string> InvertedIndex::expandPrefix(
    std::string_view prefix,
    std::size_t limit) const {
    std::vector<std::string> matches;
    if (prefix.empty() || limit == 0) {
        return matches;
    }

    const auto first = postingsByTerm_.lower_bound(prefix);
    for (auto current = first; current != postingsByTerm_.end(); ++current) {
        if (current->first.size() < prefix.size() ||
            current->first.compare(0, prefix.size(), prefix) != 0) {
            break;
        }
        matches.push_back(current->first);
        if (matches.size() >= limit) {
            break;
        }
    }
    return matches;
}

namespace {

bool isAsciiWord(std::string_view value) {
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return ch < 128;
    });
}

std::size_t boundedEditDistance(
    std::string_view left,
    std::string_view right,
    std::size_t limit) {
    if (left.size() > right.size() + limit || right.size() > left.size() + limit) {
        return limit + 1;
    }

    std::vector<std::size_t> previous(right.size() + 1);
    std::vector<std::size_t> current(right.size() + 1);
    for (std::size_t j = 0; j <= right.size(); ++j) {
        previous[j] = j;
    }

    for (std::size_t i = 1; i <= left.size(); ++i) {
        current[0] = i;
        std::size_t rowMinimum = current[0];
        for (std::size_t j = 1; j <= right.size(); ++j) {
            const std::size_t substitutionCost = left[i - 1] == right[j - 1] ? 0U : 1U;
            current[j] = std::min({
                previous[j] + 1,
                current[j - 1] + 1,
                previous[j - 1] + substitutionCost
            });
            rowMinimum = std::min(rowMinimum, current[j]);
        }
        if (rowMinimum > limit) {
            return limit + 1;
        }
        previous.swap(current);
    }
    return previous[right.size()];
}

} // namespace

std::vector<std::string> InvertedIndex::expandContaining(
    std::string_view fragment,
    std::size_t limit) const {
    std::vector<std::string> matches;
    if (fragment.empty() || limit == 0 || !isAsciiWord(fragment)) {
        return matches;
    }

    // Prefer terms where the fragment appears earlier, then lexical order.
    std::vector<std::pair<std::size_t, std::string>> candidates;
    for (const auto& [term, postings] : postingsByTerm_) {
        (void)postings;
        if (!isAsciiWord(term)) {
            continue;
        }
        const std::size_t position = term.find(fragment);
        if (position != std::string::npos) {
            candidates.emplace_back(position, term);
        }
    }
    std::sort(candidates.begin(), candidates.end(),
        [](const auto& left, const auto& right) {
            if (left.first != right.first) {
                return left.first < right.first;
            }
            return left.second < right.second;
        });

    for (const auto& candidate : candidates) {
        matches.push_back(candidate.second);
        if (matches.size() >= limit) {
            break;
        }
    }
    return matches;
}

std::vector<std::string> InvertedIndex::expandFuzzy(
    std::string_view word,
    std::size_t maxDistance,
    std::size_t limit) const {
    std::vector<std::string> matches;
    if (word.empty() || limit == 0 || maxDistance == 0 || !isAsciiWord(word)) {
        return matches;
    }

    std::vector<std::pair<std::size_t, std::string>> candidates;
    for (const auto& [term, postings] : postingsByTerm_) {
        (void)postings;
        if (!isAsciiWord(term)) {
            continue;
        }
        const std::size_t distance = boundedEditDistance(word, term, maxDistance);
        if (distance <= maxDistance) {
            candidates.emplace_back(distance, term);
        }
    }
    std::sort(candidates.begin(), candidates.end(),
        [](const auto& left, const auto& right) {
            if (left.first != right.first) {
                return left.first < right.first;
            }
            return left.second < right.second;
        });

    for (const auto& candidate : candidates) {
        matches.push_back(candidate.second);
        if (matches.size() >= limit) {
            break;
        }
    }
    return matches;
}

bool InvertedIndex::phraseOccurs(
    DocumentId id,
    const std::vector<std::string>& terms) const {
    if (terms.empty()) {
        return false;
    }

    const auto findPositions = [this, id](const std::string& term)
        -> const std::vector<std::size_t>* {
        const auto termEntry = postingsByTerm_.find(term);
        if (termEntry == postingsByTerm_.end()) {
            return nullptr;
        }
        const auto posting = std::find_if(
            termEntry->second.begin(), termEntry->second.end(),
            [id](const Posting& candidate) { return candidate.documentId == id; });
        return posting == termEntry->second.end() ? nullptr : &posting->positions;
    };

    const auto* firstPositions = findPositions(terms.front());
    if (firstPositions == nullptr) {
        return false;
    }

    std::vector<const std::vector<std::size_t>*> positionsByTerm;
    positionsByTerm.reserve(terms.size());
    positionsByTerm.push_back(firstPositions);
    for (std::size_t i = 1; i < terms.size(); ++i) {
        const auto* positions = findPositions(terms[i]);
        if (positions == nullptr) {
            return false;
        }
        positionsByTerm.push_back(positions);
    }

    for (const std::size_t start : *firstPositions) {
        bool consecutive = true;
        for (std::size_t offset = 1; offset < positionsByTerm.size(); ++offset) {
            if (!std::binary_search(
                    positionsByTerm[offset]->begin(), positionsByTerm[offset]->end(),
                    start + offset)) {
                consecutive = false;
                break;
            }
        }
        if (consecutive) {
            return true;
        }
    }
    return false;
}

std::vector<SearchResult> InvertedIndex::search(
    std::string_view query,
    const SearchOptions& options) const {
    const auto queryTokens = Tokeniser::tokenize(query);
    if (queryTokens.empty() || documentCount_ == 0) {
        return {};
    }

    std::vector<std::string> queryTerms;
    queryTerms.reserve(queryTokens.size());
    for (const auto& token : queryTokens) {
        queryTerms.push_back(token.text);
    }

    if (options.phraseMatch) {
        if (queryTerms.size() < 2) {
            return {};
        }
        std::vector<SearchResult> phraseResults;
        const auto firstTerm = postingsByTerm_.find(queryTerms.front());
        if (firstTerm == postingsByTerm_.end()) {
            return phraseResults;
        }
        // Only documents containing the first phrase token can possibly match.
        for (const auto& candidate : firstTerm->second) {
            if (phraseOccurs(candidate.documentId, queryTerms)) {
                phraseResults.push_back(SearchResult{
                    candidate.documentId, 1.0, queryTerms.size(), true
                });
            }
        }
        std::sort(phraseResults.begin(), phraseResults.end(),
            [](const SearchResult& left, const SearchResult& right) {
                return left.documentId < right.documentId;
            });
        return phraseResults;
    }

    // Repeated words in the query should not artificially multiply relevance.
    std::vector<std::string> uniqueQueryTerms;
    for (const auto& term : queryTerms) {
        if (std::find(uniqueQueryTerms.begin(), uniqueQueryTerms.end(), term) == uniqueQueryTerms.end()) {
            uniqueQueryTerms.push_back(term);
        }
    }

    struct Accumulator {
        double score{0.0};
        std::size_t matchedTerms{0};
    };
    std::unordered_map<DocumentId, Accumulator> scores;

    for (const auto& queryTerm : uniqueQueryTerms) {
        std::vector<std::string> matchingTerms;
        const auto exact = postingsByTerm_.find(queryTerm);
        if (exact != postingsByTerm_.end()) {
            matchingTerms.push_back(queryTerm);
        }

        if (options.prefixMatch && options.maxPrefixExpansions > 0) {
            const auto expanded = expandPrefix(queryTerm, options.maxPrefixExpansions);
            for (const auto& term : expanded) {
                if (std::find(matchingTerms.begin(), matchingTerms.end(), term) == matchingTerms.end()) {
                    matchingTerms.push_back(term);
                }
            }
        }

        std::unordered_map<DocumentId, double> termScores;
        for (const auto& matchedTerm : matchingTerms) {
            const auto termEntry = postingsByTerm_.find(matchedTerm);
            if (termEntry == postingsByTerm_.end()) {
                continue;
            }
            const double documentFrequency = static_cast<double>(termEntry->second.size());
            const double totalDocuments = static_cast<double>(documentCount_);
            const double inverseDocumentFrequency =
                std::log(1.0 + (totalDocuments + 1.0) / (documentFrequency + 1.0));

            for (const auto& posting : termEntry->second) {
                const double termFrequency = static_cast<double>(posting.positions.size());
                const double frequencyWeight = 1.0 + std::log(termFrequency);
                termScores[posting.documentId] += frequencyWeight * inverseDocumentFrequency;
            }
        }

        for (const auto& [id, score] : termScores) {
            auto& accumulator = scores[id];
            accumulator.score += score;
            ++accumulator.matchedTerms;
        }
    }

    std::vector<SearchResult> results;
    results.reserve(scores.size());
    for (const auto& [id, accumulator] : scores) {
        if (options.requireAllTerms && accumulator.matchedTerms != uniqueQueryTerms.size()) {
            continue;
        }
        results.push_back(SearchResult{
            id,
            accumulator.score,
            accumulator.matchedTerms,
            false
        });
    }

    std::sort(results.begin(), results.end(),
        [](const SearchResult& left, const SearchResult& right) {
            if (left.score != right.score) {
                return left.score > right.score;
            }
            return left.documentId < right.documentId;
        });
    return results;
}

std::size_t InvertedIndex::documentCount() const noexcept { return documentCount_; }
std::size_t InvertedIndex::vocabularySize() const noexcept { return postingsByTerm_.size(); }

std::size_t InvertedIndex::postingCount() const noexcept {
    std::size_t count = 0;
    for (const auto& [term, postings] : postingsByTerm_) {
        (void)term;
        count += postings.size();
    }
    return count;
}

bool InvertedIndex::containsDocument(DocumentId id) const noexcept {
    return termsByDocument_.contains(id);
}

std::size_t InvertedIndex::documentFrequency(std::string_view term) const noexcept {
    const auto entry = postingsByTerm_.find(term);
    return entry == postingsByTerm_.end() ? 0U : entry->second.size();
}

std::size_t InvertedIndex::termFrequency(DocumentId id, std::string_view term) const noexcept {
    const auto entry = postingsByTerm_.find(term);
    if (entry == postingsByTerm_.end()) return 0U;
    const auto posting = std::find_if(entry->second.begin(), entry->second.end(),
        [id](const Posting& candidate) { return candidate.documentId == id; });
    return posting == entry->second.end() ? 0U : posting->positions.size();
}

std::size_t InvertedIndex::documentLength(DocumentId id) const noexcept {
    const auto entry = lengthByDocument_.find(id);
    return entry == lengthByDocument_.end() ? 0U : entry->second;
}

double InvertedIndex::averageDocumentLength() const noexcept {
    if (documentCount_ == 0U) return 0.0;
    return static_cast<double>(totalDocumentLength_) / static_cast<double>(documentCount_);
}

} // namespace afterimage
