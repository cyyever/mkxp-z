#ifndef MKXP_PATHS_H
#define MKXP_PATHS_H

#include "exception.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

// Only encoding and RGSS-specific path rules belong here. Use std::filesystem
// directly for filesystem operations, with these conversions at UTF-8 boundaries.
namespace paths {
inline std::filesystem::path fromUtf8(std::string_view value) {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}

inline std::string toUtf8(std::u8string_view value) {
    return {reinterpret_cast<const char *>(value.data()), value.size()};
}

inline std::filesystem::path fromRgss(std::string_view value) {
    std::string portable(value);
    std::ranges::replace(portable, '\\', '/');
    return fromUtf8(portable).lexically_normal();
}

inline std::string readFile(const std::filesystem::path &path) {
    try {
        std::ifstream stream(path, std::ios::binary);
        stream.exceptions(std::ios::badbit);
        if (stream.is_open())
            return {std::istreambuf_iterator<char>(stream), {}};
    } catch (const std::ios_base::failure &) {
        // Keep file errors compatible with the Ruby exception translation.
    }
    throw Exception(Exception::NoFileError, "Failed to read file at %s",
                    toUtf8(path.u8string()).c_str());
}
}

#endif
