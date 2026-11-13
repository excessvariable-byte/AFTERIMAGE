#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace afterimage {

// A named type makes it clear when an integer represents a document ID.
using DocumentId = std::uint64_t;

// The normalized in-memory record for one item known to AFTERIMAGE.
// File extraction and persistence will be handled by separate modules.
struct Document {
    DocumentId id{0};  // Assigned by DocumentManager; 0 means "not registered yet".

    std::string title;
    std::string filePath;
    std::string fileType;
    std::string extractedText;

    // Basic file metadata collected during ingestion. Content extraction is a
    // separate module, so extractedText remains empty until Module 3.
    std::optional<std::uintmax_t> sizeBytes;
    std::optional<std::int64_t> modifiedAtEpochSeconds;
};

} // namespace afterimage
