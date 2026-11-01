#pragma once
#include <SDL3/SDL_iostream.h>
struct FileSystem { SDL_IOStream *openReadRaw(const char *filename); };
