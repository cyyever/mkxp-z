// ALStream (one BGM/BGS/ME track) against an offline SDL3_mixer mixer: the test
// pulls the mix with MIX_Generate, so positions and levels are deterministic.
#include "alstream.h"
#include "exception.h"
#include "sharedstate.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
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

static void require(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}

static std::vector<float> render(double seconds) {
    std::vector<float> pcm(size_t(seconds * RATE) * 2);
    // MIX_Generate always fills the buffer; it returns how much the tracks mixed.
    require(MIX_Generate(audioMixer, pcm.data(), int(pcm.size() * sizeof(float))) >= 0, SDL_GetError());
    return pcm;
}

static double rms(const std::vector<float> &pcm) {
    double energy = 0;
    for (float sample : pcm) energy += double(sample) * sample;
    return std::sqrt(energy / pcm.size());
}

static bool near(double actual, double expected, double tolerance = .01) {
    return std::abs(actual - expected) <= tolerance;
}

// Longest run of silent frames in the left channel.
static size_t longestSilence(const std::vector<float> &pcm) {
    size_t run = 0, longest = 0;
    for (size_t i = 0; i < pcm.size(); i += 2) {
        run = std::abs(pcm[i]) < 1e-4f ? run + 1 : 0;
        longest = std::max(longest, run);
    }
    return longest;
}

int main(int argc, char **argv) {
    try {
        require(argc == 2, "usage: audio-stream-tests FIXTURES");
        fixtures = argv[1];
        require(rgssVolumeToGain(0) == 0 && rgssVolumeToGain(1) == 1, "volume endpoints changed");
        require(std::abs(rgssVolumeToGain(.5f) - std::pow(10.f, -.875f)) < .00001f, "RGSS decibel scale changed");
        require(MIX_Init(), SDL_GetError());
        const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, RATE};
        audioMixer = MIX_CreateMixer(&spec);
        require(audioMixer, SDL_GetError());

        {
            // Closed controls are harmless.
            ALStream stream(ALStream::NotLooped);
            stream.play();
            stream.pause();
            stream.stop();
            stream.close();
            require(stream.queryState() == ALStream::Closed && stream.queryOffset() == 0, "closed controls");

            // A finite stream plays once and stops by itself.
            stream.open("tone");
            require(stream.queryState() == ALStream::Stopped, "open did not stop");
            stream.play();
            render(.5);
            require(stream.queryState() == ALStream::Playing && near(stream.queryOffset(), .5), "finite playing");
            render(.6);
            require(stream.queryState() == ALStream::Stopped, "finite stream did not stop after its end");

            // Offsets are seconds; pause freezes the position and resume ignores the offset.
            stream.play(.25);
            require(near(stream.queryOffset(), .25), "play offset");
            render(.1);
            stream.pause();
            const double paused = stream.queryOffset();
            require(near(paused, .35), "position while playing");
            require(rms(render(.1)) == 0, "paused stream is audible");
            require(stream.queryOffset() == paused && stream.queryState() == ALStream::Paused, "pause");
            stream.play(.9);
            require(stream.queryState() == ALStream::Playing && stream.queryOffset() == paused, "resume sought");
            stream.stop();
            require(stream.queryState() == ALStream::Stopped, "stop");
            stream.pause();
            stream.stop();
            require(stream.queryState() == ALStream::Stopped, "stop while stopped");

            // A finite stream started past its end has nothing to play.
            stream.play(5);
            require(stream.queryState() == ALStream::Stopped, "offset beyond EOF played");
            stream.close();
            stream.close();
            require(stream.queryState() == ALStream::Closed, "close");
        }
        {
            // Looping wraps to the start of the file, without a gap.
            ALStream stream(ALStream::Looped);
            stream.open("tone");
            stream.play();
            // A resampled track's first mix ends a few frames short (resampler
            // look-ahead) as it starts; check the loop points after that.
            render(.05);
            const auto pcm = render(2.45);
            require(stream.queryState() == ALStream::Playing, "loop stopped at EOF");
            require(near(stream.queryOffset(), .5), "loop offset did not wrap");
            std::printf("longest silence in looped WAV: %zu frames\n", longestSilence(pcm));
            require(longestSilence(pcm) < 4, "gap at the loop point");

            // An offset past the end of a looped stream wraps around.
            stream.stop();
            stream.play(2.25);
            require(near(stream.queryOffset(), .25), "looped offset beyond EOF");

            // Pitch is a frequency ratio on the source position.
            stream.stop();
            stream.setPitch(1.5f);
            stream.play();
            render(.2);
            require(near(stream.queryOffset(), .3), "pitch 150 did not advance 1.5x");
            stream.setPitch(1);

            // Volume uses the RGSS -35 dB scale.
            const double full = rms(render(.2));
            stream.setVolume(.5f);
            const double half = rms(render(.2));
            std::printf("volume .5 / 1 RMS ratio %.4f\n", half / full);
            require(near(half / full, rgssVolumeToGain(.5f), .002), "volume curve");
            stream.setVolume(0);
            require(rms(render(.1)) == 0, "volume 0 is not silent");
            stream.setVolume(1);

            // Opening another file stops the stream.
            stream.open("tone.ogg");
            require(stream.queryState() == ALStream::Stopped, "replacement kept playing");
            // Destruction while playing must be safe.
            stream.play();
            render(.1);
        }
        {
            // Every runtime codec loops; Ogg loop tags are ignored (whole file loops).
            for (const char *name : {"tone.mp3", "tone.ogg", "tone.flac", "tone-alaw", "tone-looptag.ogg"}) {
                ALStream stream(ALStream::Looped);
                stream.open(name);
                stream.play();
                double highest = 0;
                for (int i = 0; i < 25; ++i) {
                    render(.1);
                    highest = std::max(highest, stream.queryOffset());
                }
                std::printf("%s: loops, highest position %.3f s\n", name, highest);
                require(stream.queryState() == ALStream::Playing && highest > .9 && stream.queryOffset() < 1,
                        std::string(name) + " did not loop over the whole file");
            }
        }
        {
            ALStream stream(ALStream::Looped);
            bool failed = false;
            try { stream.open("missing"); } catch (const Exception &e) { failed = e.type == Exception::NoFileError; }
            require(failed, "missing file");
            failed = false;
            try { stream.open("../meson.build"); } catch (const Exception &e) { failed = e.type == Exception::SDLError; }
            require(failed && stream.queryState() == ALStream::Closed, "undecodable file");
        }
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
