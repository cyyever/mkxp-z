/*
** aldatasource.h
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

#ifndef ALDATASOURCE_H
#define ALDATASOURCE_H

#include "audio-util.h"

#include <cstdint>
#include <vector>

#include <SDL3/SDL_iostream.h>

struct ALDataSource
{
	enum Status
	{
		NoError,
		EndOfStream,
		WrapAround,
		Error
	};

	virtual ~ALDataSource() = default;

	/* Decode the next chunk of PCM data (in spec() format) into 'out',
	 * replacing its contents. On WrapAround the chunk ends exactly at
	 * the loop end. */
	virtual Status fillBuffer(std::vector<uint8_t> &out) = 0;

	virtual SDL_AudioSpec spec() = 0;

	/* If the source doesn't support seeking, it will
	 * reset back to the beginning */
	virtual void seekToOffset(double seconds) = 0;

	/* The frame count right after wrap around */
	virtual uint32_t loopStartFrames() = 0;
};

ALDataSource *createSDLSource(SDL_IOStream *ops,
                              const char *extension,
			                  uint32_t maxBufSize,
			                  bool looped);

#endif // ALDATASOURCE_H
