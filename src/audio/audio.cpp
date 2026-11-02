/*
** audio.cpp
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

#include "audio.h"

#include "audiostream.h"
#include "soundemitter.h"
#include "sharedstate.h"
#include "eventthread.h"
#include "sdl-util.h"
#include "exception.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <memory>
#include <string>
#include <utility>
#include <vector>


struct AudioPrivate
{
    
    std::vector<std::unique_ptr<AudioStream>> bgmTracks;
	AudioStream bgs;
	AudioStream me;

	SoundEmitter se;

	/* The 'MeWatch' is responsible for detecting
	 * a playing ME, quickly fading out the BGM and
	 * keeping it paused/stopped while the ME plays,
	 * and unpausing/fading the BGM back in again
	 * afterwards */
	enum MeWatchState
	{
		MeNotPlaying,
		BgmFadingOut,
		MePlaying,
		BgmFadingIn
	};

	struct
	{
		std::jthread thread;
		MeWatchState state;
	} meWatch;

	AudioPrivate(RGSSThreadData &rtData)
	    : bgs(ALStream::Looped),
	      me(ALStream::NotLooped),
	      se(rtData.config)
	{
        for (int i = 0; i < rtData.config.BGM.trackCount; i++) {
            bgmTracks.push_back(std::make_unique<AudioStream>(ALStream::Looped));
        }
        
		meWatch.state = MeNotPlaying;
		meWatch.thread = std::jthread([this](std::stop_token stop) { meWatchFun(std::move(stop)); });
	}

	~AudioPrivate()
	{
		/* Assigning an empty jthread requests stop and joins */
		meWatch.thread = {};
	}
    
    AudioStream *getTrackByIndex(int index) {
        index = std::max(index, 0);
        if (index > (int)(bgmTracks.size()) - 1) {
            throw Exception(Exception::MKXPError, "requested BGM track %d out of range (max: %d)", index, bgmTracks.size() - 1);
        }
        return bgmTracks[index].get();
    }

	void meWatchFun(const std::stop_token& stop)
	{
		/* Fade by the time that actually passed: sleep_for may oversleep
		 * (by ~6 ms per 10 ms on default Windows timers), and fixed per-tick
		 * steps would stretch the 200 ms fade-out and 1 s fade-in by that much */
		using clock = std::chrono::steady_clock;
		using fsec = std::chrono::duration<float>;
		auto lastTick = clock::now();

		while (true)
		{

			if (stop.stop_requested())
				return;

			const auto now = clock::now();
			const float elapsed = fsec(now - lastTick).count();
			lastTick = now;
			const float fadeOutStep = elapsed / 0.2f;
			const float fadeInStep  = elapsed / 1.0f;

			switch (meWatch.state)
			{
			case MeNotPlaying:
			{
				auto meLock = me.lockStream();

				if (me.stream.queryState() == ALStream::Playing)
				{
					/* ME playing detected. -> FadeOutBGM */
                    for (const auto &track : bgmTracks)
                        track->extPaused = true;
                    
					meWatch.state = BgmFadingOut;
				}

				break;
			}

			case BgmFadingOut :
			{
				auto meLock = me.lockStream();

				if (me.stream.queryState() != ALStream::Playing)
				{
					/* ME has ended while fading OUT BGM. -> FadeInBGM */
					meWatch.state = BgmFadingIn;

					break;
				}
                
                bool shouldBreak = false;
                
                for (const auto & bgmTrack : bgmTracks) {
                    AudioStream *track = bgmTrack.get();
                    
                    auto trackLock = track->lockStream();
                    
                    float vol = track->getVolume(AudioStream::External);
                    vol -= fadeOutStep;
                    
                    if (vol < 0 || track->stream.queryState() != ALStream::Playing) {
                        /* Either BGM has fully faded out, or stopped midway. -> MePlaying */
                        track->setVolume(AudioStream::External, 0);
                        track->stream.pause();
                        trackLock.unlock();
                        
                        // check to see if there are any tracks still playing,
                        // and if the last one was ended this round, this branch should exit
                        shouldBreak = shouldBreak || std::ranges::none_of(bgmTracks, [](const auto &t) {
                            return t->stream.queryState() == ALStream::Playing;
                        });
                        continue;
                    }
                    
                    track->setVolume(AudioStream::External, vol);
                }
                if (shouldBreak)
                    meWatch.state = MePlaying;

				break;
			}

			case MePlaying :
			{
				auto meLock = me.lockStream();

				if (me.stream.queryState() != ALStream::Playing)
                {
                    /* ME has ended. Decide for all tracks together: one
                     * stopped or unused track must not cancel the fade-in
                     * of the paused ones */
                    bool resumed = false;

                    for (const auto &track : bgmTracks) {
                        auto trackLock = track->lockStream();
                        track->extPaused = false;
                        
                        ALStream::State sState = track->stream.queryState();
                        
                        if (sState == ALStream::Paused) {
                            /* BGM is paused. -> FadeInBGM */
                            track->stream.play();
                            resumed = true;
                        }
                        else {
                            /* BGM is stopped: no fade-in for this track */
                            track->setVolume(AudioStream::External, 1.0f);
                            
                            if (!track->noResumeStop)
                                track->stream.play();
                        }
                    }

                    meWatch.state = resumed ? BgmFadingIn : MeNotPlaying;
				}

				break;
			}

			case BgmFadingIn :
			{
                std::vector<std::unique_lock<std::mutex>> trackLocks;
                trackLocks.reserve(bgmTracks.size());
                for (const auto &track : bgmTracks)
                    trackLocks.push_back(track->lockStream());

				if (std::ranges::none_of(bgmTracks, [](const auto &track) {
                    return track->stream.queryState() == ALStream::Playing;
                }))
				{
					/* BGM stopped midway fade in. -> MeNotPlaying */
                    for (const auto &track : bgmTracks)
                        track->setVolume(AudioStream::External, 1.0f);
					meWatch.state = MeNotPlaying;

					break;
				}

				auto meLock = me.lockStream();

				if (me.stream.queryState() == ALStream::Playing)
				{
					/* ME started playing midway BGM fade in. -> FadeOutBGM */
                    for (const auto &track : bgmTracks)
                        track->extPaused = true;
					meWatch.state = BgmFadingOut;

					break;
				}

				/* Each playing track fades in from its own level; a track
				 * that is not playing has nothing to fade */
				bool fading = false;
                for (const auto &track : bgmTracks) {
                    float vol = 1.0f;
                    if (track->stream.queryState() == ALStream::Playing)
                        vol = track->getVolume(AudioStream::External) + fadeInStep;

                    if (vol < 1)
                        fading = true;
                    else
                        vol = 1.0f;

                    track->setVolume(AudioStream::External, vol);
                }

				if (!fading)
				{
					/* BGM fully faded in. -> MeNotPlaying */
					meWatch.state = MeNotPlaying;
				}

				break;
			}
			}

			std::this_thread::sleep_for(AUDIO_SLEEP);
		}
	}
};

