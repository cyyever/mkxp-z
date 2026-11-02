// RGSS Audio semantics through the production Audio class (BGM/BGS/ME/SE,
// fades and the ME watch thread) on an offline SDL3_mixer mixer. The engine's
// fade and ME threads run on wall-clock time, so the test renders the mix in
// real time: each step mixes N ms of audio and then sleeps N ms.
#include "audio.h"
#include "audio-util.h"
#include "eventthread.h"
#include "exception.h"
#include "sharedstate.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

MIX_Mixer *audioMixer;
AudioTestState testState;
AudioTestState *shState = &testState;
static std::string fixtures;
static constexpr int RATE = 48000;

void FileSystem::openRead(OpenHandler &handler, const char *filename) {
    for (const char *ext : {"", ".wav"}) {
        const std::string path = fixtures + "/" + filename + ext;
        if (SDL_IOStream *io = SDL_IOFromFile(path.c_str(), "rb")) {
            handler.tryRead(io, ext);
            return;
        }
    }
    throw Exception(Exception::NoFileError, "%s", filename);
}

// Audio's constructor is private to SharedStatePrivate, as in the engine.
struct SharedStatePrivate {
    Audio audio;
    explicit SharedStatePrivate(RGSSThreadData &data) : audio(data) {}
};

static void require(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}

// Mix 'ms' of audio, let the engine threads see the same time pass, return RMS.
// Sleep until an absolute deadline on the mixed-audio clock instead of a fixed
// sleep_for: per-call oversleep (large on loaded CI runners) would otherwise
// accumulate, making the engine's wall-clock fades run ahead of the mixed audio
// and stretching the test's run time without bound.
static double advance(int ms) {
    static auto audioClock = std::chrono::steady_clock::now();
    std::vector<float> pcm(size_t(RATE) * ms / 1000 * 2);
    require(MIX_Generate(audioMixer, pcm.data(), int(pcm.size() * sizeof(float))) >= 0, SDL_GetError());
    audioClock += std::chrono::milliseconds(ms);
    const auto now = std::chrono::steady_clock::now();
    if (now > audioClock + std::chrono::milliseconds(250)) audioClock = now; // fell far behind: resync
    std::this_thread::sleep_until(audioClock);
    double energy = 0;
    for (float sample : pcm) energy += double(sample) * sample;
    return std::sqrt(energy / pcm.size());
}

// Whether BGM track 0 moves forward (positions wrap at the 1 s loop point).
static bool bgmAdvancing(Audio &audio) {
    const double before = audio.bgmPos();
    advance(60);
    const double after = audio.bgmPos();
    return after != before;
}

static double advanceFor(int ms) {
    double last = 0;
    for (int t = 0; t < ms; t += 20) last = advance(20);
    return last;
}

