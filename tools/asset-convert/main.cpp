#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_sound/SDL_sound.h>
#include "util/json5pp.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
using Stream = std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)>;
using Sample = std::unique_ptr<Sound_Sample, decltype(&Sound_FreeSample)>;

static std::string utf8(const fs::path &path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char *>(text.data()), text.size()};
}
static fs::path nativePath(std::string_view text) {
    return fs::path(std::u8string_view(reinterpret_cast<const char8_t *>(text.data()), text.size()));
}
static void require(bool success, const std::string &message) {
    if (!success) throw std::runtime_error(message);
}
static void requireSDL(bool success, const char *context) {
    if (!success) throw std::runtime_error(std::string(context) + ": " + SDL_GetError());
}
static void requireSound(bool success, const char *context) {
    if (!success) {
        const char *error = Sound_GetError();
        throw std::runtime_error(std::string(context) + ": " + (error ? error : "unknown SDL_sound error"));
    }
}
static std::string extension(const fs::path &path) {
    auto value = utf8(path.extension());
    for (char &ch : value) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    return value;
}
static std::string crcText(uint32_t value) {
    std::array<char, 9> text{};
    std::snprintf(text.data(), text.size(), "%08x", static_cast<unsigned int>(value));
    return text.data();
}
static void validatePaths(const fs::path &source, const fs::path &destination, const char *suffix) {
    require(fs::is_regular_file(source), "source is not a regular file");
    require(extension(destination) == suffix, std::string("destination must end with ") + suffix);
    std::error_code error;
    const bool same = fs::equivalent(source, destination, error);
    require(!same, "source and destination are the same file");
    require(!fs::exists(fs::symlink_status(destination)), "destination already exists; refusing overwrite");
}

// C++23 noreplace opens exclusively, including a competing creator after the
// preflight check. A failed write or failed verification removes only this new
// output, never a pre-existing file. Original assets are opened read-only.
class Output {
    fs::path path;
    std::ofstream stream;
    bool verified = false;
public:
    explicit Output(const fs::path &path)
        : path(path), stream(path, std::ios::binary | std::ios::noreplace) {
        require(stream.is_open(), "cannot exclusively create destination");
    }
    ~Output() {
        stream.close();
        if (!verified) {
            std::error_code error;
            fs::remove(path, error);
        }
    }
    void write(const void *data, size_t size) {
        stream.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
        require(stream.good(), "output write failed");
    }
    void text(std::string_view text) { write(text.data(), text.size()); }
    void u16(uint16_t value) {
        const std::array<uint8_t, 2> bytes{uint8_t(value), uint8_t(value >> 8)};
        write(bytes.data(), bytes.size());
    }
    void u32(uint32_t value) {
        const std::array<uint8_t, 4> bytes{uint8_t(value), uint8_t(value >> 8), uint8_t(value >> 16), uint8_t(value >> 24)};
        write(bytes.data(), bytes.size());
    }
    void finish() {
        stream.flush();
        require(stream.good(), "output flush failed");
        stream.close();
        require(!stream.fail(), "output close failed");
    }
    void commit() { verified = true; }
};