Audio::Audio(RGSSThreadData &rtData)
	: p(new AudioPrivate(rtData))
{}


void Audio::bgmPlay(const char *filename,
                    int volume,
                    int pitch,
                    double pos,
                    int track)
{
    if (track == -127) {
        for (int i = 0; i < (int)p->bgmTracks.size(); i++) {
            if (i == 0) {
                continue;
            }
            p->bgmTracks[i]->stop();
        }
        
        track = 0;
    }
	p->getTrackByIndex(track)->play(filename, volume, pitch, pos);
}

void Audio::bgmStop(int track)
{
    if (track == -127) {
        for (const auto &track : p->bgmTracks)
            track->stop();
        
        return;
    }
    
    p->getTrackByIndex(track)->stop();
}

void Audio::bgmFade(int time, int track)
{
    if (track == -127) {
        for (const auto &track : p->bgmTracks)
            track->fadeOut(time);
        
        return;
    }
    
    p->getTrackByIndex(track)->fadeOut(time);
}

int Audio::bgmGetVolume(int track)
{
    if (track == -127)
        return p->bgmTracks[0]->getVolume(AudioStream::BaseRatio) * 100;
    
    return p->getTrackByIndex(track)->getVolume(AudioStream::Base) * 100;
}

void Audio::bgmSetVolume(int volume, int track)
{
    float vol = volume / 100.0;
    if (track == -127) {
        for (const auto &track : p->bgmTracks)
            track->setVolume(AudioStream::BaseRatio, vol);
        
        return;
    }
    p->getTrackByIndex(track)->setVolume(AudioStream::Base, vol);
}


void Audio::bgsPlay(const char *filename,
                    int volume,
                    int pitch,
                    double pos)
{
	p->bgs.play(filename, volume, pitch, pos);
}

void Audio::bgsStop()
{
	p->bgs.stop();
}

void Audio::bgsFade(int time)
{
	p->bgs.fadeOut(time);
}


void Audio::mePlay(const char *filename,
                   int volume,
                   int pitch)
{
	p->me.play(filename, volume, pitch);
}

void Audio::meStop()
{
	p->me.stop();
}

void Audio::meFade(int time)
{
	p->me.fadeOut(time);
}


void Audio::sePlay(const char *filename,
                   int volume,
                   int pitch)
{
	p->se.play(filename, volume, pitch);
}

void Audio::seStop()
{
	p->se.stop();
}

double Audio::bgmPos(int track)
{
	return p->getTrackByIndex(track)->playingOffset();
}

double Audio::bgsPos()
{
	return p->bgs.playingOffset();
}

void Audio::reset()
{
    for (const auto &track : p->bgmTracks) {
    	track->stop();
    }

	p->bgs.stop();
	p->me.stop();
	p->se.stop();
}

Audio::~Audio() { delete p; }
