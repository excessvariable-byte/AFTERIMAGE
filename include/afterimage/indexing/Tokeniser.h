#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace afterimage {

struct Token {
    std::string text;
    std::size_t position{0};
};

// Converts text into lowercase searchable tokens. Positions count words,
// not characters, so adjacent positions can later prove that a phrase matches.
class Tokeniser {
public:
    static std::vector<Token> tokenize(std::string_view text);
};

} // namespace afterimage
