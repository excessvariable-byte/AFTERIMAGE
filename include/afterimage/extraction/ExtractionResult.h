#pragma once

#include <cstddef>
#include <string>

namespace afterimage {

enum class ExtractionStatus {
    Success,
    Partial,
    Empty,
    Unsupported,
    DependencyMissing,
    Failed,
    TooLarge
};

struct ExtractionResult {
    ExtractionStatus status{ExtractionStatus::Failed};
    std::string text;
    std::string message;
    std::size_t bytesRead{0};
    std::size_t pagesProcessed{0};

    [[nodiscard]] bool succeeded() const noexcept {
        return status == ExtractionStatus::Success ||
               status == ExtractionStatus::Partial ||
               status == ExtractionStatus::Empty;
    }
};

} // namespace afterimage
