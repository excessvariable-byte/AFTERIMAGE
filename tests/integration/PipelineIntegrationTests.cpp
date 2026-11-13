#include "afterimage/core/DocumentManager.h"
#include "afterimage/extraction/ExtractionService.h"
#include "afterimage/indexing/InvertedIndex.h"
#include "afterimage/graph/MemoryGraph.h"
#include "afterimage/ingestion/IngestionService.h"
#include "afterimage/ranking/RankingEngine.h"
#include "afterimage/retrieval/FragmentRetrievalEngine.h"
#include "afterimage/storage/SQLiteDocumentStore.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifndef AFTERIMAGE_TEST_CORPUS_DIR
#error AFTERIMAGE_TEST_CORPUS_DIR must be defined by CMake
#endif

namespace {
class TemporaryDirectory {
public:
    TemporaryDirectory() {
        path_ = std::filesystem::temp_directory_path() /
            ("afterimage_pipeline_" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path_);
    }
    ~TemporaryDirectory() { std::error_code ignored; std::filesystem::remove_all(path_, ignored); }
    const std::filesystem::path& path() const { return path_; }
private:
    std::filesystem::path path_;
};
const afterimage::RankedResult* findByTitle(
    const std::vector<afterimage::RankedResult>& results,
    const std::vector<afterimage::Document>& documents,
    const std::string& title) {
    for (const auto& result : results) {
        const auto entry = std::find_if(documents.begin(), documents.end(), [&](const auto& d) {
            return d.id == result.retrieval.documentId;
        });
        if (entry != documents.end() && entry->title == title) return &result;
    }
    return nullptr;
}
}

int main() {
    using namespace afterimage;
    namespace fs = std::filesystem;
    TemporaryDirectory temp;
    const fs::path corpus = temp.path() / "corpus";
    fs::copy(AFTERIMAGE_TEST_CORPUS_DIR, corpus, fs::copy_options::recursive);
    const auto databasePath = temp.path() / "state" / "afterimage.db";

    DocumentManager firstSession;
    IngestionService ingestion;
    const auto ingestReport = ingestion.ingestFolder(corpus, firstSession);
    assert(ingestReport.scan.rootValid);
    assert(ingestReport.scan.files.size() == 16);
    assert(ingestReport.importedDocumentIds.size() == 16);
    assert(ingestReport.scan.unsupportedFilesSkipped == 0);

    const auto extraction = ExtractionService{}.extractAll(firstSession);
    assert(extraction.attempted == 16);
    assert(extraction.extracted == 16);
    assert(extraction.failed == 0);
    assert(extraction.unsupported == 0);
    for (const auto& document : firstSession.allDocuments()) {
        assert(!document.extractedText.empty());
    }

    {
        SQLiteDocumentStore store(databasePath);
        store.saveAll(firstSession.allDocuments());
        assert(store.loadAll().size() == 16);
    }

    // Module 4 persistence: close the store and restore every stable ID.
    DocumentManager restored;
    {
        SQLiteDocumentStore reopened(databasePath);
        const auto persisted = reopened.loadAll();
        assert(persisted.size() == 16);
        for (const auto& document : persisted) restored.restoreDocument(document);
    }
    assert(restored.size() == 16);

    // Module 5 indexing: all extracted corpus documents enter the positional index.
    InvertedIndex index;
    for (const auto& document : restored.allDocuments()) index.addDocument(document);
    assert(index.documentCount() == 16);
    assert(index.vocabularySize() > 300);
    assert(index.documentFrequency("leader") >= 2);
    assert(index.averageDocumentLength() > 50.0);

    // Module 6 retrieval + Module 7 ranking across all supplied technical topics.
    FragmentRetrievalEngine retrieval;
    RankingEngine ranking;
    RetrievalOptions retrievalOptions;
    retrievalOptions.maxResults = 100;
    RankingOptions rankingOptions;
    rankingOptions.maxResults = 10;
    rankingOptions.enableRecencyBoost = false; // Stable, content-only test ranking.

    const auto raftCandidates = retrieval.retrieve("raft leader heartbeat", index, retrievalOptions);
    const auto raftResults = ranking.rank("raft leader heartbeat", raftCandidates,
                                           index, restored.allDocuments(), rankingOptions);
    assert(!raftResults.empty());
    const auto* raft = findByTitle(raftResults, restored.allDocuments(), "raft_consensus");
    assert(raft != nullptr);
    assert(raftResults.front().retrieval.documentId == raft->retrieval.documentId);

    // Module 8 graph: use the strongest lexical result as a memory anchor and
    // discover other documents connected through informative shared concepts.
    MemoryGraph graph;
    graph.build(restored.allDocuments());
    assert(graph.documentCount() == 16);
    assert(graph.conceptCount() > 100);
    const auto graphRelated = graph.relatedDocuments({raft->retrieval.documentId});
    assert(!graphRelated.empty());
    assert(std::none_of(graphRelated.begin(), graphRelated.end(), [&](const auto& item) {
        return item.documentId == raft->retrieval.documentId;
    }));
    assert(std::any_of(graphRelated.begin(), graphRelated.end(), [&](const auto& item) {
        const auto relatedDocument = restored.findById(item.documentId);
        return relatedDocument && relatedDocument->title == "distributed_consensus_deep_dive";
    }));

    const auto attentionCandidates = retrieval.retrieve("scaled dot product attention", index, retrievalOptions);
    const auto attentionResults = ranking.rank("scaled dot product attention", attentionCandidates,
                                                index, restored.allDocuments(), rankingOptions);
    assert(!attentionResults.empty());
    const auto* attentionDocument = findByTitle(
        attentionResults, restored.allDocuments(), "transformer_attention_mechanisms");
    assert(attentionDocument != nullptr);
    assert(attentionResults.front().retrieval.documentId == attentionDocument->retrieval.documentId);

    const auto pgCandidates = retrieval.retrieve("xmin xmax vacuum wal", index, retrievalOptions);
    const auto pgResults = ranking.rank("xmin xmax vacuum wal", pgCandidates,
                                         index, restored.allDocuments(), rankingOptions);
    assert(!pgResults.empty());
    assert(findByTitle(pgResults, restored.allDocuments(), "postgresql_mvcc_internals") != nullptr);

    // Prefix, infix, fuzzy and phrase paths operate against the larger corpus.
    assert(!retrieval.retrieve("kubern", index, retrievalOptions).empty());
    assert(!retrieval.retrieve("ntersection", index, retrievalOptions).empty());
    assert(!retrieval.retrieve("consesus", index, retrievalOptions).empty());
    RetrievalOptions phraseOnly;
    phraseOnly.phraseOnly = true;
    const auto handshake = retrieval.retrieve("three way handshake", index, phraseOnly);
    assert(handshake.size() == 1);
    assert(restored.findById(handshake.front().documentId)->title == "tcp_ip_networking");

    // Re-ingestion sees all paths as known, proving storage restoration + ingestion dedup work together.
    const auto secondScan = ingestion.ingestFolder(corpus, restored);
    assert(secondScan.importedDocumentIds.empty());
    assert(secondScan.skippedAlreadyImported == 16);
    assert(restored.size() == 16);

    std::cout << "Pipeline integration passed: 16 documents ingested, extracted, persisted, "
                 "reloaded, indexed, retrieved, ranked, and graph-linked.\n";
    return 0;
}
