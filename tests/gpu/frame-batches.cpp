#include "display/gpu-frame.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <future>
#include <thread>
#include <chrono>
#include <limits>
#include <iostream>
#include <stdexcept>

static void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
static std::vector<Uint8> read(const std::filesystem::path &path) {
    std::ifstream file(path,std::ios::binary);
    require(bool(file),"shader bytecode missing");
    return {std::istreambuf_iterator<char>(file),{}};
}
int main(int argc,char **argv) {
    SDL_Window *window=nullptr;
    try {
        require(argc==2,"usage: gpu-frame-tests DXBC_DIRECTORY");
        SDL_SetHint(SDL_HINT_ASSERT, "abort");
        require(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
        window=SDL_CreateWindow("GPU batched frame tests",32,32,SDL_WINDOW_HIDDEN);
        require(window!=nullptr,SDL_GetError());
        {
            gpu::Device device(window,SDL_GPU_SHADERFORMAT_DXBC,true,"direct3d12");
            const std::filesystem::path directory(argv[1]);
            auto vertex=read(directory/"vertexMain.dxbc");
            auto flat=read(directory/"flatColorMain.dxbc");
            auto simple=read(directory/"simpleMain.dxbc");
            gpu::QuadPipeline::FragmentBytecode fragments{};
            fragments[static_cast<size_t>(gpu::Program::FlatColor)]=flat;
            fragments[static_cast<size_t>(gpu::Program::Simple)]=simple;
            gpu::QuadPipeline pipelines(device.native(),vertex,fragments);
            gpu::FrameEncoder encoder(device,pipelines);
            auto target=device.createTexture(4,4);
            auto clearOnly=device.createTexture(2,2);
            const gpu::QuadVertex geometry[]={
                {{-1,-1},{0,1},{1,1,1,1}},{{1,-1},{1,1},{1,1,1,1}},{{-1,1},{0,0},{1,1,1,1}},
                {{-1,1},{0,0},{1,1,1,1}},{{1,-1},{1,1},{1,1,1,1}},{{1,1},{1,0},{1,1,1,1}},
            };
            gpu::VertexParameters vp{};
            for(int i=0;i<4;++i) vp.projection[i*5]=vp.transform[i*5]=1;
            vp.textureScaleTranslation[0]=vp.textureScaleTranslation[1]=1;
            gpu::FragmentParameters fp{};
            gpu::DrawState state;state.blend=gpu::Blend::Disabled;
            auto draw=[&]{encoder.draw(gpu::Program::FlatColor,geometry,{},vp,fp,state);};
            auto check=[&](auto expected){
                const auto pixels=device.downloadRGBA(target);
                for(size_t y=0;y<4;++y)for(size_t x=0;x<4;++x){
                    const auto color=expected(x,y);
                    for(size_t c=0;c<4;++c)
                        require(std::abs(int(pixels[(y*4+x)*4+c])-color[c])<=2,"batched frame pixel mismatch");
                }
            };
            // Reject invalid caller state against the actual target before any
            // backend command or vertex append. A later valid draw still works.
            encoder.beginTarget(gpu::TextureView(target),SDL_GPU_LOADOP_CLEAR);
            const float nan = std::numeric_limits<float>::quiet_NaN();
            const SDL_GPUViewport invalidViewports[] = {
                {0,0,8,4,0,1}, {1,0,4,4,0,1}, {0,1,4,4,0,1},
                {0,0,4,0,0,1}, {nan,0,4,4,0,1}, {0,0,nan,4,0,1},
                {0,0,4,4,0,2}, {0,0,4,4,1,0},
            };
            for (auto viewport : invalidViewports) {
                state.viewport = viewport;
                bool rejected = false;
                try { draw(); } catch (const std::invalid_argument &) { rejected = true; }
                require(rejected, "invalid or oversized viewport was accepted");
            }
            state.viewport = SDL_GPUViewport{0,0,4,4,0,1};
            draw();
            encoder.beginTarget(gpu::TextureView(clearOnly),SDL_GPU_LOADOP_CLEAR);
            bool wrongTargetRejected = false;
            try { draw(); } catch (const std::invalid_argument &) { wrongTargetRejected = true; }
            require(wrongTargetRejected, "viewport was validated against the previous target");
            state.viewport = SDL_GPUViewport{0,0,2,2,0,1};
            draw();
            encoder.submit().wait();
            state.viewport.reset();

            // Force an attempted overlap of two differently sized passes on one
            // device. SDL 3.4 debug validation stores viewport limits on Device;
            // the second pass must not change them while the first records.
            auto large = device.createTexture(64,64);
            auto small = device.createTexture(2,2);
            gpu::CommandBuffer largeCommand(device.native());
            SDL_GPUColorTargetInfo largeTarget{};
            largeTarget.texture=large.native();largeTarget.load_op=SDL_GPU_LOADOP_CLEAR;
            largeTarget.store_op=SDL_GPU_STOREOP_STORE;largeTarget.clear_color={0,0,1,1};
            std::promise<void> attemptedPromise, enteredPromise;
            auto attempted=attemptedPromise.get_future();auto entered=enteredPromise.get_future();
            std::exception_ptr workerError;
            std::jthread worker;
            bool overlapped=false;
            {
                gpu::RenderPass largePass(device,largeCommand,largeTarget);
                worker=std::jthread([&] {
                    attemptedPromise.set_value();
                    try {
                        gpu::CommandBuffer command(device.native());
                        SDL_GPUColorTargetInfo smallTarget{};
                        smallTarget.texture=small.native();smallTarget.load_op=SDL_GPU_LOADOP_CLEAR;
                        smallTarget.store_op=SDL_GPU_STOREOP_STORE;smallTarget.clear_color={1,0,0,1};
                        {
                            gpu::RenderPass smallPass(device,command,smallTarget);
                            enteredPromise.set_value();
                            const SDL_GPUViewport viewport{0,0,2,2,0,1};
                            SDL_SetGPUViewport(smallPass.native(),&viewport);
                        }
                        command.submitWithFence().wait();
                    } catch (...) { workerError=std::current_exception(); }
                });
                require(attempted.wait_for(std::chrono::seconds(5))==std::future_status::ready,
                        "second render-pass worker did not start");
                overlapped=entered.wait_for(std::chrono::milliseconds(100))==std::future_status::ready;
                if (!overlapped) {
                    const SDL_GPUViewport viewport{0,0,64,64,0,1};
                    SDL_SetGPUViewport(largePass.native(),&viewport);
                }
            }
            largeCommand.submitWithFence().wait();
            worker.join();
            if (workerError) std::rethrow_exception(workerError);
            require(!overlapped,"render-pass recording overlapped on one SDL device");
            for (const auto &[texture,channel] : std::array{
                    std::pair{&large,size_t(2)},std::pair{&small,size_t(0)}}) {
                const auto pixels=device.downloadRGBA(*texture);
                for(size_t i=0;i<pixels.size();++i)
                    require(pixels[i]==(i%4==channel||i%4==3?255:0),"concurrent-pass clear pixels changed");
            }

            encoder.beginTarget(gpu::TextureView(target),SDL_GPU_LOADOP_CLEAR);
            state.scissor=SDL_Rect{0,0,2,4};fp.color[0]=fp.color[3]=1;draw();
            state.scissor=SDL_Rect{2,0,2,4};fp.color[0]=0;fp.color[1]=1;draw();
            // Modify caller state after recording: each draw must retain its copy.
            fp={};state.scissor.reset();
            encoder.submit().wait();
            check([](size_t x,size_t){return x<2?std::array{255,0,0,255}:std::array{0,255,0,255};});
            encoder.beginTarget(gpu::TextureView(target),SDL_GPU_LOADOP_LOAD);
            state.blend=gpu::Blend::Normal;state.scissor=SDL_Rect{0,0,4,2};
            fp.color[0]=1;fp.color[3]=.5f;draw();
            encoder.beginTarget(gpu::TextureView(clearOnly),SDL_GPU_LOADOP_CLEAR,{0,0,1,1});
            encoder.submit().wait();
            check([](size_t x,size_t y){return y>=2?(x<2?std::array{255,0,0,255}:std::array{0,255,0,255})
                :(x<2?std::array{255,0,0,255}:std::array{128,128,0,255});});
            const auto cleared=device.downloadRGBA(clearOnly);
            for(size_t i=0;i<cleared.size();++i)require(cleared[i]==(i%4>=2?255:0),"empty target clear was dropped");
            encoder.beginTarget(gpu::TextureView(target),SDL_GPU_LOADOP_CLEAR);
            state.blend=gpu::Blend::Disabled;state.scissor.reset();
            state.viewport=SDL_GPUViewport{0,2,4,2,0,1};fp={};fp.color[2]=fp.color[3]=1;draw();
            state.viewport.reset();
            encoder.submit().wait();
            check([](size_t,size_t y){return y<2?std::array{0,0,0,0}:std::array{0,0,255,255};});
            // Restore the full target after a custom viewport in the same pass.
            // A subsequent smaller pass must start from SDL's own defaults.
            encoder.beginTarget(gpu::TextureView(target),SDL_GPU_LOADOP_CLEAR);
            state.viewport=SDL_GPUViewport{0,0,4,2,0,1};
            fp={};fp.color[1]=fp.color[3]=1;draw();
            state.viewport.reset();state.scissor=SDL_Rect{0,2,4,2};
            fp={};fp.color[2]=fp.color[3]=1;draw();
            encoder.beginTarget(gpu::TextureView(clearOnly),SDL_GPU_LOADOP_CLEAR);
            state.scissor.reset();fp={};fp.color[0]=fp.color[3]=1;draw();
            encoder.submit().wait();
            check([](size_t,size_t y){return y<2?std::array{0,255,0,255}:std::array{0,0,255,255};});
            const auto defaultTargetPixels=device.downloadRGBA(clearOnly);
            for(size_t i=0;i<defaultTargetPixels.size();++i)
                require(defaultTargetPixels[i]==(i%4==0||i%4==3?255:0),"new target inherited the previous pass viewport/scissor");
            // An error from bind after render-pass creation must end the pass,
            // cancel the command, and allow the encoder to be reset/reused.
            encoder.beginTarget(gpu::TextureView(target),SDL_GPU_LOADOP_LOAD);
            encoder.draw(gpu::Program::Simple,geometry,{},vp,fp,state);
            bool rejected=false;
            try{auto fence=encoder.submit();}catch(const std::invalid_argument&){rejected=true;}
            require(rejected,"invalid sampler count was accepted");
            encoder.reset();
            // Queue several frames without CPU waits: cycling upload/GPU buffers
            // must preserve each submitted draw rather than overwrite in-flight data.
            std::vector<gpu::Fence> pending;
            std::vector<gpu::Texture> frameTargets;
            for(int i=0;i<20;++i){
                frameTargets.push_back(device.createTexture(4,4));
                encoder.beginTarget(gpu::TextureView(frameTargets.back()),SDL_GPU_LOADOP_CLEAR);
                fp={};fp.color[i%2]=fp.color[3]=1;
                std::array<gpu::QuadVertex,6> half;
                std::copy(std::begin(geometry),std::end(geometry),half.begin());
                for(auto &v:half)v.position[0]=v.position[0]*.5f+(i%2? .5f:-.5f);
                encoder.draw(gpu::Program::FlatColor,half,{},vp,fp,state);
                pending.push_back(encoder.submit());
            }
            pending.back().wait();
            for(size_t frame=0;frame<frameTargets.size();++frame){
                const auto pixels=device.downloadRGBA(frameTargets[frame]);
                for(size_t y=0;y<4;++y)for(size_t x=0;x<4;++x){
                    const bool drawn=frame%2?x>=2:x<2;
                    for(size_t c=0;c<4;++c)
                        require(pixels[(y*4+x)*4+c]==(drawn&&(c==frame%2||c==3)?255:0),"in-flight frame vertices were overwritten");
                }
            }
            // Copies share the submission and retain their exact position among draws/clears.
            auto orderedSource=device.createTexture(4,4);
            auto orderedCopy=device.createTexture(4,4);
            auto orderedChain=device.createTexture(4,4);
            encoder.beginTarget(gpu::TextureView(orderedSource),SDL_GPU_LOADOP_CLEAR,{1,0,0,1});
            encoder.copy(orderedSource,orderedCopy);
            encoder.beginTarget(gpu::TextureView(orderedSource),SDL_GPU_LOADOP_CLEAR,{0,0,1,1});
            encoder.copy(orderedCopy,orderedChain);
            encoder.submitWithoutFence();
            for(auto *texture:{&orderedCopy,&orderedChain}){
                const auto pixels=device.downloadRGBA(*texture);
                for(size_t i=0;i<pixels.size();++i)
                    require(pixels[i]==(i%4==0||i%4==3?255:0),"ordered copy lost earlier target contents");
            }
            const auto bluePixels=device.downloadRGBA(orderedSource);
            for(size_t i=0;i<bluePixels.size();++i)
                require(bluePixels[i]==(i%4==2||i%4==3?255:0),"clear after copy lost submission order");
            const std::vector<Uint8> transparent={13,27,39,0, 1,2,3,64, 4,5,6,128, 7,8,9,255};
            auto rawSource=device.createTexture(2,2),rawCopy=device.createTexture(2,2);
            device.uploadRGBA(rawSource,transparent);
            encoder.copy(rawSource,rawCopy);
            bool copyDrawRejected=false;
            try{draw();}catch(const std::logic_error &){copyDrawRejected=true;}
            require(copyDrawRejected,"draw after copy requires an explicit target");
            encoder.submitWithoutFence();
            require(device.downloadRGBA(rawCopy)==transparent,"copy-only frame changed transparent RGB");

            // Normal engine flushes leave completion ownership entirely to SDL.
            // Cycle the same upload buffers through multiple queued submissions,
            // then read back every target after the queued work has completed.
            std::vector<gpu::Texture> asyncTargets;
            for(int i=0;i<20;++i){
                asyncTargets.push_back(device.createTexture(4,4));
                encoder.beginTarget(gpu::TextureView(asyncTargets.back()),SDL_GPU_LOADOP_CLEAR);
                fp={};fp.color[i%3]=fp.color[3]=1;
                encoder.draw(gpu::Program::FlatColor,geometry,{},vp,fp,state);
                encoder.submitWithoutFence();
            }
            for(size_t frame=0;frame<asyncTargets.size();++frame){
                const auto pixels=device.downloadRGBA(asyncTargets[frame]);
                for(size_t i=0;i<pixels.size();++i)
                    require(pixels[i]==(i%4==frame%3||i%4==3?255:0),
                            "SDL-owned asynchronous submission pixels changed");
            }
            // Explicitly acquired fences may be discarded by exception/shutdown
            // cleanup. Their RAII release must not recycle in-flight state early.
            auto discardedFenceTarget=device.createTexture(4,4);
            for(int i=0;i<8;++i){
                encoder.beginTarget(gpu::TextureView(discardedFenceTarget),SDL_GPU_LOADOP_CLEAR);
                fp={};fp.color[i%3]=fp.color[3]=1;
                encoder.draw(gpu::Program::FlatColor,geometry,{},vp,fp,state);
                static_cast<void>(encoder.submit());
                const auto pixels=device.downloadRGBA(discardedFenceTarget);
                for(size_t j=0;j<pixels.size();++j)
                    require(pixels[j]==(j%4==size_t(i%3)||j%4==3?255:0),
                            "discarded acquired fence corrupted submitted pixels");
            }

            encoder.beginTarget(gpu::TextureView(target),SDL_GPU_LOADOP_CLEAR);
            state.scissor=SDL_Rect{-100,-100,1,1};draw();
            encoder.submit().wait();
            check([](size_t,size_t){return std::array{0,0,0,0};});
        }
        SDL_DestroyWindow(window);SDL_Quit();
        std::cout<<"GPU batched frame tests passed\n";return 0;
    }catch(const std::exception &error){
        std::cerr<<error.what()<<'\n';if(window)SDL_DestroyWindow(window);SDL_Quit();return 1;
    }
}
