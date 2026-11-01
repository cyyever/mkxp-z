#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include "util/sdl-util.h"

#include <chrono>
#include <cstring>
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
    for (const auto &path : {png, jpg}) {
        Surface loaded(IMG_Load(path.c_str()), SDL_DestroySurface);
        check(bool(loaded), "load saved image with the configured codecs");
        check(loaded->w == 7 && loaded->h == 5, "saved image dimensions");
        Surface converted(SDL_ConvertSurface(loaded.get(), SDL_PIXELFORMAT_ABGR8888), SDL_DestroySurface);
        check(bool(converted), "SDL3 pixel-format conversion");
    }
    Surface loadedBmp(SDL_LoadBMP(bmp.c_str()), SDL_DestroySurface);
    check(bool(loadedBmp) && loadedBmp->w == 7 && loadedBmp->h == 5, "SDL3 BMP roundtrip");

    Stream source(SDL_IOFromFile(png.c_str(), "rb"), SDL_CloseIO);
    check(bool(source), "open image source");
    Surface loaded(IMG_LoadTyped_IO(borrowIO(source.get()), true, "PNG"), SDL_DestroySurface);
    check(bool(loaded), "typed image entry point and borrowed decoder IO");
    check(SDL_SeekIO(source.get(), 0, SDL_IO_SEEK_SET) == 0,
          "decoder closes only the borrowed wrapper");
    Stream borrowed(borrowIO(source.get()), SDL_CloseIO);
    check(bool(borrowed), "open borrowed stream");
    check(SDL_GetIOSize(borrowed.get()) == SDL_GetIOSize(source.get()), "opaque stream size");
    check(SDL_SeekIO(borrowed.get(), 0, SDL_IO_SEEK_END) >= 0, "opaque stream seek");
    char byte = 0;
    check(SDL_ReadIO(borrowed.get(), &byte, 1) == 0 &&
          SDL_GetIOStatus(borrowed.get()) == SDL_IO_STATUS_EOF, "borrowed EOF status propagation");
    borrowed.reset();
    check(SDL_SeekIO(source.get(), 0, SDL_IO_SEEK_SET) == 0, "borrowed stream close preserves source");

    Surface scaled(SDL_CreateSurface(14, 10, SDL_PIXELFORMAT_ABGR8888), SDL_DestroySurface);
    check(bool(scaled) && SDL_BlitSurfaceScaled(surface.get(), nullptr, scaled.get(), nullptr,
                                               SDL_SCALEMODE_NEAREST), "SDL3 scaled blit");
    check(SDL_StretchSurface(surface.get(), nullptr, scaled.get(), nullptr, SDL_SCALEMODE_LINEAR),
          "SDL3 linear stretch");
    Surface invalid(IMG_Load((directory / "missing.png").string().c_str()), SDL_DestroySurface);
    check(!invalid, "missing image is rejected");
}

static void fonts(const char *filename) {
    check(TTF_Init(), "SDL3_ttf initialization uses boolean success");
    {
        std::unique_ptr<TTF_Font, decltype(&TTF_CloseFont)> font(TTF_OpenFont(filename, 18.0f), TTF_CloseFont);
        check(bool(font), "open bundled font");
        const std::string text = "UTF-8 \xc3\xa9";
        int width = 0, height = 0;
        check(TTF_GetStringSize(font.get(), text.data(), text.size(), &width, &height) &&
              width > 0 && height > 0, "length-aware UTF-8 text measurement");
        Surface rendered(TTF_RenderText_Blended(font.get(), text.data(), text.size(),
                                                SDL_Color{255, 255, 255, 255}), SDL_DestroySurface);
        check(bool(rendered) && rendered->w == width && rendered->h == height,
              "SDL3_ttf UTF-8 rendering");
    }
    TTF_Quit();
}

int main(int argc, char **argv) {
    try {
        check(argc == 2, "bundled font argument");
        check(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD), "SDL3 initialization");
        check(std::strcmp(SDL_GetCurrentVideoDriver(), "dummy") == 0,
              "migration tests must use the dummy video driver");
        {
            TemporaryFiles temporary;
            std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
                SDL_CreateWindow("migration test", 640, 480, SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
            check(bool(window), "dummy window creation");
            check(SDL_SetWindowSize(window.get(), 800, 600), "window resize");
            int width = 0, height = 0;
            check(SDL_GetWindowSize(window.get(), &width, &height) && width == 800 && height == 600,
                  "resized window dimensions");
            const auto userEvent = SDL_RegisterEvents(1);
            check(userEvent != 0, "SDL3 user-event registration");
            SDL_Event event{};
            event.type = userEvent;
            check(SDL_PushEvent(&event), "push user event");
            bool received = false;
            while (SDL_PollEvent(&event))
                received |= event.type == userEvent;
            check(received, "receive user event");
            imagesAndStreams(temporary.root);
            fonts(argv[1]);
        }
        SDL_Quit();
        std::cout << "SDL3 image, stream, font and window checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
