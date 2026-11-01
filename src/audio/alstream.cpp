/*
** alstream.cpp
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

#include "alstream.h"

#include "sharedstate.h"
#include "eventthread.h"
#include "filesystem.h"
#include "exception.h"
#include "aldatasource.h"
#include "debugwriter.h"

#include <algorithm>
#include <format>
#include <thread>
#include <vector>

/* Keep roughly this many bytes of decoded data queued ahead of playback */
static constexpr int QUEUE_AHEAD_BYTES = 3 * STREAM_BUF_SIZE * 2;

ALStream::ALStream(LoopMode loopMode)
	: looped(loopMode == Looped),
	  stream(SDL_CreateAudioStream(nullptr, nullptr))
{
	if (!stream)
		throw Exception(Exception::SDLError, "SDL_CreateAudioStream: %s", SDL_GetError());

	setVolume(1.0f);
}

ALStream::~ALStream()
{
	close();

	/* The stream thread may have finished on its own without being joined */
	thread = {};

}

void ALStream::close()
{
	checkStopped();

	switch (state)
	{
	case Playing:
	case Paused:
		stopStream();
	case Stopped:
		source.reset();
		state = Closed;
	case Closed:
		return;
	}
}

void ALStream::open(const std::string &filename)
{
	openSource(filename);

	state = Stopped;
}

void ALStream::stop()
{
	checkStopped();

	switch (state)
	{
	case Closed:
	case Stopped:
		return;
	case Playing:
	case Paused:
		stopStream();
	}

	state = Stopped;
}

void ALStream::play(double offset)
{
	if (!source)
		return;

	checkStopped();

	switch (state)
	{
	case Closed:
	case Playing:
		return;
	case Stopped:
		startStream(offset);
		break;
	case Paused :
		/* Queued data stays in the stream while it is unbound */
		if (!SDL_BindAudioStream(audioDevice, stream.get()))
			throw Exception(Exception::SDLError, "SDL_BindAudioStream: %s", SDL_GetError());
	}

	state = Playing;
}

void ALStream::pause()
{
	checkStopped();

	switch (state)
	{
	case Closed:
	case Stopped:
	case Paused:
		return;
	case Playing:
		SDL_UnbindAudioStream(stream.get());
	}

	state = Paused;
}

void ALStream::setVolume(float value)
{
	SDL_SetAudioStreamGain(stream.get(), rgssVolumeToGain(value));
}

void ALStream::setPitch(float value)
{
	SDL_SetAudioStreamFrequencyRatio(stream.get(), value);
}

ALStream::State ALStream::queryState()
{
	checkStopped();

	return state;
}

double ALStream::queryOffset()
{
	if (state == Closed || !source)
		return 0;

	std::scoped_lock lock(posMut);

	if (sampleRate == 0 || frameSize == 0)
		return 0;

	/* Frames handed to SDL minus those still waiting in its input queue */
	const uint64_t queued = std::max(SDL_GetAudioStreamQueued(stream.get()), 0) / frameSize;
	const uint64_t played = pushedFrames - std::min(pushedFrames, queued);

	/* Forget wrap points that playback has moved past */
	while (wraps.size() > 1 && wraps[1].first <= played)
		wraps.pop_front();

	uint64_t frame = startFrames + played;

	if (!wraps.empty() && wraps.front().first <= played)
		frame = wraps.front().second + (played - wraps.front().first);

	return static_cast<double>(frame) / sampleRate;
}

struct ALStreamOpenHandler : FileSystem::OpenHandler
{
	bool looped;
	ALDataSource *source;
	std::string errorMsg;

	ALStreamOpenHandler(bool looped)
	    : looped(looped), source(0)
	{}

	bool tryRead(SDL_IOStream *ops, const char *ext)
	{
		try
		{
			source = createSDLSource(ops, ext, STREAM_BUF_SIZE, looped);
		}
		catch (const Exception &e)
		{
			/* All source constructors will close the passed ops
			 * before throwing errors */
			errorMsg = e.msg;
			return false;
		}

		return true;
	}
};

