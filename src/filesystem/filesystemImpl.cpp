//
//  filesystemImpl.cpp
//  Player
//
//  Created by ゾロアーク on 11/21/20.
//

#include "filesystemImpl.h"
#include "util/exception.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>

namespace fs = std::filesystem;

namespace {
fs::path fromUtf8(std::string_view path) {
    // char8_t selects UTF-8 conversion, independent of the Windows code page.
    return fs::path(std::u8string(path.begin(), path.end()));
}

std::string toUtf8(const std::u8string &path) {
    return {reinterpret_cast<const char *>(path.data()), path.size()};
}
}

bool filesystemImpl::fileExists(const char *path) {
    std::error_code error;
    const auto status = fs::status(fromUtf8(path), error);
    return !error && fs::exists(status) && !fs::is_directory(status);
}

std::string filesystemImpl::contentsOfFileAsString(const char *path) {
    try {
        // Preserve the original bytes, including CRLF and embedded NULs.
        std::ifstream stream(fromUtf8(path), std::ios::binary);
        stream.exceptions(std::ios::badbit);
        if (stream.is_open())
            return {std::istreambuf_iterator<char>(stream), {}};
    } catch (const std::ios_base::failure &) {
        // Translate read failures to the same engine exception as open failures.
    }
    throw Exception(Exception::NoFileError, "Failed to read file at %s", path);
}

bool filesystemImpl::setCurrentDirectory(const char *path) {
    std::error_code error;
    fs::current_path(fromUtf8(path), error);
    return !error;
}

std::string filesystemImpl::getCurrentDirectory() {
    try {
        return toUtf8(fs::current_path().u8string());
    } catch (const fs::filesystem_error &) {
        throw Exception(Exception::MKXPError, "Failed to retrieve current path");
    }
}

std::string filesystemImpl::normalizePath(const char *path, bool preferred, bool absolute) {
    // RGSS paths can contain Windows separators on every host. Normalize those
    // before collapsing dot components, including when running on Unix.
    std::string portable(path);
    std::ranges::replace(portable, '\\', '/');
    auto result = fromUtf8(portable);
    if (absolute && !result.is_absolute())
        result = fs::current_path() / result;
    result = result.lexically_normal();
    return preferred ? toUtf8(result.make_preferred().u8string())
                     : toUtf8(result.generic_u8string());
}
