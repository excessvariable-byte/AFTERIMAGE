# Module 4 — Persistent Storage Engine

`SQLiteDocumentStore` stores AFTERIMAGE's document records in a local SQLite database.

- Database file: `data/afterimage.db` (created automatically by the application).
- `loadAll()` restores saved documents and their original IDs.
- `saveDocument()` inserts or updates one document.
- `saveAll()` writes a batch inside a transaction.
- `file_path` is unique, and `file_type` is indexed.

SQLite is embedded: no database server or network connection is required. SQL values are bound through prepared statements rather than concatenated into SQL strings.
