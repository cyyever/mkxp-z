#include "display/gpu-frame.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
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
        require(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
        window=SDL_CreateWindow("GPU batched frame tests",32,32,SDL_WINDOW_HIDDEN);
        require(window!=nullptr,SDL_GetError());
        {
            gpu::Device device(window,SDL_GPU_SHADERFORMAT_DXBC,false,"direct3d12");
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
