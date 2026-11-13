#include "afterimage/indexing/InvertedIndex.h"
#include "afterimage/indexing/Tokeniser.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

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

} // namespace

int main() {
    using namespace afterimage;

    // Tokenisation: punctuation separates words, case is ignored, and each
    // token remembers its zero-based word position.
    const auto tokens = Tokeniser::tokenize("Graph, TRAVERSAL! graph-123");
    assert(tokens.size() == 4);
    assert(tokens[0].text == "graph" && tokens[0].position == 0);
    assert(tokens[1].text == "traversal" && tokens[1].position == 1);
    assert(tokens[2].text == "graph" && tokens[2].position == 2);
    assert(tokens[3].text == "123" && tokens[3].position == 3);

    InvertedIndex index;
    const auto graphDoc = makeDocument(
        1, "graph_notes", "Graph traversal uses breadth first search. Graph structures are useful.");
    const auto treeDoc = makeDocument(
        2, "tree_notes", "Binary search trees use depth first search.");
    const auto memoryDoc = makeDocument(
        3, "memory_fragment", "I remember reading about graph theory last week.");
    const auto emptyDoc = makeDocument(4, "empty", "   ... ");

    index.addDocument(graphDoc);
    index.addDocument(treeDoc);
    index.addDocument(memoryDoc);
    index.addDocument(emptyDoc);

    assert(index.documentCount() == 4);
    assert(index.vocabularySize() > 10);
    assert(index.postingCount() >= index.vocabularySize());
    assert(index.containsDocument(1));
    assert(!index.containsDocument(999));
    assert(index.documentFrequency("graph") == 2);
    assert(index.documentFrequency("missing") == 0);
    assert(index.termFrequency(1, "graph") == 2);
    assert(index.termFrequency(2, "graph") == 0);
    assert(index.documentLength(1) == Tokeniser::tokenize(graphDoc.extractedText).size());
    assert(index.documentLength(999) == 0);
    assert(index.averageDocumentLength() > 0.0);

    const auto graphResults = index.search("GRAPH");
    assert(graphResults.size() == 2);
    assert(graphResults.front().documentId == 1); // More occurrences rank first.
    assert(graphResults.front().score > 0.0);

    const auto prefixResults = index.search("travers");
    assert(prefixResults.size() == 1);
    assert(prefixResults.front().documentId == 1);
    const auto expansions = index.expandPrefix("travers");
    assert(expansions.size() == 1 && expansions.front() == "traversal");
    assert(index.expandPrefix("travers", 0).empty());
    assert(index.expandPrefix("missing").empty());
    assert(index.expandPrefix("").empty());
    SearchOptions exactOnly;
    exactOnly.prefixMatch = false;
    assert(index.search("travers", exactOnly).empty());

    SearchOptions allTerms;
    allTerms.requireAllTerms = true;
    const auto allResults = index.search("graph search", allTerms);
    assert(allResults.size() == 1);
    assert(allResults.front().documentId == 1);
    assert(allResults.front().matchedTerms == 2);

    SearchOptions phrase;
    phrase.phraseMatch = true;
    const auto phraseResults = index.search("breadth first search", phrase);
    assert(phraseResults.size() == 1);
    assert(phraseResults.front().documentId == 1);
    assert(phraseResults.front().phraseMatched);
    assert(index.search("first breadth", phrase).empty());

    // Re-indexing an existing ID replaces its previous terms.
    const auto oldLength = index.documentLength(1);
    index.addDocument(makeDocument(1, "graph_notes", "Only quantum mechanics now."));
    assert(index.search("traversal").empty());
    assert(index.search("quantum").size() == 1);
    assert(index.documentLength(1) == 4);
    assert(index.documentLength(1) < oldLength);
    assert(index.documentFrequency("graph") == 1);

    assert(index.removeDocument(2));
    assert(!index.removeDocument(2));
    assert(index.documentCount() == 3);
    assert(index.documentLength(2) == 0);

    bool rejectedZeroId = false;
    try {
        index.addDocument(makeDocument(0, "invalid", "not allowed"));
    } catch (const std::invalid_argument&) {
        rejectedZeroId = true;
    }
    assert(rejectedZeroId);

    index.clear();
    assert(index.documentCount() == 0);
    assert(index.vocabularySize() == 0);
    assert(index.averageDocumentLength() == 0.0);
    assert(index.documentLength(1) == 0);
    assert(index.search("anything").empty());

    std::cout << "All Indexing tests passed.\n";
    return 0;
}
