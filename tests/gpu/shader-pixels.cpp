#include "display/gpu-device.h"
#include "display/gpu-shader.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

static void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
static std::vector<Uint8> read(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    require(bool(file), "shader bytecode file missing");
    return std::vector<Uint8>(std::istreambuf_iterator<char>(file), {});
}
struct DrawResources {
    SDL_GPUDevice *device;
    SDL_GPUBuffer *vertices = nullptr;
    SDL_GPUTransferBuffer *transfer = nullptr;
    SDL_GPUSampler *sampler = nullptr;
    ~DrawResources() {
        if (sampler) SDL_ReleaseGPUSampler(device, sampler);
        if (transfer) SDL_ReleaseGPUTransferBuffer(device, transfer);
        if (vertices) SDL_ReleaseGPUBuffer(device, vertices);
    }
};
int main(int argc, char **argv) {
    SDL_Window *window = nullptr;
    try {
        require(argc == 2, "usage: gpu-shader-tests DXBC_DIRECTORY");
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        window = SDL_CreateWindow("GPU quad parity", 32, 32, SDL_WINDOW_HIDDEN);
        require(window != nullptr, SDL_GetError());
        {
            gpu::Device device(window, SDL_GPU_SHADERFORMAT_DXBC, false, "direct3d12");
            const std::filesystem::path directory(argv[1]);
            auto vertexCode = read(directory / "vertexMain.dxbc");
            std::array<std::vector<Uint8>, 10> fragmentCode;
            gpu::QuadPipeline::FragmentBytecode fragments;
            for (size_t i = 0; i < fragmentCode.size(); ++i) {
                fragmentCode[i] = read(directory / (std::string(gpu::QuadPipeline::entryPoint(static_cast<gpu::Program>(i))) + ".dxbc"));
                fragments[i] = fragmentCode[i];
            }
            gpu::QuadPipeline pipelines(device.native(), vertexCode, fragments);
            DrawResources resources{device.native()};
            const gpu::QuadVertex vertices[] = {
                {{-1,-1},{0,1},{1,1,1,.5f}}, {{1,-1},{1,1},{1,1,1,.5f}}, {{-1,1},{0,0},{1,1,1,.5f}},
                {{-1,1},{0,0},{1,1,1,.5f}}, {{1,-1},{1,1},{1,1,1,.5f}}, {{1,1},{1,0},{1,1,1,.5f}},
            };
            SDL_GPUBufferCreateInfo bi{}; bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX; bi.size = sizeof(vertices);
            resources.vertices = SDL_CreateGPUBuffer(device.native(), &bi);
            require(resources.vertices != nullptr, SDL_GetError());
            SDL_GPUTransferBufferCreateInfo ti{}; ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; ti.size = sizeof(vertices);
            resources.transfer = SDL_CreateGPUTransferBuffer(device.native(), &ti);
            require(resources.transfer != nullptr, SDL_GetError());
            auto *mapping = SDL_MapGPUTransferBuffer(device.native(), resources.transfer, false);
            require(mapping != nullptr, SDL_GetError());
            SDL_memcpy(mapping, vertices, sizeof(vertices));
            SDL_UnmapGPUTransferBuffer(device.native(), resources.transfer);
            {
                gpu::CommandBuffer command(device.native());
                auto *copy = SDL_BeginGPUCopyPass(command.native());
                SDL_GPUTransferBufferLocation from{resources.transfer, 0};
                SDL_GPUBufferRegion to{resources.vertices, 0, sizeof(vertices)};
                SDL_UploadToGPUBuffer(copy, &from, &to, false);
                SDL_EndGPUCopyPass(copy); command.submitAndWait();
            }
            SDL_GPUSamplerCreateInfo si{};
            si.min_filter = si.mag_filter = SDL_GPU_FILTER_NEAREST;
            si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
            si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            resources.sampler = SDL_CreateGPUSampler(device.native(), &si);
            require(resources.sampler != nullptr, SDL_GetError());
            auto solid = [&](std::array<Uint8,4> color) {
                auto texture = device.createTexture(2,2);
                std::vector<Uint8> pixels;
                for (int i = 0; i < 4; ++i) pixels.insert(pixels.end(), color.begin(), color.end());
                device.uploadRGBA(texture, pixels); return texture;
            };
            auto source = solid({128,64,32,128});
            auto second = solid({32,128,192,255});
            auto mask = solid({128,128,128,255});
            auto target = device.createTexture(2,2);
            gpu::VertexParameters vp{};
            for (int i = 0; i < 4; ++i) vp.projection[i*5] = vp.transform[i*5] = 1;
            vp.textureScaleTranslation[0] = vp.textureScaleTranslation[1] = 1;
            gpu::FragmentParameters fp{}; fp.parameters[0] = 1;
            const SDL_GPUTextureSamplerBinding bindings[] = {
                {source.native(),resources.sampler}, {second.native(),resources.sampler}, {mask.native(),resources.sampler}
            };
            auto draw = [&](gpu::Program program, gpu::Blend blend, unsigned count, std::array<float,4> expected) {
                gpu::CommandBuffer command(device.native());
                SDL_GPUColorTargetInfo color{}; color.texture = target.native();
                color.load_op = SDL_GPU_LOADOP_CLEAR; color.store_op = SDL_GPU_STOREOP_STORE;
                color.clear_color = {0.2f,0.3f,0.4f,0.6f};
                auto *pass = SDL_BeginGPURenderPass(command.native(), &color, 1, nullptr);
                require(pass != nullptr, SDL_GetError());
                const SDL_GPUBufferBinding buffer{resources.vertices,0};
                pipelines.bind(command.native(), pass, program, blend, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                    buffer, std::span(bindings).first(count), vp, fp);
                SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
                SDL_EndGPURenderPass(pass); command.submitAndWait();
                const auto pixels = device.downloadRGBA(target);
                for (size_t i = 0; i < pixels.size(); ++i) {
                    if (std::abs(float(pixels[i]) - expected[i%4]*255) > 2) {
                        std::cerr << gpu::QuadPipeline::entryPoint(program) << " channel " << i%4 << ": " << unsigned(pixels[i]) << " expected " << expected[i%4]*255 << '\n';
                        throw std::runtime_error("GPU quad pixel mismatch");
                    }
                }
            };
            const float r=128.f/255, g=64.f/255, b=32.f/255, a=128.f/255;
            draw(gpu::Program::Simple,gpu::Blend::Disabled,1,{r,g,b,a});
            draw(gpu::Program::Simple,gpu::Blend::Normal,1,{r*a+.2f*(1-a),g*a+.3f*(1-a),b*a+.4f*(1-a),a+.6f*(1-a)});
            draw(gpu::Program::Simple,gpu::Blend::Addition,1,{r*a+.2f,g*a+.3f,b*a+.4f,1});
            draw(gpu::Program::Simple,gpu::Blend::Subtraction,1,{0,.3f-g*a,.4f-b*a,.6f});
            draw(gpu::Program::Simple,gpu::Blend::KeepDestinationAlpha,1,{r*a+.2f*(1-a),g*a+.3f*(1-a),b*a+.4f*(1-a),.6f});
            draw(gpu::Program::VertexColor,gpu::Blend::Disabled,0,{1,1,1,.5f});
            draw(gpu::Program::VertexAlpha,gpu::Blend::Disabled,1,{r,g,b,a*.5f});
            fp.color[0]=.1f; fp.color[1]=.2f; fp.color[2]=.3f; fp.color[3]=.4f;
            draw(gpu::Program::FlatColor,gpu::Blend::Disabled,0,{.1f,.2f,.3f,.4f});
            fp = {};
            fp.parameters[0]=.5f;
            draw(gpu::Program::UniformAlpha,gpu::Blend::Disabled,1,{r,g,b,a*.5f});
            fp.tone[0]=.1f; fp.tone[3]=.5f; fp.flash[2]=1; fp.flash[3]=.25f;
            const float gray=r*.299f+g*.587f+b*.114f;
            draw(gpu::Program::Plane,gpu::Blend::Disabled,1,{(r*.5f+gray*.5f+.1f)*.75f,(g*.5f+gray*.5f)*.75f,(b*.5f+gray*.5f)*.75f+.25f,a*.5f});
            fp = {}; fp.parameters[1]=.25f;
            draw(gpu::Program::SimpleTransition,gpu::Blend::Disabled,2,{r*.75f+32.f/255*.25f,g*.75f+128.f/255*.25f,b*.75f+192.f/255*.25f,a*.75f+.25f});
            fp.parameters[1]=0; fp.parameters[2]=1;
            draw(gpu::Program::Transition,gpu::Blend::Disabled,3,{r*(1-a)+32.f/255*a,g*(1-a)+128.f/255*a,b*(1-a)+192.f/255*a,a*(1-a)+a});
            fp = {}; fp.parameters[0]=.5f; fp.subrect[2]=fp.subrect[3]=1;
            const float weight=a*.5f;
            draw(gpu::Program::BitmapBlend,gpu::Blend::Disabled,2,{r*weight+32.f/255*(1-weight),g*weight+128.f/255*(1-weight),b*weight+192.f/255*(1-weight),1});
            fp = {}; fp.parameters[3]=1; // full hue rotation must preserve RGB and alpha
            draw(gpu::Program::Hue,gpu::Blend::Disabled,1,{r,g,b,a});
            const std::array<Uint8,16> red = {255,0,0,255, 255,0,0,255, 255,0,0,255, 255,0,0,255};
            device.uploadRGBA(source, red);
            fp.parameters[3]=1.f/3;
            draw(gpu::Program::Hue,gpu::Blend::Disabled,1,{0,1,0,1});
            require(pipelines.get(gpu::Program::Simple,gpu::Blend::Normal,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM)
                == pipelines.get(gpu::Program::Simple,gpu::Blend::Normal,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM), "pipeline cache missed");
        }
        SDL_DestroyWindow(window); SDL_Quit();
        std::cout << "GPU shader pixel tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        if (window) SDL_DestroyWindow(window);
        SDL_Quit(); return 1;
    }
}
