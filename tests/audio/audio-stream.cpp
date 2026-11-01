#include "alstream.h"
#include "aldatasource.h"
#include "exception.h"
#include "sharedstate.h"

#include <SDL3/SDL_init.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

using namespace std::chrono_literals;
SDL_AudioDeviceID audioDevice;
AudioTestState testState;
AudioTestState *shState = &testState;
static std::string requestedSource;
static std::atomic<uint32_t> lastSeekFrame;
static std::atomic<unsigned> liveSources;

// The generated PCM source isolates queueing, looping and playback from codecs
// and asset lookup. SDL's real dummy device consumes the resulting stream.
struct GeneratedSource : ALDataSource {
    bool looped;
    uint32_t frame = 0;
    uint32_t length;
    SDL_AudioSpec format;
    GeneratedSource(bool looped, bool empty, bool mono)
        : looped(looped), length(empty ? 0 : (mono ? 2205 : 4800)),
          format{mono ? SDL_AUDIO_U8 : SDL_AUDIO_S16, mono ? 1 : 2, mono ? 22050 : 48000} { ++liveSources; }
    ~GeneratedSource() override { --liveSources; }
    SDL_AudioSpec spec() override { return format; }
    void seekToOffset(double seconds) override {
        frame = std::min(static_cast<uint32_t>(seconds * format.freq), length);
        lastSeekFrame = frame;
    }
    uint32_t loopStartFrames() override { return 0; }
    Status fillBuffer(std::vector<uint8_t> &out) override {
        out.assign((length - frame) * SDL_AUDIO_FRAMESIZE(format), 0);
        frame = length;
        if (looped) { frame = 0; return WrapAround; }
        return EndOfStream;
    }
};

void FileSystem::openRead(OpenHandler &handler, const char *filename) {
    requestedSource = filename;
    static constexpr char signature[] = "TEST";
    if (!handler.tryRead(SDL_IOFromConstMem(signature, 4), "generated"))
        throw std::runtime_error("generated source rejected");
}
ALDataSource *createSDLSource(SDL_IOStream *ops, const char *, uint32_t, bool looped) {
    SDL_CloseIO(ops);
    return new GeneratedSource(looped, requestedSource == "empty", requestedSource == "mono");
}
static void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
static void waitStopped(ALStream &stream) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (stream.queryState() == ALStream::Playing && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(5ms);
    require(stream.queryState() == ALStream::Stopped, "finite stream did not stop after draining");
}

int main() {
    try {
        require(SDL_Init(SDL_INIT_AUDIO), SDL_GetError());
        audioDevice = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
        require(audioDevice != 0, SDL_GetError());
        require(rgssVolumeToGain(0) == 0 && rgssVolumeToGain(1) == 1, "volume endpoints changed");
        require(std::abs(rgssVolumeToGain(.5f) - std::pow(10.f, -.875f)) < .00001f, "RGSS decibel scale changed");
        {
            ALStream stream(ALStream::NotLooped);
            require(stream.queryState() == ALStream::Closed, "new stream not closed");
            stream.open("finite");
            stream.play();
            waitStopped(stream);
            stream.play(.02);
            waitStopped(stream);
            stream.open("mono");
            stream.play();
            waitStopped(stream);
            stream.close();
            require(stream.queryState() == ALStream::Closed, "close did not release source");
        }
        {
            ALStream stream(ALStream::Looped);
            stream.open("loop");
            stream.setPitch(1.5f);
            stream.setVolume(.5f);
            stream.play();
            std::this_thread::sleep_for(250ms);
            require(stream.queryState() == ALStream::Playing, "loop stopped at EOF");
            require(stream.queryOffset() < .11, "loop offset did not wrap");
            stream.pause();
            const double paused = stream.queryOffset();
            std::this_thread::sleep_for(100ms);
            require(stream.queryState() == ALStream::Paused, "pause changed state");
            require(std::abs(stream.queryOffset() - paused) < .001, "paused audio kept advancing");
            stream.play();
            std::this_thread::sleep_for(50ms);
            require(stream.queryState() == ALStream::Playing, "resume failed");
            stream.stop();
            require(stream.queryState() == ALStream::Stopped, "stop failed");
            stream.play();
            std::this_thread::sleep_for(30ms);
            // Destruction must join an active worker before freeing SDL resources.
        }
        {
            ALStream stream(ALStream::NotLooped);
            // Closed operations must be harmless and must not create a worker.
            stream.play();
            stream.pause();
            stream.stop();
            stream.close();
            require(stream.queryState() == ALStream::Closed && stream.queryOffset() == 0,
                    "closed controls changed stream state");
            stream.open("finite");
            stream.play(.04);
            require(lastSeekFrame == 1920, "play offset was not passed to decoder in seconds");
            stream.pause();
            const double paused = stream.queryOffset();
            stream.pause();
            std::this_thread::sleep_for(150ms);
            require(stream.queryState() == ALStream::Paused, "finite paused queue drained");
            require(std::abs(stream.queryOffset() - paused) < .001, "finite pause lost queued position");
            stream.play(.09);
            require(lastSeekFrame == 1920, "resume unexpectedly sought instead of retaining queued audio");
            waitStopped(stream);
            stream.play(.2); // Decoder clamps an offset beyond EOF to an empty stream.
            require(lastSeekFrame == 4800, "seek beyond EOF was not clamped by decoder");
            waitStopped(stream);
            stream.play(0);
            require(lastSeekFrame == 0, "replay after EOF did not rewind source");
            waitStopped(stream);
        }
        {
            ALStream stream(ALStream::Looped);
            stream.open("loop");
            stream.play();
            stream.pause();
            stream.stop();
            stream.stop();
            require(stream.queryState() == ALStream::Stopped, "stop while paused failed");
            stream.play(.03);
            require(lastSeekFrame == 1440, "restart after paused stop did not seek");
            // Replacing an active source must join its worker and release its data.
            stream.open("mono");
            require(stream.queryState() == ALStream::Stopped && liveSources == 1,
                    "active source replacement leaked or kept playing");
            stream.play(.02);
            require(lastSeekFrame == 441, "replacement retained old sample rate");
            stream.close();
            stream.close();
            require(liveSources == 0 && stream.queryOffset() == 0,
                    "close did not discard replacement source and position");
        }
        {
            ALStream stream(ALStream::Looped);
            stream.open("empty");
            stream.play();
            waitStopped(stream);
        }
        {
            ALStream stream(ALStream::NotLooped);
            stream.open("finite");
            const auto savedDevice = audioDevice;
            audioDevice = 0;
            bool failed = false;
            try { stream.play(); } catch (const Exception &) { failed = true; }
            audioDevice = savedDevice;
            require(failed, "invalid audio device silently accepted");
        }
        SDL_CloseAudioDevice(audioDevice);
        SDL_Quit();
        std::cout << "audio stream tests passed\n";
        return 0;
    } catch (const Exception &error) {
        std::cerr << error.msg << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
    }
    SDL_Quit();
    return 1;
}
