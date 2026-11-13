#include "afterimage/extraction/ContentExtractor.h"
#include "afterimage/extraction/TextNormaliser.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <optional>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace afterimage {
namespace {

bool isTextFormat(const std::string& extension) {
    return extension == "txt" || extension == "md" || extension == "markdown";
}

bool isImageFormat(const std::string& extension) {
    return extension == "png" || extension == "jpg" || extension == "jpeg" ||
           extension == "tif" || extension == "tiff" || extension == "bmp" ||
           extension == "webp" || extension == "gif";
}

struct ProcessResult {
    bool started{false};
    bool timedOut{false};
    bool outputTruncated{false};
    int exitCode{-1};
    std::string output;
    std::string error;
};

#if defined(__unix__) || defined(__APPLE__)

// Run a local executable without invoking a shell. This avoids shell-injection
// bugs and correctly handles paths containing spaces, quotes, or metacharacters.
ProcessResult runProcess(
    const std::vector<std::string>& arguments,
    std::size_t maxOutputBytes,
    unsigned int timeoutSeconds
) {
    ProcessResult result;
    if (arguments.empty() || arguments.front().empty()) {
        result.error = "No executable was configured.";
        return result;
    }

    int pipeDescriptors[2]{};
    if (::pipe(pipeDescriptors) != 0) {
        result.error = "Cannot create a process output pipe: " + std::string(std::strerror(errno));
        return result;
    }

    posix_spawn_file_actions_t actions;
    int actionResult = posix_spawn_file_actions_init(&actions);
    if (actionResult != 0) {
        ::close(pipeDescriptors[0]);
        ::close(pipeDescriptors[1]);
        result.error = "Cannot initialise process actions: " + std::string(std::strerror(actionResult));
        return result;
    }

    actionResult = posix_spawn_file_actions_adddup2(&actions, pipeDescriptors[1], STDOUT_FILENO);
    if (actionResult == 0) actionResult = posix_spawn_file_actions_addclose(&actions, pipeDescriptors[0]);
    if (actionResult == 0) actionResult = posix_spawn_file_actions_addclose(&actions, pipeDescriptors[1]);
    if (actionResult == 0) {
        actionResult = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    }
    if (actionResult != 0) {
        posix_spawn_file_actions_destroy(&actions);
        ::close(pipeDescriptors[0]);
        ::close(pipeDescriptors[1]);
        result.error = "Cannot configure process output: " + std::string(std::strerror(actionResult));
        return result;
    }

    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const std::string& argument : arguments) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);

    pid_t child = -1;
    const int spawnResult = posix_spawnp(
        &child, argv.front(), &actions, nullptr, argv.data(), environ
    );
    posix_spawn_file_actions_destroy(&actions);
    ::close(pipeDescriptors[1]);

    if (spawnResult != 0) {
        ::close(pipeDescriptors[0]);
        result.error = spawnResult == ENOENT
            ? "Required program not found: " + arguments.front()
            : "Cannot start " + arguments.front() + ": " + std::string(std::strerror(spawnResult));
        return result;
    }
    result.started = true;

    const int oldFlags = ::fcntl(pipeDescriptors[0], F_GETFL, 0);
    if (oldFlags < 0 || ::fcntl(pipeDescriptors[0], F_SETFL, oldFlags | O_NONBLOCK) != 0) {
        result.error = "Cannot configure the process output pipe: " + std::string(std::strerror(errno));
        ::kill(child, SIGKILL);
        while (::waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
        ::close(pipeDescriptors[0]);
        return result;
    }

    const auto startedAt = std::chrono::steady_clock::now();
    bool pipeClosed = false;
    bool childExited = false;
    int childStatus = 0;
    char buffer[16 * 1024];

    while (!pipeClosed || !childExited) {
        pollfd descriptor{pipeDescriptors[0], POLLIN | POLLHUP, 0};
        const int pollResult = pipeClosed ? ::poll(nullptr, 0, 100) : ::poll(&descriptor, 1, 100);
        if (pollResult > 0 && (descriptor.revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            while (true) {
                const ssize_t count = ::read(pipeDescriptors[0], buffer, sizeof(buffer));
                if (count > 0) {
                    const std::size_t received = static_cast<std::size_t>(count);
                    const std::size_t remaining = result.output.size() < maxOutputBytes
                        ? maxOutputBytes - result.output.size()
                        : 0U;
                    const std::size_t keep = std::min(remaining, received);
                    result.output.append(buffer, keep);
                    if (keep < received) result.outputTruncated = true;
                } else if (count == 0) {
                    pipeClosed = true;
                    break;
                } else if (errno == EINTR) {
                    continue;
                } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                } else {
                    pipeClosed = true;
                    break;
                }
            }
        } else if (pollResult < 0 && errno != EINTR) {
            result.error = "Error while reading program output: " + std::string(std::strerror(errno));
            pipeClosed = true;
        }

        if (!childExited) {
            const pid_t waited = ::waitpid(child, &childStatus, WNOHANG);
            if (waited == child) {
                childExited = true;
            } else if (waited < 0 && errno != EINTR) {
                childExited = true;
                result.error = "Could not collect external process status.";
            }
        }

        if (!childExited && timeoutSeconds > 0U &&
            std::chrono::steady_clock::now() - startedAt > std::chrono::seconds(timeoutSeconds)) {
            result.timedOut = true;
            ::kill(child, SIGKILL);
            while (::waitpid(child, &childStatus, 0) < 0 && errno == EINTR) {}
            childExited = true;
        }

        if (pipeClosed && !childExited) {
            // The child may still be doing work after closing stdout; keep waiting
            // with the same bounded timeout rather than leaving a zombie process.
        }
    }

    ::close(pipeDescriptors[0]);
    if (!result.timedOut && WIFEXITED(childStatus)) result.exitCode = WEXITSTATUS(childStatus);
    else if (!result.timedOut && WIFSIGNALED(childStatus)) result.exitCode = 128 + WTERMSIG(childStatus);
    return result;
}

