#pragma once

#include "afterimage/core/Document.h"

#include <filesystem>
#include <vector>

struct sqlite3;

namespace afterimage {

// SQLite-backed repository for durable document records.
class SQLiteDocumentStore {
public:
    explicit SQLiteDocumentStore(const std::filesystem::path& databasePath);
    ~SQLiteDocumentStore();

    SQLiteDocumentStore(const SQLiteDocumentStore&) = delete;
    SQLiteDocumentStore& operator=(const SQLiteDocumentStore&) = delete;

    std::vector<Document> loadAll() const;
    void saveDocument(const Document& document);
    void saveAll(const std::vector<Document>& documents);
    const std::filesystem::path& databasePath() const noexcept;

private:
    sqlite3* database_{nullptr};
    std::filesystem::path databasePath_;
    void initialiseSchema();
};

} // namespace afterimage