static Surface loadImage(const fs::path &path, std::string *decoder = nullptr) {
    Surface decoded(IMG_Load(utf8(path).c_str()), SDL_DestroySurface);
    bool bmpFallback = false;
    if (!decoded && extension(path) == ".bmp") {
        decoded.reset(SDL_LoadBMP(utf8(path).c_str()));
        bmpFallback = true;
    }
    if (decoder) *decoder = bmpFallback
        ? "SDL3 SDL_LoadBMP fallback (source BMP unsupported by SDL3_image)"
        : "SDL3_image (engine build; vendored JPEG uses stb_image)";
    requireSDL(bool(decoded), "SDL3_image decode failed");
    Surface normalized(SDL_ConvertSurface(decoded.get(), SDL_PIXELFORMAT_ABGR8888), SDL_DestroySurface);
    requireSDL(bool(normalized), "ABGR8888 conversion failed");
    return normalized;
}
struct SurfaceLock {
    SDL_Surface *surface;
    explicit SurfaceLock(SDL_Surface *surface) : surface(surface) {
        requireSDL(SDL_LockSurface(surface), "surface lock failed");
    }
    ~SurfaceLock() { SDL_UnlockSurface(surface); }
};
static json5pp::value convertImage(const fs::path &source, const fs::path &destination) {
    validatePaths(source, destination, ".png");
    std::string decoder;
    auto original = loadImage(source, &decoder);
    Stream encoded(SDL_IOFromDynamicMem(), SDL_CloseIO);
    requireSDL(bool(encoded), "PNG buffer creation failed");
    requireSDL(IMG_SavePNG_IO(original.get(), encoded.get(), false), "PNG encode failed");
    const Sint64 size = SDL_GetIOSize(encoded.get());
    const void *bytes = SDL_GetPointerProperty(SDL_GetIOProperties(encoded.get()), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr);
    require(size > 0 && bytes, "PNG encoder returned an empty buffer");
    Output output(destination);
    output.write(bytes, static_cast<size_t>(size));
    output.finish();
    auto reloaded = loadImage(destination);
    require(original->w == reloaded->w && original->h == reloaded->h, "PNG verification changed dimensions");
    SurfaceLock originalLock(original.get()), reloadedLock(reloaded.get());
    uint32_t crc = 0;
    for (int row = 0; row < original->h; ++row) {
        const auto *a = static_cast<const uint8_t *>(original->pixels) + size_t(row) * original->pitch;
        const auto *b = static_cast<const uint8_t *>(reloaded->pixels) + size_t(row) * reloaded->pitch;
        require(std::memcmp(a, b, size_t(original->w) * 4) == 0, "PNG verification changed decoded pixel bytes");
        crc = SDL_crc32(crc, a, size_t(original->w) * 4);
    }
    output.commit();
    return json5pp::object({{"kind", "image"}, {"source", utf8(source)}, {"destination", utf8(destination)},
        {"decoder", decoder},
        {"width", original->w}, {"height", original->h}, {"pixel_format", "ABGR8888"},
        {"decoded_crc32", crcText(crc)}, {"verified", "exact normalized decoded pixel bytes"}});
}

