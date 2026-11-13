#include "afterimage/ranking/RankingEngine.h"

#include "afterimage/indexing/Tokeniser.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace afterimage {
namespace {

double pathReliability(RetrievalPath path) noexcept {
    switch (path) {
        case RetrievalPath::Exact: return 1.0;
        case RetrievalPath::Prefix: return 0.88;
        case RetrievalPath::Infix: return 0.72;
        case RetrievalPath::Fuzzy: return 0.58;
        case RetrievalPath::Phrase: return 0.0; // Phrase evidence is handled as a separate boost.
    }
    return 0.0;
}

std::unordered_set<std::string> tokenSet(std::string_view text) {
    std::unordered_set<std::string> output;
    for (const auto& token : Tokeniser::tokenize(text)) {
        output.insert(token.text);
    }
    return output;
}

std::int64_t currentUnixSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

std::string RankingBreakdown::explanation() const {
    std::ostringstream output;
    output.setf(std::ios::fixed);
    output.precision(3);
    output << "BM25=" << bm25Score
           << ", title+=" << titleBoost
           << ", path+=" << pathBoost
           << ", phrase+=" << phraseBoost
           << ", coverage+=" << coverageBoost
           << ", recency+=" << recencyBoost;
    return output.str();
}

std::vector<RankedResult> RankingEngine::rank(
    std::string_view query,
    const std::vector<RetrievalResult>& candidates,
    const InvertedIndex& index,
    const std::vector<Document>& documents,
    const RankingOptions& options) const {
    if (candidates.empty() || options.maxResults == 0U || index.documentCount() == 0U) {
        return {};
    }

    const double k1 = std::max(0.01, options.k1);
    const double b = std::clamp(options.b, 0.0, 1.0);
    const double averageLength = std::max(1.0, index.averageDocumentLength());
    const double totalDocuments = static_cast<double>(index.documentCount());
    const std::int64_t referenceTime = options.referenceTimeEpochSeconds == 0
        ? currentUnixSeconds() : options.referenceTimeEpochSeconds;

    std::unordered_map<DocumentId, const Document*> documentById;
    documentById.reserve(documents.size());
    for (const auto& document : documents) {
        documentById.emplace(document.id, &document);
    }

    std::unordered_set<std::string> uniqueQueryTerms;
    for (const auto& token : Tokeniser::tokenize(query)) {
        uniqueQueryTerms.insert(token.text);
    }
    const std::size_t queryTermCount = uniqueQueryTerms.size();

    std::vector<RankedResult> ranked;
    ranked.reserve(candidates.size());

    for (const auto& candidate : candidates) {
        const auto documentEntry = documentById.find(candidate.documentId);
        if (documentEntry == documentById.end()) {
            continue;
        }
        const Document& document = *documentEntry->second;
        RankingBreakdown breakdown;

        // Keep only the strongest retrieval route per expanded vocabulary term.
        std::unordered_map<std::string, double> bestReliabilityByTerm;
        for (const auto& match : candidate.matches) {
            if (match.path == RetrievalPath::Phrase || match.matchedTerm.empty()) {
                continue;
            }
            const double reliability = pathReliability(match.path);
            auto [entry, inserted] = bestReliabilityByTerm.emplace(match.matchedTerm, reliability);
            if (!inserted && reliability > entry->second) {
                entry->second = reliability;
            }
        }

        const std::size_t documentLength = index.documentLength(candidate.documentId);
        const double length = static_cast<double>(documentLength);
        for (const auto& [term, reliability] : bestReliabilityByTerm) {
            const std::size_t tfCount = index.termFrequency(candidate.documentId, term);
            const std::size_t dfCount = index.documentFrequency(term);
            if (tfCount == 0U || dfCount == 0U) {
                continue;
            }

            const double tf = static_cast<double>(tfCount);
            const double df = static_cast<double>(dfCount);
            const double inverseDocumentFrequency = std::log(
                1.0 + (totalDocuments - df + 0.5) / (df + 0.5));
            const double lengthNormaliser = 1.0 - b + b * (length / averageLength);
            const double denominator = tf + k1 * lengthNormaliser;
            const double termScore = inverseDocumentFrequency *
                ((tf * (k1 + 1.0)) / denominator);
            breakdown.bm25Score += termScore * reliability;
        }

        const auto titleTerms = tokenSet(document.title);
        const auto pathTerms = tokenSet(document.filePath);
        std::unordered_set<std::string> titleHits;
        std::unordered_set<std::string> pathHits;
        for (const auto& [term, reliability] : bestReliabilityByTerm) {
            (void)reliability;
            if (titleTerms.contains(term)) {
                titleHits.insert(term);
            } else if (pathTerms.contains(term)) {
                // The title is normally derived from the filename. Count it only
                // once as a title match instead of double-counting it as path evidence.
                pathHits.insert(term);
            }
        }
        breakdown.titleTermHits = titleHits.size();
        breakdown.pathTermHits = pathHits.size();
        breakdown.titleBoost = options.titleBoostWeight *
            static_cast<double>(breakdown.titleTermHits);
        breakdown.pathBoost = options.pathBoostWeight *
            static_cast<double>(breakdown.pathTermHits);

        if (candidate.phraseMatched) {
            breakdown.phraseBoost = options.phraseBoostWeight;
        }

        if (queryTermCount > 0U) {
            breakdown.queryCoverage = std::clamp(
                static_cast<double>(candidate.matchedFragments) /
                    static_cast<double>(queryTermCount), 0.0, 1.0);
            breakdown.coverageBoost = options.coverageBoostWeight * breakdown.queryCoverage;
        }

        if (options.enableRecencyBoost && options.recencyBoostWeight > 0.0 &&
            options.recencyHalfLifeDays > 0.0 && document.modifiedAtEpochSeconds.has_value()) {
            const double ageSeconds = std::max(
                0.0, static_cast<double>(referenceTime - *document.modifiedAtEpochSeconds));
            const double ageDays = ageSeconds / 86400.0;
            breakdown.recencyBoost = options.recencyBoostWeight *
                std::exp(-std::log(2.0) * ageDays / options.recencyHalfLifeDays);
        }

        breakdown.finalScore = breakdown.bm25Score + breakdown.titleBoost +
            breakdown.pathBoost + breakdown.phraseBoost + breakdown.coverageBoost +
            breakdown.recencyBoost;
        ranked.push_back(RankedResult{candidate, breakdown});
    }

    std::sort(ranked.begin(), ranked.end(), [](const RankedResult& left, const RankedResult& right) {
        if (std::abs(left.breakdown.finalScore - right.breakdown.finalScore) > 1e-12) {
            return left.breakdown.finalScore > right.breakdown.finalScore;
        }
        return left.retrieval.documentId < right.retrieval.documentId;
    });

    if (ranked.size() > options.maxResults) {
        ranked.resize(options.maxResults);
    }
    return ranked;
}

} // namespace afterimage
