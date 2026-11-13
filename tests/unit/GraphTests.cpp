#include "afterimage/graph/MemoryGraph.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {
afterimage::Document doc(afterimage::DocumentId id, std::string title, std::string text) {
    afterimage::Document value;
    value.id = id;
    value.title = std::move(title);
    value.filePath = "/test/" + value.title;
    value.extractedText = std::move(text);
    return value;
}
}

int main() {
    using namespace afterimage;
    const std::vector<Document> docs{
        doc(1, "Raft Consensus", "Raft consensus leader election heartbeat log replication quorum."),
        doc(2, "Leader Election Notes", "Leader election heartbeat timeout quorum consensus protocol."),
        doc(3, "Distributed Consensus", "Consensus protocol quorum voting and leader election safety."),
        doc(4, "Image Processing", "Pixels raster filtering convolution and image segmentation."),
        doc(5, "Unrelated Notes", "Botanical taxonomy chlorophyll photosynthesis and plant cells.")
    };

    MemoryGraph graph;
    graph.build(docs);
    assert(graph.documentCount() == 5);
    assert(graph.conceptCount() > 0);
    assert(graph.edgeCount() >= graph.documentCount());
    assert(graph.containsDocument(1));
    assert(!graph.containsDocument(999));
    assert(!graph.conceptsFor(1).empty());

    const auto related = graph.relatedDocuments({1});
    assert(!related.empty());
    assert(related.front().documentId == 2 || related.front().documentId == 3);
    assert(related.front().score > 0.0);
    assert(related.front().shortestPathHops == 2);
    assert(!related.front().connectingConcepts.empty());
    assert(std::find_if(related.begin(), related.end(), [](const auto& item) {
        return item.documentId == 4 || item.documentId == 5;
    }) == related.end());

    // Multiple seeds are merged and never returned as their own related result.
    const auto multiSeed = graph.relatedDocuments({1, 2});
    for (const auto& item : multiSeed) assert(item.documentId != 1 && item.documentId != 2);

    GraphOptions limited;
    limited.maximumResults = 1;
    assert(graph.relatedDocuments({1}, limited).size() <= 1);
    limited.maximumResults = 0;
    assert(graph.relatedDocuments({1}, limited).empty());
    limited.maximumResults = 5;
    limited.maximumHops = 1;
    assert(graph.relatedDocuments({1}, limited).empty());
    assert(graph.relatedDocuments({999}).empty());
    assert(graph.relatedDocuments({1}, GraphOptions{4, 120, 0.85, 4, 8, 100000.0}).empty());

    // Rebuilding replaces old state rather than accumulating duplicate nodes/edges.
    graph.build({docs.front()});
    assert(graph.documentCount() == 1);
    assert(graph.relatedDocuments({1}).empty());
    graph.clear();
    assert(graph.documentCount() == 0);
    assert(graph.conceptCount() == 0);
    assert(graph.edgeCount() == 0);

    // Optional four-hop traversal can discover a document through an
    // intermediate memory: alpha-document -> gamma-document -> delta-document.
    const std::vector<Document> chain{
        doc(10, "Alpha Topic", "alpha bridge concept"),
        doc(11, "Gamma Topic", "alpha alpha gamma gamma bridge concept"),
        doc(12, "Delta Topic", "gamma gamma delta delta concept")
    };
    graph.build(chain);
    const auto directOnly = graph.relatedDocuments({10});
    assert(std::any_of(directOnly.begin(), directOnly.end(), [](const auto& item) {
        return item.documentId == 11;
    }));
    assert(std::none_of(directOnly.begin(), directOnly.end(), [](const auto& item) {
        return item.documentId == 12;
    }));
    GraphOptions multiHop;
    multiHop.maximumHops = 4;
    const auto expanded = graph.relatedDocuments({10}, multiHop);
    const auto indirect = std::find_if(expanded.begin(), expanded.end(), [](const auto& item) {
        return item.documentId == 12;
    });
    assert(indirect != expanded.end());
    assert(indirect->shortestPathHops == 4);

    std::cout << "All MemoryGraph tests passed.\n";
    return 0;
}
