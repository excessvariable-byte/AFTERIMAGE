#include "afterimage/core/DocumentManager.h"
#include "afterimage/storage/SQLiteDocumentStore.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
class TemporaryDirectory {
public:
    TemporaryDirectory() {
        path_ = std::filesystem::temp_directory_path() /
            ("afterimage_storage_" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path_);
    }
    ~TemporaryDirectory() { std::error_code ignored; std::filesystem::remove_all(path_, ignored); }
    const std::filesystem::path& path() const { return path_; }
private:
    std::filesystem::path path_;
};
afterimage::Document makeDocument(afterimage::DocumentId id, std::string title,
                                  std::string path, std::string body) {
    afterimage::Document d;
    d.id = id;
    d.title = std::move(title);
    d.filePath = std::move(path);
    d.fileType = "txt";
    d.extractedText = std::move(body);
    return d;
}
}

int main() {
    using namespace afterimage;
    TemporaryDirectory temp;
    const auto dbPath = temp.path() / "nested" / "afterimage.db";
    auto original = makeDocument(42, "Memory Structures", "/virtual/memory.txt",
                                 "A graph connects related memories.");
    original.sizeBytes = 35;
    original.modifiedAtEpochSeconds = 1790000000;
    auto second = makeDocument(43, "Consensus", "/virtual/consensus.md",
                               "Raft uses a leader and followers.");

    {
        SQLiteDocumentStore store(dbPath);
        assert(store.databasePath() == dbPath);
        store.saveDocument(original);
        store.saveAll({second});
        const auto loaded = store.loadAll();
        assert(loaded.size() == 2);
        assert(loaded[0].id == 42);
        assert(loaded[0].extractedText == original.extractedText);
        assert(loaded[0].sizeBytes == 35);
        assert(loaded[0].modifiedAtEpochSeconds == 1790000000);
        assert(loaded[1].id == 43);
    }

    // Reopen the database, preserving IDs and values across a close/reopen boundary.
    {
        SQLiteDocumentStore reopened(dbPath);
        const auto loaded = reopened.loadAll();
        assert(loaded.size() == 2);
        assert(loaded.front().title == "Memory Structures");

        DocumentManager manager;
        for (const auto& document : loaded) manager.restoreDocument(document);
        Document next;
        next.title = "Next document";
        next.filePath = "/virtual/next.txt";
        next.fileType = "txt";
        const auto nextId = manager.addDocument(next);
        assert(nextId > 43);

        auto updated = loaded.front();
        updated.extractedText = "Updated content";
        reopened.saveDocument(updated);
        assert(reopened.loadAll().front().extractedText == "Updated content");

        // A batch containing a duplicate unique path fails and rolls back earlier rows.
        auto newRow = makeDocument(44, "Should Roll Back", "/virtual/rollback.txt", "temporary");
        auto duplicatePath = makeDocument(45, "Duplicate Path", "/virtual/consensus.md", "conflict");
        bool rejected = false;
        try { reopened.saveAll({newRow, duplicatePath}); }
        catch (const std::exception&) { rejected = true; }
        assert(rejected);
        const auto afterRollback = reopened.loadAll();
        assert(afterRollback.size() == 2);
        for (const auto& document : afterRollback) assert(document.id != 44);

        // Invalid IDs are rejected before a SQL write.
        auto invalid = makeDocument(0, "Invalid", "/virtual/invalid.txt", "no id");
        bool rejectedZeroId = false;
        try { reopened.saveDocument(invalid); }
        catch (const std::invalid_argument&) { rejectedZeroId = true; }
        assert(rejectedZeroId);
    }

    std::cout << "All Storage tests passed.\n";
    return 0;
}
