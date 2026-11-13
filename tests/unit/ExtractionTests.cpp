#include "afterimage/core/DocumentManager.h"
#include "afterimage/extraction/ContentExtractor.h"
#include "afterimage/extraction/ExtractionService.h"
#include "afterimage/extraction/TextNormaliser.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifndef AFTERIMAGE_TEST_FIXTURE_DIR
#error AFTERIMAGE_TEST_FIXTURE_DIR must be defined by CMake
#endif

namespace {
class TemporaryDirectory {
public:
    TemporaryDirectory() {
        path_ = std::filesystem::temp_directory_path() /
            ("afterimage_extraction_" + std::to_string(
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
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}
afterimage::Document makeDoc(const std::filesystem::path& path, std::string type) {
    afterimage::Document doc;
    doc.title = path.stem().string();
    doc.filePath = path.string();
    doc.fileType = std::move(type);
    return doc;
}
bool containsInsensitive(const std::string& text, const std::string& needle) {
    auto lower = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    };
    return lower(text).find(lower(needle)) != std::string::npos;
}
}

int main() {
    using namespace afterimage;
    namespace fs = std::filesystem;
    TemporaryDirectory temp;

    const auto textPath = temp.path() / "memory.txt";
    writeFile(textPath, "\xEF\xBB\xBF" "First line  \r\nSecond line\rThird line\n\nParagraph two  \t");
    const auto result = ContentExtractor{}.extract(makeDoc(textPath, "txt"));
    assert(result.status == ExtractionStatus::Success);
    assert(result.text == "First line\nSecond line\nThird line\n\nParagraph two");
    assert(result.bytesRead > 0);

    const auto markdownPath = temp.path() / "notes.md";
    writeFile(markdownPath, "# Heading\r\n\r\nBody text\r\n");
    const auto markdown = ContentExtractor{}.extract(makeDoc(markdownPath, "md"));
    assert(markdown.status == ExtractionStatus::Success);
    assert(markdown.text == "# Heading\n\nBody text");

    assert(TextNormaliser::normalise("A  \r\nB\n\nC  ") == "A\nB\n\nC");
    assert(TextNormaliser::normalise("\xEF\xBB\xBFhello") == "hello");
    assert(TextNormaliser::normalise("x\ry") == "x\ny");
    assert(TextNormaliser::normalise("page one\fpage two") == "page one\npage two");

    const auto emptyPath = temp.path() / "empty.txt";
    writeFile(emptyPath, " \r\n\t ");
    const auto empty = ContentExtractor{}.extract(makeDoc(emptyPath, "txt"));
    assert(empty.status == ExtractionStatus::Empty);
    assert(empty.succeeded());

    const auto largePath = temp.path() / "large.txt";
    writeFile(largePath, "0123456789");
    ExtractionOptions smallLimit;
    smallLimit.maxFileBytes = 5;
    const auto tooLarge = ContentExtractor{smallLimit}.extract(makeDoc(largePath, "txt"));
    assert(tooLarge.status == ExtractionStatus::TooLarge);

    const fs::path fixtures = AFTERIMAGE_TEST_FIXTURE_DIR;
    const auto textPdf = ContentExtractor{}.extract(makeDoc(fixtures / "pdf_text_layer.pdf", "pdf"));
    if (textPdf.status == ExtractionStatus::Success || textPdf.status == ExtractionStatus::Partial) {
        assert(containsInsensitive(textPdf.text, "B-tree"));
        assert(containsInsensitive(textPdf.text, "Raft consensus"));
    } else {
        assert(textPdf.status == ExtractionStatus::DependencyMissing);
    }

    const auto image = ContentExtractor{}.extract(makeDoc(fixtures / "ocr_sample.png", "png"));
    if (image.status == ExtractionStatus::Success || image.status == ExtractionStatus::Partial) {
        assert(containsInsensitive(image.text, "AFTERIMAGE"));
        assert(containsInsensitive(image.text, "CONSENSUS"));
        assert(image.pagesProcessed == 1);
    } else {
        assert(image.status == ExtractionStatus::DependencyMissing);
    }

    const auto scannedPdf = ContentExtractor{}.extract(makeDoc(fixtures / "scanned_pdf_fixture.pdf", "pdf"));
    if (scannedPdf.status == ExtractionStatus::Success || scannedPdf.status == ExtractionStatus::Partial) {
        assert(containsInsensitive(scannedPdf.text, "AFTERIMAGE"));
        assert(containsInsensitive(scannedPdf.text, "HEARTBEAT"));
        assert(scannedPdf.pagesProcessed >= 1);
    } else {
        assert(scannedPdf.status == ExtractionStatus::DependencyMissing);
    }

    // Page limits must be explicit: a two-page scan with a one-page OCR budget is Partial.
    ExtractionOptions onePageOnly;
    onePageOnly.maxPdfPagesForOcr = 1;
    const auto limitedPdf = ContentExtractor{onePageOnly}.extract(
        makeDoc(fixtures / "scanned_pdf_two_pages.pdf", "pdf"));
    if (limitedPdf.status != ExtractionStatus::DependencyMissing) {
        assert(limitedPdf.status == ExtractionStatus::Partial);
        assert(limitedPdf.pagesProcessed == 1);
        assert(containsInsensitive(limitedPdf.text, "AFTERIMAGE"));
    }

    // Special characters in paths are safe because the adapter passes argv directly, not a shell string.
    const auto oddPath = temp.path() / "folder with spaces & symbols" / "my report [1].pdf";
    fs::create_directories(oddPath.parent_path());
    fs::copy_file(fixtures / "pdf_text_layer.pdf", oddPath);
    const auto pathWithSpaces = ContentExtractor{}.extract(makeDoc(oddPath, "pdf"));
    if (pathWithSpaces.status == ExtractionStatus::Success || pathWithSpaces.status == ExtractionStatus::Partial) {
        assert(containsInsensitive(pathWithSpaces.text, "B-tree"));
    } else {
        assert(pathWithSpaces.status == ExtractionStatus::DependencyMissing);
    }

    const auto corruptPdfPath = temp.path() / "damaged.pdf";
    writeFile(corruptPdfPath, "this is not a PDF document");
    const auto corruptPdf = ContentExtractor{}.extract(makeDoc(corruptPdfPath, "pdf"));
    assert(corruptPdf.status == ExtractionStatus::Failed ||
           corruptPdf.status == ExtractionStatus::DependencyMissing);

#if defined(__unix__) || defined(__APPLE__)
    // A hung external extractor is terminated by the configured process timeout.
    const auto slowTool = temp.path() / "slow-pdftotext";
    writeFile(slowTool, "#!/bin/sh\nexec /bin/sleep 3\n");
    fs::permissions(slowTool,
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec,
                    fs::perm_options::replace);
    ExtractionOptions timeoutOptions;
    timeoutOptions.pdfToTextExecutable = slowTool.string();
    timeoutOptions.processTimeoutSeconds = 1;
    const auto timedOut = ContentExtractor{timeoutOptions}.extract(makeDoc(fixtures / "pdf_text_layer.pdf", "pdf"));
    assert(timedOut.status == ExtractionStatus::Failed);
    assert(timedOut.message.find("timed out") != std::string::npos);
#endif

    // Missing external executables fail clearly without shell invocation or a crash.
    ExtractionOptions missingTools;
    missingTools.pdfToTextExecutable = "afterimage-no-such-pdftotext";
    missingTools.pdfToPpmExecutable = "afterimage-no-such-pdftoppm";
    missingTools.pdfInfoExecutable = "afterimage-no-such-pdfinfo";
    missingTools.tesseractExecutable = "afterimage-no-such-tesseract";
    const auto noPdfTool = ContentExtractor{missingTools}.extract(makeDoc(fixtures / "pdf_text_layer.pdf", "pdf"));
    assert(noPdfTool.status == ExtractionStatus::DependencyMissing);
    const auto noOcrTool = ContentExtractor{missingTools}.extract(makeDoc(fixtures / "ocr_sample.png", "png"));
    assert(noOcrTool.status == ExtractionStatus::DependencyMissing);
    DocumentManager missingDependencyManager;
    const auto missingDependencyId = missingDependencyManager.addDocument(makeDoc(fixtures / "ocr_sample.png", "png"));
    const auto missingDependencyReport = ExtractionService{missingTools}.extractDocuments(
        missingDependencyManager, {missingDependencyId});
    assert(missingDependencyReport.attempted == 1);
    assert(missingDependencyReport.dependencyMissing == 1);

    ExtractionOptions ocrDisabled;
    ocrDisabled.enableOcr = false;
    assert(ContentExtractor{ocrDisabled}.extract(makeDoc(fixtures / "ocr_sample.png", "png")).status ==
           ExtractionStatus::Unsupported);

    ExtractionOptions outputLimit;
    outputLimit.maxExtractedTextBytes = 8;
    const auto limitedText = ContentExtractor{outputLimit}.extract(makeDoc(textPath, "txt"));
    assert(limitedText.status == ExtractionStatus::Partial);
    assert(limitedText.text.size() <= 8);

    const auto missingFile = ContentExtractor{}.extract(makeDoc(temp.path() / "missing.txt", "txt"));
    assert(missingFile.status == ExtractionStatus::Failed);
    const auto unsupportedPath = temp.path() / "report.docx";
    writeFile(unsupportedPath, "unsupported format");
    const auto unsupported = ContentExtractor{}.extract(makeDoc(unsupportedPath, "docx"));
    assert(unsupported.status == ExtractionStatus::Unsupported);

    DocumentManager manager;
    const auto id = manager.addDocument(makeDoc(textPath, "txt"));
    manager.addDocument(makeDoc(emptyPath, "txt"));
    const auto report = ExtractionService{}.extractAll(manager);
    assert(report.attempted == 2);
    assert(report.extracted == 1);
    assert(report.empty == 1);
    assert(manager.findById(id)->extractedText == result.text);

    // Selected extraction should not touch unrelated stored documents.
    DocumentManager selectedManager;
    const auto selectedId = selectedManager.addDocument(makeDoc(textPath, "txt"));
    const auto skippedId = selectedManager.addDocument(makeDoc(markdownPath, "md"));
    const auto selectedReport = ExtractionService{}.extractDocuments(selectedManager, {selectedId, selectedId});
    assert(selectedReport.attempted == 1);
    assert(selectedManager.findById(selectedId)->extractedText == result.text);
    assert(selectedManager.findById(skippedId)->extractedText.empty());

    std::cout << "Extraction tests passed (plain text, PDF text layer, scanned-PDF OCR, image OCR, limits, missing tools, and selected refresh).\n";
    return 0;
}
