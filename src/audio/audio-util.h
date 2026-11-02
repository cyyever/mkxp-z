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
#include <SDL3_mixer/SDL_mixer.h>

#include <cfloat>
#include <chrono>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <string>

/* Polling interval of the audio worker threads */
inline constexpr std::chrono::milliseconds AUDIO_SLEEP{10};

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

/* The SDL_mixer mixer every track plays through; created in main() */
extern MIX_Mixer *audioMixer;

struct MixAudioDeleter
{
	void operator()(MIX_Audio *audio) const { MIX_DestroyAudio(audio); }
};
using MixAudioPtr = std::unique_ptr<MIX_Audio, MixAudioDeleter>;

struct MixTrackDeleter
{
	void operator()(MIX_Track *track) const { MIX_DestroyTrack(track); }
};
using MixTrackPtr = std::unique_ptr<MIX_Track, MixTrackDeleter>;

/* SDL_mixer decoder names for WAV, FLAC, MP3 and Ogg Vorbis. The vendored
 * build has only the header-only ones; a system SDL_mixer may use the libraries.
 * Inspect the selected decoder, never the filename hint. */
inline bool isRuntimeDecoder(const char *decoder)
{
	for (const char *runtime : {"WAV", "DRFLAC", "FLAC", "DRMP3", "MPG123", "STBVORBIS", "VORBIS"})
		if (SDL_strcmp(decoder, runtime) == 0)
			return true;
	return false;
}

/* Load 'ops' (always closed) for playback on audioMixer. Loop tags such as
 * Ogg LOOPSTART are ignored: the engine always plays or loops the whole file.
 * 'predecode' decodes everything now; otherwise the encoded file stays in
 * memory and is decoded while playing. */
inline MixAudioPtr loadRuntimeAudio(SDL_IOStream *ops, bool predecode, std::string &error)
{
	const SDL_PropertiesID props = SDL_CreateProperties();
	SDL_SetPointerProperty(props, MIX_PROP_AUDIO_LOAD_IOSTREAM_POINTER, ops);
	SDL_SetBooleanProperty(props, MIX_PROP_AUDIO_LOAD_CLOSEIO_BOOLEAN, true);
	SDL_SetBooleanProperty(props, MIX_PROP_AUDIO_LOAD_PREDECODE_BOOLEAN, predecode);
	SDL_SetBooleanProperty(props, MIX_PROP_AUDIO_LOAD_IGNORE_LOOPS_BOOLEAN, true);
	SDL_SetPointerProperty(props, MIX_PROP_AUDIO_LOAD_PREFERRED_MIXER_POINTER, audioMixer);
	MixAudioPtr audio(MIX_LoadAudioWithProperties(props));
	SDL_DestroyProperties(props);

	const char *name = audio ? SDL_GetStringProperty(MIX_GetAudioProperties(audio.get()),
	                                                 MIX_PROP_AUDIO_DECODER_STRING, "") : "";
	if (audio && !isRuntimeDecoder(name))
	{
		SDL_SetError("Audio assets must use WAV, FLAC, MP3 or Ogg Vorbis decoding, not %s", name);
		audio.reset();
	}
	if (!audio)
		error = SDL_GetError();
	return audio;
}

#endif // AUDIOUTIL_H
