# Module 7 — Ranking & Relevance Engine

Module 6 generates candidate documents and records how each candidate matched. Module 7 re-orders those candidates with a BM25 relevance score and explainable boosts.

## Scoring

For each distinct matched vocabulary term, BM25 uses:

`IDF(t) = ln(1 + (N - df(t) + 0.5) / (df(t) + 0.5))`

`BM25(t,d) = IDF(t) * (tf(t,d) * (k1 + 1)) / (tf(t,d) + k1 * (1 - b + b * |d| / avgdl))`

The implementation discounts expanded-term matches according to retrieval-path confidence: exact > prefix > infix > fuzzy. Phrase evidence is not double-counted as term frequency; it receives a separate phrase bonus.

The final score is:

`BM25 + title boost + path boost + phrase boost + query-coverage boost + optional recency boost`

- **Title boost:** a matched vocabulary term also appears in the document title.
- **Path boost:** a term appears in the path but not already in the title.
- **Phrase boost:** Module 6 confirmed the exact phrase.
- **Coverage boost:** fraction of distinct query fragments matched in the same document.
- **Recency boost:** exponential half-life decay from the file modification timestamp. It can be disabled and tests can supply a fixed reference timestamp.

The ranking breakdown is returned with every result, so the CLI can explain the score components.

## Index statistics

The inverted index now exposes document frequency, term frequency, document length, and average document length. The index tracks total token counts incrementally when documents are added, replaced, removed, or cleared.
