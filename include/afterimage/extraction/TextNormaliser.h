#pragma once

#include <string>
#include <string_view>

namespace afterimage {

class TextNormaliser {
public:
    // Preserves paragraph boundaries while normalising line endings and
    // removing trailing whitespace on each line.
    [[nodiscard]] static std::string normalise(std::string_view input);
};

} // namespace afterimage
