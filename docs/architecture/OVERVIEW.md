# AFTERIMAGE architecture

AFTERIMAGE is a local-first memory retrieval application. The C++ core is organised as a modular monolith: modules have clear interfaces, but are initially built into one application/library rather than deployed as separate services.

## Current data flow

1. **Ingestion** discovers files in user-approved folders, captures metadata, detects duplicates and identifies changed files.
2. **Extraction** reads TXT/Markdown, extracts selectable PDF text with Poppler, OCRs images with Tesseract, and falls back to page rendering plus OCR for scanned/image-only PDFs.
3. **Storage** persists document records and extracted text in SQLite.
4. **Text indexing** tokenises stored text and builds an in-memory positional inverted index.
5. **Fragment retrieval** uses exact terms, prefixes, infixes, bounded fuzzy variants and exact phrases, then merges evidence per document.
6. **Ranking** re-scores retrieved candidates using BM25 and explainable title, path, phrase, query-coverage, and optional recency signals.

## Planned next flow

8. **Relationship graph** will connect documents, people, concepts and events.
9. **Interface** will let users inspect results, refine clues and understand why a memory was retrieved.

## Index lifecycle

SQLite is the source of truth for document records and extracted text. The Module 5 inverted index and Modules 6–7 retrieval/ranking structures are derived data: the index is rebuilt from the loaded document collection at search startup. During indexing, only new or metadata-changed files are extracted again unless `--force` is requested. This avoids repeatedly OCRing unchanged scans.

## Current limitations

PDF text and OCR support depend on locally installed Poppler/Tesseract tools. Password-protected or damaged files may fail; poor scans, handwriting, dense tables and mixed text/image PDFs may produce incomplete output. OCR and PDF layout reconstruction are not perfect. The current search is lexical: exact terms, prefixes, infix fragments, bounded ASCII fuzzy fallback and positional phrase matching are supported. BM25 ranks lexical relevance; it does not understand semantic equivalence. Semantic embeddings, temporal indexing and relationship-graph retrieval are future modules.
