# Module 5 — Text Indexing Engine

Module 5 builds a searchable in-memory inverted index from the extracted text stored in each `Document`.

## What is implemented

- **Tokenisation:** splits text into lowercase searchable tokens and records each token's zero-based word position.
- **Inverted index:** maps each term to postings containing document IDs and all positions where the term occurs.
- **Term frequency:** repeated occurrences contribute to a document's score.
- **TF-IDF-style scoring:** combines logarithmic term frequency with inverse document frequency. This is a first relevance baseline; BM25 and richer ranking belong in Module 7.
- **Prefix expansion:** a partial token such as `travers` can expand to indexed words such as `traversal`.
- **All-term search:** optionally requires every query token to match.
- **Phrase search:** uses recorded positions to require adjacent query words in the exact order.
- **Update and removal:** re-indexing an existing document ID replaces its old postings; documents can be removed from the index.

## Data structures

`std::map<std::string, std::vector<Posting>>` stores terms in lexical order and supports prefix lookup with `lower_bound`. Each `Posting` stores a `DocumentId` and a sorted vector of word positions.

`std::unordered_map<DocumentId, std::vector<std::string>>` remembers the unique terms belonging to each document, which makes document removal practical without scanning the whole vocabulary.

## Persistence decision

The index is currently rebuilt from `Document::extractedText` after Module 4 loads the SQLite document records. The SQLite database remains the source of truth. This avoids having to keep two independently persisted representations in sync. Persistent precomputed indexes can be considered later if corpus size makes rebuild time significant.

## Limitations

- Token boundaries are currently ASCII-oriented. UTF-8 bytes are kept together in tokens, but language-aware Unicode case folding and word segmentation are not implemented.
- No stemming, lemmatisation, stop-word removal, fuzzy edit-distance matching, or semantic embeddings yet.
- Prefix matching is bounded by `SearchOptions::maxPrefixExpansions`.
- Scores are a simple TF-IDF-style baseline, not a final ranking model.
- Only TXT and Markdown currently provide extracted text; PDF and images remain unsearchable until their extractors are added.
