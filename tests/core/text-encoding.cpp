#include "util/encoding.h"
#include "util/font-encoding.h"

#include <stdexcept>
#include <string_view>

using namespace std::literals;

void require(bool condition) {
    if (!condition)
        throw std::runtime_error("text encoding boundary failure");
}

template<class Function>
void requireFailure(Function operation) {
    bool failed = false;
    try {
        operation();
    } catch (const Exception &) {
        failed = true;
    }
    require(failed);
}

int main() {
    for (auto text : {""sv, "Untitled"sv, "\xe9\x9b\xa8\xe8\xa1\x80"sv,
                      "\xc2\x80"sv, "\xf4\x8f\xbf\xbf"sv, "a\0b"sv}) {
        require(Encoding::validUtf8(text));
        require(Encoding::convertString(text) == text);
    }
    for (auto text : {"\x80"sv, "\xc0\xaf"sv, "\xe0\x80\x80"sv,
                      "\xed\xa0\x80"sv, "\xf4\x90\x80\x80"sv,
                      "\xf5\x80\x80\x80"sv, "\xe9\x9b"sv, "\xc2x"sv,
                      "\xd3\xea\xd1\xaa"sv, // Legacy Chinese bytes.
                      "\xef\xbb\xbf\xff"sv,
                      "\xff\xfe\0A"sv, "\xfe\xff\0A"sv, // UTF-16 BOMs.
                      "\0\0\xfe\xff\0\0\0A"sv}) { // UTF-32 BOM.
        require(!Encoding::validUtf8(text));
        requireFailure([&] { Encoding::convertString(text); });
    }
    require(Encoding::convertString("\xef\xbb\xbfhello"sv) == "hello");

    // Font metadata is a fixed binary record format, not game text input.
    const auto expected = "A\0\xe9\x9b\xa8\xe8\xa1\x80\xf0\x9f\x8e\xae"sv;
    require(FontEncoding::decodeName("\0A\0\0\x96\xe8\x88\x40\xd8\x3c\xdf\xae"sv) == expected);
    require(FontEncoding::decodeName("\0\0\0A\0\0\0\0\0\0\x96\xe8\0\0\x88\x40\0\x01\xf3\xae"sv, true) == expected);
    require(FontEncoding::decodeName("\x07\xff\x08\0\xd7\xff\xe0\0\xdb\xff\xdf\xff"sv)
            == "\xdf\xbf\xe0\xa0\x80\xed\x9f\xbf\xee\x80\x80\xf4\x8f\xbf\xbf"sv);
    for (auto bytes : {"\xff"sv, "\xd8\0"sv, "\xd8\0\0A"sv, "\xdc\0"sv})
        requireFailure([&] { FontEncoding::decodeName(bytes); });
    for (auto bytes : {"\0\0\xd8\0"sv, "\0\x11\0\0"sv, "\0\0\0"sv})
        requireFailure([&] { FontEncoding::decodeName(bytes, true); });
}
