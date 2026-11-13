#include "afterimage/indexing/Tokeniser.h"

#include <cctype>

namespace afterimage {
namespace {

bool isTokenCharacter(unsigned char character) {
    // ASCII letters and digits are handled explicitly. Bytes >= 128 are kept
    // inside tokens so UTF-8 words are not split into one token per byte.
    return std::isalnum(character) != 0 || character >= 128;
}

char lowerAscii(unsigned char character) {
    if (character >= 'A' && character <= 'Z') {
        return static_cast<char>(character - 'A' + 'a');
    }
    return static_cast<char>(character);
}

} // namespace

std::vector<Token> Tokeniser::tokenize(std::string_view text) {
    std::vector<Token> tokens;
    std::string current;

    const auto finishToken = [&]() {
        if (!current.empty()) {
            tokens.push_back(Token{current, tokens.size()});
            current.clear();
        }
    };

    for (const char rawCharacter : text) {
        const auto character = static_cast<unsigned char>(rawCharacter);
        if (isTokenCharacter(character)) {
            current.push_back(lowerAscii(character));
        } else {
            finishToken();
        }
    }
    finishToken();
    return tokens;
}

} // namespace afterimage
