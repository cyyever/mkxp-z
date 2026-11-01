#include "gpu-shader.h"
#include <cstddef>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

namespace gpu {
namespace {
constexpr std::array names = {"simpleMain", "flatColorMain", "vertexColorMain", "vertexAlphaMain", "uniformAlphaMain", "planeMain", "transitionMain", "simpleTransitionMain", "bitmapBlendMain", "hueMain", "spriteMain", "tilemapMain", "flashMapMain", "grayMain", "blurMain", "invertMain", "compressAlphaMain", "subtractMain", "bicubicMain", "lanczosMain", "shadowHorizontalMain", "shadowVerticalMain", "xbrzMain"};
constexpr std::array<Uint32, 23> samplers = {1,0,0,1,1,1,3,2,2,1,2,1,0,1,1,1,1,2,1,1,1,1,1};
constexpr std::array<Uint32, 23> uniforms = {0,1,0,0,1,1,1,1,1,1,2,1,1,1,2,0,0,1,2,2,2,2,2};
size_t index(Program program) {
    const auto value = static_cast<size_t>(program);
    if (value >= names.size()) throw std::invalid_argument("Invalid GPU quad program");
    return value;
}
[[noreturn]] void fail(const char *operation) { throw std::runtime_error(std::string(operation) + ": " + SDL_GetError()); }
SDL_GPUColorTargetBlendState blendState(Blend mode) {
    SDL_GPUColorTargetBlendState state{};
    state.enable_blend = mode != Blend::Disabled;
    state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    switch (mode) {
    case Blend::Disabled: case Blend::Normal: break;
    case Blend::Addition:
        state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE; break;
    case Blend::Subtraction:
        state.color_blend_op = SDL_GPU_BLENDOP_REVERSE_SUBTRACT;
        state.alpha_blend_op = SDL_GPU_BLENDOP_REVERSE_SUBTRACT;
        state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE; break;
    case Blend::ColorAdditionKeepDestinationAlpha:
    case Blend::ColorSubtractionKeepDestinationAlpha:
        state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        state.color_blend_op = mode == Blend::ColorAdditionKeepDestinationAlpha
            ? SDL_GPU_BLENDOP_ADD : SDL_GPU_BLENDOP_REVERSE_SUBTRACT;
        break;
    case Blend::KeepDestinationAlpha:
        state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE; break;
    default: throw std::invalid_argument("Invalid GPU blend mode");
    }
    return state;
}
}
Shader::Shader(SDL_GPUDevice *d, std::span<const Uint8> code, const char *entry,
               SDL_GPUShaderStage stage, Uint32 samplerCount, Uint32 uniformCount) : device(d), shader(nullptr) {
    if (code.size() < 4 || std::memcmp(code.data(), "DXBC", 4))
        throw std::invalid_argument("GPU shader requires offline DXBC bytecode");
    SDL_GPUShaderCreateInfo info{};
    info.code = code.data(); info.code_size = code.size(); info.entrypoint = entry;
    info.format = SDL_GPU_SHADERFORMAT_DXBC; info.stage = stage;
    info.num_samplers = samplerCount; info.num_uniform_buffers = uniformCount;
    shader = SDL_CreateGPUShader(device, &info);
    if (!shader) fail("SDL_CreateGPUShader");
}
Shader::~Shader() { SDL_ReleaseGPUShader(device, shader); }
const char *QuadPipeline::entryPoint(Program program) { return names[index(program)]; }
Uint32 QuadPipeline::samplerCount(Program program) { return samplers[index(program)]; }
QuadPipeline::QuadPipeline(SDL_GPUDevice *d, std::span<const Uint8> vertexCode, const FragmentBytecode &code, std::span<const Uint8> tilemapVertexCode)
    : device(d), vertex(d, vertexCode, "vertexMain", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1) {
    if (!tilemapVertexCode.empty())
        tilemapVertex = std::make_unique<Shader>(device, tilemapVertexCode, "tilemapVertexMain", SDL_GPU_SHADERSTAGE_VERTEX, 0, 2);
    for (size_t i = 0; i < fragments.size(); ++i)
        if (!code[i].empty()) fragments[i] = std::make_unique<Shader>(device, code[i], names[i], SDL_GPU_SHADERSTAGE_FRAGMENT, samplers[i], uniforms[i]);
}
QuadPipeline::~QuadPipeline() {
    for (auto &[key, pipeline] : cache) SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
}
SDL_GPUGraphicsPipeline *QuadPipeline::get(Program program, Blend blend, SDL_GPUTextureFormat format) {
    const auto i = index(program);
    if (!fragments[i]) throw std::logic_error("GPU effect bytecode was not supplied");
    const auto key = std::tuple(program, blend, format);
    if (const auto found = cache.find(key); found != cache.end()) return found->second;
    SDL_GPUVertexBufferDescription buffer{};
    buffer.slot = 0; buffer.pitch = sizeof(QuadVertex); buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    const SDL_GPUVertexAttribute attributes[] = {
        {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(QuadVertex, position)},
        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(QuadVertex, texcoord)},
        {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(QuadVertex, color)},
    };
    SDL_GPUColorTargetDescription target{};
    target.format = format; target.blend_state = blendState(blend);
    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = program == Program::Tilemap && tilemapVertex ? tilemapVertex->native() : vertex.native(); info.fragment_shader = fragments[i]->native();
    info.vertex_input_state.vertex_buffer_descriptions = &buffer;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes;
    info.vertex_input_state.num_vertex_attributes = 3;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.target_info.color_target_descriptions = &target; info.target_info.num_color_targets = 1;
    auto *pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
    if (!pipeline) fail("SDL_CreateGPUGraphicsPipeline");
    try { cache.emplace(key, pipeline); }
    catch (...) { SDL_ReleaseGPUGraphicsPipeline(device, pipeline); throw; }
    return pipeline;
}
void QuadPipeline::bind(SDL_GPUCommandBuffer *command, SDL_GPURenderPass *pass,
    Program program, Blend blend, SDL_GPUTextureFormat format, const SDL_GPUBufferBinding &vertices,
    std::span<const SDL_GPUTextureSamplerBinding> textures, const VertexParameters &vp, const FragmentParameters &fp, const EffectParameters &effects, const TilemapParameters &tilemap) {
    const auto i = index(program);
    if (program == Program::Tilemap && tilemapVertex && tilemap.animation[0] < 0)
        throw std::invalid_argument("Tilemap animation index must be nonnegative");
    if (textures.size() != samplers[i]) throw std::invalid_argument("GPU quad sampler count mismatch");
    SDL_BindGPUGraphicsPipeline(pass, get(program, blend, format));
    SDL_BindGPUVertexBuffers(pass, 0, &vertices, 1);
    SDL_PushGPUVertexUniformData(command, 0, &vp, sizeof(vp));
    if (program == Program::Tilemap && tilemapVertex) SDL_PushGPUVertexUniformData(command, 1, &tilemap, sizeof(tilemap));
    if (uniforms[i] > 1) SDL_PushGPUFragmentUniformData(command, 1, &effects, sizeof(effects));
    if (uniforms[i]) SDL_PushGPUFragmentUniformData(command, 0, &fp, sizeof(fp));
    if (!textures.empty()) SDL_BindGPUFragmentSamplers(pass, 0, textures.data(), static_cast<Uint32>(textures.size()));
}
}
