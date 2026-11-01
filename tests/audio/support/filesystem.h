#pragma once
#include <SDL3/SDL_iostream.h>
struct FileSystem {
    struct OpenHandler { virtual bool tryRead(SDL_IOStream *, const char *) = 0; };
    void openRead(OpenHandler &handler, const char *filename);
};
