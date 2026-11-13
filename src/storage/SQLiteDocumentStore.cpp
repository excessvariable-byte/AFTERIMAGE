#include "afterimage/storage/SQLiteDocumentStore.h"

#include <sqlite3.h>

#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>

namespace afterimage {
namespace {

void check(int result, sqlite3* db, const std::string& action) {
    if (result != SQLITE_OK && result != SQLITE_DONE && result != SQLITE_ROW) {
        throw std::runtime_error(action + ": " + sqlite3_errmsg(db));
    }
}

class Statement {
public:
    Statement(sqlite3* db, const char* sql) : db_(db) {
        check(sqlite3_prepare_v2(db_, sql, -1, &statement_, nullptr), db_, "Prepare SQL statement");
    }
    ~Statement() { sqlite3_finalize(statement_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    sqlite3_stmt* get() const noexcept { return statement_; }
    void reset() {
        check(sqlite3_reset(statement_), db_, "Reset SQL statement");
        check(sqlite3_clear_bindings(statement_), db_, "Clear SQL bindings");
    }
private:
    sqlite3* db_;
    sqlite3_stmt* statement_{nullptr};
};

void bindText(sqlite3* db, sqlite3_stmt* statement, int index, const std::string& value) {
    check(sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT), db, "Bind text");
}

void bindOptionalInteger(sqlite3* db, sqlite3_stmt* statement, int index,
                          const std::optional<std::int64_t>& value) {
    const int result = value
        ? sqlite3_bind_int64(statement, index, *value)
        : sqlite3_bind_null(statement, index);
    check(result, db, "Bind optional integer");
}

void bindDocument(sqlite3* db, sqlite3_stmt* statement, const Document& d) {
    if (d.id == 0 || d.id > static_cast<DocumentId>(std::numeric_limits<sqlite3_int64>::max()))
        throw std::invalid_argument("Document ID cannot be represented by SQLite INTEGER.");
    check(sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(d.id)), db, "Bind document ID");
    bindText(db, statement, 2, d.title);
    bindText(db, statement, 3, d.filePath);
    bindText(db, statement, 4, d.fileType);
    bindText(db, statement, 5, d.extractedText);
    if (d.sizeBytes && *d.sizeBytes <= static_cast<std::uintmax_t>(std::numeric_limits<sqlite3_int64>::max()))
        check(sqlite3_bind_int64(statement, 6, static_cast<sqlite3_int64>(*d.sizeBytes)), db, "Bind file size");
    else
        check(sqlite3_bind_null(statement, 6), db, "Bind file size");
    bindOptionalInteger(db, statement, 7, d.modifiedAtEpochSeconds);
}

Document readDocument(sqlite3_stmt* s) {
    Document d;
    d.id = static_cast<DocumentId>(sqlite3_column_int64(s, 0));
    auto readString = [s](int column) {
        const auto* value = sqlite3_column_text(s, column);
        return value ? std::string(reinterpret_cast<const char*>(value)) : std::string{};
    };
    d.title = readString(1);
    d.filePath = readString(2);
    d.fileType = readString(3);
    d.extractedText = readString(4);
    if (sqlite3_column_type(s, 5) != SQLITE_NULL)
        d.sizeBytes = static_cast<std::uintmax_t>(sqlite3_column_int64(s, 5));
    if (sqlite3_column_type(s, 6) != SQLITE_NULL)
        d.modifiedAtEpochSeconds = static_cast<std::int64_t>(sqlite3_column_int64(s, 6));
    return d;
}

} // namespace

SQLiteDocumentStore::SQLiteDocumentStore(const std::filesystem::path& databasePath)
    : databasePath_(databasePath) {
    if (databasePath_.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(databasePath_.parent_path(), error);
        if (error) throw std::runtime_error("Cannot create database directory: " + error.message());
    }
    const int result = sqlite3_open_v2(databasePath_.string().c_str(), &database_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (result != SQLITE_OK) {
        const std::string message = database_ ? sqlite3_errmsg(database_) : "SQLite could not allocate a database handle";
        if (database_) sqlite3_close(database_);
        database_ = nullptr;
        throw std::runtime_error("Cannot open database: " + message);
    }
    sqlite3_busy_timeout(database_, 5000);
    try { initialiseSchema(); }
    catch (...) { sqlite3_close(database_); database_ = nullptr; throw; }
}

SQLiteDocumentStore::~SQLiteDocumentStore() {
    if (database_) sqlite3_close(database_);
}

void SQLiteDocumentStore::initialiseSchema() {
    const char* sql = R"SQL(
        PRAGMA foreign_keys = ON;
        CREATE TABLE IF NOT EXISTS documents (
            id INTEGER PRIMARY KEY,
            title TEXT NOT NULL,
            file_path TEXT NOT NULL UNIQUE,
            file_type TEXT NOT NULL,
            extracted_text TEXT NOT NULL DEFAULT '',
            size_bytes INTEGER,
            modified_at_epoch_seconds INTEGER
        );
        CREATE INDEX IF NOT EXISTS idx_documents_file_type ON documents(file_type);
    )SQL";
    char* errorMessage = nullptr;
    const int result = sqlite3_exec(database_, sql, nullptr, nullptr, &errorMessage);
    if (result != SQLITE_OK) {
        const std::string message = errorMessage ? errorMessage : sqlite3_errmsg(database_);
        sqlite3_free(errorMessage);
        throw std::runtime_error("Create database schema: " + message);
    }
}

std::vector<Document> SQLiteDocumentStore::loadAll() const {
    Statement query(database_, "SELECT id,title,file_path,file_type,extracted_text,size_bytes,modified_at_epoch_seconds FROM documents ORDER BY id");
    std::vector<Document> documents;
    int result;
    while ((result = sqlite3_step(query.get())) == SQLITE_ROW) documents.push_back(readDocument(query.get()));
    check(result, database_, "Load documents");
    return documents;
}

void SQLiteDocumentStore::saveDocument(const Document& document) {
    Statement statement(database_, R"SQL(
        INSERT INTO documents(id,title,file_path,file_type,extracted_text,size_bytes,modified_at_epoch_seconds)
        VALUES(?,?,?,?,?,?,?)
        ON CONFLICT(id) DO UPDATE SET title=excluded.title,file_path=excluded.file_path,
          file_type=excluded.file_type,extracted_text=excluded.extracted_text,
          size_bytes=excluded.size_bytes,modified_at_epoch_seconds=excluded.modified_at_epoch_seconds
    )SQL");
    bindDocument(database_, statement.get(), document);
    check(sqlite3_step(statement.get()), database_, "Save document");
}

void SQLiteDocumentStore::saveAll(const std::vector<Document>& documents) {
    char* errorMessage = nullptr;
    check(sqlite3_exec(database_, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr, &errorMessage), database_, "Begin save transaction");
    try {
        Statement statement(database_, R"SQL(
            INSERT INTO documents(id,title,file_path,file_type,extracted_text,size_bytes,modified_at_epoch_seconds)
            VALUES(?,?,?,?,?,?,?)
            ON CONFLICT(id) DO UPDATE SET title=excluded.title,file_path=excluded.file_path,
              file_type=excluded.file_type,extracted_text=excluded.extracted_text,
              size_bytes=excluded.size_bytes,modified_at_epoch_seconds=excluded.modified_at_epoch_seconds
        )SQL");
        for (const auto& document : documents) {
            bindDocument(database_, statement.get(), document);
            check(sqlite3_step(statement.get()), database_, "Save document batch");
            statement.reset();
        }
        check(sqlite3_exec(database_, "COMMIT", nullptr, nullptr, nullptr), database_, "Commit save transaction");
    } catch (...) {
        sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}

const std::filesystem::path& SQLiteDocumentStore::databasePath() const noexcept { return databasePath_; }

} // namespace afterimage
