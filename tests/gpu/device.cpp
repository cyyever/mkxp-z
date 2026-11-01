#include "display/gpu-device.h"
#include "display/gpu-frame-handoff.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <array>
#include <bit>
#include <algorithm>
#include <stdexcept>
#include <exception>
#include <thread>
#include <future>

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
            gpu::Device device(window, SDL_GPU_SHADERFORMAT_DXBC | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL);
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
            gpu::FrameHandoff frames;
            std::thread producer([&] {
                try {
                    auto rendered = device.createTexture(w, h);
                    gpu::CommandBuffer command(device.native());
                    device.copy(command, destination, rendered);
                    auto ready = command.submitWithFence();
                    require(frames.publish({std::move(rendered), std::move(ready), 42}), "frame publish rejected");
                } catch (...) { workerError = std::current_exception(); }
            });
            producer.join();
            if (workerError) std::rethrow_exception(workerError);
            auto completed = frames.tryTake();
            require(completed && completed->sequence == 42, "frame handoff lost ordering");
            completed->ready.wait();
            require(device.downloadRGBA(completed->texture) == expected, "handoff changed rendered pixels");
            require(!frames.tryTake(), "frame was delivered twice");
            // Occupy the bounded slot, then close while a second producer is
            // publishing. close must wake it regardless of its scheduling.
            require(frames.publish(std::move(*completed)), "frame slot was not reusable");
            std::promise<void> entering;
            auto entered = entering.get_future();
            bool acceptedAfterClose = true;
            std::thread blocked([&] {
                entering.set_value();
                acceptedAfterClose = frames.publish({});
            });
            entered.wait();
            frames.close();
            blocked.join();
            require(!acceptedAfterClose && !frames.tryTake(), "closed handoff retained or accepted a frame");
            frames.close();
            bool rejected = false;
            try { device.uploadRGBA(destination, std::span<const Uint8>(expected).first(1)); }
            catch (const std::invalid_argument &) { rejected = true; }
            require(rejected, "short upload was accepted");
            auto mipmapped = device.createTexture(4,4,SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,3);
            require(mipmapped.mipLevels()==3,"mip level count was discarded");
            std::vector<Uint8> mipPixels(4*4*4);
            const std::array<std::array<Uint8,4>,4> blocks={{{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,255,255}}};
            for(size_t y=0;y<4;++y)for(size_t x=0;x<4;++x)
                std::copy(blocks[(y/2)*2+x/2].begin(),blocks[(y/2)*2+x/2].end(),mipPixels.begin()+(y*4+x)*4);
            device.uploadRGBA(mipmapped,mipPixels);
            device.generateMipmaps(mipmapped);
            const auto level1=device.downloadRGBA(mipmapped,1);
            for(size_t i=0;i<level1.size();++i)
                require(level1[i]==blocks[i/4][i%4],"generated first mip changed block colors");
            const auto level2=device.downloadRGBA(mipmapped,2);
            require(level2.size()==4&&level2[3]==255,"generated terminal mip dimensions/alpha changed");
            for(size_t i=0;i<3;++i)require(std::abs(int(level2[i])-128)<=1,"generated terminal mip average changed");
            for(const auto dimensions : {std::array{2u,1u},std::array{1u,2u},std::array{4u,1u},std::array{1u,4u},std::array{3u,5u}}) {
                const Uint32 levels=static_cast<Uint32>(std::bit_width(std::max(dimensions[0],dimensions[1])));
                auto thin=device.createTexture(dimensions[0],dimensions[1],SDL_GPU_TEXTUREUSAGE_SAMPLER|SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,levels);
                std::vector<Uint8> blue(dimensions[0]*dimensions[1]*4);
                for(size_t i=0;i<blue.size();i+=4)blue[i+2]=blue[i+3]=255;
                device.uploadRGBA(thin,blue);device.generateMipmaps(thin);
                for(Uint32 level=1;level<levels;++level) {
                    const auto decoded=device.downloadRGBA(thin,level);
                    for(size_t i=0;i<decoded.size();++i)
                        if(decoded[i]!=(i%4>=2?255:0)) {
                            std::cerr<<"mip mismatch "<<dimensions[0]<<"x"<<dimensions[1]<<" level "<<level<<" byte "<<i<<" value "<<unsigned(decoded[i])<<'\n';
                            throw std::runtime_error("thin mip lost solid color");
                        }
                }
            }
            bool mipRejected=false;
            try{auto invalid=device.createTexture(4,4,SDL_GPU_TEXTUREUSAGE_SAMPLER,4);}
            catch(const std::invalid_argument&){mipRejected=true;}
            require(mipRejected,"excessive mip count accepted");
            mipRejected=false;
            try{device.generateMipmaps(destination);}catch(const std::invalid_argument&){mipRejected=true;}
            require(mipRejected,"single-level mip generation accepted");
            mipRejected=false;
            try{device.downloadRGBA(mipmapped,3);}catch(const std::invalid_argument&){mipRejected=true;}
            require(mipRejected,"out-of-range mip readback accepted");
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
