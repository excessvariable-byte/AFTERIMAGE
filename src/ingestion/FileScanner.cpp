#include "afterimage/ingestion/FileScanner.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <system_error>
#include <utility>

namespace afterimage {
namespace {

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool isHiddenName(const std::filesystem::path& path) {
    const std::string name = path.filename().string();
    return !name.empty() && name.front() == '.';
}

std::filesystem::path normalizedAbsolutePath(
    const std::filesystem::path& path,
    std::error_code& error
) {
    auto normalized = std::filesystem::weakly_canonical(path, error);
    if (!error) {
        return normalized;
    }

    error.clear();
    normalized = std::filesystem::absolute(path, error);
    if (!error) {
        return normalized.lexically_normal();
    }

    return path.lexically_normal();
}

std::int64_t toUnixSeconds(std::filesystem::file_time_type fileTime) {
    // file_clock and system_clock are not guaranteed to share an epoch.
    // This clock-offset conversion works with C++20 standard-library builds
    // commonly shipped with macOS, Linux and Windows toolchains.
    const auto systemTime = std::chrono::time_point_cast<
        std::chrono::system_clock::duration
    >(
        fileTime - std::filesystem::file_time_type::clock::now()
        + std::chrono::system_clock::now()
    );

    return std::chrono::duration_cast<std::chrono::seconds>(
        systemTime.time_since_epoch()
    ).count();
}

void addIssue(ScanResult& result, const std::filesystem::path& path,
              const std::string& message) {
    result.issues.push_back(ScanIssue{path, message});
}

} // namespace

ScanResult FileScanner::scan(
    const std::filesystem::path& root,
    const IngestionOptions& options
) const {
    ScanResult result;

    if (root.empty()) {
        addIssue(result, root, "The selected folder path is empty.");
        return result;
    }

    std::error_code error;
    const auto normalizedRoot = normalizedAbsolutePath(root, error);
    if (error) {
        addIssue(result, root, "Unable to resolve the selected folder path: " + error.message());
        return result;
    }

    const bool rootIsDirectory = std::filesystem::is_directory(normalizedRoot, error);
    if (error) {
        addIssue(result, normalizedRoot, "Unable to inspect the selected folder: " + error.message());
        return result;
    }
    if (!rootIsDirectory) {
        addIssue(result, normalizedRoot, "The selected path is not a directory.");
        return result;
    }

    result.rootPath = normalizedRoot;
    result.rootValid = true;

    const auto directoryOptions = std::filesystem::directory_options::skip_permission_denied;
    std::filesystem::recursive_directory_iterator iterator(
        normalizedRoot, directoryOptions, error
    );
    const std::filesystem::recursive_directory_iterator end;

    if (error) {
        addIssue(result, normalizedRoot, "Unable to begin scanning this folder: " + error.message());
        return result;
    }

    while (iterator != end) {
        const auto entry = *iterator;
        const auto entryPath = entry.path();
        ++result.entriesVisited;

        std::error_code statusError;
        const auto linkStatus = entry.symlink_status(statusError);

        if (statusError) {
            addIssue(result, entryPath, "Unable to inspect entry: " + statusError.message());
        } else if (std::filesystem::is_symlink(linkStatus)) {
            // Do not follow symlinks: they can point outside the selected folder
            // or form directory cycles.
            ++result.symbolicLinksSkipped;
        } else if (options.skipHidden && isHiddenName(entryPath)) {
            ++result.hiddenEntriesSkipped;
            if (std::filesystem::is_directory(linkStatus)) {
                iterator.disable_recursion_pending();
            }
        } else if (std::filesystem::is_directory(linkStatus)) {
            if (!options.recursive) {
                iterator.disable_recursion_pending();
            }
        } else if (std::filesystem::is_regular_file(linkStatus)) {
            const std::string extension = toLower(entryPath.extension().string());
            const std::string extensionWithoutDot =
                !extension.empty() && extension.front() == '.'
                    ? extension.substr(1)
                    : extension;

            if (!options.supportedExtensions.contains(extensionWithoutDot)) {
                ++result.unsupportedFilesSkipped;
            } else {
                std::error_code pathError;
                const auto normalizedPath = normalizedAbsolutePath(entryPath, pathError);

                DiscoveredFile file;
                file.path = pathError ? entryPath.lexically_normal() : normalizedPath;
                file.title = entryPath.stem().string();
                file.extension = extensionWithoutDot;

                std::error_code sizeError;
                const auto size = entry.file_size(sizeError);
                if (sizeError) {
                    addIssue(result, entryPath, "Unable to read file size: " + sizeError.message());
                } else {
                    file.sizeBytes = size;
                }

                std::error_code timeError;
                const auto modified = entry.last_write_time(timeError);
                if (timeError) {
                    addIssue(result, entryPath, "Unable to read modification time: " + timeError.message());
                } else {
                    file.modifiedAtEpochSeconds = toUnixSeconds(modified);
                }

                result.files.push_back(std::move(file));
            }
        }

        iterator.increment(error);
        if (error) {
            addIssue(result, entryPath, "Unable to continue scanning after this entry: " + error.message());
            error.clear();
        }
    }

    std::sort(result.files.begin(), result.files.end(),
              [](const DiscoveredFile& left, const DiscoveredFile& right) {
                  return left.path.generic_string() < right.path.generic_string();
              });

    return result;
}

} // namespace afterimage
