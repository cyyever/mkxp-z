#include "display/gpu-render-context.h"
#include "display/render/shader.h"
#include "display/render/quad.h"
#include "util/exception.h"
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

static void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
static std::vector<Uint8> read(const std::filesystem::path &path){
    std::ifstream file(path,std::ios::binary);require(bool(file),"DXBC input missing");
    return {std::istreambuf_iterator<char>(file),{}};
}
int main(int argc,char **argv){
    SDL_Window *window=nullptr;
    try{
        require(argc==2,"usage: gpu-context-tests DXBC_DIRECTORY");
        require(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
        window=SDL_CreateWindow("Production GPU context pixels",32,32,SDL_WINDOW_HIDDEN);
        require(window!=nullptr,SDL_GetError());
        {
            gpu::Device device(window,SDL_GPU_SHADERFORMAT_DXBC,false,"direct3d12");
            const std::filesystem::path directory(argv[1]);
            auto vs=read(directory/"vertexMain.dxbc"),simple=read(directory/"simpleMain.dxbc"),flat=read(directory/"flatColorMain.dxbc"),sprite=read(directory/"spriteMain.dxbc");
            gpu::QuadPipeline::FragmentBytecode programs{};
            programs[static_cast<size_t>(gpu::Program::Simple)]=simple;
            programs[static_cast<size_t>(gpu::Program::FlatColor)]=flat;
            programs[static_cast<size_t>(gpu::Program::Sprite)]=sprite;
            gpu::QuadPipeline pipelines(device.native(),vs,programs);
            auto target=device.createTexture(4,4);
            gpu::RenderContext context(device,pipelines,4,4);
            gpu::RenderContext::install(&context);
            context.setDefaultTarget(gpu::TextureView(target));context.bindDefaultTarget();
            context.setBlend(gpu::Blend::Disabled);
            ShaderSet shaders;
            Quad quad;quad.setTexPosRect(FloatRect(0,0,2,2),FloatRect(0,0,4,4));
            const std::array<Uint8,16> checker={255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255};
            const std::array<Uint8,16> blue={0,0,255,255, 0,0,255,255, 0,0,255,255, 0,0,255,255};
            auto texture=TEX::gen();TEX::bind(texture);TEX::uploadImage(2,2,checker.data());
            shaders.simple.bind();shaders.simple.applyViewportProj();shaders.simple.setTexSize(Vec2i(2,2));
            quad.draw();context.flush();
            auto pixels=device.downloadRGBA(target);
            for(size_t y=0;y<4;++y)for(size_t x=0;x<4;++x)for(size_t c=0;c<4;++c)
                require(pixels[(y*4+x)*4+c]==checker[((y/2)*2+x/2)*4+c],"production Quad/ShaderSet/context changed orientation or UVs");
            // Source mutation must flush already-recorded CPU draws before
            // replacing storage. Then a second draw sees the new image only.
            quad.draw();TEX::uploadImage(2,2,blue.data());
            context.setScissor(SDL_Rect{2,0,2,4});quad.draw();context.flush();context.setScissor(std::nullopt);
            pixels=device.downloadRGBA(target);
            for(size_t y=0;y<4;++y)for(size_t x=0;x<4;++x)for(size_t c=0;c<4;++c)
                require(pixels[(y*4+x)*4+c]==(x>=2?blue[c]:checker[((y/2)*2+x/2)*4+c]),"texture mutation changed previously queued pixels");
            auto pattern=TEX::gen();TEX::bind(pattern);TEX::uploadImage(2,2,checker.data());
            shaders.sprite.setPattern(pattern,Vec2(2,2));
            TEX::del(pattern); // ShaderSet retains an invalid alias until disabled.
            shaders.sprite.setShouldRenderPattern(false);TEX::bind(texture);
            shaders.sprite.bind();shaders.sprite.applyViewportProj();shaders.sprite.setTexSize(Vec2i(2,2));
            shaders.sprite.setOpacity(1);quad.draw();context.flush();
            pixels=device.downloadRGBA(target);
            for(size_t i=0;i<pixels.size();++i)require(pixels[i]==blue[i%4],"disabled disposed pattern rejected or changed a sprite");
            // Deletion must submit pending work while its source storage lives.
            shaders.simple.bind();quad.draw();TEX::del(texture);
            pixels=device.downloadRGBA(target);
            for(size_t i=0;i<pixels.size();++i)require(pixels[i]==blue[i%4],"source deletion invalidated a queued native draw");
            Shader::unbind();
        }
        SDL_DestroyWindow(window);SDL_Quit();
        std::cout<<"Production GPU context pixel tests passed\n";return 0;
    }catch(const Exception &error){std::cerr<<error.msg<<'\n';}
    catch(const std::exception &error){std::cerr<<error.what()<<'\n';}
    if(window)SDL_DestroyWindow(window);SDL_Quit();return 1;
}
