#ifndef MKXP_FONT_ENCODING_H
#define MKXP_FONT_ENCODING_H

#include "util/exception.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace FontEncoding {

// SFNT name records are big-endian UTF-16 or UTF-32, independently of the
// UTF-8 encoding required by game/configuration text.
inline std::string decodeName(std::string_view text, bool utf32 = false) {
    const unsigned int width = utf32 ? 4 : 2;
    if (text.size() % width)
        throw Exception(Exception::MKXPError, "Truncated Unicode text");
    const auto readUnit = [&](size_t position) {
        std::uint32_t value = 0;
        for (unsigned int byte = 0; byte < width; ++byte) {
            const auto shift = 8 * (width - byte - 1);
            value |= std::uint32_t(static_cast<unsigned char>(text[position + byte])) << shift;
        }
        return value;
    };
    std::string result;
    result.reserve(text.size());
    for (size_t position = 0; position < text.size(); position += width) {
        auto value = readUnit(position);
        if (width == 2 && value >= 0xd800 && value <= 0xdbff) {
            position += width;
            if (position == text.size())
                throw Exception(Exception::MKXPError, "Truncated Unicode surrogate pair");
            const auto low = readUnit(position);
            if (low < 0xdc00 || low > 0xdfff)
                throw Exception(Exception::MKXPError, "Invalid Unicode surrogate pair");
            value = 0x10000 + ((value - 0xd800) << 10) + (low - 0xdc00);
        }
        if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            throw Exception(Exception::MKXPError, "Invalid Unicode character");
        if (value < 0x80) {
            result.push_back(static_cast<char>(value));
        } else {
            if (value < 0x800) {
                result.push_back(static_cast<char>(0xc0 | (value >> 6)));
            } else {
                if (value < 0x10000) {
                    result.push_back(static_cast<char>(0xe0 | (value >> 12)));
                } else {
                    result.push_back(static_cast<char>(0xf0 | (value >> 18)));
                    result.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
                }
                result.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
            }
            result.push_back(static_cast<char>(0x80 | (value & 0x3f)));
        }
    }
    return result;
}

}
#endif
