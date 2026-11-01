#pragma once
#include <SDL3/SDL_gpu.h>
#include <array>
#include <map>
#include <memory>
#include <span>
#include <tuple>

namespace gpu {
enum class Program { Simple, FlatColor, VertexColor, VertexAlpha, UniformAlpha, Plane, Transition, SimpleTransition, BitmapBlend, Hue, Sprite, Tilemap, FlashMap, Gray, Blur, Invert, CompressAlpha, Subtract, Bicubic, Lanczos, ShadowHorizontal, ShadowVertical, Xbrz, Weather, Count };
enum class Blend { Disabled, Normal, Addition, Subtraction, KeepDestinationAlpha, ColorAdditionKeepDestinationAlpha, ColorSubtractionKeepDestinationAlpha };
struct QuadVertex { float position[2]; float texcoord[2]; float color[4]; };
struct alignas(16) VertexParameters {
    // Column-major matrices, matching the offline HLSL matrix layout.
    float projection[16];
    float transform[16];
    float textureScaleTranslation[4];
};
struct alignas(16) FragmentParameters { float color[4], tone[4], flash[4], parameters[4], subrect[4]; };
struct alignas(16) EffectParameters {
    float patternScaleOffset[4], spriteFlags[4], bush[4], filterSize[4], filterParameters[4];
    float blurDirection[4], shadowBounds[4], shadowDimensions[4], shadowSlopes[4];
};
struct alignas(16) TilemapParameters {
    int animation[4];
    int autotileFrames0[4];
    int autotileFrames1[4];
};
static_assert(sizeof(EffectParameters) == 144);
static_assert(sizeof(TilemapParameters) == 48);
static_assert(sizeof(QuadVertex) == 32);
static_assert(sizeof(VertexParameters) == 144);
static_assert(sizeof(FragmentParameters) == 80);

// The producer records the exported entrypoint rather than assuming that a
// cross compiler preserves the HLSL function name. Bytes stay alive through
// shader creation; Shader does not retain the borrowed descriptor.
struct ShaderCode {
    std::span<const Uint8> bytes;
    SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
    const char *entrypoint = nullptr;
};
// Select the first supplied variant accepted by the actual device. No runtime
// translation or renderer fallback is performed.
ShaderCode selectShaderCode(std::span<const ShaderCode>, SDL_GPUShaderFormat supported);

class Shader {
public:
    Shader(SDL_GPUDevice *, ShaderCode, SDL_GPUShaderStage, Uint32 samplers, Uint32 uniforms);
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
    using FragmentShaders = std::array<ShaderCode, static_cast<size_t>(Program::Count)>;
    QuadPipeline(SDL_GPUDevice *, ShaderCode vertexCode, const FragmentShaders &,
                 ShaderCode tilemapVertexCode = {});
    using FragmentBytecode = std::array<std::span<const Uint8>, static_cast<size_t>(Program::Count)>;
    QuadPipeline(SDL_GPUDevice *, std::span<const Uint8> vertexCode, const FragmentBytecode &,
                 std::span<const Uint8> tilemapVertexCode = {});
    ~QuadPipeline();
    QuadPipeline(const QuadPipeline &) = delete;
    QuadPipeline &operator=(const QuadPipeline &) = delete;
    SDL_GPUGraphicsPipeline *get(Program, Blend, SDL_GPUTextureFormat);
    void bind(SDL_GPUCommandBuffer *, SDL_GPURenderPass *, Program, Blend,
        SDL_GPUTextureFormat, const SDL_GPUBufferBinding &vertices,
        std::span<const SDL_GPUTextureSamplerBinding> textures,
        const VertexParameters &, const FragmentParameters &,
        const EffectParameters &effects = {}, const TilemapParameters &tilemap = {});
    static const char *entryPoint(Program);
    static Uint32 samplerCount(Program);
private:
    void loadWeatherShader();
    SDL_GPUDevice *device;
    Shader vertex;
    std::unique_ptr<Shader> tilemapVertex;
    std::array<std::unique_ptr<Shader>, static_cast<size_t>(Program::Count)> fragments;
    std::map<std::tuple<Program, Blend, SDL_GPUTextureFormat>, SDL_GPUGraphicsPipeline *> cache;
};
}
