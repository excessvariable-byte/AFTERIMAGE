#include "afterimage/graph/MemoryGraph.h"
#include "afterimage/indexing/Tokeniser.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>

namespace afterimage {
namespace {

const std::unordered_set<std::string>& stopWords() {
    static const std::unordered_set<std::string> words{
        "about","above","after","again","against","also","among","another","because",
        "been","before","being","below","between","both","could","does","doing",
        "down","during","each","else","ever","every","from","further","have",
        "having","here","into","just","less","many","more","most","much","other",
        "over","same","some","such","than","that","their","them","then","there",
        "these","they","this","those","through","under","until","very","what","when",
        "where","which","while","with","would","your","will","shall","should","using",
        "used","uses","document","documents","notes","note","and","the","for","are",
        "was","were","not","but","can","may","per","via","its","all","any","being",
        "each","few","get","got","has","had","how","our","out","own","put","say",
        "see","set","she","too","use","way","who","why","you","than","then","also",
        "based","common","different","including","however","often","typically","usually",
        "actually","allows","across","within","without","provide","provides","provided",
        "include","includes","including","make","makes","made","need","needs","needed",
        "work","works","working","used","using","system","systems","data","value","values",
        "machine","machines","time","times","operation","operations","state","states",
        "network","networks","process","processes","memory","file","files","node","nodes",
        "cluster","clusters","control","controls","component","components","method","methods",
        "result","results","example","examples","level","levels","point","points","part","parts",
        "implementation","implementations","approach","approaches","design","based","support","supports"
    };
    return words;
}

std::vector<std::string> extractConcepts(const Document& document, std::size_t minLength,
                                         std::size_t limit) {
    std::unordered_map<std::string, std::size_t> titleFrequency;
    std::unordered_map<std::string, std::size_t> bodyFrequency;
    for (const auto& token : Tokeniser::tokenize(document.title)) {
        if (token.text.size() >= minLength && !stopWords().contains(token.text)) {
            ++titleFrequency[token.text];
        }
    }
    for (const auto& token : Tokeniser::tokenize(document.extractedText)) {
        if (token.text.size() >= minLength && !stopWords().contains(token.text)) {
            ++bodyFrequency[token.text];
        }
    }

    std::unordered_map<std::string, std::size_t> frequency = titleFrequency;
    for (const auto& [term, count] : bodyFrequency) {
        // Single incidental body words create weak, accidental graph edges.
        // Keep repeated body concepts or terms explicitly present in the title.
        if (count >= 2 || titleFrequency.contains(term)) frequency[term] += count;
    }
    for (auto& [term, count] : frequency) {
        (void)term;
        count += 2 * titleFrequency[term];
    }

    std::vector<std::pair<std::string, std::size_t>> ranked(frequency.begin(), frequency.end());
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });
    if (ranked.size() > limit) ranked.resize(limit);

    std::vector<std::string> concepts;
    concepts.reserve(ranked.size());
    for (auto& entry : ranked) concepts.push_back(std::move(entry.first));
    return concepts;
}
} // namespace

void MemoryGraph::clear() noexcept {
    conceptsByDocument_.clear();
    documentsByConcept_.clear();
    documentIds_.clear();
    edgeCount_ = 0;
}

void MemoryGraph::build(const std::vector<Document>& documents, const GraphOptions& options) {
    clear();
    const auto minLength = std::max<std::size_t>(1, options.minimumConceptLength);
    const auto perDocumentLimit = std::max<std::size_t>(1, options.maximumConceptsPerDocument);

    // First pass: extract candidate concepts and count document frequency.
    std::unordered_map<DocumentId, std::vector<std::string>> candidates;
    std::unordered_map<std::string, std::size_t> documentFrequency;
    for (const auto& document : documents) {
        if (document.id == 0 || documentIds_.contains(document.id)) continue;
        documentIds_.insert(document.id);
        auto concepts = extractConcepts(document, minLength, perDocumentLimit);
        for (const auto& conceptTerm : concepts) ++documentFrequency[conceptTerm];
        candidates.emplace(document.id, std::move(concepts));
    }

    // Second pass: remove overly common concepts; they create noisy hubs.
    const double maxRatio = std::clamp(options.maximumDocumentFrequencyRatio, 0.05, 1.0);
    const double maxDf = std::max(2.0, maxRatio * static_cast<double>(documentIds_.size()));
    for (auto& [documentId, concepts] : candidates) {
        auto& accepted = conceptsByDocument_[documentId];
        for (const auto& conceptTerm : concepts) {
            const auto df = documentFrequency[conceptTerm];
            if (static_cast<double>(df) > maxDf && documentIds_.size() > 2) continue;
            accepted.push_back(conceptTerm);
            documentsByConcept_[conceptTerm].push_back(documentId);
            ++edgeCount_;
        }
    }
}

