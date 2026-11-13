#include "afterimage/core/DocumentManager.h"
#include "afterimage/ingestion/FileScanner.h"
#include "afterimage/ingestion/IngestionService.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
class TemporaryDirectory {
public:
    TemporaryDirectory() {
        path_ = std::filesystem::temp_directory_path() /
            ("afterimage_ingestion_" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path_);
    }
    ~TemporaryDirectory() { std::error_code ignored; std::filesystem::remove_all(path_, ignored); }
    const std::filesystem::path& path() const { return path_; }
private:
    std::filesystem::path path_;
};
void writeFile(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}
}

int main() {
    using namespace afterimage;
    namespace fs = std::filesystem;

    TemporaryDirectory temp;
    writeFile(temp.path() / "alpha.txt", "plain text");
    writeFile(temp.path() / "nested" / "notes.MD", "markdown");
    writeFile(temp.path() / "nested" / "image.png", "image bytes");
    writeFile(temp.path() / "nested" / "scan.tiff", "fake tiff bytes");
    writeFile(temp.path() / "nested" / "ignore.docx", "unsupported");
    writeFile(temp.path() / ".hidden.txt", "hidden file");
    writeFile(temp.path() / ".private" / "secret.pdf", "hidden folder");

    FileScanner scanner;
    const auto scan = scanner.scan(temp.path());
    assert(scan.rootValid);
    assert(scan.files.size() == 4);
    assert(scan.unsupportedFilesSkipped == 1);
    assert(scan.hiddenEntriesSkipped >= 2);
    for (const auto& file : scan.files) {
        assert(file.path.is_absolute());
        assert(!file.title.empty());
        assert(file.sizeBytes.has_value());
        assert(file.modifiedAtEpochSeconds.has_value());
    }

    IngestionOptions nonRecursive;
    nonRecursive.recursive = false;
    const auto shallow = scanner.scan(temp.path(), nonRecursive);
    assert(shallow.rootValid);
    assert(shallow.files.size() == 1);
    assert(shallow.files.front().title == "alpha");

    IngestionOptions includeHidden;
    includeHidden.skipHidden = false;
    const auto withHidden = scanner.scan(temp.path(), includeHidden);
    assert(withHidden.files.size() >= 6);

    const auto missing = scanner.scan(temp.path() / "does_not_exist");
    assert(!missing.rootValid);
    assert(!missing.issues.empty());
    assert(missing.files.empty());
    const auto notDirectory = scanner.scan(temp.path() / "alpha.txt");
    assert(!notDirectory.rootValid);

    // Symlinks are not followed; this avoids cycles and imports outside the root.
    std::error_code linkError;
    fs::create_symlink(temp.path() / "alpha.txt", temp.path() / "linked.txt", linkError);
    if (!linkError) {
        const auto withLink = scanner.scan(temp.path());
        assert(withLink.symbolicLinksSkipped >= 1);
    }

    TemporaryDirectory ingestRoot;
    writeFile(ingestRoot.path() / "database.txt", "database indexing");
    writeFile(ingestRoot.path() / "nested" / "graphs.md", "BFS and DFS");
    writeFile(ingestRoot.path() / "unsupported.csv", "not imported");

    DocumentManager manager;
    IngestionService service;
    const auto first = service.ingestFolder(ingestRoot.path(), manager);
    assert(first.scan.rootValid);
    assert(first.scan.files.size() == 2);
    assert(first.importedDocumentIds.size() == 2);
    assert(first.skippedAlreadyImported == 0);
    assert(manager.size() == 2);

    const auto& imported = manager.allDocuments().front();
    assert(imported.id != 0);
    assert(!imported.filePath.empty());
    assert(imported.sizeBytes.has_value());
    assert(imported.modifiedAtEpochSeconds.has_value());
    assert(imported.extractedText.empty());

    const auto second = service.ingestFolder(ingestRoot.path(), manager);
    assert(second.importedDocumentIds.empty());
    assert(second.skippedAlreadyImported == 2);
    assert(second.changedFilesDetected == 0);
    assert(manager.size() == 2);

    // A changed source file invalidates old extracted text and is scheduled for re-extraction.
    const DocumentId changedId = first.importedDocumentIds.front();
    manager.setExtractedText(changedId, "old indexed content");
    const auto changedPath = fs::path(manager.findById(changedId)->filePath);
    writeFile(changedPath, "database indexing with changed content and a longer sentence");
    const auto third = service.ingestFolder(ingestRoot.path(), manager);
    assert(third.changedFilesDetected == 1);
    assert(third.updatedDocumentIds.size() == 1);
    assert(third.updatedDocumentIds.front() == changedId);
    assert(manager.findById(changedId)->extractedText.empty());

    // A restored library entry prevents duplicate ingestion across sessions.
    DocumentManager restored;
    auto known = manager.allDocuments().front();
    restored.restoreDocument(known);
    const auto afterRestore = service.ingestFolder(ingestRoot.path(), restored);
    assert(afterRestore.importedDocumentIds.size() == 1);
    assert(afterRestore.skippedAlreadyImported == 1);

    std::cout << "All Ingestion tests passed.\n";
    return 0;
}
