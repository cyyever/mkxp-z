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
        require(argc == 2 || (argc == 3 && std::string(argv[2]) == "--extra-shaders"), "usage: gpu-shader-tests DXBC_DIRECTORY [--extra-shaders]");
        const bool extraShaders = argc == 3;
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        window = SDL_CreateWindow("GPU quad parity", 32, 32, SDL_WINDOW_HIDDEN);
        require(window != nullptr, SDL_GetError());
        {
            gpu::Device device(window, SDL_GPU_SHADERFORMAT_DXBC, false, "direct3d12");
            const auto directory = std::filesystem::absolute(argv[1]);
            auto vertexCode = read(directory / "vertexMain.dxbc");
            std::array<std::vector<Uint8>, static_cast<size_t>(gpu::Program::Count)> fragmentCode;
            gpu::QuadPipeline::FragmentBytecode fragments;
            for (size_t i = 0; i < fragmentCode.size(); ++i) {
                if (static_cast<gpu::Program>(i) == gpu::Program::Weather) continue;
                if (static_cast<gpu::Program>(i) == gpu::Program::Xbrz && !extraShaders) continue;
                fragmentCode[i] = read(directory / (std::string(gpu::QuadPipeline::entryPoint(static_cast<gpu::Program>(i))) + ".dxbc"));
                fragments[i] = fragmentCode[i];
            }
            auto tilemapVertexCode = read(directory / "tilemapVertexMain.dxbc");
            gpu::QuadPipeline pipelines(device.native(), vertexCode, fragments, tilemapVertexCode);
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
            gpu::EffectParameters effects{};
            gpu::TilemapParameters tilemap{};
            gpu::FragmentParameters fp{}; fp.parameters[0] = 1;
            SDL_GPUTextureSamplerBinding bindings[] = {
                {source.native(),resources.sampler}, {second.native(),resources.sampler}, {mask.native(),resources.sampler}
            };
            auto draw = [&](gpu::Program program, gpu::Blend blend, unsigned count, std::array<float,4> expected, std::span<const Uint8> reference = {}) {
                gpu::CommandBuffer command(device.native());
                SDL_GPUColorTargetInfo color{}; color.texture = target.native();
                color.load_op = SDL_GPU_LOADOP_CLEAR; color.store_op = SDL_GPU_STOREOP_STORE;
                color.clear_color = {0.2f,0.3f,0.4f,0.6f};
                auto *pass = SDL_BeginGPURenderPass(command.native(), &color, 1, nullptr);
                require(pass != nullptr, SDL_GetError());
                const SDL_GPUBufferBinding buffer{resources.vertices,0};
                pipelines.bind(command.native(), pass, program, blend, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                    buffer, std::span(bindings).first(count), vp, fp, effects, tilemap);
                SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
                SDL_EndGPURenderPass(pass); command.submitAndWait();
                const auto pixels = device.downloadRGBA(target);
                for (size_t i = 0; i < pixels.size(); ++i) {
                    if (std::abs(float(pixels[i]) - (reference.empty() ? expected[i%4]*255 : reference[i])) > 2) {
                        std::cerr << gpu::QuadPipeline::entryPoint(program) << " channel " << i%4 << ": " << unsigned(pixels[i]) << " expected " << (reference.empty() ? expected[i%4]*255 : reference[i]) << '\n';
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
            draw(gpu::Program::Simple,gpu::Blend::ColorAdditionKeepDestinationAlpha,1,{r+.2f,g+.3f,b+.4f,.6f});
            draw(gpu::Program::Simple,gpu::Blend::ColorSubtractionKeepDestinationAlpha,1,{0,.3f-g,.4f-b,.6f});
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
            fp = {}; fp.parameters[0]=1;
            draw(gpu::Program::Tilemap,gpu::Blend::Disabled,1,{r,g,b,a});
            fp.parameters[0]=.25f;
            draw(gpu::Program::FlashMap,gpu::Blend::Disabled,0,{.25f,.25f,.25f,1});
            fp = {}; fp.tone[3]=1;
            draw(gpu::Program::Gray,gpu::Blend::Disabled,1,{gray,gray,gray,a});
            fp = {}; fp.parameters[0]=1;
            effects.blurDirection[0]=.5f;
            draw(gpu::Program::Blur,gpu::Blend::Disabled,1,{r,g,b,a});
            draw(gpu::Program::Invert,gpu::Blend::Disabled,1,{1-r,1-g,1-b,a});
            draw(gpu::Program::CompressAlpha,gpu::Blend::Disabled,1,{r*a,g*a,b*a,0});
            fp.parameters[0]=.5f; fp.subrect[2]=fp.subrect[3]=1;
            draw(gpu::Program::Subtract,gpu::Blend::Disabled,2,{0,96.f/255,176.f/255,1});
            fp = {}; fp.parameters[0]=1;
            effects = {}; effects.spriteFlags[0]=1; effects.spriteFlags[3]=1;
            effects.patternScaleOffset[0]=effects.patternScaleOffset[1]=1;
            effects.blurDirection[2]=.5f; effects.bush[1]=1; effects.bush[2]=.25f;
            draw(gpu::Program::Sprite,gpu::Blend::Disabled,2,{(r+32.f/255)*.5f,(g+128.f/255)*.5f,(b+192.f/255)*.5f,a*.25f});
            effects.spriteFlags[1]=1;
            draw(gpu::Program::Sprite,gpu::Blend::Disabled,2,{r+16.f/255,g+64.f/255,b+96.f/255,a*.25f});
            effects = {}; effects.filterSize[0]=effects.filterSize[1]=2;
            effects.filterSize[2]=effects.filterSize[3]=.5f;
            effects.filterParameters[0]=effects.filterParameters[1]=1.f/3;
            draw(gpu::Program::Bicubic,gpu::Blend::Disabled,1,{r,g,b,a});
            draw(gpu::Program::Lanczos,gpu::Blend::Disabled,1,{r,g,b,a});
            effects.filterParameters[2]=effects.filterParameters[3]=1;
            if (!fragmentCode[static_cast<size_t>(gpu::Program::Xbrz)].empty())
                draw(gpu::Program::Xbrz,gpu::Blend::Disabled,1,{r,g,b,a});
            effects = {}; effects.shadowBounds[1]=2;
            effects.shadowDimensions[0]=effects.shadowDimensions[1]=2;
            effects.shadowDimensions[3]=-1; effects.shadowSlopes[1]=10;
            draw(gpu::Program::ShadowHorizontal,gpu::Blend::Disabled,1,{0,0,0,0});
            effects.shadowDimensions[2]=-1; effects.shadowDimensions[3]=0;
            draw(gpu::Program::ShadowVertical,gpu::Blend::Disabled,1,{0,0,0,0});
            effects = {};
            // Animated atlas UVs must select the second 96-pixel autotile frame.
            auto atlas=device.createTexture(192,128);
            std::vector<Uint8> atlasPixels(192*128*4);
            for (size_t i=0;i<atlasPixels.size();i+=4) {
                atlasPixels[i + ((i/4)%192 < 96 ? 0 : 1)] = 255;
                atlasPixels[i+3]=255;
            }
            device.uploadRGBA(atlas,atlasPixels);
            bindings[0].texture=atlas.native();
            vp.textureScaleTranslation[0]=1.f/192; vp.textureScaleTranslation[1]=1.f/128;
            tilemap.animation[0]=1; tilemap.autotileFrames0[0]=2;
            draw(gpu::Program::Tilemap,gpu::Blend::Disabled,1,{0,1,0,1});
            bindings[0].texture=source.native();
            vp.textureScaleTranslation[0]=vp.textureScaleTranslation[1]=1;
            const std::array<Uint8,16> checker = {255,0,0,255, 0,0,255,255, 0,255,0,255, 255,255,255,255};
            device.uploadRGBA(source,checker);
            effects = {}; effects.blurDirection[0]=.5f;
            const std::array<Uint8,16> horizontalBlur = {170,0,85,255, 85,0,170,255, 85,255,85,255, 170,255,170,255};
            draw(gpu::Program::Blur,gpu::Blend::Disabled,1,{},horizontalBlur);
            effects = {}; effects.filterSize[0]=effects.filterSize[1]=2;
            effects.filterSize[2]=effects.filterSize[3]=.5f;
            effects.filterParameters[1]=.5f; // interpolating Catmull-Rom B=0,C=.5
            draw(gpu::Program::Bicubic,gpu::Blend::Disabled,1,{},checker);
            draw(gpu::Program::Lanczos,gpu::Blend::Disabled,1,{},checker);
            effects.filterParameters[2]=effects.filterParameters[3]=1;
            const std::array<Uint8,16> red = {255,0,0,255, 255,0,0,255, 255,0,0,255, 255,0,0,255};
            device.uploadRGBA(source, red);
            fp.parameters[3]=1.f/3;
            draw(gpu::Program::Hue,gpu::Blend::Disabled,1,{0,1,0,1});
            // A requested game-owned program must fail closed, then load an
            // offline variant for the actual backend. Shader bytes are never
            // compiled or replaced with an embedded effect at runtime.
            struct GameDirectory {
                std::filesystem::path previous=std::filesystem::current_path();
                std::filesystem::path temporary=std::filesystem::temp_directory_path()/
                    ("mkxp-weather-"+std::to_string(SDL_GetTicksNS()));
                GameDirectory(){std::filesystem::create_directories(temporary/"shaders");std::filesystem::current_path(temporary);}
                ~GameDirectory(){std::error_code error;std::filesystem::current_path(previous,error);std::filesystem::remove_all(temporary,error);}
            } gameDirectory;
            const auto weatherPipeline = [&] {
                return pipelines.get(gpu::Program::Weather,gpu::Blend::Disabled,
                    SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
            };
            const auto expectWeatherError = [&] {
                bool rejected=false;
                try {weatherPipeline();} catch(const std::runtime_error &error) {
                    rejected=std::string(error.what()).starts_with("Game weather shader:");
                }
                require(rejected,"requested invalid game weather shader did not fail closed");
            };
            expectWeatherError(); // Missing manifest.
            const auto manifest = [](const char *text) {
                std::ofstream file("shaders/weather.json",std::ios::binary);file<<text;
            };
            manifest(R"({"version":2,"variants":[]})");expectWeatherError();
            manifest(R"({"version":1,"variants":[]})");expectWeatherError();
            manifest(R"({"version":1,"variants":[{"format":"unknown"}]})");expectWeatherError();
            manifest(R"({"version":1,"variants":[{"format":"dxbc","file":"../outside.dxbc","entrypoint":"flatColorMain"}]})");expectWeatherError();
            manifest(R"({"version":1,"variants":[{"format":"dxbc","file":"shaders/missing.dxbc","entrypoint":"flatColorMain"}]})");expectWeatherError();
            manifest(R"({"version":1,"variants":[{"format":"dxbc","file":"C:/outside.dxbc","entrypoint":"flatColorMain"}]})");expectWeatherError();
            manifest("{malformed");expectWeatherError();
            {std::ofstream file("shaders/weather.json");file<<std::string(64*1024+1,' ');}
            expectWeatherError();
            const auto unicodeShader=std::filesystem::u8path("shaders/雨雪.dxbc");
            std::filesystem::copy_file(directory/"flatColorMain.dxbc",unicodeShader);
            manifest(R"({"version":1,"variants":[{"format":"dxbc","file":"shaders/雨雪.dxbc","entrypoint":"flatColorMain"}]})");
            manifest(R"({"version":1,"variants":[{"format":"dxbc","file":"shaders/雨雪.dxbc","entrypoint":""}]})");expectWeatherError();
            manifest(R"({"version":1,"variants":[{"format":"dxbc","file":"shaders/雨雪.dxbc","entrypoint":"flatColorMain"}]})");
            require(weatherPipeline()!=nullptr,"offline game weather shader did not load");
            fp={};fp.color[1]=1;fp.color[3]=.5f;
            draw(gpu::Program::Weather,gpu::Blend::Disabled,0,{0,1,0,.5f});
            require(weatherPipeline()==weatherPipeline(),"game shader pipeline cache missed");
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
