#include "primitives-common.h"
#include "display/gpu-bitmap.h"
#include "display/gpu-frame.h"
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>

static std::vector<Uint8> read(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    bench::check(bool(input), "Missing compiled shader");
    return {std::istreambuf_iterator<char>(input), {}};
}
static std::vector<gpu::QuadVertex> tiles() {
    std::vector<gpu::QuadVertex> vertices;
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
        const float left = float(x) / 4 - 1, right = float(x+1) / 4 - 1;
        const float top = 1 - float(y) / 4, bottom = 1 - float(y+1) / 4;
        const auto tint = bench::tileColor(x,y);
        const gpu::QuadVertex quad[] = {
            {{left,top},{0,0},{}}, {{right,top},{0,0},{}},
            {{right,bottom},{0,0},{}}, {{left,bottom},{0,0},{}}
        };
        for (const int index : {0,1,2,2,3,0}) {
            auto vertex = quad[index];
            for (int c = 0; c < 4; ++c) vertex.color[c] = float(tint[c]) / 255;
            vertices.push_back(vertex);
        }
    }
    return vertices;
}
int main(int argc, char **argv) {
    SDL_Window *window = nullptr;
    try {
        bench::check(argc >= 2, "usage: gpu-primitives SHADER_DIRECTORY [SAMPLES=30] [ITERATIONS=10]");
        const int samples = argc > 2 ? std::stoi(argv[2]) : 30;
        const int iterations = argc > 3 ? std::stoi(argv[3]) : 10;
        bench::check(samples >= 3 && iterations > 0, "Invalid sample count");
        bench::check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        window = SDL_CreateWindow("SDL_GPU primitive benchmark",32,32,SDL_WINDOW_HIDDEN);
        bench::check(window != nullptr,SDL_GetError());
        {
            gpu::Device device(window,SDL_GPU_SHADERFORMAT_DXBC,false,"direct3d12");
            const auto properties = SDL_GetGPUDeviceProperties(device.native());
            const std::string adapter = SDL_GetStringProperty(properties,SDL_PROP_GPU_DEVICE_NAME_STRING,"unknown");
            const std::string driver = SDL_GetGPUDeviceDriver(device.native());
            const std::string version = std::string("SDL ") + std::to_string(SDL_GetVersion()) + "; " +
                SDL_GetStringProperty(properties,SDL_PROP_GPU_DEVICE_DRIVER_INFO_STRING,
                    SDL_GetStringProperty(properties,SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING,"unknown"));
            const std::filesystem::path directory(argv[1]);
            const auto vertexCode = read(directory / "vertexMain.dxbc");
            std::array<std::vector<Uint8>,size_t(gpu::Program::Count)> code;
            gpu::QuadPipeline::FragmentBytecode fragments{};
            for (size_t i = 0; i < code.size(); ++i) {
                const auto path = directory / (std::string(gpu::QuadPipeline::entryPoint(gpu::Program(i)))+".dxbc");
                if (std::filesystem::exists(path)) { code[i] = read(path); fragments[i] = code[i]; }
            }
            gpu::QuadPipeline pipelines(device.native(),vertexCode,fragments);
            gpu::BitmapRenderer renderer(device,pipelines);
            gpu::FrameEncoder encoder(device,pipelines);
            for (const auto dimensions : {std::array{640,480},std::array{1920,1080},std::array{3840,2160}}) {
                const auto [width,height] = dimensions;
                auto source = device.createTexture(width,height);
                auto target = device.createTexture(width,height);
                device.uploadRGBA(source,bench::pixels(width,height,"blit"));
                device.uploadRGBA(target,bench::pixels(width,height,"clear"));
                const auto geometry = tiles();
                gpu::VertexParameters vp{};
                for (int i = 0; i < 4; ++i) vp.projection[i*5] = vp.transform[i*5] = 1;
                vp.textureScaleTranslation[0] = vp.textureScaleTranslation[1] = 1;
                gpu::FragmentParameters fp{};
                gpu::DrawState state; state.blend = gpu::Blend::Disabled;
                for (const std::string operation : {"clear","fill64","framefill64","blit","batch64"}) {
                    auto execute = [&](int count) {
                        if (operation == "batch64" || operation == "framefill64") {
                            encoder.beginTarget(gpu::TextureView(target),SDL_GPU_LOADOP_LOAD);
                            if (operation == "batch64") {
                                for (int i = 0; i < count; ++i)
                                    encoder.draw(gpu::Program::VertexColor,geometry,{},vp,fp,state);
                            } else {
                                const gpu::QuadVertex full[] = {
                                    {{-1,1},{0,0},{1,1,1,1}}, {{1,1},{0,0},{1,1,1,1}},
                                    {{1,-1},{0,0},{1,1,1,1}}, {{1,-1},{0,0},{1,1,1,1}},
                                    {{-1,-1},{0,0},{1,1,1,1}}, {{-1,1},{0,0},{1,1,1,1}}
                                };
                                auto clipped = state;
                                for (int i = 0; i < count; ++i) for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
                                    const auto color = bench::tileColor(x,y);
                                    gpu::FragmentParameters fill{};
                                    for (int c = 0; c < 4; ++c) fill.color[c] = float(color[c])/255;
                                    clipped.scissor = SDL_Rect{x*width/8,y*height/8,width/8,height/8};
                                    encoder.draw(gpu::Program::FlatColor,full,{},vp,fill,clipped);
                                }
                            }
                            encoder.submit().wait();
                        } else {
                            gpu::CommandBuffer command(device.native());
                            for (int i = 0; i < count; ++i) {
                                if (operation == "clear") renderer.clear(command,target,{0,0,1,1});
                                else if (operation == "blit") renderer.blit(command,target,{0,0,float(width),float(height)},
                                    source,{0,0,float(width),float(height)});
                                else for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
                                    const auto color = bench::tileColor(x,y);
                                    renderer.fill(command,target,{float(x*width/8),float(y*height/8),float(width/8),float(height/8)},
                                        {float(color[0])/255,float(color[1])/255,float(color[2])/255,1});
                                }
                            }
                            command.submitAndWait();
                        }
                    };
                    execute(1);
                    const auto pixels = device.downloadRGBA(target);
                    bench::check(pixels == bench::pixels(width,height,operation),"GPU benchmark pixels differ from workload");
                    bench::measure("SDL_GPU",driver,adapter,version,operation,width,height,samples,iterations,
                        bench::checksum(pixels),execute);
                }
            }
        }
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
}
