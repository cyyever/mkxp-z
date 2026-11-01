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

#include <thread>
#include <memory>
#include <string>
#include <vector>


struct AudioPrivate
{
    
    std::vector<std::unique_ptr<AudioStream>> bgmTracks;
	AudioStream bgs;
	AudioStream me;

	SoundEmitter se;

    
    float volumeRatio;

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
	      se(rtData.config),
          volumeRatio(1)
	{
        for (int i = 0; i < rtData.config.BGM.trackCount; i++) {
            bgmTracks.push_back(std::make_unique<AudioStream>(ALStream::Looped));
        }
        
		meWatch.state = MeNotPlaying;
		meWatch.thread = std::jthread([this](std::stop_token stop) { meWatchFun(stop); });
	}

	~AudioPrivate()
	{
		/* Assigning an empty jthread requests stop and joins */
		meWatch.thread = {};
	}
    
    AudioStream *getTrackByIndex(int index) {
        if (index < 0) index = 0;
        if (index > (int)(bgmTracks.size()) - 1) {
            throw Exception(Exception::MKXPError, "requested BGM track %d out of range (max: %d)", index, bgmTracks.size() - 1);
        }
        return bgmTracks[index].get();
    }

	void meWatchFun(std::stop_token stop)
	{
		const float fadeOutStep = 1.f / (std::chrono::milliseconds{200}  / AUDIO_SLEEP);
		const float fadeInStep  = 1.f / (std::chrono::milliseconds{1000} / AUDIO_SLEEP);

		while (true)
		{

			if (stop.stop_requested())
				return;

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
                
                for (int i = 0; i < (int)(bgmTracks.size()); i++) {
                    AudioStream *track = bgmTracks[i].get();
                    
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
                        std::vector<AudioStream*> playingTracks;
                        for (const auto &t : bgmTracks)
                            if (t->stream.queryState() == ALStream::Playing)
                                playingTracks.push_back(t.get());
                        
                        
                        if (playingTracks.size() <= 0 && !shouldBreak) shouldBreak = true;
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
                    /* ME has ended */
                    for (const auto &track : bgmTracks) {
                        auto trackLock = track->lockStream();
                        track->extPaused = false;
                        
                        ALStream::State sState = track->stream.queryState();
                        
                        if (sState == ALStream::Paused) {
                            /* BGM is paused. -> FadeInBGM */
                            track->stream.play();
                            meWatch.state = BgmFadingIn;
                        }
                        else {
                            /* BGM is stopped. -> MeNotPlaying */
                            track->setVolume(AudioStream::External, 1.0f);
                            
                            if (!track->noResumeStop)
                                track->stream.play();
                            
                            meWatch.state = MeNotPlaying;
                        }
                    }
				}

				break;
			}

			case BgmFadingIn :
			{
                std::vector<std::unique_lock<std::mutex>> trackLocks;
                trackLocks.reserve(bgmTracks.size());
                for (const auto &track : bgmTracks)
                    trackLocks.push_back(track->lockStream());

				if (bgmTracks[0]->stream.queryState() == ALStream::Stopped)
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

				float vol = bgmTracks[0]->getVolume(AudioStream::External);
				vol += fadeInStep;

				if (vol >= 1)
				{
					/* BGM fully faded in. -> MeNotPlaying */
					vol = 1.0f;
					meWatch.state = MeNotPlaying;
				}

                for (const auto &track : bgmTracks)
                    track->setVolume(AudioStream::External, vol);

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
