# Content Extraction

The extraction layer is separate from filesystem discovery and uses a format-specific adapter selected from each document's extension.

- `TextNormaliser`: strips a UTF-8 BOM, normalises CRLF/CR and PDF page-break characters, and trims line-end whitespace while preserving paragraph boundaries.
- `ContentExtractor`: reads TXT/Markdown natively, extracts selectable PDF text with Poppler `pdftotext`, OCRs common image formats with Tesseract, and falls back to Poppler page rendering plus Tesseract for scanned/image-only PDFs.
- `ExtractionOptions`: configures maximum source/text sizes, OCR language, page cap, render size and external-process timeout.
- `ExtractionResult`: distinguishes complete, partial, empty, unsupported, missing-dependency, failed and too-large outcomes.
- `ExtractionService`: processes selected document IDs or the full library, isolates per-document errors, and writes successful/partial text back through `DocumentManager`.

External tools are started using a POSIX argument vector rather than a shell command, avoiding quoting/injection errors for paths containing spaces or special characters. The process runner captures stdout, suppresses stderr, enforces a timeout and bounds output size. Temporary PDF render directories are cleaned up automatically.

macOS setup:

```bash
brew install poppler tesseract
```

Poppler provides PDF text extraction and page rendering. Tesseract provides OCR for images and scanned PDFs. English OCR uses `eng` by default; other language data can be installed separately and selected through `--ocr-language`.

Default limits are 50 MiB per input file, 12 MiB extracted text, 20 OCR pages per PDF, 1800 pixels for the rendered page's maximum dimension, and 120 seconds per external process. These limits are configurable in `ExtractionOptions`.

OCR is not perfect: poor scans, handwriting, tables, diagrams, unusual fonts and mixed text/image PDFs can produce incomplete output. The adapter returns `Partial` where page/text limits are reached and reports missing tools separately from damaged/encrypted files.
