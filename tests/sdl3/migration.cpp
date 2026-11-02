#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include "util/sdl-util.h"
#include "util/png-io.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
using Stream = std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)>;

static void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(std::string(message) + ": " + SDL_GetError());
}

struct TemporaryFiles {
    fs::path root = fs::temp_directory_path() /
        ("mkxp-sdl3-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryFiles() { fs::create_directory(root); }
    ~TemporaryFiles() {
        std::error_code ignored;
        for (const auto *name : {"image.png", "image.jpg", "image.bmp"})
            fs::remove(root / name, ignored);
        fs::remove(root, ignored);
    }
};

static void imagesAndStreams(const fs::path &directory) {
    Surface surface(SDL_CreateSurface(7, 5, SDL_PIXELFORMAT_ABGR8888), SDL_DestroySurface);
    check(bool(surface), "create source surface");
    const auto *format = SDL_GetPixelFormatDetails(surface->format);
    check(SDL_FillSurfaceRect(surface.get(), nullptr, SDL_MapRGBA(format, nullptr, 21, 42, 84, 255)),
          "fill source surface");
    const auto png = (directory / "image.png").string();
    const auto jpg = (directory / "image.jpg").string();
    const auto bmp = (directory / "image.bmp").string();
    check(IMG_SavePNG(surface.get(), png.c_str()), "SDL3 PNG save uses boolean success");
    check(IMG_SaveJPG(surface.get(), jpg.c_str(), 95), "SDL3 JPEG save uses boolean success");
    check(SDL_SaveBMP(surface.get(), bmp.c_str()), "SDL3 BMP save uses boolean success");
    // The runtime policy remains PNG-only even with a full system image library.
    for (const auto &path : {png, jpg, bmp}) {
        Stream source(SDL_IOFromFile(path.c_str(), "rb"), SDL_CloseIO);
        Surface runtime(image::loadPng(borrowIO(source.get())), SDL_DestroySurface);
        check(bool(runtime) == (path == png), "runtime accepts PNG contents only");
        check(SDL_SeekIO(source.get(), 0, SDL_IO_SEEK_SET) == 0,
              "runtime owns only the supplied stream wrapper");
    }

    Stream source(SDL_IOFromFile(png.c_str(), "rb"), SDL_CloseIO);
    check(bool(source), "open image source");
    Stream borrowed(borrowIO(source.get()), SDL_CloseIO);
    check(bool(borrowed), "open borrowed stream");
    check(SDL_GetIOSize(borrowed.get()) == SDL_GetIOSize(source.get()), "opaque stream size");
    check(SDL_SeekIO(borrowed.get(), 0, SDL_IO_SEEK_END) >= 0, "opaque stream seek");
    char byte = 0;
    check(SDL_ReadIO(borrowed.get(), &byte, 1) == 0 &&
          SDL_GetIOStatus(borrowed.get()) == SDL_IO_STATUS_EOF, "borrowed EOF status propagation");
    borrowed.reset();
    check(SDL_SeekIO(source.get(), 0, SDL_IO_SEEK_SET) == 0, "borrowed stream close preserves source");

}

int main() {
    try {
        check(SDL_Init(0), "SDL3 initialization");
        {
            TemporaryFiles temporary;
            imagesAndStreams(temporary.root);
        }
        SDL_Quit();
        std::cout << "SDL3 PNG policy and borrowed IO checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
