// The streaming data source (BGM/BGS/ME) decoding through SDL3_mixer.
#include "aldatasource.h"
#include "exception.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>

static std::string fixtures;

static void require(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}

static std::unique_ptr<ALDataSource> open(const char *name, bool looped) {
    SDL_IOStream *io = SDL_IOFromFile((fixtures + "/" + name).c_str(), "rb");
    require(io, SDL_GetError());
    return std::unique_ptr<ALDataSource>(createSDLSource(io, "", 32768, looped));
}

// Frames until the first EndOfStream/WrapAround, and which status ended it.
static uint64_t pass(ALDataSource &source, ALDataSource::Status &end) {
    const int frameSize = SDL_AUDIO_FRAMESIZE(source.spec());
    std::vector<uint8_t> chunk;
    uint64_t frames = 0;
    for (;;) {
        end = source.fillBuffer(chunk);
        require(end != ALDataSource::Error, "decode error");
        frames += chunk.size() / frameSize;
        if (end != ALDataSource::NoError) return frames;
        require(!chunk.empty(), "empty chunk before the end");
    }
}

int main(int argc, char **argv) {
    try {
        require(argc == 2, "usage: mixer-source-tests FIXTURES");
        fixtures = argv[1];
        require(MIX_Init(), SDL_GetError());
        for (const char *name : {"tone.wav", "tone.flac", "tone.mp3", "tone.ogg", "tone-looptag.ogg", "tone-alaw.wav"}) {
            ALDataSource::Status end;
            auto once = open(name, false);
            const int rate = once->spec().freq;
            const uint64_t total = pass(*once, end);
            require(end == ALDataSource::EndOfStream, std::string(name) + ": finite source did not end");
            require(total > rate * 9 / 10 && total < rate * 11 / 10, std::string(name) + ": length");

            // A looped source wraps after exactly one pass and keeps going.
            auto loop = open(name, true);
            require(pass(*loop, end) == total && end == ALDataSource::WrapAround,
                    std::string(name) + ": loop did not wrap at the end of the file");
            require(pass(*loop, end) == total && end == ALDataSource::WrapAround,
                    std::string(name) + ": second loop pass differs");
            require(loop->loopStartFrames() == 0, "loops restart from the beginning");

            // Seeking is frame-exact; seeking to 0 rewinds.
            loop->seekToOffset(.25);
            const uint64_t rest = pass(*loop, end);
            require(rest == total - uint64_t(.25 * rate), std::string(name) + ": seek position");
            loop->seekToOffset(0);
            require(pass(*loop, end) == total, std::string(name) + ": rewind");
            std::printf("%s: %llu frames at %d Hz\n", name, (unsigned long long)total, rate);
        }

        bool rejected = false;
        try { open("../meson.build", true); } catch (const Exception &e) { rejected = e.type == Exception::SDLError; }
        require(rejected, "undecodable data was accepted");

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
