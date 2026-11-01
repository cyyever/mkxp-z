#ifndef MKXP_ENCODING_H
#define MKXP_ENCODING_H

#include "util/exception.h"
#include "util/text-encoding.h"

#include <string>

namespace Encoding {

inline std::string convertString(std::string_view text) {
    if (!validUtf8(text))
        throw Exception(Exception::MKXPError, "Invalid UTF-8 text");
    if (text.starts_with("\xef\xbb\xbf"))
        text.remove_prefix(3);
    return std::string(text);
}

}
#endif
