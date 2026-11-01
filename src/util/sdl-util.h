#ifndef SDLUTIL_H
#define SDLUTIL_H

#include <SDL3/SDL_iostream.h>

#include <string>
#include <memory>

// A decoder may own its IO handle while the underlying stream must survive
// a format retry. SDL3 streams are opaque and cannot be copied like RWops.
inline SDL_IOStream *borrowIO(SDL_IOStream *source) {
    if (!source) {
        SDL_SetError("Cannot borrow a null IO stream");
        return nullptr;
    }
    SDL_IOStreamInterface ioInterface;
    SDL_INIT_INTERFACE(&ioInterface);
    ioInterface.size = [](void *p) -> Sint64 { return SDL_GetIOSize(static_cast<SDL_IOStream *>(p)); };
    ioInterface.seek = [](void *p, Sint64 offset, SDL_IOWhence whence) -> Sint64 {
        return SDL_SeekIO(static_cast<SDL_IOStream *>(p), offset, whence);
    };
    ioInterface.read = [](void *p, void *buffer, size_t bytes, SDL_IOStatus *status) -> size_t {
        auto *stream = static_cast<SDL_IOStream *>(p);
        const auto read = SDL_ReadIO(stream, buffer, bytes);
        *status = SDL_GetIOStatus(stream);
        return read;
    };
    return SDL_OpenIO(&ioInterface, source);
}

// Reads from the current position without taking ownership. Keep the stream
// alive until status is checked: SDL_LoadFile_IO can return data after an error.
inline bool readFileSDL(SDL_IOStream *stream, std::string &out) {
    if (!stream)
        return false;
    size_t size = 0;
    std::unique_ptr<void, decltype(&SDL_free)> data(SDL_LoadFile_IO(stream, &size, false), SDL_free);
    const auto status = SDL_GetIOStatus(stream);
    if (!data || (status != SDL_IO_STATUS_READY && status != SDL_IO_STATUS_EOF))
        return false;
    out.append(static_cast<const char *>(data.get()), size);
    return true;
}

inline bool readFileSDL(const char *path, std::string &out) {
    std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> stream(SDL_IOFromFile(path, "rb"), SDL_CloseIO);
    return stream && readFileSDL(stream.get(), out);
}

#endif // SDLUTIL_H
