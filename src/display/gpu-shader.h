#pragma once
#include <SDL3/SDL_gpu.h>
#include <array>
#include <map>
#include <memory>
#include <span>
#include <tuple>

namespace gpu {
enum class Program { Simple, FlatColor, VertexColor, VertexAlpha, UniformAlpha, Plane, Transition, SimpleTransition, BitmapBlend, Hue, Count };
enum class Blend { Disabled, Normal, Addition, Subtraction, KeepDestinationAlpha };
struct QuadVertex { float position[2]; float texcoord[2]; float color[4]; };
struct alignas(16) VertexParameters {
    // Column-major matrices, matching the HLSL default and existing GL uploads.
    float projection[16];
    float transform[16];
    float textureScaleTranslation[4];
};
struct alignas(16) FragmentParameters { float color[4], tone[4], flash[4], parameters[4], subrect[4]; };
static_assert(sizeof(QuadVertex) == 32);
static_assert(sizeof(VertexParameters) == 144);
static_assert(sizeof(FragmentParameters) == 80);

class Shader {
public:
    Shader(SDL_GPUDevice *, std::span<const Uint8>, const char *entry,
           SDL_GPUShaderStage, Uint32 samplers, Uint32 uniforms);
    ~Shader();
    Shader(const Shader &) = delete;
    Shader &operator=(const Shader &) = delete;
    SDL_GPUShader *native() const { return shader; }
private:
    SDL_GPUDevice *device;
    SDL_GPUShader *shader;
};

// Device and caller-owned buffers/textures must outlive pending commands.
// One cache per render thread; pipelines are created once per program/blend/format.
class QuadPipeline {
public:
    using FragmentBytecode = std::array<std::span<const Uint8>, static_cast<size_t>(Program::Count)>;
    QuadPipeline(SDL_GPUDevice *, std::span<const Uint8> vertexCode, const FragmentBytecode &);
    ~QuadPipeline();
    QuadPipeline(const QuadPipeline &) = delete;
    QuadPipeline &operator=(const QuadPipeline &) = delete;
    SDL_GPUGraphicsPipeline *get(Program, Blend, SDL_GPUTextureFormat);
    void bind(SDL_GPUCommandBuffer *, SDL_GPURenderPass *, Program, Blend,
        SDL_GPUTextureFormat, const SDL_GPUBufferBinding &vertices,
        std::span<const SDL_GPUTextureSamplerBinding> textures,
        const VertexParameters &, const FragmentParameters &);
    static const char *entryPoint(Program);
private:
    SDL_GPUDevice *device;
    Shader vertex;
    std::array<std::unique_ptr<Shader>, static_cast<size_t>(Program::Count)> fragments;
    std::map<std::tuple<Program, Blend, SDL_GPUTextureFormat>, SDL_GPUGraphicsPipeline *> cache;
};
}
