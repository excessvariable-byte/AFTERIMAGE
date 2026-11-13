# Module 6 — Fragment Retrieval Engine

Module 6 sits above the Module 5 inverted index. It generates candidates through several independent retrieval paths, merges candidates by `DocumentId`, and retains match evidence so the application can explain why a document was returned.

## Retrieval paths

1. **Exact:** query tokens match the same indexed word.
2. **Prefix:** a fragment at the start of a word expands through the ordered vocabulary (`sched` → `scheduling`).
3. **Infix:** a fragment from the middle of a word is found by a bounded vocabulary scan (`ched` → `scheduling`).
4. **Fuzzy:** a typo-tolerant, bounded Levenshtein-distance expansion can recover a near spelling (`schedulng` → `scheduling`). Fuzzy matching is restricted to ASCII vocabulary terms for now.
5. **Phrase:** exact adjacent words add a phrase signal. `--phrase` makes phrase matching the only enabled retrieval path.

## Candidate merging and explanations

A document can be found through multiple paths, but it is returned only once. Its score accumulates weighted evidence from distinct query-fragment/path/term combinations. Each `RetrievalResult` keeps the matches and can produce a human-readable explanation.

The weights are starting heuristics, not calibrated probabilities. Module 7 will replace this baseline with more principled ranking, such as BM25, recency signals, field weighting, and score calibration.

## Complexity notes

- Exact term lookup uses the inverted index map.
- Prefix expansion starts at `std::map::lower_bound` and visits only the matching prefix range (plus its limit).
- Infix expansion scans the vocabulary: O(V × L) in a simple upper-bound description, where V is vocabulary size and L is average term comparison length.
- Fuzzy expansion scans vocabulary and runs bounded edit distance per term; it is the most expensive route in this module. Query lengths, edit distance and expansion counts are bounded by `RetrievalOptions`.
- Candidate merging uses hash maps keyed by `DocumentId`.

## Limitations

- No date, person, location, or relationship clues yet; those require metadata and graph/time indexes.
- No semantic embedding retrieval.
- Fuzzy comparison is ASCII-oriented; UTF-8 token preservation does not imply Unicode-aware edit distance or case folding.
- The current scoring weights are heuristic and may over-reward documents matching the same query clue through multiple distinct routes. Module 7 will address calibration and ranking quality.
- The underlying index is still rebuilt from SQLite-extracted text at startup.
