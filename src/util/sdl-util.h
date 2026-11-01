#ifndef SDLUTIL_H
#define SDLUTIL_H

#include <SDL3/SDL_iostream.h>

#include <string>
#include <iostream>
#include <cstring>
#include <memory>

// A decoder may own its IO handle while the underlying stream must survive
// a format retry. SDL3 streams are opaque and cannot be copied like RWops.
inline SDL_IOStream *borrowIO(SDL_IOStream *source) {
    SDL_IOStreamInterface interface;
    SDL_INIT_INTERFACE(&interface);
    interface.size = [](void *p) -> Sint64 { return SDL_GetIOSize(static_cast<SDL_IOStream *>(p)); };
    interface.seek = [](void *p, Sint64 offset, SDL_IOWhence whence) -> Sint64 {
        return SDL_SeekIO(static_cast<SDL_IOStream *>(p), offset, whence);
    };
    interface.read = [](void *p, void *buffer, size_t bytes, SDL_IOStatus *status) -> size_t {
        auto *stream = static_cast<SDL_IOStream *>(p);
        const auto read = SDL_ReadIO(stream, buffer, bytes);
        *status = SDL_GetIOStatus(stream);
        return read;
    };
    return SDL_OpenIO(&interface, source);
}

inline bool readFileSDL(const char *path, std::string &out) {
    SDL_IOStream *stream = SDL_IOFromFile(path, "rb");
    if (!stream)
        return false;
    size_t size = 0;
    std::unique_ptr<void, decltype(&SDL_free)> data(SDL_LoadFile_IO(stream, &size, true), SDL_free);
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

		if (eback() == base)
		{
			memmove(base, egptr() - pbSize, pbSize);
			start += pbSize;
		}

		size_t n = SDL_ReadIO(ops, start, bufSize - (start - base));
		if (n == 0)
			return traits_type::eof();

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
	    : ops(SDL_IOFromFile(filename, mode)),
	      buf(ops),
	      s(&buf)
	{}

	~SDLRWStream()
	{
		if (ops)
			SDL_CloseIO(ops);
	}

	operator bool() const
	{
		return ops != 0;
	}

	std::istream &stream()
	{
		return s;
	}

private:
	SDL_IOStream *ops;
	SDLRWBuf<> buf;
	std::istream s;
};

#endif // SDLUTIL_H
