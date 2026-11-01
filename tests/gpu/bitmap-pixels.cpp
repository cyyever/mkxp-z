#include "display/gpu-bitmap.h"
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

static void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
static std::vector<Uint8> read(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    check(bool(input), "Shader bytecode missing");
    return {std::istreambuf_iterator<char>(input), {}};
}
static std::array<Uint8, 4> pixel(const std::vector<Uint8> &bytes, size_t width, size_t x, size_t y) {
    const auto offset = (y * width + x) * 4;
    return {bytes[offset], bytes[offset+1], bytes[offset+2], bytes[offset+3]};
}
int main(int argc, char **argv) {
    SDL_Window *window = nullptr;
    try {
        check(argc == 2, "usage: bitmap-pixels DXBC_DIRECTORY");
        check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        window = SDL_CreateWindow("GPU bitmap pixel regression", 32, 32, SDL_WINDOW_HIDDEN);
        check(window != nullptr, SDL_GetError());
        {
            gpu::Device device(window, SDL_GPU_SHADERFORMAT_DXBC, false, "direct3d12");
            const std::filesystem::path shaders(argv[1]);
            const auto vertex = read(shaders / "vertexMain.dxbc");
            std::array<std::vector<Uint8>, size_t(gpu::Program::Count)> code;
            gpu::QuadPipeline::FragmentBytecode bytecode{};
            for (size_t i = 0; i < code.size(); ++i) {
                auto path = shaders / (std::string(gpu::QuadPipeline::entryPoint(gpu::Program(i))) + ".dxbc");
                if (std::filesystem::exists(path)) { code[i] = read(path); bytecode[i] = code[i]; }
            }
            gpu::QuadPipeline pipelines(device.native(), vertex, bytecode);
            gpu::BitmapRenderer renderer(device, pipelines);
            auto target = device.createTexture(7, 5);
            {
                gpu::CommandBuffer command(device.native());
                renderer.clear(command, target);
                renderer.fill(command, target, {1,1,3,2}, {1,0,0,1});
                renderer.fill(command, target, {6,4,4,3}, {0,1,0,1});
                command.submitAndWait();
            }
            auto result = device.downloadRGBA(target);
            for (size_t y = 0; y < 5; ++y) for (size_t x = 0; x < 7; ++x) {
                const std::array<Uint8,4> expected = x == 6 && y == 4 ? std::array<Uint8,4>{0,255,0,255} :
                    x >= 1 && x < 4 && y >= 1 && y < 3 ? std::array<Uint8,4>{255,0,0,255} : std::array<Uint8,4>{};
                check(pixel(result,7,x,y) == expected, "Repeated fills/clipping changed bitmap pixels");
            }
            auto source = device.createTexture(2,2);
            const std::vector<Uint8> quadrants = {255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255};
            device.uploadRGBA(source, quadrants);
            {
                gpu::CommandBuffer command(device.native());
                renderer.clear(command, target);
                renderer.blit(command, target, {-2,-2,4,4}, source, {0,0,2,2});
                command.submitAndWait();
            }
            result = device.downloadRGBA(target);
            for (size_t y = 0; y < 5; ++y) for (size_t x = 0; x < 7; ++x)
                check(pixel(result,7,x,y) == (x < 2 && y < 2 ? std::array<Uint8,4>{255,255,255,255} : std::array<Uint8,4>{}),
                      "Negative destination clipping changed source UV/orientation");
            auto translucent = device.createTexture(1,1);
            const std::array<Uint8,4> red = {255,0,0,128};
            device.uploadRGBA(translucent, red);
            {
                gpu::CommandBuffer command(device.native());
                renderer.clear(command, target, {0,0,1,1});
                auto snapshot = renderer.blend(command, target, {2,1,3,2}, translucent, {0,0,1,1}, .5f);
                command.submitAndWait();
            }
            result = device.downloadRGBA(target);
            check(pixel(result,7,2,1) == std::array<Uint8,4>{64,0,191,255}, "RMXP bitmap alpha composition differs");
            check(pixel(result,7,0,0) == std::array<Uint8,4>{0,0,255,255}, "Bitmap blend modified outside rectangle");
            {
                gpu::CommandBuffer command(device.native());
                auto snapshot = renderer.blend(command, target, {0,0,7,5}, target, {0,0,7,5}, 1);
                command.submitAndWait();
            }
            check(device.downloadRGBA(target) == result, "Self blit did not sample a stable snapshot");
            {
                gpu::CommandBuffer command(device.native());
                renderer.clear(command, target, {1,0,0,1});
                auto snapshot = renderer.hue(command, target, 1.f/3.f);
                command.submitAndWait();
            }
            check(pixel(device.downloadRGBA(target),7,0,0) == std::array<Uint8,4>{0,255,0,255}, "GPU bitmap hue differs");
        }
        SDL_DestroyWindow(window); SDL_Quit();
        std::cout << "PASS: GPU bitmap fill, clipping, UV, alpha, self blit and hue\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        if (window) SDL_DestroyWindow(window);
        SDL_Quit(); return 1;
    }
}
