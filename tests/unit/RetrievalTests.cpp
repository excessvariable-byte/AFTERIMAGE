#include "afterimage/indexing/InvertedIndex.h"
#include "afterimage/retrieval/FragmentRetrievalEngine.h"

#include <cassert>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

afterimage::Document makeDocument(
    afterimage::DocumentId id,
    std::string title,
    std::string text) {
    afterimage::Document document;
    document.id = id;
    document.title = std::move(title);
    document.filePath = "/virtual/" + document.title + ".txt";
    document.fileType = ".txt";
    document.extractedText = std::move(text);
    return document;
}

bool hasPath(const afterimage::RetrievalResult& result, afterimage::RetrievalPath path) {
    for (const auto& match : result.matches) {
        if (match.path == path) return true;
    }
    return false;
}

} // namespace

int main() {
    using namespace afterimage;

    InvertedIndex index;
    index.addDocument(makeDocument(
        1, "scheduling_graph_notes",
        "Graph traversal scheduling uses breadth first search. A queue stores nodes."));
    index.addDocument(makeDocument(
        2, "graph_structure_notes",
        "Graph structures support traversal, adjacency lists, and connected components."));
    index.addDocument(makeDocument(
        3, "binary_tree_notes",
        "Binary search trees use depth first search and balanced rotations."));
    index.addDocument(makeDocument(4, "leader_notes", "A leader sends a heartbeat to followers."));
    index.addDocument(makeDocument(5, "header_notes", "A packet header stores protocol metadata."));

    FragmentRetrievalEngine engine;

    // Exact word route.
    auto results = engine.retrieve("graph", index);
    assert(!results.empty());
    assert(results.front().documentId == 1 || results.front().documentId == 2);
    assert(hasPath(results.front(), RetrievalPath::Exact));

    // Prefix route: a remembered beginning matches a longer indexed token.
    results = engine.retrieve("sched", index);
    assert(results.size() == 1);
    assert(results.front().documentId == 1);
    assert(hasPath(results.front(), RetrievalPath::Prefix));

    // Infix route: the remembered fragment occurs inside a word.
    results = engine.retrieve("ched", index);
    assert(results.size() == 1);
    assert(results.front().documentId == 1);
    assert(hasPath(results.front(), RetrievalPath::Infix));

    // Fuzzy route: one missing character is tolerated for sufficiently long words.
    results = engine.retrieve("schedulng", index);
    assert(results.size() == 1);
    assert(results.front().documentId == 1);
    assert(hasPath(results.front(), RetrievalPath::Fuzzy));

    // Phrase evidence merges with exact/prefix evidence; one document is returned once.
    results = engine.retrieve("graph traversal", index);
    assert(!results.empty());
    assert(results.front().documentId == 1);
    assert(results.front().phraseMatched);
    assert(hasPath(results.front(), RetrievalPath::Phrase));
    assert(results.front().explanation().find("exact phrase") != std::string::npos);

    // Require every unique clue fragment to match somewhere in the same document.
    RetrievalOptions allFragments;
    allFragments.requireAllFragments = true;
    results = engine.retrieve("graph sched", index, allFragments);
    assert(results.size() == 1);
    assert(results.front().documentId == 1);
    assert(results.front().matchedFragments == 2);

    // A query with one absent fragment fails when all fragments are required.
    results = engine.retrieve("graph nonexistent", index, allFragments);
    assert(results.empty());

    // Phrase-only mode requires adjacent exact words in the correct order.
    RetrievalOptions phraseOnly;
    phraseOnly.phraseOnly = true;
    results = engine.retrieve("breadth first search", index, phraseOnly);
    assert(results.size() == 1);
    assert(results.front().documentId == 1);
    assert(results.front().phraseMatched);
    assert(results.front().matchedFragments == 3);
    assert(engine.retrieve("first breadth", index, phraseOnly).empty());

    // Retrieval can be reduced to exact matching, without fuzzy/prefix/infix expansion.
    RetrievalOptions exactOnly;
    exactOnly.enablePrefix = false;
    exactOnly.enableInfix = false;
    exactOnly.enableFuzzy = false;
    exactOnly.enablePhrase = false;
    assert(engine.retrieve("ched", index, exactOnly).empty());
    assert(engine.retrieve("schedulng", index, exactOnly).empty());
    assert(engine.retrieve("graph", index, exactOnly).size() == 2);

    RetrievalOptions noFuzzy;
    noFuzzy.enableFuzzy = false;
    assert(engine.retrieve("schedulng", index, noFuzzy).empty());

    // Fuzzy matching is a fallback: an exact hit for 'leader' should prevent
    // unrelated one-edit terms such as 'header' from becoming extra candidates.
    results = engine.retrieve("leader", index);
    assert(results.size() == 1);
    assert(results.front().documentId == 4);
    assert(!hasPath(results.front(), RetrievalPath::Fuzzy));

    // Result limits are applied after score ordering.
    RetrievalOptions oneResult;
    oneResult.maxResults = 1;
    results = engine.retrieve("graph traversal", index, oneResult);
    assert(results.size() == 1);

    // Empty queries do not create candidates.
    assert(engine.retrieve(" !!! ", index).empty());

    std::cout << "All Retrieval tests passed.\n";
    return 0;
}
