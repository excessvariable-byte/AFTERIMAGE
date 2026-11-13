# AFTERIMAGE

AFTERIMAGE is a local-first C++20 search tool for finding personal documents from remembered fragments and discovering related documents through a lightweight memory graph. It indexes a folder, stores extracted text in SQLite, and lets you search the library from a small interactive terminal prompt.

## Quick start (macOS)

Install the build and document-extraction tools:

```bash
brew install cmake sqlite poppler tesseract
```

`poppler` supplies `pdftotext`, `pdftoppm`, and `pdfinfo`. `tesseract` supplies OCR for images and scanned PDFs. AFTERIMAGE launches these tools directly as subprocesses; it does not construct shell commands from file paths.

Build and test:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Index your documents (recursive by default):

```bash
./build/afterimage index ~/Documents
```

Then open the simple interactive search prompt:

```bash
./build/afterimage
```

Search directly without opening the prompt:

```bash
./build/afterimage search "raft leader heartbeat"
./build/afterimage search "schedulng" --max-results 5
./build/afterimage search "scaled dot product attention" --no-recency
```

The database defaults to `~/.afterimage/library.db`. Override it with `--db /path/to/library.db` or set the `AFTERIMAGE_DB` environment variable. When launched from the project root, the app also reuses an existing legacy `data/afterimage.db` if the new home database has not been created yet.

### Install the `afterimage` terminal command

After building, install the executable into your user-local bin folder (no `sudo` needed):

