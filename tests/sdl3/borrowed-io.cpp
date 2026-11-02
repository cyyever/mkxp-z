// Test the engine's ownership/status adapter, rather than SDL's codecs or UI.
#include "util/sdl-util.h"

#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

using Stream = std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)>;

static void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        constexpr char bytes[] = "a\0bc";
        Stream source(SDL_IOFromConstMem(bytes, sizeof(bytes) - 1), SDL_CloseIO);
        check(bool(source), "create source stream");
        {
            Stream borrowed(borrowIO(source.get()), SDL_CloseIO);
            check(bool(borrowed), "create borrowed stream");
            check(SDL_GetIOSize(borrowed.get()) == sizeof(bytes) - 1, "borrowed size");
            char result[sizeof(bytes)]{};
            check(SDL_ReadIO(borrowed.get(), result, sizeof(result)) == sizeof(bytes) - 1 &&
                  std::memcmp(result, bytes, sizeof(bytes) - 1) == 0, "borrowed binary read");
            check(SDL_GetIOStatus(borrowed.get()) == SDL_IO_STATUS_EOF, "borrowed EOF propagation");
            check(SDL_SeekIO(borrowed.get(), 1, SDL_IO_SEEK_SET) == 1 &&
                  SDL_TellIO(source.get()) == 1, "borrowed seek updates source");
        }
        check(SDL_SeekIO(source.get(), 0, SDL_IO_SEEK_SET) == 0,
              "closing borrowed stream closed its source");
        std::cout << "PASS: borrowed IO binary data, EOF, seek and ownership\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
