#ifndef MKXP_PNG_IO_H
#define MKXP_PNG_IO_H

#include <SDL3/SDL_surface.h>

namespace image {
// Native PNG loading validates the format and closes the stream on every path.
inline SDL_Surface *loadPng(SDL_IOStream *stream) {
    return stream ? SDL_LoadPNG_IO(stream, true) : nullptr;
}
}

#endif
