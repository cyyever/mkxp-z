/*
** soundemitter.cpp
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

#include "soundemitter.h"

#include "sharedstate.h"
#include "filesystem.h"
#include "exception.h"
#include "config.h"
#include "util.h"
#include "debugwriter.h"

#include <SDL3_sound/SDL_sound.h>

#define SE_CACHE_MEM (10*1024*1024) // 10 MB

struct SoundBuffer
{
	/* Uniquely identifies this or equal buffer */
	std::string key;

	/* Fully decoded PCM data and its format */
	std::vector<uint8_t> pcm;
	SDL_AudioSpec spec;

	/* Link into the buffer cache priority list */
	IntruListLink<SoundBuffer> link;

	SoundBuffer() : link(this) {}

};

/* Before: [a][b][c][d], After (index=1): [a][c][d][b] */
static void
arrayPushBack(std::vector<size_t> &array, size_t size, size_t index)
{
	size_t v = array[index];

	for (size_t t = index; t < size-1; ++t)
		array[t] = array[t+1];

	array[size-1] = v;
}

static bool isPlaying(SDL_AudioStream *src)
{
	return SDL_GetAudioStreamDevice(src) != 0
	    && (SDL_GetAudioStreamQueued(src) > 0 || SDL_GetAudioStreamAvailable(src) > 0);
}

SoundEmitter::SoundEmitter(const Config &conf)
    : bufferBytes(0),
      srcCount(conf.SE.sourceCount),
      sources(srcCount),
      sourceKeys(srcCount),
      srcPrio(srcCount)
{
	for (size_t i = 0; i < srcCount; ++i)
	{
		/* The input format is set per sound in play() */
		sources[i].reset(SDL_CreateAudioStream(nullptr, nullptr));
		if (!sources[i])
			throw Exception(Exception::SDLError, "SDL_CreateAudioStream: %s", SDL_GetError());
		srcPrio[i] = i;
	}
}

SoundEmitter::~SoundEmitter() = default;

void SoundEmitter::play(const std::string &filename,
                        int volume,
                        int pitch)
{
	float _volume = clamp<int>(volume, 0, 100) / 100.0f;
	float _pitch  = clamp<int>(pitch, 50, 150) / 100.0f;

	SoundBuffer *buffer = allocateBuffer(filename);

	if (!buffer)
		return;

	/* Try to find first free source */
	size_t i;
	for (i = 0; i < srcCount; ++i)
		if (!isPlaying(sources[srcPrio[i]].get()))
			break;

	/* If we didn't find any, try to find the lowest priority source
	 * with the same buffer to overtake */
	if (i == srcCount)
		for (size_t j = 0; j < srcCount; ++j)
			if (sourceKeys[srcPrio[j]] == filename)
				i = j;

	/* If we didn't find any, overtake the one with lowest priority */
	if (i == srcCount)
		i = 0;

	size_t srcIndex = srcPrio[i];

	/* Push the used source to the back of the priority list */
	arrayPushBack(srcPrio, srcCount, i);

	SDL_AudioStream *src = sources[srcIndex].get();
	SDL_UnbindAudioStream(src);
	SDL_ClearAudioStream(src);

	sourceKeys[srcIndex] = filename;

	if (!SDL_SetAudioStreamFormat(src, &buffer->spec, nullptr))
		throw Exception(Exception::SDLError, "SDL_SetAudioStreamFormat: %s", SDL_GetError());
	SDL_SetAudioStreamGain(src, rgssVolumeToGain(_volume));
	SDL_SetAudioStreamFrequencyRatio(src, _pitch);
	if (!SDL_PutAudioStreamData(src, buffer->pcm.data(), static_cast<int>(buffer->pcm.size())))
		throw Exception(Exception::SDLError, "SDL_PutAudioStreamData: %s", SDL_GetError());
	/* No more data follows; let the resampler emit its tail */
	SDL_FlushAudioStream(src);
	if (!SDL_BindAudioStream(audioDevice, src))
		throw Exception(Exception::SDLError, "SDL_BindAudioStream: %s", SDL_GetError());
}

void SoundEmitter::stop()
{
	for (size_t i = 0; i < srcCount; i++)
	{
		SDL_UnbindAudioStream(sources[i].get());
		SDL_ClearAudioStream(sources[i].get());
	}
}

struct SoundOpenHandler : FileSystem::OpenHandler
{
	std::unique_ptr<SoundBuffer> buffer;

	bool tryRead(SDL_IOStream *ops, const char *ext)
	{
		std::unique_ptr<Sound_Sample, decltype(&Sound_FreeSample)> sample(
			Sound_NewSample(ops, ext, nullptr, STREAM_BUF_SIZE), Sound_FreeSample);

		if (!sample)
		{
			return false;
		}

		/* Do all of the decoding in the handler so we don't have
		 * to keep the source ops around */
		uint32_t decBytes = Sound_DecodeAll(sample.get());
		const auto *data = static_cast<const uint8_t *>(sample->buffer);

		if (sample->flags & SOUND_SAMPLEFLAG_ERROR)
			return false;

		buffer = std::make_unique<SoundBuffer>();
		buffer->spec = sample->actual;
		buffer->pcm.assign(data, data + decBytes);


		return true;
	}
};

SoundBuffer *SoundEmitter::allocateBuffer(const std::string &filename)
{
	const auto found = bufferHash.find(filename);
	SoundBuffer *buffer = found == bufferHash.end() ? nullptr : found->second.get();

	if (buffer)
	{
		/* Buffer still in cashe.
		 * Move to front of priority list */
		buffers.remove(buffer->link);
		buffers.append(buffer->link);

		return buffer;
	}
	else
	{
		/* Buffer not in cache, needs to be loaded */
		SoundOpenHandler handler;
		shState->fileSystem().openRead(handler, filename.c_str());
		buffer = handler.buffer.get();

		if (!buffer)
		{
			char buf[512];
			snprintf(buf, sizeof(buf), "Unable to decode sound: %s: %s",
			         filename.c_str(), Sound_GetError());
			Debug() << buf;

			return 0;
		}

		buffer->key = filename;
		size_t wouldBeBytes = bufferBytes + buffer->pcm.size();

		/* If memory limit is reached, delete lowest priority buffer
		 * until there is room or no buffers left */
		while (wouldBeBytes > SE_CACHE_MEM && !buffers.isEmpty())
		{
			SoundBuffer *last = buffers.tail();
			buffers.remove(last->link);

			wouldBeBytes -= last->pcm.size();

			bufferHash.erase(last->key);
		}

		bufferHash.insert_or_assign(filename, std::move(handler.buffer));
		buffers.prepend(buffer->link);

		bufferBytes = wouldBeBytes;

		return buffer;
	}
}
