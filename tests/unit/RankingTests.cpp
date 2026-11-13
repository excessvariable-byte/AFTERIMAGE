#include <algorithm>
#include "afterimage/indexing/InvertedIndex.h"
#include "afterimage/ranking/RankingEngine.h"
#include "afterimage/retrieval/FragmentRetrievalEngine.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {
afterimage::Document makeDocument(afterimage::DocumentId id, std::string title,
                                  std::string path, std::string body,
                                  std::int64_t modified) {
    afterimage::Document d;
    d.id = id;
    d.title = std::move(title);
    d.filePath = std::move(path);
    d.fileType = "txt";
    d.extractedText = std::move(body);
    d.modifiedAtEpochSeconds = modified;
    return d;
}
}

int main() {
    using namespace afterimage;
    const std::int64_t now = 2'000'000'000;
    std::vector<Document> documents{
        makeDocument(1, "Raft Consensus", "/library/raft_consensus.txt",
            "Raft leader heartbeat election timeout. Raft leader log replication and quorum commit.", now - 86400),
        makeDocument(2, "General Systems Notes", "/library/general_notes.txt",
            "A cluster can elect a leader. The leader sends messages and replicates a log entry. "
            "These notes also discuss many unrelated storage, network, memory, and compiler concepts.", now - 86400 * 180),
        makeDocument(3, "Raft Overview", "/library/archive/raft_overview.txt",
            "Raft leader heartbeat election timeout. Raft leader log replication and quorum commit.", now - 86400 * 365),
        makeDocument(4, "Operations", "/library/raft_context/operations.txt",
            "Raft leader coordinates a quorum and replicates commands.", now - 86400 * 30)
    };

    InvertedIndex index;
    for (const auto& document : documents) index.addDocument(document);
    assert(index.documentCount() == 4);

    FragmentRetrievalEngine retrieval;
    RetrievalOptions retrievalOptions;
    retrievalOptions.maxResults = 100;
    const auto candidates = retrieval.retrieve("raft leader", index, retrievalOptions);
    assert(candidates.size() == 4);

    RankingEngine engine;
    RankingOptions rankingOptions;
    rankingOptions.referenceTimeEpochSeconds = now;
    rankingOptions.maxResults = 10;
    const auto ranked = engine.rank("raft leader", candidates, index, documents, rankingOptions);
    assert(ranked.size() == 4);
    assert(ranked.front().retrieval.documentId == 1);
    assert(ranked.front().breakdown.bm25Score > 0.0);
    assert(ranked.front().breakdown.titleTermHits >= 1);
    assert(ranked.front().breakdown.phraseBoost > 0.0);
    assert(ranked.front().breakdown.coverageBoost > 0.0);
    assert(ranked.front().breakdown.finalScore > ranked.front().breakdown.bm25Score);
    assert(ranked.front().breakdown.explanation().find("BM25=") != std::string::npos);

    const auto pathBoosted = std::find_if(ranked.begin(), ranked.end(), [](const RankedResult& item) {
        return item.retrieval.documentId == 4;
    });
    assert(pathBoosted != ranked.end());
    assert(pathBoosted->breakdown.pathBoost > 0.0);
    assert(pathBoosted->breakdown.titleBoost == 0.0);

    // Recency is independently switchable and deterministic with a fixed clock.
    const auto recentPairCandidates = retrieval.retrieve("heartbeat", index, retrievalOptions);
    RankingOptions withRecency;
    withRecency.referenceTimeEpochSeconds = now;
    const auto withRecent = engine.rank("heartbeat", recentPairCandidates, index, documents, withRecency);
    assert(withRecent.front().retrieval.documentId == 1);
    assert(withRecent.front().breakdown.recencyBoost > withRecent.back().breakdown.recencyBoost);

    RankingOptions withoutRecency = withRecency;
    withoutRecency.enableRecencyBoost = false;
    const auto noRecent = engine.rank("heartbeat", recentPairCandidates, index, documents, withoutRecency);
    for (const auto& item : noRecent) assert(item.breakdown.recencyBoost == 0.0);

    // Limiting occurs after ranking; zero results returns an empty vector.
    RankingOptions topOne = rankingOptions;
    topOne.maxResults = 1;
    assert(engine.rank("raft leader", candidates, index, documents, topOne).size() == 1);
    topOne.maxResults = 0;
    assert(engine.rank("raft leader", candidates, index, documents, topOne).empty());
    assert(engine.rank("raft leader", {}, index, documents).empty());

    // Candidates with no corresponding document metadata are safely discarded.
    auto orphan = candidates;
    orphan.push_back(RetrievalResult{999, 2.0, {}, 0, false});
    const auto withoutOrphan = engine.rank("raft leader", orphan, index, documents, rankingOptions);
    assert(withoutOrphan.size() == ranked.size());

    // BM25 length normalisation and IDF are finite, positive for matching terms.
    for (const auto& item : ranked) {
        assert(std::isfinite(item.breakdown.finalScore));
        assert(item.breakdown.finalScore >= 0.0);
    }

    std::cout << "All Ranking tests passed.\n";
    return 0;
}
