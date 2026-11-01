#include "util/text-encoding.h"

#include <stdexcept>
#include <string_view>

using namespace std::literals;

void require(bool condition) {
    if (!condition)
        throw std::runtime_error("text encoding boundary failure");
}

int main() {
    for (auto text : {""sv, "Untitled"sv, "\xe9\x9b\xa8\xe8\xa1\x80"sv,
                      "\xc2\x80"sv, "\xf4\x8f\xbf\xbf"sv, "a\0b"sv}) {
        require(Encoding::validUtf8(text));
        require(Encoding::textCharset(text) == "UTF-8"sv);
    }
    for (auto text : {"\x80"sv, "\xc0\xaf"sv, "\xe0\x80\x80"sv,
                      "\xed\xa0\x80"sv, "\xf4\x90\x80\x80"sv,
                      "\xf5\x80\x80\x80"sv, "\xe9\x9b"sv, "\xc2x"sv})
        require(!Encoding::validUtf8(text));

    require(Encoding::textCharset("\xd3\xea\xd1\xaa"sv) == "GB18030"sv);
    require(Encoding::textCharset("\xff\xfe\0\0"sv) == "UTF-32"sv);
    require(Encoding::textCharset("\0\0\xfe\xff"sv) == "UTF-32"sv);
    require(Encoding::textCharset("\xff\xfe"sv) == "UTF-16"sv);
    require(Encoding::textCharset("\xfe\xff"sv) == "UTF-16"sv);
    // A BOM commits even malformed input to Unicode; conversion must fail
    // rather than retrying it as the legacy Chinese fallback.
    require(Encoding::textCharset("\xef\xbb\xbf\xff"sv) == "UTF-8"sv);
    require(!Encoding::validUtf8("\xef\xbb\xbf\xff"sv));
    require(Encoding::textCharset("\xff\xfe\xff"sv) == "UTF-16"sv);
}
