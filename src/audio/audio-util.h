/*
** audio-util.h
**
** This file is part of mkxp.
**
** Copyright (C) 2013 - 2021 Amaryllis Kulla <ancurio@mapleshrine.eu>
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

#ifndef AUDIOUTIL_H
#define AUDIOUTIL_H

#include <SDL3/SDL_audio.h>

#include <cfloat>
#include <chrono>
#include <cmath>
#include <memory>

/* Polling interval of the audio worker threads */
inline constexpr std::chrono::milliseconds AUDIO_SLEEP{10};
#define STREAM_BUF_SIZE 32768

/* The playback device all audio streams are bound to; opened in main() */
extern SDL_AudioDeviceID audioDevice;

struct AudioStreamDeleter
{
	void operator()(SDL_AudioStream *stream) const { SDL_DestroyAudioStream(stream); }
};
using AudioStreamPtr = std::unique_ptr<SDL_AudioStream, AudioStreamDeleter>;

/*
 * RPG Maker uses a -35 decibel scale for volume. 100% volume is 0 dB, 99%
 * volume is -0.35 dB, 98% volume is -0.7 dB, 97% volume is -1.05 dB and so
 * on. 0% volume is an exception - the scale is hardcoded to be silent at 0%
 * volume.
 *
 * This was deduced by running an RPG Maker XP game in Wine and attaching
 * winedbg to the game, with a breakpoint set in the
 * `IDirectSoundBuffer8::SetVolume` function in Wine's implementation of
 * dsound.dll:
 *
 * https://github.com/wine-mirror/wine/blob/1941a915368b8898da21d0dd4157fd68a4f4c9dd/dlls/dsound/buffer.c#L203
 */
inline float rgssVolumeToGain(float value)
{
	if (value <= FLT_EPSILON)
		return 0;

	return std::pow(10.0f, -(35.0f / 20.0f) * (1.0f - value));
}

#endif // AUDIOUTIL_H
