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
#include "filesystem.h"
#include "exception.h"

#include <algorithm>
#include <format>

ALStream::ALStream(LoopMode loopMode)
	: looped(loopMode == Looped),
	  track(MIX_CreateTrack(audioMixer))
{
	if (!track)
		throw Exception(Exception::SDLError, "MIX_CreateTrack: %s", SDL_GetError());

	setVolume(1.0f);
}

ALStream::~ALStream() = default;

void ALStream::close()
{
	checkStopped();

	switch (state)
	{
	case Playing:
	case Paused:
		MIX_StopTrack(track.get(), 0);
	case Stopped:
		MIX_SetTrackAudio(track.get(), nullptr);
		audio.reset();
		state = Closed;
	case Closed:
		return;
	}
}

namespace {
struct ALStreamOpenHandler : FileSystem::OpenHandler
{
	MixAudioPtr audio;
	std::string errorMsg;

	bool tryRead(SDL_IOStream *ops, const char *) override
	{
		/* The encoded file stays in memory and is decoded while playing */
		audio = loadRuntimeAudio(ops, false, errorMsg);
		return bool(audio);
	}
};
} // namespace

void ALStream::open(const std::string &filename)
{
	ALStreamOpenHandler handler;
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

	SDL_AudioSpec spec;
	if (!handler.audio || !MIX_GetAudioFormat(handler.audio.get(), &spec))
		throw Exception(Exception::SDLError, std::format("Unable to decode audio stream: {}: {}", filename, handler.errorMsg));

	if (!MIX_SetTrackAudio(track.get(), handler.audio.get()))
		throw Exception(Exception::SDLError, "MIX_SetTrackAudio: %s", SDL_GetError());

	audio = std::move(handler.audio);
	sampleRate = spec.freq;
	startFrame = 0;
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
		MIX_StopTrack(track.get(), 0);
	}

	state = Stopped;
}

void ALStream::play(double offset)
{
	if (!audio)
		return;

	checkStopped();

	switch (state)
	{
	case Closed:
	case Playing:
		return;
	case Paused:
		/* Resumes where it was paused; the offset is ignored */
		MIX_ResumeTrack(track.get());
		state = Playing;
		return;
	case Stopped:
		break;
	}

	int64_t frame = offset > 0 ? static_cast<int64_t>(offset * sampleRate) : 0;
	const int64_t length = MIX_GetAudioDuration(audio.get());
	if (length > 0 && frame >= length)
	{
		/* Past the end: a looped stream wraps around, others have nothing left */
		if (!looped)
		{
			startFrame = length;
			return;
		}
		frame %= length;
	}

	const SDL_PropertiesID options = SDL_CreateProperties();
	SDL_SetNumberProperty(options, MIX_PROP_PLAY_LOOPS_NUMBER, looped ? -1 : 0);
	SDL_SetNumberProperty(options, MIX_PROP_PLAY_START_FRAME_NUMBER, frame);
	const bool started = MIX_PlayTrack(track.get(), options);
	SDL_DestroyProperties(options);

	if (!started)
		throw Exception(Exception::SDLError, "MIX_PlayTrack: %s", SDL_GetError());

	startFrame = frame;
	state = Playing;
}

void ALStream::pause()
{
	checkStopped();

	if (state != Playing)
		return;

	MIX_PauseTrack(track.get());
	state = Paused;
}

void ALStream::setVolume(float value)
{
	MIX_SetTrackGain(track.get(), rgssVolumeToGain(value));
}

void ALStream::setPitch(float value)
{
	MIX_SetTrackFrequencyRatio(track.get(), value);
}

ALStream::State ALStream::queryState()
{
	checkStopped();

	return state;
}

double ALStream::queryOffset()
{
	if (state == Closed || sampleRate == 0)
		return 0;

	/* A stopped stream reports where it was last started */
	const bool ended = state == Stopped
	    || (state == Playing && !MIX_TrackPlaying(track.get()) && !MIX_TrackPaused(track.get()));
	const int64_t frame = ended ? startFrame : MIX_GetTrackPlaybackPosition(track.get());

	return static_cast<double>(std::max<int64_t>(frame, 0)) / sampleRate;
}

void ALStream::checkStopped()
{
	/* This only concerns the scenario where state is still
	 * 'Playing', but the track has already ended on its own */
	if (state == Playing && !MIX_TrackPlaying(track.get()) && !MIX_TrackPaused(track.get()))
		state = Stopped;
}
