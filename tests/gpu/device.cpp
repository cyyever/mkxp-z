#include "display/gpu-device.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <stdexcept>
#include <exception>
#include <thread>

static void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    SDL_Window *window = nullptr;
    try {
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        window = SDL_CreateWindow("GPU resource verification", 64, 64, SDL_WINDOW_HIDDEN);
        require(window != nullptr, SDL_GetError());
        {
            gpu::Device device(window, SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL);
            std::cout << "GPU driver: " << SDL_GetGPUDeviceDriver(device.native()) << '\n';
            // Odd-sized rows expose alignment and stride mistakes; values cover
            // alpha, low bits, channel order and vertical orientation.
            constexpr Uint32 w = 7, h = 5;
            std::vector<Uint8> expected(w * h * 4);
            for (size_t i = 0; i < expected.size(); ++i) expected[i] = Uint8(i * 37);
            auto source = device.createTexture(w, h);
            device.uploadRGBA(source, expected);
            require(device.downloadRGBA(source) == expected, "upload/readback changed RGBA pixels");
            auto destination = device.createTexture(w, h);
            {
                gpu::CommandBuffer command(device.native());
                device.copy(command, source, destination);
                command.submitAndWait();
            }
            require(device.downloadRGBA(destination) == expected, "GPU copy changed pixels");
            auto moved = std::move(source);
            require(source.native() == nullptr, "moved texture retains ownership");
            require(device.downloadRGBA(moved) == expected, "texture move lost pixels");
            // RGSS can render/read back offscreen on its worker; presentation
            // remains on the SDL window thread. Propagate worker failures.
            std::exception_ptr workerError;
            std::thread worker([&] {
                try {
                    require(device.downloadRGBA(destination) == expected, "worker readback changed pixels");
                    gpu::CommandBuffer command(device.native());
                    Uint32 width, height;
                    bool wrongThreadRejected = false;
                    try { device.acquireFrame(command, width, height); }
                    catch (const std::logic_error &) { wrongThreadRejected = true; }
                    require(wrongThreadRejected, "worker was allowed to acquire main-thread swapchain");
                } catch (...) { workerError = std::current_exception(); }
            });
            worker.join();
            if (workerError) std::rethrow_exception(workerError);
            bool rejected = false;
            try { device.uploadRGBA(destination, std::span<const Uint8>(expected).first(1)); }
            catch (const std::invalid_argument &) { rejected = true; }
            require(rejected, "short upload was accepted");
            { gpu::CommandBuffer canceled(device.native()); }
            {
                gpu::CommandBuffer frame(device.native());
                Uint32 width, height;
                auto *swapchain = device.acquireFrame(frame, width, height);
                if (swapchain) {
                    SDL_GPUColorTargetInfo color{};
                    color.texture = swapchain;
                    color.load_op = SDL_GPU_LOADOP_CLEAR;
                    color.store_op = SDL_GPU_STOREOP_STORE;
                    color.clear_color = {0.25f, 0.5f, 0.75f, 1};
                    auto *pass = SDL_BeginGPURenderPass(frame.native(), &color, 1, nullptr);
                    require(pass != nullptr, SDL_GetError());
                    SDL_EndGPURenderPass(pass);
                }
                frame.submitAndWait();
            }
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::cout << "GPU resource tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
}
