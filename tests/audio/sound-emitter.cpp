// SoundEmitter (Audio.se_play) against an offline SDL3_mixer mixer: the test
// pulls the mix with MIX_Generate, so timing and levels are deterministic.
#include "soundemitter.h"
#include "config.h"
#include "exception.h"
#include "sharedstate.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

MIX_Mixer *audioMixer;
AudioTestState testState;
AudioTestState *shState = &testState;
static std::string fixtures;
static size_t fileOpens = 0;
static constexpr int RATE = 48000;

// Like the engine's lookup: the exact name, then with an extension; a missing
// file throws, an undecodable one leaves the handler empty.
void FileSystem::openRead(OpenHandler &handler, const char *filename) {
    // Distinct cache keys share a fixture so eviction can be tested without
    // checking in megabytes of duplicate audio.
    if (std::string_view(filename).starts_with("cache-")) filename = "tone.flac";
    for (const char *ext : {"", ".wav"}) {
        const std::string path = fixtures + "/" + filename + ext;
        if (SDL_IOStream *io = SDL_IOFromFile(path.c_str(), "rb")) {
            ++fileOpens;
            handler.tryRead(io, ext);
            return;
        }
    }
    throw Exception(Exception::NoFileError, "%s", filename);
}

static void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

static double render(int frames, size_t *loudFrames = nullptr) {
    std::vector<float> pcm(frames * 2);
    // MIX_Generate always fills the buffer; it returns how much the tracks mixed.
    require(MIX_Generate(audioMixer, pcm.data(), int(pcm.size() * sizeof(float))) >= 0, SDL_GetError());
    double energy = 0;
    for (size_t i = 0; i < pcm.size(); i += 2) {
        energy += double(pcm[i]) * pcm[i];
        if (loudFrames && std::abs(pcm[i]) > 1e-4f) ++*loudFrames;
    }
    return std::sqrt(energy / frames);
}

static size_t playing(SoundEmitter &se) {
    size_t count = 0;
    for (const auto &source : se.sources)
        count += MIX_TrackPlaying(source.get());
    return count;
}

// Frames of audible output until every source has finished.
static size_t audibleFrames(SoundEmitter &se) {
    size_t loud = 0;
    for (int i = 0; i < 1000 && playing(se); ++i)
        render(RATE / 100, &loud);
    return loud;
}

int main(int argc, char **argv) {
    try {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        require(argc == 2, "usage: sound-emitter-tests FIXTURES");
        fixtures = argv[1];
        require(MIX_Init(), SDL_GetError());
        const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, RATE};
        audioMixer = MIX_CreateMixer(&spec);
        require(audioMixer, SDL_GetError());

        Config config;
        config.SE.sourceCount = 3;
        {
            SoundEmitter se(config);
            require(se.sources.size() == 3, "SE.sourceCount not honoured");

            // Free sources first, then the newest source playing the same file,
            // then the oldest source; a used source moves to the back.
            se.play("tone", 100, 100);
            se.play("tone", 100, 100);
            se.play("tone", 100, 100);
            require(playing(se) == 3, "three SEs should play at once");
            se.play("tone.flac", 100, 100);
            require(playing(se) == 3, "fourth SE exceeded SE.sourceCount");
            require(se.sourceKeys[0] == "tone.flac", "oldest source was not evicted");
            require((se.srcPrio == std::vector<size_t>{1, 2, 0}), "priority after eviction");
            se.play("tone", 100, 100);
            require(se.sourceKeys[2] == "tone" && (se.srcPrio == std::vector<size_t>{1, 0, 2}),
                    "same-file source was not reused");
            se.stop();
            require(playing(se) == 0, "se_stop left sources playing");

            // Volume uses the RGSS -35 dB scale; 0 is silent.
            se.play("tone", 100, 100);
            const double full = render(RATE / 5);
            se.stop();
            se.play("tone", 50, 100);
            const double half = render(RATE / 5);
            se.stop();
            std::printf("volume 50 / 100 RMS ratio %.4f (expected %.4f)\n", half / full, rgssVolumeToGain(.5f));
            require(std::abs(half / full - rgssVolumeToGain(.5f)) < .002, "volume curve changed");
            se.play("tone", 0, 100);
            require(render(RATE / 10) == 0, "volume 0 is not silent");
            se.stop();

            // Pitch is a frequency ratio: 50 plays twice as long, 150 two thirds.
            se.play("tone", 100, 100);
            const double normal = double(audibleFrames(se));
            se.play("tone", 100, 50);
            const double slow = audibleFrames(se) / normal;
            se.play("tone", 100, 150);
            const double fast = audibleFrames(se) / normal;
            std::printf("pitch 50: %.3fx, pitch 150: %.3fx duration\n", slow, fast);
            require(std::abs(slow - 2) < .02 && std::abs(fast - 2 / 3.) < .02, "pitch ratio changed");

            // Every runtime codec, including A-law WAV which SDL_sound rejected.
            for (const char *name : {"tone.mp3", "tone.ogg", "tone-alaw"}) {
                se.play(name, 100, 100);
                std::printf("%s plays\n", name);
                require(render(RATE / 10) > .01, name);
                se.stop();
            }

            bool rejected = false;
            try { se.play("../meson.build", 100, 100); } catch (const Exception &e) {
                rejected = e.type == Exception::SDLError;
            }
            require(rejected, "undecodable SE was accepted");
            rejected = false;
            try { se.play("missing", 100, 100); } catch (const Exception &e) {
                rejected = e.type == Exception::NoFileError;
            }
            require(rejected, "missing SE did not raise NoFileError");

            // Destroying the emitter with sounds still playing must be safe.
            se.play("tone", 100, 100);
        }
        // Hash-table growth and cache eviction must not invalidate tracks that
        // still reference decoded audio. A cache hit must avoid reopening it.
        config.SE.sourceCount = 64;
        {
            SoundEmitter se(config);
            se.play("tone.flac", 100, 100);
            const size_t opened = fileOpens;
            se.play("tone.flac", 0, 100);
            require(fileOpens == opened, "cached SE reopened its file");
            for (int i = 0; i < 40; ++i)
                se.play("cache-" + std::to_string(i), 0, 100);
            require(render(RATE / 10) > .05, "cache eviction stopped a playing SE");
            const size_t beforeReload = fileOpens;
            se.play("tone.flac", 0, 100);
            require(fileOpens == beforeReload + 1, "SE cache never evicted its old audio");
            se.stop();
            require(render(RATE / 10) == 0, "evicted SE survived se_stop");
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
