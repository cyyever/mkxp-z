#ifndef MKXP_TEXT_ENCODING_H
#define MKXP_TEXT_ENCODING_H

#include <string_view>

namespace Encoding {

inline bool validUtf8(std::string_view text) {
    for (size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first < 0x80)
            continue;
        unsigned int value;
        unsigned int minimum;
        size_t remaining;
        if (first >= 0xc2 && first <= 0xdf) {
            value = first & 0x1f;
            minimum = 0x80;
            remaining = 1;
        } else if (first >= 0xe0 && first <= 0xef) {
            value = first & 0x0f;
            minimum = 0x800;
            remaining = 2;
        } else if (first >= 0xf0 && first <= 0xf4) {
            value = first & 0x07;
            minimum = 0x10000;
            remaining = 3;
        } else {
            return false;
        }
        if (remaining > text.size() - i)
            return false;
        while (remaining--) {
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80)
                return false;
            value = (value << 6) | (next & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            return false;
    }
    return true;
}

// Supported game/config text is UTF-8 or legacy Simplified Chinese. A Unicode
// BOM takes precedence and commits the input to that encoding, so malformed
// Unicode cannot be silently retried as Chinese text.
inline const char *textCharset(std::string_view text) {
    using namespace std::literals;
    if (text.starts_with("\xff\xfe\0\0"sv) || text.starts_with("\0\0\xfe\xff"sv))
        return "UTF-32";
    if (text.starts_with("\xff\xfe"sv) || text.starts_with("\xfe\xff"sv))
        return "UTF-16";
    if (text.starts_with("\xef\xbb\xbf"sv) || validUtf8(text))
        return "UTF-8";
    return "GB18030";
}

}
#endif
