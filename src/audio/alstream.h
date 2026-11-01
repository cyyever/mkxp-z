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

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

struct ALDataSource;

/* State-machine like audio playback stream.
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

	std::unique_ptr<ALDataSource> source;
	AudioStreamPtr stream;

	/* Set by the streaming thread once the data source has
	 * no more data; the stream ends when the queue drains */
	std::atomic<bool> sourceExhausted{false};

	/* Playback position bookkeeping, shared with the streaming thread.
	 * Positions are in source frames pushed since startStream(). */
	std::mutex posMut;
	uint64_t startFrames = 0;
	uint64_t pushedFrames = 0;
	int frameSize = 0;
	int sampleRate = 0;
	/* (pushed frame at which the source wrapped, loop start frame) */
	std::deque<std::pair<uint64_t, uint32_t>> wraps;

	void openSource(const std::string &filename);

	void stopStream();
	void startStream(double offset);

	void checkStopped();

	/* thread func */
	void streamData(std::stop_token stop);

	/* Declared last so it is joined before the members it uses are destroyed */
	std::jthread thread;
};

#endif // ALSTREAM_H
