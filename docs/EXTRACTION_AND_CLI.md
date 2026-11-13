# Extraction and CLI design

## Extraction adapter design

`ContentExtractor` selects a format-specific path from the normalised extension:

- Text: native `std::ifstream` reader.
- PDF text layer: `pdftotext -enc UTF-8 -layout <input> -`.
- Scanned PDF fallback: `pdftoppm` renders a bounded number of pages to a temporary directory, then Tesseract recognises each page.
- Image: Tesseract recognises the file directly.

External programs are started with `posix_spawnp` and an argument vector. No shell is involved, so a path containing spaces or shell metacharacters stays a single argument. Stdout is captured through a pipe, stderr is suppressed, and the process runner enforces a timeout and output cap. Temporary render directories are removed with RAII.

`ExtractionStatus::Partial` distinguishes a usable but truncated result from a complete result. `DependencyMissing` identifies missing external programs separately from malformed input. These distinctions flow into `ExtractionReport` and the CLI summary.

## Incremental re-extraction

`FileScanner` discovers source size and modification time. `IngestionService` maps normalised paths to stored document IDs. Existing documents whose source metadata changes are updated through `DocumentManager::updateFileMetadata`, which clears stale extracted text and returns the ID for re-extraction. New and changed IDs are passed to `ExtractionService::extractDocuments`; unchanged documents are not sent to Poppler/Tesseract again.

The SQLite database remains the durable source of truth. The inverted index is reconstructed from persisted extracted text at search startup.

## Command line

- `afterimage index <folder>` indexes new and changed files.
- `afterimage` opens the interactive query loop.
- `afterimage search "fragment"` performs a one-shot query.
- `--db` selects a database, and `AFTERIMAGE_DB` can set a default override.
- `--force` re-extracts the full library, for example after installing OCR dependencies.

The default database path is `~/.afterimage/library.db` on macOS/Linux. An empty library prints the exact command needed to index a folder.