```bash
cmake --install build --prefix "$HOME/.local"
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

After that, use it from any terminal directory:

```bash
afterimage index ~/Documents
afterimage
afterimage search "a fragment you remember"
```

## Terminal commands

```text
afterimage                         Open interactive search
afterimage index <folder>          Index or refresh a folder
afterimage index <folder> --force  Re-extract every document in the library
afterimage search "fragment"       Run one search and exit
afterimage --help                  Show all options
```

Within the prompt:

- Enter a remembered word, phrase, or fragment to search.
- `:stats` shows how many documents and indexed terms are in the library.
- `:help` shows prompt help.
- `:quit` exits.

The interactive prompt is deliberately small: no query syntax is required for common searches. Search results can also show supplementary **Related memories**, discovered through shared informative concepts in the Module 8 graph. Direct lexical matches remain separate and always take priority.

## Supported file types

| File type | Processing method | Notes |
|---|---|---|
| TXT, MD, Markdown | Native C++ text reader | UTF-8 BOM and line endings normalised |
| PDF with a text layer | Poppler `pdftotext` | Layout-aware text extraction |
| Scanned/image-only PDF | Poppler `pdftoppm` + Tesseract OCR | First 20 pages by default; configurable in `ExtractionOptions` |
| PNG, JPG, JPEG | Tesseract OCR | Reads visible printed text |
| TIFF, BMP, WebP, GIF | Tesseract OCR | Depends on the formats supported by the installed Tesseract/Leptonica build |

PDF and image extraction has sensible safeguards:

- Maximum input file size: 50 MiB by default.
- Maximum extracted text: 12 MiB per document by default.
- OCR page cap for scanned PDFs: 20 pages by default.
- OCR render dimension: 1800 pixels maximum dimension by default.
- External process timeout: 120 seconds per tool invocation by default.
- Missing tools, encrypted/corrupt PDFs, timeouts, partial extraction and size limits produce explicit statuses and messages.

No extractor can guarantee perfect results for every PDF. Password-protected files, damaged documents, vector-only diagrams, unusual encodings, handwriting, low-resolution scans and complex tables may need separate processing or manual review. Text extraction is not the same as preserving the visual page layout. Scanned PDFs use OCR only when the PDF text layer is empty.

English OCR uses Tesseract language data `eng` by default. Set another language or combination with `--ocr-language`, for example `--ocr-language hin+eng`, after installing the corresponding language data.

## Incremental indexing and refresh

AFTERIMAGE stores each document's source file size and modification time. When you run `index` again:

- New files are imported and extracted.
- Changed files have their old extracted text invalidated and are extracted again.
- Unchanged files are skipped, so image OCR and PDF extraction are not repeated unnecessarily.

Use `--force` to re-extract the entire stored library. This is useful after installing Poppler/Tesseract, changing OCR language, or updating extraction settings. The index itself is derived from saved text and is rebuilt when searching; SQLite remains the source of truth.

## Modules

1. Core document model — stable IDs, in-memory document library, metadata refresh.
2. Ingestion — recursive scanning, supported extension filtering, hidden-file and symlink handling, duplicate detection, changed-file detection.
3. Extraction — plain text, PDF text layer, image OCR, scanned-PDF OCR fallback, size/time/page limits, explicit errors.
4. Persistent storage — SQLite records, prepared statements, transactions, stable IDs across restarts.
5. Text indexing — tokenisation, positional inverted index, term statistics, prefix and phrase search.
6. Fragment retrieval — exact, prefix, infix, typo-tolerant and phrase candidate generation.
7. Ranking — BM25, title/path evidence, phrase and coverage boosts, optional recency, explanations.
8. Memory relationship graph — rebuildable document/concept graph, shared-concept links, IDF-weighted related-document recommendations and bounded graph traversal.

## Test coverage

The CTest suite currently has twelve test groups:

1. Document manager and stable IDs.
2. File scanning, extension discovery, hidden files and symlink handling.
3. SQLite persistence and reload.
4. Text extraction, PDF text layer, scanned-PDF OCR, image OCR, limits and missing-tool handling.
5. Tokenisation and positional inverted index.
6. Fragment retrieval paths.
7. BM25 and relevance ranking.
8. End-to-end ingestion, extraction, persistence, indexing, retrieval, ranking and graph linking across the 16-document technical test corpus.
9. CLI help.
10. CLI index → search → idempotent re-index workflow.
11. Memory graph — concept extraction, common-term filtering, shared-concept ranking, seed exclusion, result limits and optional multi-hop traversal.
12. CLI PDF/image media workflow — index fixtures, search extracted/OCR text and verify persistence.

Tests use checked-in PDF and image fixtures. If Poppler or Tesseract is not installed, media tests accept the explicit `DependencyMissing` status; when the tools are installed, they verify extracted words and the OCR fallback.

Run tests with:

```bash
ctest --test-dir build --output-on-failure
```

## Module 8 — Memory Relationship Graph

The graph is built in memory from the restored document library. It creates document-to-concept links using title terms and repeated body terms, filters stopwords and overly common concepts, and ranks related documents by the rarity of their shared concepts. The CLI displays up to five supplementary related memories after direct search results, including the concepts connecting them. The graph is rebuilt from SQLite-backed documents on each search session; SQLite remains the source of truth.

This first graph version uses transparent lexical heuristics. It is not yet a knowledge graph with reliable person/event extraction, nor does it claim semantic understanding. A shared word is evidence of a possible relationship, not proof.

## Advanced test corpus

`data/advanced_test_corpus` includes the 16 technical documents supplied for testing: B-Trees, Raft, vector embeddings, neural network architectures, virtual memory, TCP/IP, compiler optimisation, distributed transactions, LSM-Trees, garbage collection, Paxos/FLP, Linux VFS, PostgreSQL MVCC, transformer attention, Kubernetes, and reactive async programming. These are synthetic regression fixtures, not authoritative technical references.

## Current limitations

- OCR extracts visible text; it does not understand charts, diagrams, screenshots without readable text, or handwritten notes reliably.
- PDF layout extraction can be imperfect for multi-column pages, tables, and unusual fonts.
- Scanned-PDF OCR is limited to a configurable number of pages and can return a `Partial` result.
- Metadata-based change detection uses file size and modification time. A file changed while preserving both values may not be detected automatically; use `--force` to refresh all files.
- The index is rebuilt in memory on each search session. A persistent on-disk index is a future optimisation.
- Search remains lexical and fragment-oriented; semantic embeddings, people/event extraction, date-aware retrieval and relationship graphs are future modules.
- The external PDF/OCR process adapter currently targets macOS and Linux. SQLite and the rest of the project remain separate from these external tools.


## AFTERIMAGE 2.0 — Human-like Personal Memory Retrieval System

AFTERIMAGE 2.0 transforms traditional keyword file search into an intelligent human memory retrieval system.

### Key Capabilities:
- **Natural Conversational Queries**: Understands queries like *"I saw something in the OOPS that says classes and objects"* by stripping conversational filler and isolating core remembered concepts.
- **Temporal Memory Reasoning**: Understands time expressions like *"last week"*, *"yesterday"*, *"last month"*, or *"in 2025"*, mapping them to epoch time windows and boosting matching documents to the top.
- **Typo Tolerance & Irregular Typing**: Corrects human typos like *"assignmnet"* → *assignment*, *"schedulng"* → *scheduling*, *"algoritm"* → *algorithm*.
- **Concept Knowledge Graph**: Maps technical terms across domain taxonomies (e.g. `OOPS` → `classes`, `objects`, `inheritance`, `polymorphism`).
- **Exact Page References & Highlighted Passages**: Pinpoints the exact page and contextual passage where the memory matches, with `<mark>` keyword highlighting.
- **In-Browser Document Reader & Extraction Inspector**: Inspect native PDF vs OCR extracted text, navigate page-by-page, and edit titles, tags, and personal notes.
- **Developer Console & Database Browser**: Built-in SQL query workbench, database overview cards, inverted index explorer, and graph statistics.
- **Non-Blocking Background Indexing**: Asynchronous folder scanning and extraction with real-time progress bar, live log streaming, and cancellation.

### Launch on macOS

Double-click `START_AFTERIMAGE.command` or run:

```bash
python3 web/server.py
```

Then visit **http://127.0.0.1:8765** in your browser.

### Local AI meaning search

AFTERIMAGE can add semantic retrieval through Ollama running on the same Mac.
Install Ollama, then download an embedding model once:

```bash
ollama pull nomic-embed-text
```

Open **Developer Console** in AFTERIMAGE and select **Build Local AI Index**.
The semantic index stores compact numeric vectors, not a second copy of your
extracted text. Searches continue to use exact matches, dates and file formats;
the local model adds results with similar meaning.

### Spotlight integration on macOS

Build the project and install `build/Afterimage.app` in `/Applications`. The
app then appears in Spotlight: press Command-Space, type **Afterimage**, and
open it to start the local server and open the Afterimage interface. The
Developer Console's **Sync to Spotlight** action updates the metadata bridge;
Afterimage remains the place for local AI ranking and matching passages.
