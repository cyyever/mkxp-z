#ifndef MKXP_FILENAME_NORMALIZATION_H
#define MKXP_FILENAME_NORMALIZATION_H

#include <string>
#include <string_view>

namespace paths {
// Normalize lookup keys only. Keep the original native path for opening files.
#ifdef __APPLE__
std::string normalizedNfc(std::string_view value);
#else
inline std::string normalizedNfc(std::string_view value) {
    return std::string(value);
}
#endif
}

#endif