std::vector<RelatedDocument> MemoryGraph::relatedDocuments(
    const std::vector<DocumentId>& seedDocuments, const GraphOptions& options) const {
    if (seedDocuments.empty() || options.maximumResults == 0 || options.maximumHops < 2) return {};

    std::unordered_set<DocumentId> seeds;
    for (const auto id : seedDocuments) if (documentIds_.contains(id)) seeds.insert(id);
    if (seeds.empty()) return {};

    struct State { DocumentId id; std::size_t hops; double score; };
    std::queue<State> queue;
    std::unordered_map<DocumentId, double> bestScore;
    std::unordered_map<DocumentId, std::size_t> bestHops;
    std::unordered_map<DocumentId, std::unordered_map<std::string, std::size_t>> evidenceDepth;

    for (const auto id : seeds) {
        bestScore[id] = 0.0;
        bestHops[id] = 0;
        queue.push({id, 0, 0.0});
    }

    while (!queue.empty()) {
        const State current = queue.front();
        queue.pop();
        if (current.hops + 2 > options.maximumHops) continue;
        const auto conceptIt = conceptsByDocument_.find(current.id);
        if (conceptIt == conceptsByDocument_.end()) continue;

        for (const auto& conceptTerm : conceptIt->second) {
            const auto docsIt = documentsByConcept_.find(conceptTerm);
            if (docsIt == documentsByConcept_.end()) continue;
            const double idf = std::log(1.0 + static_cast<double>(documentIds_.size()) /
                                               static_cast<double>(docsIt->second.size()));
            for (const auto neighbour : docsIt->second) {
                if (neighbour == current.id || seeds.contains(neighbour)) continue;
                const std::size_t nextHops = current.hops + 2;
                const double nextScore = current.score + idf / static_cast<double>(nextHops / 2);
                auto& conceptDepth = evidenceDepth[neighbour][conceptTerm];
                if (conceptDepth == 0 || nextHops < conceptDepth) conceptDepth = nextHops;
                const auto existing = bestHops.find(neighbour);
                if (existing == bestHops.end() || nextHops < existing->second) {
                    bestHops[neighbour] = nextHops;
                    bestScore[neighbour] = nextScore;
                    queue.push({neighbour, nextHops, nextScore});
                } else if (nextHops == existing->second) {
                    bestScore[neighbour] = std::max(bestScore[neighbour], nextScore);
                }
            }
        }
    }

    std::vector<RelatedDocument> results;
    for (const auto& [id, pathScore] : bestScore) {
        (void)pathScore;
        if (seeds.contains(id)) continue;
        RelatedDocument result;
        result.documentId = id;
        result.score = 0.0;
        result.shortestPathHops = bestHops[id];
        for (const auto& [conceptTerm, hops] : evidenceDepth[id]) {
            (void)hops;
            result.connectingConcepts.push_back(conceptTerm);
        }
        std::sort(result.connectingConcepts.begin(), result.connectingConcepts.end(),
            [&](const std::string& left, const std::string& right) {
                const auto leftHops = evidenceDepth[id].at(left);
                const auto rightHops = evidenceDepth[id].at(right);
                const double leftScore = std::log(1.0 + static_cast<double>(documentIds_.size()) /
                    static_cast<double>(documentsByConcept_.at(left).size())) /
                    static_cast<double>(std::max<std::size_t>(1, leftHops / 2));
                const double rightScore = std::log(1.0 + static_cast<double>(documentIds_.size()) /
                    static_cast<double>(documentsByConcept_.at(right).size())) /
                    static_cast<double>(std::max<std::size_t>(1, rightHops / 2));
                if (std::abs(leftScore - rightScore) > 1e-12) return leftScore > rightScore;
                return left < right;
            });
        for (const auto& conceptTerm : result.connectingConcepts) {
            const double idf = std::log(1.0 + static_cast<double>(documentIds_.size()) /
                static_cast<double>(documentsByConcept_.at(conceptTerm).size()));
            const auto hops = evidenceDepth[id].at(conceptTerm);
            result.score += idf / static_cast<double>(std::max<std::size_t>(1, hops / 2));
        }
        if (result.score < options.minimumScore) continue;
        if (result.connectingConcepts.size() > 5) result.connectingConcepts.resize(5);
        results.push_back(std::move(result));
    }
    std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) {
        if (std::abs(a.score - b.score) > 1e-12) return a.score > b.score;
        return a.documentId < b.documentId;
    });
    if (results.size() > options.maximumResults) results.resize(options.maximumResults);
    return results;
}

std::size_t MemoryGraph::documentCount() const noexcept { return documentIds_.size(); }
std::size_t MemoryGraph::conceptCount() const noexcept { return documentsByConcept_.size(); }
std::size_t MemoryGraph::edgeCount() const noexcept { return edgeCount_; }
bool MemoryGraph::containsDocument(DocumentId id) const noexcept { return documentIds_.contains(id); }

const std::vector<std::string>& MemoryGraph::conceptsFor(DocumentId id) const {
    static const std::vector<std::string> empty;
    const auto found = conceptsByDocument_.find(id);
    return found == conceptsByDocument_.end() ? empty : found->second;
}

} // namespace afterimage
