#include "filesystemImpl.h"
#include "util/exception.h"

#include <SDL_filesystem.h>
#include <SDL_stdinc.h>
#include <memory>

std::string filesystemImpl::getDefaultGameRoot() {
    const std::unique_ptr<char, decltype(&SDL_free)> path(SDL_GetBasePath(), SDL_free);
    if (!path)
        throw Exception(Exception::MKXPError, "Failed to retrieve executable path");
    return path.get();
}
