#include "font-metrics-under-test.h"
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
struct Source {
  std::vector<Uint8> bytes;
  Sint64 position = 0;
  Sint64 start = 0;
  bool errorAtEnd = false;
  bool unknownSize = false;
  bool failRestore = false;
  bool wrongRestore = false;
  int closes = 0;
  int restores = 0;
};
Sint64 SDLCALL size(void *userdata) {
  const auto &source = *static_cast<Source *>(userdata);
  return source.unknownSize ? -1 : static_cast<Sint64>(source.bytes.size());
}
Sint64 SDLCALL seek(void *userdata, Sint64 offset, SDL_IOWhence whence) {
  auto &source = *static_cast<Source *>(userdata);
  const Sint64 base = whence == SDL_IO_SEEK_CUR ? source.position
                      : whence == SDL_IO_SEEK_END
                          ? static_cast<Sint64>(source.bytes.size())
                          : 0;
  const Sint64 target = base + offset;
  if (whence == SDL_IO_SEEK_SET && target == source.start) {
    ++source.restores;
    if (source.failRestore) {
      SDL_SetError("Injected restore failure");
      return -1;
    }
    if (source.wrongRestore)
      return source.position = target + 1;
  }
  if (target < 0 || target > static_cast<Sint64>(source.bytes.size()))
    return -1;
  source.position = target;
  return target;
}
size_t SDLCALL read(void *userdata, void *destination, size_t requested,
                    SDL_IOStatus *status) {
  auto &source = *static_cast<Source *>(userdata);
  const auto remaining =
      source.bytes.size() - static_cast<size_t>(source.position);
  const auto count = std::min(remaining, requested);
  if (count) {
    SDL_memcpy(destination, source.bytes.data() + source.position, count);
    source.position += static_cast<Sint64>(count);
  } else if (source.errorAtEnd) {
    *status = SDL_IO_STATUS_ERROR;
    SDL_SetError("Injected error after valid font prefix");
  } else {
    *status = SDL_IO_STATUS_EOF;
  }
  return count;
}
bool SDLCALL close(void *userdata) {
  ++static_cast<Source *>(userdata)->closes;
  return true;
}
using Stream = std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)>;
Stream open(Source &source) {
  SDL_IOStreamInterface interface;
  SDL_INIT_INTERFACE(&interface);
  interface.size = size;
  interface.seek = seek;
  interface.read = read;
  interface.close = close;
  Stream stream(SDL_OpenIO(&interface, &source), SDL_CloseIO);
  check(bool(stream), SDL_GetError());
  return stream;
}
Source fixture(std::span<const Uint8> font, Sint64 start = 17) {
  Source source;
  source.bytes.assign(static_cast<size_t>(start), 0);
  source.bytes.insert(source.bytes.end(), font.begin(), font.end());
  source.position = source.start = start;
  return source;
}
void exercise(std::span<const Uint8> font) {
  // The prefix is itself a complete valid font. FreeType would accept it even
  // though the borrowed backend reports an error on the following read.
  auto failed = fixture(font);
  failed.errorAtEnd = failed.unknownSize = true;
  auto stream = open(failed);
  auto metrics = loadFontMetrics(stream.get());
  check(!metrics, "Read error accepted valid font prefix as metrics");
  check(failed.restores == 1 && failed.position == failed.start,
        "Error path did not restore borrowed offset");
  check(failed.closes == 0, "Metrics loader closed borrowed stream");
  stream.reset();
  check(failed.closes == 1, "Borrowed stream ownership leaked or closed twice");

  for (int variant = 0; variant < 3; ++variant) {
    const bool unknown = variant == 1;
    auto valid = fixture(font, variant == 0 ? 0 : 17);
    valid.unknownSize = unknown;
    auto input = open(valid);
    metrics = loadFontMetrics(input.get());
    check(metrics && metrics->face && metrics->face->family_name,
          "Valid font metrics rejected");
    check(valid.position == valid.start && valid.restores == 1,
          "Success did not restore source offset");
    check(valid.closes == 0, "Valid source closed unexpectedly");
    if (!unknown) {
      // Real SDL_ttf consumes the same borrowed stream after metric readback.
      const auto properties = SDL_CreateProperties();
      SDL_SetPointerProperty(properties, TTF_PROP_FONT_CREATE_IOSTREAM_POINTER,
                             input.get());
      SDL_SetNumberProperty(
          properties, TTF_PROP_FONT_CREATE_IOSTREAM_OFFSET_NUMBER, valid.start);
      SDL_SetFloatProperty(properties, TTF_PROP_FONT_CREATE_SIZE_FLOAT, 16);
      std::unique_ptr<TTF_Font, decltype(&TTF_CloseFont)> ttf(
          TTF_OpenFontWithProperties(properties), TTF_CloseFont);
      SDL_DestroyProperties(properties);
      check(bool(ttf), SDL_GetError());
      ttf.reset();
      check(valid.closes == 0, "SDL_ttf did not preserve borrowed ownership");
    }
    metrics.reset();
    input.reset();
    check(valid.closes == 1, "Valid source leaked or closed twice");
  }
  for (const bool wrongOffset : {false, true}) {
    auto unrestorable = fixture(font);
    unrestorable.failRestore = !wrongOffset;
    unrestorable.wrongRestore = wrongOffset;
    auto input = open(unrestorable);
    metrics = loadFontMetrics(input.get());
    check(!metrics && unrestorable.restores == 1,
          "Failed or inaccurate restore accepted font metrics");
    check(unrestorable.closes == 0, "Failed restore closed borrowed stream");
  }
}
} // namespace
int main(int argc, char **argv) {
  try {
    check(argc == 2, "usage: font-metrics-tests FONT.ttf");
    size_t size = 0;
    std::unique_ptr<void, decltype(&SDL_free)> bytes(
        SDL_LoadFile(argv[1], &size), SDL_free);
    check(bool(bytes), SDL_GetError());
    check(TTF_Init(), SDL_GetError());
    try {
      exercise({static_cast<const Uint8 *>(bytes.get()), size});
    } catch (...) {
      TTF_Quit();
      throw;
    }
    TTF_Quit();
    std::cout << "PASS font IO: read-error rejection, valid READY/EOF, offset "
                 "restoration, real SDL_ttf reuse and borrowed ownership\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