void ALStream::openSource(const std::string &filename)
{
	ALStreamOpenHandler handler(looped);
	try
	{
		shState->fileSystem().openRead(handler, filename.c_str());
	} catch (const Exception &e)
	{
		/* If no file was found then we leave the stream open.
		 * A PHYSFSError means we found a match but couldn't
		 * open the file, so we'll close it in that case. */
		if (e.type != Exception::NoFileError)
			close();
		
		throw;
	}

	close();

	if (!handler.source)
	{
        throw Exception(Exception::SDLError, std::format("Unable to decode audio stream: {}: {}", filename, handler.errorMsg));
	}
	
	source.reset(handler.source);
}

void ALStream::stopStream()
{
	if (thread.joinable())
	{
		thread.request_stop();
		thread.join();
	}

	/* Unbind and drop queued data _after_ the thread has terminated,
	 * because it might still have been pushing data */
	SDL_UnbindAudioStream(stream.get());
	SDL_ClearAudioStream(stream.get());

	std::scoped_lock lock(posMut);
	pushedFrames = 0;
	wraps.clear();
}

void ALStream::startStream(double offset)
{
	SDL_UnbindAudioStream(stream.get());
	SDL_ClearAudioStream(stream.get());

	const SDL_AudioSpec spec = source->spec();
	if (!SDL_SetAudioStreamFormat(stream.get(), &spec, nullptr))
		throw Exception(Exception::SDLError, "SDL_SetAudioStreamFormat: %s", SDL_GetError());

	{
		std::scoped_lock lock(posMut);
		sampleRate = spec.freq;
		frameSize = SDL_AUDIO_FRAMESIZE(spec);
		startFrames = static_cast<uint64_t>(offset * spec.freq);
		pushedFrames = 0;
		wraps.clear();
	}

	sourceExhausted = false;

	source->seekToOffset(offset);

	/* Binding before any data is queued is fine; SDL plays silence until data arrives */
	if (!SDL_BindAudioStream(audioDevice, stream.get()))
		throw Exception(Exception::SDLError, "SDL_BindAudioStream: %s", SDL_GetError());

	thread = std::jthread([this](std::stop_token stop) { streamData(stop); });
}

void ALStream::checkStopped()
{
	/* This only concerns the scenario where
	 * state is still 'Playing', but the stream
	 * has already ended on its own (EOF, Error) */
	if (state != Playing)
		return;

	/* If the source still has data, an empty queue
	 * is just a buffer underrun */
	if (!sourceExhausted)
		return;

	if (SDL_GetAudioStreamQueued(stream.get()) > 0 || SDL_GetAudioStreamAvailable(stream.get()) > 0)
		return;

	stopStream();
	state = Stopped;
}

/* thread func */
void ALStream::streamData(std::stop_token stop)
{
	std::vector<uint8_t> chunk;

	while (!stop.stop_requested())
	{
		shState->rtData().syncPoint.passSecondarySync();

		/* Top up the queue, then sleep until it drains a bit */
		while (!stop.stop_requested() && SDL_GetAudioStreamQueued(stream.get()) < QUEUE_AHEAD_BYTES)
		{
			const ALDataSource::Status status = source->fillBuffer(chunk);

			if (status == ALDataSource::Error || chunk.empty())
			{
				SDL_FlushAudioStream(stream.get());
				sourceExhausted = true;
				return;
			}

			{
				std::scoped_lock lock(posMut);
				if (!SDL_PutAudioStreamData(stream.get(), chunk.data(), static_cast<int>(chunk.size())))
				{
					sourceExhausted = true;
					return;
				}
				pushedFrames += chunk.size() / frameSize;

				/* The chunk ended at the loop end; playback position
				 * jumps back to the loop start once it gets there */
				if (status == ALDataSource::WrapAround)
					wraps.emplace_back(pushedFrames, source->loopStartFrames());
			}

			if (status == ALDataSource::EndOfStream)
			{
				/* Let the resampler emit its tail */
				SDL_FlushAudioStream(stream.get());
				sourceExhausted = true;
				return;
			}
		}

		std::this_thread::sleep_for(AUDIO_SLEEP);
	}
}
