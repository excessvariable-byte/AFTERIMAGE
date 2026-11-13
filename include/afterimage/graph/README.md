# Module 8 — Memory Relationship Graph

`MemoryGraph` builds a rebuildable bipartite graph from the document library:

- document nodes represent indexed files;
- concept nodes represent informative terms extracted from titles and repeated body terms;
- `mentions` edges connect a document to each selected concept.

Two documents are related when they share a concept. The graph filters stopwords and concepts that occur across too much of the library, then weights connections using inverse document frequency. This reduces generic hubs and makes rarer shared concepts more useful.

## API

```cpp
afterimage::MemoryGraph graph;
graph.build(documents);
const auto related = graph.relatedDocuments({seedDocumentId});
```

`GraphOptions` controls concept length, per-document concept limits, common-concept filtering, maximum traversal hops, result count and minimum score. The default is a precise two-hop document → concept → document lookup. Set `maximumHops = 4` to allow one intermediate related document.

The graph is deliberately derived in memory from the SQLite document library. It is rebuilt when a search session starts; it is not a second source of truth and does not modify document records.

## Current boundaries

Concept extraction is deterministic lexical heuristics, not a trained named-entity recogniser or semantic model. It does not yet identify people, dates, events, organisations, or causal relationships. A shared concept indicates a possible connection, not proof that two files are semantically equivalent. Treat graph recommendations as supplementary to direct lexical matches.