#else

ProcessResult runProcess(
    const std::vector<std::string>&,
    std::size_t,
    unsigned int
) {
    ProcessResult result;
    result.error = "External PDF/OCR adapters currently support macOS and Linux.";
    return result;
}

#endif

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        static std::atomic<unsigned long long> sequence{0};
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("afterimage_ocr_" + std::to_string(tick) + "_" +
             std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
        std::filesystem::create_directories(path_);
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
private:
    std::filesystem::path path_;
};

ExtractionResult failure(ExtractionStatus status, std::string message, std::size_t bytesRead = 0) {
    ExtractionResult result;
    result.status = status;
    result.message = std::move(message);
    result.bytesRead = bytesRead;
    return result;
}

std::string normaliseOutput(std::string output, const ExtractionOptions& options) {
    if (options.normaliseWhitespace) return TextNormaliser::normalise(output);
    return output;
}

ExtractionResult finishText(
    std::string text,
    const std::string& message,
    std::size_t bytesRead,
    bool partial,
    std::size_t pagesProcessed,
    const ExtractionOptions& options
) {
    ExtractionResult result;
    result.text = normaliseOutput(std::move(text), options);
    result.bytesRead = bytesRead;
    result.pagesProcessed = pagesProcessed;
    result.status = result.text.empty() ? ExtractionStatus::Empty
        : (partial ? ExtractionStatus::Partial : ExtractionStatus::Success);
    result.message = result.text.empty() ? "The file contains no extractable text." : message;
    return result;
}

ExtractionResult extractPlainText(const Document& document, const ExtractionOptions& options) {
    ExtractionResult result;
    std::error_code error;
    const auto fileSize = std::filesystem::file_size(document.filePath, error);
    if (error) return failure(ExtractionStatus::Failed, "Cannot read file size: " + error.message());
    if (fileSize > options.maxFileBytes) {
        return failure(ExtractionStatus::TooLarge, "File exceeds the configured extraction size limit.");
    }

    std::ifstream input(document.filePath, std::ios::binary);
    if (!input) return failure(ExtractionStatus::Failed, "Unable to open file for reading.");

    result.text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (input.bad()) return failure(ExtractionStatus::Failed, "An I/O error occurred while reading the file.");
    result.bytesRead = result.text.size();
    if (result.text.size() > options.maxExtractedTextBytes) {
        result.text.resize(static_cast<std::size_t>(options.maxExtractedTextBytes));
        result.text = normaliseOutput(std::move(result.text), options);
        result.status = ExtractionStatus::Partial;
        result.message = "Text extracted, but the configured output size limit was reached.";
        return result;
    }
    result.text = normaliseOutput(std::move(result.text), options);
    result.status = result.text.empty() ? ExtractionStatus::Empty : ExtractionStatus::Success;
    result.message = result.text.empty() ? "The file contains no extractable text." : "Text extracted.";
    return result;
}

