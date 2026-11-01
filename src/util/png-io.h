#ifndef MKXP_PNG_IO_H
#define MKXP_PNG_IO_H

#include <SDL3_image/SDL_image.h>

namespace image {
// Owns and closes the stream on both success and rejection, like Bitmap loads.
inline SDL_Surface *loadPng(SDL_IOStream *stream) {
    if (!stream) return nullptr;
    if (!IMG_isPNG(stream)) {
        SDL_CloseIO(stream);
        SDL_SetError("Bitmap assets must be PNG");
        return nullptr;
    }
    SDL_Surface *surface = IMG_LoadPNG_IO(stream);
    SDL_CloseIO(stream);
    return surface;
}
}

#endif
