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

#include <algorithm>
#include <format>

#define SE_CACHE_MEM (10*1024*1024) // 10 MB

struct SoundBuffer
{
	/* Uniquely identifies this or equal buffer */
	std::string key;

	/* Fully decoded audio; tracks still playing it hold their own reference */
	MixAudioPtr audio;
	size_t bytes = 0;
};

SoundEmitter::SoundEmitter(const Config &conf)
    : bufferBytes(0),
      srcCount(conf.SE.sourceCount),
      sources(srcCount),
      sourceKeys(srcCount),
      srcPrio(srcCount)
{
	for (size_t i = 0; i < srcCount; ++i)
	{
		sources[i].reset(MIX_CreateTrack(audioMixer));
		if (!sources[i])
			throw Exception(Exception::SDLError, "MIX_CreateTrack: %s", SDL_GetError());
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

	/* Try to find first free source */
	size_t i;
	for (i = 0; i < srcCount; ++i)
		if (!MIX_TrackPlaying(sources[srcPrio[i]].get()))
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
	std::rotate(srcPrio.begin() + i, srcPrio.begin() + i + 1, srcPrio.end());

	MIX_Track *src = sources[srcIndex].get();
	MIX_StopTrack(src, 0);

	sourceKeys[srcIndex] = filename;

	if (!MIX_SetTrackAudio(src, buffer->audio.get()))
		throw Exception(Exception::SDLError, "MIX_SetTrackAudio: %s", SDL_GetError());
	MIX_SetTrackGain(src, rgssVolumeToGain(_volume));
	MIX_SetTrackFrequencyRatio(src, _pitch);
	if (!MIX_PlayTrack(src, 0))
		throw Exception(Exception::SDLError, "MIX_PlayTrack: %s", SDL_GetError());
}

void SoundEmitter::stop()
{
	for (size_t i = 0; i < srcCount; i++)
		MIX_StopTrack(sources[i].get(), 0);
}

namespace {
struct SoundOpenHandler : FileSystem::OpenHandler
{
	std::unique_ptr<SoundBuffer> buffer;
	std::string error;

	bool tryRead(SDL_IOStream *ops, const char *) override
	{
		/* Decode everything now so we don't have to keep the source ops around */
		MixAudioPtr audio = loadRuntimeAudio(ops, true, error);
		if (!audio)
			return false;

		SDL_AudioSpec spec;
		const Sint64 frames = MIX_GetAudioDuration(audio.get());
		if (!MIX_GetAudioFormat(audio.get(), &spec) || frames < 0) {
			error = SDL_GetError();
			return false;
		}

		buffer = std::make_unique<SoundBuffer>();
		buffer->bytes = static_cast<size_t>(frames) * SDL_AUDIO_FRAMESIZE(spec);
		buffer->audio = std::move(audio);

		return true;
	}
};
} // namespace

SoundBuffer *SoundEmitter::allocateBuffer(const std::string &filename)
{
	const auto found = bufferHash.find(filename);
	SoundBuffer *buffer = found == bufferHash.end() ? nullptr : found->second->get();

	if (buffer)
	{
		/* Buffer still in cache.
		 * Move to the back of the priority list */
		buffers.splice(buffers.end(), buffers, found->second);

		return buffer;
	}
	
	
		/* Buffer not in cache, needs to be loaded */
		SoundOpenHandler handler;
		shState->fileSystem().openRead(handler, filename.c_str());
		buffer = handler.buffer.get();

		if (!buffer)
		{
            throw Exception(Exception::SDLError,
                            std::format("Unable to decode sound: {}: {}", filename, handler.error));
		}

		buffer->key = filename;
		size_t wouldBeBytes = bufferBytes + buffer->bytes;

		/* If memory limit is reached, delete lowest priority buffer
		 * until there is room or no buffers left */
		while (wouldBeBytes > SE_CACHE_MEM && !buffers.empty())
		{
			wouldBeBytes -= buffers.back()->bytes;
			bufferHash.erase(buffers.back()->key);
			buffers.pop_back();
		}

		buffers.push_front(std::move(handler.buffer));
		bufferHash.emplace(filename, buffers.begin());

		bufferBytes = wouldBeBytes;

		return buffers.front().get();
	
}