std::optional<std::size_t> readPdfPageCount(const std::filesystem::path& pdf,
                                             const ExtractionOptions& options) {
    const ProcessResult info = runProcess(
        {options.pdfInfoExecutable, pdf.string()}, 256U * 1024U, options.processTimeoutSeconds
    );
    if (!info.started || info.timedOut || info.exitCode != 0) return std::nullopt;
    std::istringstream lines(info.output);
    std::string line;
    while (std::getline(lines, line)) {
        constexpr std::string_view prefix{"Pages:"};
        if (line.starts_with(prefix)) {
            std::istringstream value(line.substr(prefix.size()));
            std::size_t pages = 0;
            if (value >> pages) return pages;
        }
    }
    return std::nullopt;
}

std::vector<std::filesystem::path> renderedPages(const std::filesystem::path& directory) {
    std::vector<std::pair<std::size_t, std::filesystem::path>> numbered;
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error) || error) { error.clear(); continue; }
        const std::string name = it->path().filename().string();
        if (!name.starts_with("page-") || it->path().extension() != ".png") continue;
        const std::string number = name.substr(5, name.size() - 9);
        try {
            std::size_t consumed = 0;
            const auto page = static_cast<std::size_t>(std::stoull(number, &consumed));
            if (consumed == number.size()) numbered.emplace_back(page, it->path());
        } catch (...) {
            // Ignore files not matching Poppler's page-number pattern.
        }
    }
    std::sort(numbered.begin(), numbered.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
    std::vector<std::filesystem::path> result;
    result.reserve(numbered.size());
    for (auto& item : numbered) result.push_back(std::move(item.second));
    return result;
}

ExtractionResult ocrPdf(const std::filesystem::path& pdf, std::size_t inputBytes,
                        const ExtractionOptions& options) {
    if (!options.enableOcr) {
        return failure(ExtractionStatus::Empty,
                       "No text layer was found. OCR is disabled; enable OCR to process scanned PDFs.", inputBytes);
    }
    if (options.maxPdfPagesForOcr == 0U) {
        return failure(ExtractionStatus::Failed, "The configured PDF OCR page limit is zero.", inputBytes);
    }

    TemporaryDirectory temporary;
    const auto outputPrefix = temporary.path() / "page";
    const ProcessResult render = runProcess(
        {options.pdfToPpmExecutable, "-f", "1", "-l", std::to_string(options.maxPdfPagesForOcr),
         "-scale-to", std::to_string(options.pdfRenderMaxDimension), "-png", pdf.string(), outputPrefix.string()},
        64U * 1024U, options.processTimeoutSeconds
    );
    if (!render.started) {
        return failure(ExtractionStatus::DependencyMissing,
                       "Scanned PDF OCR requires Poppler's pdftoppm. " + render.error, inputBytes);
    }
    if (render.timedOut) return failure(ExtractionStatus::Failed, "PDF page rendering timed out.", inputBytes);
    if (render.exitCode != 0) {
        return failure(ExtractionStatus::Failed, "Poppler could not render this PDF. It may be damaged or encrypted.", inputBytes);
    }

    const auto pages = renderedPages(temporary.path());
    if (pages.empty()) return failure(ExtractionStatus::Failed, "No pages could be rendered from this PDF.", inputBytes);

    std::string combined;
    bool outputTruncated = false;
    std::size_t processed = 0;
    for (const auto& page : pages) {
        const std::string separator = combined.empty() ? std::string{} : std::string{"\n\n"};
        const std::size_t maximum = static_cast<std::size_t>(options.maxExtractedTextBytes);
        const std::size_t separatorRoom = combined.size() < maximum ? maximum - combined.size() : 0U;
        if (separator.size() > separatorRoom) {
            combined.append(separator, 0, separatorRoom);
            outputTruncated = true;
            break;
        }
        combined += separator;
        const std::size_t remaining = combined.size() < maximum ? maximum - combined.size() : 0U;
        const ProcessResult ocr = runProcess(
            {options.tesseractExecutable, page.string(), "stdout", "-l", options.ocrLanguage,
             "--psm", std::to_string(options.ocrPageSegmentationMode), "quiet"},
            remaining, options.processTimeoutSeconds
        );
        if (!ocr.started) {
            return failure(ExtractionStatus::DependencyMissing,
                           "Scanned PDF OCR requires Tesseract. " + ocr.error, inputBytes);
        }
        if (ocr.timedOut) return failure(ExtractionStatus::Failed, "OCR timed out while processing a PDF page.", inputBytes);
        if (ocr.exitCode != 0) return failure(ExtractionStatus::Failed, "Tesseract could not recognise a rendered PDF page.", inputBytes);
        combined += ocr.output;
        outputTruncated = outputTruncated || ocr.outputTruncated;
        ++processed;
        if (combined.size() >= maximum) {
            outputTruncated = true;
            break;
        }
    }

    const auto pageCount = readPdfPageCount(pdf, options);
    const bool pageLimitReached = pageCount
        ? *pageCount > processed
        : (processed >= options.maxPdfPagesForOcr);
    const bool partial = outputTruncated || pageLimitReached;
    const std::string message = partial
        ? "OCR extracted part of the scanned PDF (page or text limit reached)."
        : "Scanned PDF processed with OCR.";
    return finishText(std::move(combined), message, inputBytes, partial, processed, options);
}