struct DecodedAudio {
    SDL_AudioSpec spec;
    std::vector<uint8_t> bytes;
    std::string decoder;
};
static DecodedAudio loadAudio(const fs::path &path, bool allowWavFallback = true) {
    Sample sample(Sound_NewSampleFromFile(utf8(path).c_str(), nullptr, 65536), Sound_FreeSample);
    if (!sample && allowWavFallback && extension(path) == ".wav") {
        // SDL_sound's WAV decoder omits A-law, while SDL3's WAV loader supports
        // it. Keep this fallback explicit in the report and verify its decoded
        // PCM through SDL_sound after writing an ordinary PCM/float WAV.
        SDL_AudioSpec spec{};
        Uint8 *bytes = nullptr;
        Uint32 size = 0;
        const bool loaded = SDL_LoadWAV(utf8(path).c_str(), &spec, &bytes, &size);
        std::unique_ptr<void, decltype(&SDL_free)> owned(bytes, SDL_free);
        requireSDL(loaded, "SDL3 WAV fallback decode failed");
        DecodedAudio result{spec, {}, "SDL3 SDL_LoadWAV fallback (source WAV unsupported by SDL_sound)"};
        if (size) result.bytes.assign(bytes, bytes + size);
        return result;
    }
    requireSound(bool(sample), "SDL_sound decode failed");
    DecodedAudio result{sample->actual, {}, sample->decoder->description};
    require(sample->desired.format == result.spec.format && sample->desired.freq == result.spec.freq
        && sample->desired.channels == result.spec.channels, "decoder unexpectedly enabled sample conversion");
    while (!(sample->flags & SOUND_SAMPLEFLAG_EOF)) {
        const Uint32 count = Sound_Decode(sample.get());
        requireSound(!(sample->flags & SOUND_SAMPLEFLAG_ERROR), "SDL_sound read failed");
        require(count <= sample->buffer_size, "decoder returned an invalid buffer length");
        require(result.bytes.size() + uint64_t(count) <= UINT32_MAX - 64, "decoded audio exceeds RIFF WAV size limit");
        const auto *bytes = static_cast<const uint8_t *>(sample->buffer);
        result.bytes.insert(result.bytes.end(), bytes, bytes + count);
        require(count != 0 || (sample->flags & SOUND_SAMPLEFLAG_EOF), "audio decoder made no progress before EOF");
    }
    return result;
}
static json5pp::value convertAudio(const fs::path &source, const fs::path &destination) {
    validatePaths(source, destination, ".wav");
    const auto original = loadAudio(source);
    const auto &spec = original.spec;
    const bool floating = spec.format == SDL_AUDIO_F32LE;
    require(floating || spec.format == SDL_AUDIO_U8 || spec.format == SDL_AUDIO_S16LE || spec.format == SDL_AUDIO_S32LE,
        "exact WAV conversion supports U8/S16LE/S32LE/F32LE; unsupported formats are not quantized");
    require(spec.freq > 0 && spec.channels > 0, "invalid decoded audio specification");
    const uint64_t blockSize = uint64_t(spec.channels) * SDL_AUDIO_BYTESIZE(spec.format);
    const uint64_t byteRate = uint64_t(spec.freq) * blockSize;
    require(blockSize <= UINT16_MAX && spec.channels <= UINT16_MAX && byteRate <= UINT32_MAX,
        "audio specification exceeds WAV field limits");
    require(original.bytes.size() % blockSize == 0, "decoder returned an incomplete sample frame");
    const uint32_t dataSize = static_cast<uint32_t>(original.bytes.size());
    const uint32_t padding = dataSize % 2;
    const uint32_t formatSize = floating ? 18 : 16;
    const uint32_t riffSize = 4 + 8 + formatSize + (floating ? 12 : 0) + 8 + dataSize + padding;
    const uint32_t frames = static_cast<uint32_t>(dataSize / blockSize);
    Output output(destination);
    output.text("RIFF"); output.u32(riffSize); output.text("WAVE");
    output.text("fmt "); output.u32(formatSize); output.u16(floating ? 3 : 1);
    output.u16(static_cast<uint16_t>(spec.channels)); output.u32(spec.freq); output.u32(static_cast<uint32_t>(byteRate));
    output.u16(static_cast<uint16_t>(blockSize)); output.u16(SDL_AUDIO_BITSIZE(spec.format));
    if (floating) {
        output.u16(0); // WAVEFORMATEX extension byte count.
        output.text("fact"); output.u32(4); output.u32(frames);
    }
    output.text("data"); output.u32(dataSize); output.write(original.bytes.data(), dataSize);
    if (padding) output.text(std::string_view("\0", 1));
    output.finish();
    const auto reloaded = loadAudio(destination, false);
    require(spec.format == reloaded.spec.format && spec.freq == reloaded.spec.freq && spec.channels == reloaded.spec.channels,
        "WAV verification changed sample format, rate or channels");
    require(original.bytes == reloaded.bytes, "WAV verification changed decoded PCM sample bits");
    output.commit();
    return json5pp::object({{"kind", "audio"}, {"source", utf8(source)}, {"destination", utf8(destination)},
        {"decoder", original.decoder}, {"rate", spec.freq}, {"channels", spec.channels},
        {"sdl_audio_format", int(spec.format)}, {"bits_per_sample", int(SDL_AUDIO_BITSIZE(spec.format))},
        {"frames", double(frames)}, {"sample_bytes", double(dataSize)},
        {"wave_format", floating ? "IEEE_FLOAT" : "PCM"},
        {"decoded_crc32", crcText(SDL_crc32(0, original.bytes.data(), original.bytes.size()))},
        {"verified", "exact SDL_sound decoded PCM bytes"}});
}

static int run(std::vector<std::string> arguments) {
    if (arguments.size() >= 2 && (arguments[1] == "--image" || arguments[1] == "--audio"))
        arguments[1].erase(0, 2);
    if (arguments.size() == 2 && arguments[1] == "--help") {
        std::cout << "mkxp-asset-convert image SOURCE DESTINATION.png\nmkxp-asset-convert audio SOURCE DESTINATION.wav\n";
        return 0;
    }
    if (arguments.size() != 4 || (arguments[1] != "image" && arguments[1] != "audio")) {
        std::cerr << "Usage: mkxp-asset-convert image|audio SOURCE DESTINATION.png|wav\n";
        return 2;
    }
    int result = 1;
    bool sound = false;
    try {
        requireSDL(SDL_Init(0), "SDL initialization failed");
        if (arguments[1] == "audio") {
            requireSound(Sound_Init() != 0, "SDL_sound initialization failed");
            sound = true;
        }
        const auto source = nativePath(arguments[2]), destination = nativePath(arguments[3]);
        const auto report = arguments[1] == "image" ? convertImage(source, destination) : convertAudio(source, destination);
        std::cout << report.stringify() << '\n';
        result = 0;
    } catch (const std::exception &error) {
        std::cerr << "mkxp-asset-convert: " << error.what() << '\n';
    }
    if (sound) Sound_Quit();
    SDL_Quit();
    return result;
}

#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    std::vector<std::string> arguments;
    for (int index = 0; index < argc; ++index) arguments.push_back(utf8(fs::path(argv[index])));
    return run(arguments);
}
#else
int main(int argc, char **argv) { return run({argv, argv + argc}); }
#endif
