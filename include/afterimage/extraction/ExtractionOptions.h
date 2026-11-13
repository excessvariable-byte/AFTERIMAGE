#pragma once

#include <cstdint>
#include <string>

namespace afterimage {

struct ExtractionOptions {
    // Input and output safety limits. These apply before indexing any extracted content.
    std::uintmax_t maxFileBytes{50ULL * 1024ULL * 1024ULL};
    std::uintmax_t maxExtractedTextBytes{12ULL * 1024ULL * 1024ULL};
    bool normaliseWhitespace{true};

    // External tools are invoked directly (without a shell), so paths containing
    // spaces or shell characters are passed as ordinary arguments.
    std::string pdfToTextExecutable{"pdftotext"};
    std::string pdfToPpmExecutable{"pdftoppm"};
    std::string pdfInfoExecutable{"pdfinfo"};
    std::string tesseractExecutable{"tesseract"};

    // OCR is used for image files and as a fallback for scanned/image-only PDFs.
    bool enableOcr{true};
    std::string ocrLanguage{"eng"};
    int ocrPageSegmentationMode{3};
    std::size_t maxPdfPagesForOcr{20};
    int pdfRenderMaxDimension{1800};
    unsigned int processTimeoutSeconds{120};
};

} // namespace afterimage
