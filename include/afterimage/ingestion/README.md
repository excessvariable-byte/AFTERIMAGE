# Ingestion module

Module 2 discovers supported files under a user-selected folder and creates or refreshes `Document` records through `IngestionService`.

- `IngestionOptions.h`: supported extensions and traversal settings.
- `DiscoveredFile.h`: scanner output and per-file issues.
- `FileScanner.h/.cpp`: recursive filesystem traversal and metadata collection.
- `IngestionService.h/.cpp`: converts discovered files into `DocumentManager` records, avoids duplicate paths, and detects size/modification-time changes.

The scanner does not read file contents. The extraction module processes new or changed document IDs. When source metadata changes, `DocumentManager` invalidates the old extracted text so it cannot be searched as if it still described the current file. Hidden entries and symlinks are skipped by default.
