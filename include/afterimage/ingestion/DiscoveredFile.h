#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace afterimage {

// A filesystem item discovered by the scanner, before content extraction.
struct DiscoveredFile {
    std::filesystem::path path;  // Normalized absolute path.
    std::string title;           // Filename without its extension.
    std::string extension;       // Lowercase extension, without the dot.
    std::optional<std::uintmax_t> sizeBytes;
    std::optional<std::int64_t> modifiedAtEpochSeconds;
};

struct ScanIssue {
    std::filesystem::path path;
    std::string message;
};

struct ScanResult {
    std::filesystem::path rootPath;
    bool rootValid{false};

    std::vector<DiscoveredFile> files;
    std::vector<ScanIssue> issues;

    std::size_t entriesVisited{0};
    std::size_t unsupportedFilesSkipped{0};
    std::size_t hiddenEntriesSkipped{0};
    std::size_t symbolicLinksSkipped{0};
};

} // namespace afterimage
