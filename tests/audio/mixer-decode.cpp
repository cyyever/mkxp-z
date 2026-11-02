// Decode every runtime audio format through SDL3_mixer's header-only decoders.
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "%s:%d: CHECK(%s) failed: %s\n", __FILE__, __LINE__, #condition, SDL_GetError()); \
    std::exit(1); } } while (0)

static bool hasDecoder(std::string_view name)
{
    for (int i = 0; i < MIX_GetNumAudioDecoders(); ++i)
        if (name == MIX_GetAudioDecoder(i))
            return true;
    return false;
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    const std::string fixtures = argv[1];
    CHECK(MIX_Init());

    for (const char *name : {"WAV", "DRFLAC", "DRMP3", "STBVORBIS"})
        CHECK(hasDecoder(name));

    // Each fixture is about one second of a 440 Hz sine at amplitude 1/8 (RMS 0.088;
    // 0.062 per channel when ffmpeg upmixed it to stereo).
    // tone-looptag.ogg carries LOOPSTART/LOOPLENGTH; the engine ignores loop tags
    // and plays the whole file, which needs the patched stb_vorbis decoder.
    const SDL_PropertiesID ignoreLoops = SDL_CreateProperties();
    SDL_SetBooleanProperty(ignoreLoops, MIX_PROP_AUDIO_LOAD_IGNORE_LOOPS_BOOLEAN, true);
    struct Fixture { const char *file; const char *decoder; };
    for (const Fixture &fixture : {Fixture{"tone.wav", "WAV"}, Fixture{"tone.flac", "DRFLAC"},
                                   Fixture{"tone.mp3", "DRMP3"}, Fixture{"tone.ogg", "STBVORBIS"},
                                   Fixture{"tone-looptag.ogg", "STBVORBIS"}}) {
        const std::string path = fixtures + "/" + fixture.file;
        MIX_AudioDecoder *decoder = MIX_CreateAudioDecoder(path.c_str(), ignoreLoops);
        CHECK(decoder);
        const char *used = SDL_GetStringProperty(MIX_GetAudioDecoderProperties(decoder),
                                                 MIX_PROP_AUDIO_DECODER_STRING, "");
        CHECK(std::string_view(used) == fixture.decoder);

        const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 48000};
        std::vector<float> samples;
        float chunk[4096];
        int got;
        while ((got = MIX_DecodeAudio(decoder, chunk, sizeof(chunk), &spec)) > 0)
            samples.insert(samples.end(), chunk, chunk + got / sizeof(float));
        CHECK(got == 0);
        MIX_DestroyAudioDecoder(decoder);

        const size_t frames = samples.size() / 2;
        double energy = 0;
        for (float sample : samples)
            energy += double(sample) * sample;
        const double rms = std::sqrt(energy / samples.size());
        std::printf("%s: %zu frames at 48 kHz, RMS %.3f\n", fixture.file, frames, rms);
        // The MediaFoundation MP3 encoder drops a few tail frames.
        CHECK(frames > 44000 && frames < 52000);
        CHECK(rms > 0.05 && rms < 0.12);
    }

    SDL_DestroyProperties(ignoreLoops);
    MIX_Quit();
    std::puts("PASS");
    return 0;
}
