/*
** alstream.h
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

#ifndef ALSTREAM_H
#define ALSTREAM_H

#include "audio-util.h"

#include <cstdint>
#include <string>

/* State-machine like playback of one file on its own SDL_mixer track.
 * This class is NOT thread safe */
struct ALStream
{
	enum State
	{
		Closed,
		Stopped,
		Playing,
		Paused
	};

	enum LoopMode
	{
		Looped,
		NotLooped
	};

	ALStream(LoopMode loopMode);
	~ALStream();

	void close();
	void open(const std::string &filename);
	void stop();
	void play(double offset = 0);
	void pause();

	void setVolume(float value);
	void setPitch(float value);
	State queryState();
	double queryOffset();

private:
	bool looped;
	State state = Closed;

	MixAudioPtr audio;
	MixTrackPtr track;
	int sampleRate = 0;

	/* Frame the last play() started at */
	int64_t startFrame = 0;

	void checkStopped();
};

#endif // ALSTREAM_H
