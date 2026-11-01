//
//  encoding.h
//  mkxp-z
//
//  Created by ゾロアーク on 6/22/21.
//

#ifndef encoding_h
#define encoding_h

#include <string>
#include <string.h>

#include "util/exception.h"
#include "util/text-encoding.h"
#include <iconv.h>
#include <errno.h>

namespace Encoding {

static std::string convertString(std::string &str, const char *charset) {
    // Conversion doesn't need to happen if it's already UTF-8
    if (!strcmp(charset, "UTF-8") || !strcmp(charset, "ASCII")) {
        if (!validUtf8(str))
            throw Exception(Exception::MKXPError, "Invalid UTF-8 text");
        return str.starts_with("\xef\xbb\xbf") ? str.substr(3) : str;
    }
    
    struct Converter {
        iconv_t handle;
        ~Converter() { iconv_close(handle); }
    };
    const auto handle = iconv_open("UTF-8", charset);
    if (handle == (iconv_t)-1)
        throw Exception(Exception::MKXPError, "Unsupported text encoding: %s", charset);
    Converter converter{handle};
    
    size_t inLen = str.size();
    size_t outLen = inLen * 4;
    std::string buf(outLen, '\0');
    char *inPtr = str.data();
    char *outPtr = buf.data();
    
    errno = 0;
    size_t result = iconv(converter.handle, &inPtr, &inLen, &outPtr, &outLen);
    
    if (result != (size_t)-1 && errno == 0)
    {
        buf.resize(buf.size()-outLen);
    }
    else {
        throw Exception(Exception::MKXPError, "Unable to convert string (encoding: %s)", charset);
    }
    
    return buf;
}

static std::string convertString(std::string &str) {
    return convertString(str, textCharset(str));
}
}

#endif /* encoding_h */
