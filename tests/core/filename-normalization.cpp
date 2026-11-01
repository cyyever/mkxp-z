#include "util/filename-normalization.h"

#include <iostream>
#include <stdexcept>

static void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

static std::string utf8(std::u8string_view value) {
    return {reinterpret_cast<const char *>(value.data()), value.size()};
}

int main() {
    try {
        const auto decomposed = utf8(u8"Fonts/cafe\u0301.ttf");
        const auto original = decomposed;
#ifdef __APPLE__
        check(paths::normalizedNfc(decomposed) == utf8(u8"Fonts/caf\u00e9.ttf"),
              "compose decomposed accents in lookup keys");
        check(paths::normalizedNfc(utf8(u8"\u1100\u1161.rxdata")) == utf8(u8"\uac00.rxdata"),
              "compose Hangul filename lookup keys");
#else
        check(paths::normalizedNfc(decomposed) == decomposed,
              "leave non-Apple filesystem spelling unchanged");
#endif
        check(decomposed == original, "do not mutate on-disk filename spelling");
        check(paths::normalizedNfc(utf8(u8"\u96e8\u8840.rxdata")) == utf8(u8"\u96e8\u8840.rxdata"),
              "preserve already-normalized Chinese names");
        check(paths::normalizedNfc({}).empty(), "normalize empty filename");
        const std::string embeddedNull("a\0b", 3);
        check(paths::normalizedNfc(embeddedNull) == embeddedNull, "preserve explicit string length");
        const std::string invalid("\xff", 1);
        check(paths::normalizedNfc(invalid) == invalid, "preserve invalid UTF-8 for existing lookup behavior");
        std::cout << "Filename normalization tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
