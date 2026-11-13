#pragma once

#include "afterimage/ingestion/DiscoveredFile.h"
#include "afterimage/ingestion/IngestionOptions.h"

#include <filesystem>

namespace afterimage {

// Discovers supported files and records basic filesystem metadata.
// It deliberately does not open files or extract their contents.
class FileScanner {
public:
    ScanResult scan(
        const std::filesystem::path& root,
        const IngestionOptions& options = {}
    ) const;
};

} // namespace afterimage