int main(int argc, char **argv) {
    try {
        require(argc == 2, "usage: audio-semantics-tests FIXTURES");
        fixtures = argv[1];
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        require(MIX_Init(), SDL_GetError());
        const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, RATE};
        audioMixer = MIX_CreateMixer(&spec);
        require(audioMixer, SDL_GetError());

        RGSSThreadData data;
        data.config.SE.sourceCount = 4;
        // One BGM track, the default; several tracks are tested below.
        data.config.BGM.trackCount = 1;
        auto state = std::make_unique<SharedStatePrivate>(data);
        Audio &audio = state->audio;

        // Steady level of the 1 s, 440 Hz fixtures at volume 100.
        audio.bgmPlay("tone.ogg", 100, 100);
        const double full = advanceFor(200);
        require(full > .05, "BGM is silent");

        // Replaying the same BGM with another volume updates it in place.
        const double before = audio.bgmPos();
        audio.bgmPlay("tone.ogg", 50, 100);
        const double quieter = advance(40);
        require(audio.bgmPos() > before, "volume change restarted the BGM");
        std::printf("BGM volume 50 / 100: %.4f\n", quieter / full);
        require(std::abs(quieter / full - rgssVolumeToGain(.5f)) < .01, "in-place volume");
        require(audio.bgmGetVolume() == 100, "bgm_volume ratio");

        // ... and the same call with nothing changed does nothing.
        const double same = audio.bgmPos();
        audio.bgmPlay("tone.ogg", 50, 100);
        require(audio.bgmPos() >= same, "identical bgm_play restarted");

        // A different pitch restarts it, playing faster.
        audio.bgmPlay("tone.ogg", 100, 150);
        advance(100);
        const double pitched = audio.bgmPos();
        std::printf("BGM pos after 100 ms at pitch 150: %.3f\n", pitched);
        require(pitched > .13 && pitched < .17, "pitch change did not restart at 1.5x speed");

        // bgm_play with a position starts there and fades in over one second.
        audio.bgmPlay("tone.mp3", 100, 100, .5);
        const double start = audio.bgmPos();
        const double fadingIn = advance(100);
        const double fadedIn = advanceFor(1100);
        std::printf("BGM start pos %.3f, level after 0.1 s %.3f, after 1.2 s %.3f\n", start, fadingIn, fadedIn);
        require(std::abs(start - .5) < .01, "bgm_play position");
        require(fadingIn < fadedIn * .6 && std::abs(fadedIn - full) < .01, "position fade-in");

        // BGM loops (also the MP3).
        require(advanceFor(1000) > .05 && audio.bgmPos() < 1, "MP3 BGM did not loop");

        // bgm_fade fades out and stops; bgm_play afterwards starts again.
        audio.bgmFade(300);
        const double fading = advance(100);
        advanceFor(300);
        require(fading > 0 && advance(40) == 0, "bgm_fade did not stop the BGM");
        audio.bgmPlay("tone.ogg", 100, 100);
        require(advanceFor(100) > .05, "BGM did not restart after a fade");

        // An ME fades the BGM out within 200 ms and pauses it while playing.
        audio.mePlay("tone.flac", 100, 100);
        advanceFor(300);
        const double paused = audio.bgmPos();
        advanceFor(200);
        require(audio.bgmPos() == paused, "BGM kept playing during the ME");
        // When the ME ends the BGM resumes where it was and fades in over 1 s.
        advanceFor(600);
        require(bgmAdvancing(audio), "BGM did not resume after the ME");
        const double resumedLevel = advance(100);
        const double resumedFull = advanceFor(1100);
        std::printf("BGM after ME: level %.3f, then %.3f\n", resumedLevel, resumedFull);
        require(resumedLevel < resumedFull * .8 && std::abs(resumedFull - full) < .01, "BGM fade-in after the ME");

        // A BGM stopped during an ME stays stopped after it.
        audio.mePlay("tone.flac", 100, 100);
        advanceFor(300);
        audio.bgmStop();
        advanceFor(1000);
        require(advanceFor(200) == 0, "stopped BGM came back after the ME");

        // A BGM started during an ME waits for the ME to end.
        audio.mePlay("tone.flac", 100, 100);
        advanceFor(100);
        audio.bgmPlay("tone.ogg", 100, 100);
        advanceFor(300);
        require(audio.bgmPos() == 0, "BGM started during the ME");
        advanceFor(2000);
        require(audio.bgmPos() > 0, "BGM started during the ME never played");

        // me_fade ends the ME early and the BGM resumes.
        audio.mePlay("tone.flac", 100, 100);
        advanceFor(500);
        require(!bgmAdvancing(audio), "BGM kept playing during the ME");
        audio.meFade(100);
        advanceFor(1500);
        require(bgmAdvancing(audio), "BGM did not resume after me_fade");
        audio.meStop();

        // BGS is independent of ME and BGM.
        audio.bgmStop();
        audio.bgsPlay("tone.mp3", 100, 100, .25);
        require(std::abs(audio.bgsPos() - .25) < .01, "bgs position");
        advanceFor(1200);
        require(audio.bgsPos() > .25 && audio.bgsPos() < 1, "BGS did not loop");
        audio.bgsFade(200);
        advanceFor(300);
        require(advance(40) == 0, "bgs_fade");
        audio.bgsPlay("tone.ogg", 100, 100);
        advance(40);
        audio.bgsStop();
        require(advance(40) == 0, "bgs_stop");

        // SE goes through the same mixer; __reset__ stops everything.
        audio.bgmPlay("tone.ogg");
        audio.bgsPlay("tone.ogg");
        audio.mePlay("tone.flac");
        audio.sePlay("tone-alaw");
        advance(100);
        audio.reset();
        advance(40);
        require(advance(40) == 0, "__reset__ left audio playing");

        // Errors keep their types.
        bool rejected = false;
        try { audio.bgmPlay("missing"); } catch (const Exception &e) { rejected = e.type == Exception::NoFileError; }
        require(rejected, "missing BGM");
        rejected = false;
        try { audio.sePlay("../meson.build"); } catch (const Exception &e) { rejected = e.type == Exception::SDLError; }
        require(rejected, "undecodable SE");

        // Several BGM tracks (BGM.trackCount), addressed by index; no index means all tracks.
        RGSSThreadData multiData;
        multiData.config.BGM.trackCount = 2;
        auto multiState = std::make_unique<SharedStatePrivate>(multiData);
        Audio &multi = multiState->audio;
        multi.bgmPlay("tone.ogg", 100, 100, 0, 1);
        require(advance(40) > .05 && multi.bgmPos(1) > 0 && multi.bgmPos(0) == 0, "second BGM track");
        multi.bgmSetVolume(50, 1);
        require(multi.bgmGetVolume(1) == 50, "per-track bgm volume");
        multi.bgmStop();
        require(advance(40) == 0, "bgm_stop did not stop every track");
        rejected = false;
        try { multi.bgmPlay("tone.ogg", 100, 100, 0, 2); } catch (const Exception &) { rejected = true; }
        require(rejected, "BGM track beyond BGM.trackCount accepted");

        // An ME pauses every playing BGM track; when it ends, each one fades back
        // in to its prior volume, like the single-track case, whichever tracks
        // are unused or stopped.
        // Returns the level once the fade-in has finished.
        auto meCycle = [&](const char *what, double prior) {
            multi.mePlay("tone.flac", 100, 100);
            advanceFor(1060);
            const double early = advance(100);
            const double after = advanceFor(1700);
            std::printf("%s: level %.4f before the ME, %.4f fading in, %.4f after\n", what, prior, early, after);
            require(early < after * .8, std::string(what) + ": no fade-in after the ME");
            return after;
        };
        multi.bgmPlay("tone.ogg", 100, 100, 0, 0);
        const double trackZeroLevel = advanceFor(200);
        require(std::abs(meCycle("track 0 of 2", trackZeroLevel) - trackZeroLevel) < .005,
                "track 0 of 2: not back at its prior volume");
        multi.bgmStop();
        multi.bgmPlay("tone.ogg", 80, 100, 0, 1);
        const double trackOneLevel = advanceFor(200);
        require(std::abs(meCycle("track 1 of 2", trackOneLevel) - trackOneLevel) < .005,
                "track 1 of 2: not back at its prior volume");
        // Both tracks play the same 440 Hz tone, so the level of their sum depends
        // on their relative phase, which legitimately shifts by however long the
        // tracks' pause/resume instants differ. Check each track's volume alone.
        multi.bgmPlay("tone.ogg", 100, 100, 0, 0);
        meCycle("tracks 0 and 1", advanceFor(200));
        const double pos0 = multi.bgmPos(0), pos1 = multi.bgmPos(1);
        advance(60);
        require(multi.bgmPos(0) != pos0 && multi.bgmPos(1) != pos1, "a BGM track did not resume after the ME");
        const int trackOneVolume = multi.bgmGetVolume(1);
        multi.bgmSetVolume(0, 1);
        const double zeroAlone = advanceFor(200);
        multi.bgmSetVolume(trackOneVolume, 1);
        std::printf("after the ME: track 0 alone %.4f, track 1 alone ", zeroAlone);
        require(std::abs(zeroAlone - trackZeroLevel) < .005, "track 0 lost its volume over the ME");
        multi.bgmStop(0);
        const double oneAlone = advanceFor(200);
        std::printf("%.4f\n", oneAlone);
        require(std::abs(oneAlone - trackOneLevel) < .005, "track 1 lost its volume over the ME");

        multiState.reset();

        // Shutting down while everything plays must not hang or crash.
        audio.bgmPlay("tone.ogg");
        audio.mePlay("tone.flac");
        audio.bgmFade(1000);
        advance(50);
        state.reset();
        MIX_DestroyMixer(audioMixer);
        MIX_Quit();
        std::puts("PASS");
        return 0;
    } catch (const Exception &error) {
        std::fprintf(stderr, "%s\n", error.msg.c_str());
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
    }
    return 1;
}
