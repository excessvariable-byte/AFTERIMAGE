#include "afterimage/extraction/TextNormaliser.h"

#include <cctype>

namespace afterimage {

std::string TextNormaliser::normalise(std::string_view input) {
    std::string output;
    output.reserve(input.size());

    std::size_t start = 0;
    // Strip a UTF-8 byte-order mark when present.
    if (input.size() >= 3 &&
        static_cast<unsigned char>(input[0]) == 0xEF &&
        static_cast<unsigned char>(input[1]) == 0xBB &&
        static_cast<unsigned char>(input[2]) == 0xBF) {
        start = 3;
    }

    for (std::size_t i = start; i < input.size(); ++i) {
        char ch = input[i];
        if (ch == '\f') {
            ch = '\n';
        }
        if (ch == '\r') {
            if (i + 1 < input.size() && input[i + 1] == '\n') {
                ++i;
            }
            ch = '\n';
        }
        if (ch == '\n') {
            while (!output.empty() && (output.back() == ' ' || output.back() == '\t')) {
                output.pop_back();
            }
            output.push_back('\n');
        } else {
            output.push_back(ch);
        }
    }

    while (!output.empty() && (output.back() == '\n' || output.back() == ' ' || output.back() == '\t')) {
        output.pop_back();
    }
    return output;
}

} // namespace afterimage
