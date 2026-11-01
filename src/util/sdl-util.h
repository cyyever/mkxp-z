#ifndef SDLUTIL_H
#define SDLUTIL_H

#include <SDL3/SDL_iostream.h>

#include <algorithm>
#include <string>
#include <iostream>
#include <cstring>
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

inline bool readFileSDL(const char *path, std::string &out) {
    size_t size = 0;
    std::unique_ptr<void, decltype(&SDL_free)> data(SDL_LoadFile(path, &size), SDL_free);
    if (!data)
        return false;
    out.append(static_cast<const char *>(data.get()), size);
    return true;
}

template<size_t bufSize = 248, size_t pbSize = 8>
class SDLRWBuf : public std::streambuf
{
public:
	SDLRWBuf(SDL_IOStream *ops)
	    : ops(ops)
	{
		char *end = buf + bufSize + pbSize;
		setg(end, end, end);
	}

private:
	int_type underflow()
	{
		if (!ops)
			return traits_type::eof();

		if (gptr() < egptr())
			return traits_type::to_int_type(*gptr());

		char *base = buf;
		char *start = base;

		const auto putback = std::min<size_t>(pbSize, gptr() - eback());
		memmove(base, gptr() - putback, putback);
		start += putback;

		size_t n = SDL_ReadIO(ops, start, bufSize);
		if (n == 0) {
            const auto status = SDL_GetIOStatus(ops);
            if (status != SDL_IO_STATUS_READY && status != SDL_IO_STATUS_EOF)
                throw std::ios_base::failure(SDL_GetError());
			return traits_type::eof();
        }

		setg(base, start, start + n);

		return underflow();
	}

	SDL_IOStream *ops;
	char buf[bufSize+pbSize];
};

class SDLRWStream
{
public:
	SDLRWStream(const char *filename,
	            const char *mode)
	    : ops(SDL_IOFromFile(filename, mode), SDL_CloseIO),
	      buf(ops.get()),
	      s(&buf)
	{}

	~SDLRWStream() = default;

	operator bool() const
	{
		return static_cast<bool>(ops);
	}

	std::istream &stream()
	{
		return s;
	}

private:
	std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> ops;
	SDLRWBuf<> buf;
	std::istream s;
};

#endif // SDLUTIL_H
