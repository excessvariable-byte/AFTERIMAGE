# AFTERIMAGE Module Map

| Module | Responsibility | Primary implementation |
|---|---|---|
| 1. Core document model | Document records, stable IDs and source metadata refresh | `include/afterimage/core`, `src/core` |
| 2. Ingestion | Recursive filesystem discovery, duplicate paths and changed-file detection | `include/afterimage/ingestion`, `src/ingestion` |
| 3. Extraction | TXT/Markdown, PDF text extraction, image OCR and scanned-PDF OCR | `include/afterimage/extraction`, `src/extraction` |
| 4. Persistence | SQLite document repository | `include/afterimage/storage`, `src/storage` |
| 5. Indexing | Tokeniser and positional inverted index | `include/afterimage/indexing`, `src/indexing` |
| 6. Fragment retrieval | Multi-route candidate generation and evidence merging | `include/afterimage/retrieval`, `src/retrieval` |
| 7. Ranking | BM25, metadata and recency signals, explanation | `include/afterimage/ranking`, `src/ranking` |

SQLite is the durable source of truth. Extracted text is saved in the document records. The inverted index is derived from stored text and rebuilt when a search session starts. Ingestion only re-extracts new or changed files unless `--force` is requested.
