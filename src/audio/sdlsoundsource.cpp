/*
** sdlsoundsource.cpp
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
#include "sdl-util.h"

#include <SDL3_sound/SDL_sound.h>

struct SDLSoundSource : ALDataSource
{
	Sound_Sample *sample;
	SDL_IOStream *srcOps = nullptr;
	bool looped;

	SDLSoundSource(SDL_IOStream *ops,
	               const char *extension,
	               uint32_t maxBufSize,
	               bool looped)
	    : srcOps(ops),
	      looped(looped)
	{
		/* SDL converts whatever format the decoder produces */
		sample = Sound_NewSample(borrowIO(srcOps), extension, nullptr, maxBufSize);

		if (!sample)
		{
			SDL_CloseIO(srcOps);
			throw Exception(Exception::SDLError, "SDL_sound: %s", Sound_GetError());
		}
	}

	~SDLSoundSource()
	{
		Sound_FreeSample(sample);
		SDL_CloseIO(srcOps);
	}

	Status fillBuffer(std::vector<uint8_t> &out) override
	{
		uint32_t decoded = Sound_Decode(sample);

		if (sample->flags & SOUND_SAMPLEFLAG_EAGAIN)
		{
			/* Try to decode one more time on EAGAIN */
			decoded = Sound_Decode(sample);

			/* Give up */
			if (sample->flags & SOUND_SAMPLEFLAG_EAGAIN)
				return ALDataSource::Error;
		}

		if (sample->flags & SOUND_SAMPLEFLAG_ERROR)
			return ALDataSource::Error;

		const auto *data = static_cast<const uint8_t *>(sample->buffer);
		out.assign(data, data + decoded);

		if (sample->flags & SOUND_SAMPLEFLAG_EOF)
		{
			if (!looped)
				return ALDataSource::EndOfStream;

			if (!Sound_Rewind(sample))
				return ALDataSource::Error;
			return ALDataSource::WrapAround;
		}

		return ALDataSource::NoError;
	}

	SDL_AudioSpec spec() override
	{
		return sample->actual;
	}

	void seekToOffset(double seconds) override
	{
		if (seconds <= 0)
		{
			Sound_Rewind(sample);
		}
		else
		{
			// Unfortunately there is no easy API in SDL_sound for seeking with better precision than 1ms.
			// TODO: Work around this by flooring instead of rounding, and then manually consuming the remaining samples.
			Sound_Seek(sample, static_cast<uint32_t>(lround(seconds * 1000)));
		}
	}

	uint32_t loopStartFrames() override
	{
		/* Loops from the beginning of the file */
		return 0;
	}
};

ALDataSource *createSDLSource(SDL_IOStream *ops,
                              const char *extension,
			                  uint32_t maxBufSize,
			                  bool looped)
{
	return new SDLSoundSource(ops, extension, maxBufSize, looped);
}
