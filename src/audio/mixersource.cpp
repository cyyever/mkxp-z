/*
** mixersource.cpp
**
** This file is part of mkxp.
**
** Copyright (C) 2014 - 2021 Amaryllis Kulla <ancurio@mapleshrine.eu>
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** mkxp is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "aldatasource.h"
#include "exception.h"

#include <algorithm>
#include <string>
#include <vector>

/* Streams PCM from an SDL_mixer decoder. The decoder has no seek call, so
 * rewinding reopens it and seeking decodes up to the requested frame. */
struct MixerSource : ALDataSource
{
	SDL_IOStream *srcOps;
	MixDecoderPtr decoder;
	SDL_AudioSpec format;
	uint32_t bufSize;
	bool looped;

	/* The chunk after the one handed out, to know when a chunk is the last */
	std::vector<uint8_t> next;
	bool nextReady = false;

	MixerSource(SDL_IOStream *ops,
	            uint32_t maxBufSize,
	            bool looped)
	    : srcOps(ops),
	      looped(looped)
	{
		decoder = openRuntimeDecoder(srcOps);
		if (!decoder || !MIX_GetAudioDecoderFormat(decoder.get(), &format))
		{
			std::string error = SDL_GetError();
			decoder.reset();
			SDL_CloseIO(srcOps);
			throw Exception(Exception::SDLError, "SDL_mixer: %s", error.c_str());
		}

		const uint32_t frameSize = SDL_AUDIO_FRAMESIZE(format);
		bufSize = std::max(maxBufSize / frameSize, 1u) * frameSize;
	}

	~MixerSource()
	{
		decoder.reset();
		SDL_CloseIO(srcOps);
	}

	bool decode(std::vector<uint8_t> &out)
	{
		out.resize(bufSize);
		const int got = MIX_DecodeAudio(decoder.get(), out.data(), int(out.size()), &format);
		out.resize(std::max(got, 0));
		return got >= 0;
	}

	bool rewind()
	{
		nextReady = false;
		decoder.reset();
		if (SDL_SeekIO(srcOps, 0, SDL_IO_SEEK_SET) < 0)
			return false;
		decoder = openRuntimeDecoder(srcOps);
		return bool(decoder);
	}

	Status fillBuffer(std::vector<uint8_t> &out) override
	{
		if (!decoder)
			return ALDataSource::Error;

		if (!nextReady && !decode(next))
			return ALDataSource::Error;

		out.swap(next);
		nextReady = false;

		if (out.empty())
			return ALDataSource::EndOfStream;

		if (!decode(next))
			return ALDataSource::Error;
		nextReady = true;

		if (!next.empty())
			return ALDataSource::NoError;

		/* 'out' is the last chunk of the file */
		if (!looped)
			return ALDataSource::EndOfStream;

		if (!rewind())
			return ALDataSource::Error;
		return ALDataSource::WrapAround;
	}

	SDL_AudioSpec spec() override
	{
		return format;
	}

	void seekToOffset(double seconds) override
	{
		if (!rewind() || seconds <= 0)
			return;

		/* Decode and drop everything before the requested frame */
		uint64_t skip = static_cast<uint64_t>(seconds * format.freq) * SDL_AUDIO_FRAMESIZE(format);
		std::vector<uint8_t> scratch;
		while (skip > 0)
		{
			if (!decode(scratch) || scratch.empty())
				return;
			if (scratch.size() > skip)
			{
				/* Keep the tail past the seek point for the next fill */
				next.assign(scratch.begin() + skip, scratch.end());
				nextReady = true;
				return;
			}
			skip -= scratch.size();
		}
	}

	uint32_t loopStartFrames() override
	{
		/* Loops from the beginning of the file */
		return 0;
	}
};

ALDataSource *createSDLSource(SDL_IOStream *ops,
                              const char *,
			                  uint32_t maxBufSize,
			                  bool looped)
{
	return new MixerSource(ops, maxBufSize, looped);
}