ExtractionResult extractPdf(const Document& document, std::size_t inputBytes,
                            const ExtractionOptions& options) {
    const ProcessResult pdfText = runProcess(
        {options.pdfToTextExecutable, "-enc", "UTF-8", "-layout", document.filePath, "-"},
        static_cast<std::size_t>(options.maxExtractedTextBytes), options.processTimeoutSeconds
    );
    if (pdfText.timedOut) return failure(ExtractionStatus::Failed, "PDF text extraction timed out.", inputBytes);
    if (pdfText.started && pdfText.exitCode != 0) {
        return failure(ExtractionStatus::Failed,
                       "Poppler could not extract this PDF. It may be damaged, encrypted, or unsupported.", inputBytes);
    }
    if (pdfText.started && !normaliseOutput(pdfText.output, options).empty()) {
        return finishText(pdfText.output,
                          pdfText.outputTruncated ? "PDF text extracted, but the text output limit was reached."
                                                  : "PDF text extracted with Poppler.",
                          inputBytes, pdfText.outputTruncated, 0, options);
    }

    // A valid PDF can contain only page images. Try OCR in that case, and also
    // when pdftotext is absent but the Poppler renderer and Tesseract are present.
    const ExtractionResult ocr = ocrPdf(document.filePath, inputBytes, options);
    if (ocr.status == ExtractionStatus::DependencyMissing && !pdfText.started) {
        return failure(ExtractionStatus::DependencyMissing,
                       "PDF text extraction needs Poppler (pdftotext). " + pdfText.error + " " + ocr.message,
                       inputBytes);
    }
    return ocr;
}

ExtractionResult extractImage(const Document& document, std::size_t inputBytes,
                              const ExtractionOptions& options) {
    if (!options.enableOcr) {
        return failure(ExtractionStatus::Unsupported, "Image OCR is disabled in the current extraction settings.", inputBytes);
    }
    const ProcessResult ocr = runProcess(
        {options.tesseractExecutable, document.filePath, "stdout", "-l", options.ocrLanguage,
         "--psm", std::to_string(options.ocrPageSegmentationMode), "quiet"},
        static_cast<std::size_t>(options.maxExtractedTextBytes), options.processTimeoutSeconds
    );
    if (!ocr.started) {
        return failure(ExtractionStatus::DependencyMissing,
                       "Image OCR requires Tesseract. Install it and retry. " + ocr.error, inputBytes);
    }
    if (ocr.timedOut) return failure(ExtractionStatus::Failed, "Image OCR timed out.", inputBytes);
    if (ocr.exitCode != 0) return failure(ExtractionStatus::Failed, "Tesseract could not process this image.", inputBytes);
    return finishText(ocr.output,
                      ocr.outputTruncated ? "Image OCR completed, but the text output limit was reached."
                                          : "Image processed with Tesseract OCR.",
                      inputBytes, ocr.outputTruncated, 1, options);
}

} // namespace

ContentExtractor::ContentExtractor(ExtractionOptions options) : options_(std::move(options)) {}

ExtractionResult ContentExtractor::extract(const Document& document) const {
    std::error_code error;
    const auto fileSize = std::filesystem::file_size(document.filePath, error);
    if (error) return failure(ExtractionStatus::Failed, "Cannot read file size: " + error.message());
    if (fileSize > options_.maxFileBytes) {
        return failure(ExtractionStatus::TooLarge, "File exceeds the configured extraction size limit.",
                       static_cast<std::size_t>(fileSize));
    }
    const std::size_t inputBytes = static_cast<std::size_t>(fileSize);

    if (isTextFormat(document.fileType)) return extractPlainText(document, options_);
    if (document.fileType == "pdf") return extractPdf(document, inputBytes, options_);
    if (isImageFormat(document.fileType)) return extractImage(document, inputBytes, options_);
    return failure(ExtractionStatus::Unsupported, "Unsupported file format.", inputBytes);
}

} // namespace afterimage
