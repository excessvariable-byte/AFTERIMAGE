#include "afterimage/core/DocumentManager.h"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
afterimage::Document makeDocument(std::string title, std::string path) {
    afterimage::Document document;
    document.title = std::move(title);
    document.filePath = std::move(path);
    document.fileType = "txt";
    return document;
}
}

int main() {
    using namespace afterimage;
    DocumentManager manager;
    assert(manager.empty());
    assert(manager.size() == 0);

    const auto firstId = manager.addDocument(makeDocument("Database Notes", "notes/database.txt"));
    const auto secondId = manager.addDocument(makeDocument("Graph Notes", "notes/graphs.txt"));
    assert(firstId == 1);
    assert(secondId == 2);
    assert(firstId != secondId);
    assert(manager.size() == 2);

    const auto found = manager.findById(firstId);
    assert(found.has_value());
    assert(found->title == "Database Notes");
    assert(!manager.findById(999999).has_value());
    assert(manager.setExtractedText(firstId, "B-tree and LSM-tree"));
    assert(manager.findById(firstId)->extractedText == "B-tree and LSM-tree");
    assert(!manager.setExtractedText(999999, "missing"));

    // Returned values are copies, not references into the vector.
    auto copy = *manager.findById(firstId);
    copy.title = "Changed outside manager";
    assert(manager.findById(firstId)->title == "Database Notes");

    bool rejectedEmptyTitle = false;
    try { manager.addDocument(makeDocument("", "unknown.txt")); }
    catch (const std::invalid_argument&) { rejectedEmptyTitle = true; }
    assert(rejectedEmptyTitle);

    // Storage restoration preserves IDs and advances the next generated ID.
    DocumentManager restored;
    auto persisted = makeDocument("Persisted record", "persisted.txt");
    persisted.id = 77;
    restored.restoreDocument(persisted);
    assert(restored.findById(77).has_value());
    const auto nextId = restored.addDocument(makeDocument("New record", "new.txt"));
    assert(nextId == 78);

    bool rejectedDuplicateId = false;
    try { restored.restoreDocument(persisted); }
    catch (const std::invalid_argument&) { rejectedDuplicateId = true; }
    assert(rejectedDuplicateId);

    bool rejectedZeroRestoredId = false;
    auto zeroId = makeDocument("Invalid restored", "zero.txt");
    try { restored.restoreDocument(zeroId); }
    catch (const std::invalid_argument&) { rejectedZeroRestoredId = true; }
    assert(rejectedZeroRestoredId);

    bool rejectedRestoredEmptyTitle = false;
    auto emptyTitle = makeDocument("", "empty.txt");
    emptyTitle.id = 90;
    try { restored.restoreDocument(emptyTitle); }
    catch (const std::invalid_argument&) { rejectedRestoredEmptyTitle = true; }
    assert(rejectedRestoredEmptyTitle);

    std::cout << "All DocumentManager tests passed.\n";
    return 0;
}
