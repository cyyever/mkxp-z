#include "util/filename-normalization.h"

#import <Foundation/Foundation.h>

std::string paths::normalizedNfc(std::string_view value) {
    if (value.empty()) return {};
    @autoreleasepool {
        NSString *source = [[NSString alloc] initWithBytes:value.data()
                                                  length:value.size()
                                                encoding:NSUTF8StringEncoding];
        if (!source) return std::string(value);
        NSData *encoded = [source.precomposedStringWithCanonicalMapping
            dataUsingEncoding:NSUTF8StringEncoding];
        if (!encoded) return std::string(value);
        return {static_cast<const char *>(encoded.bytes), static_cast<size_t>(encoded.length)};
    }
}
