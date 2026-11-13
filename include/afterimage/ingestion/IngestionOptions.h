#pragma once

#include <string>
#include <unordered_set>

namespace afterimage {

// Controls which files the scanner considers. Extensions are stored without
// the leading dot and compared case-insensitively.
struct IngestionOptions {
    std::unordered_set<std::string> supportedExtensions{
        "txt", "md", "markdown", "pdf", "png", "jpg", "jpeg",
        "tif", "tiff", "bmp", "webp", "gif"
    };

    // true: scan subdirectories too. false: inspect only the selected folder.
    bool recursive{true};

    // Hidden files/folders (names beginning with '.') are skipped by default.
    // This avoids importing macOS housekeeping files such as .DS_Store.
    bool skipHidden{true};
};

} // namespace afterimage
