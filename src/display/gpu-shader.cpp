#include "gpu-shader.h"
#include "util/json5pp.hpp"
#include <filesystem>
#include <fstream>
#include <cstddef>
#include <bit>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace gpu {
namespace {
constexpr std::array names = {"simpleMain", "flatColorMain", "vertexColorMain", "vertexAlphaMain", "uniformAlphaMain", "planeMain", "transitionMain", "simpleTransitionMain", "bitmapBlendMain", "hueMain", "spriteMain", "tilemapMain", "flashMapMain", "grayMain", "blurMain", "invertMain", "compressAlphaMain", "subtractMain", "bicubicMain", "lanczosMain", "shadowHorizontalMain", "shadowVerticalMain", "xbrzMain", "weatherMain"};
constexpr std::array<Uint32, 24> samplers = {1,0,0,1,1,1,3,2,2,1,2,1,0,1,1,1,1,2,1,1,1,1,1,0};
constexpr std::array<Uint32, 24> uniforms = {0,1,0,0,1,1,1,1,1,1,2,1,1,1,2,0,0,1,2,2,2,2,2,1};
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
ShaderCode selectShaderCode(std::span<const ShaderCode> variants, SDL_GPUShaderFormat supported) {
    for (const auto &code : variants)
        if (!code.bytes.empty() && (code.format & supported)) return code;
    throw std::invalid_argument("No embedded shader variant matches the GPU device formats");
}
Shader::Shader(SDL_GPUDevice *d, std::span<const Uint8> bytes, const char *entry,
               SDL_GPUShaderStage stage, Uint32 samplerCount, Uint32 uniformCount)
    : Shader(d, ShaderCode{bytes, SDL_GPU_SHADERFORMAT_DXBC, entry}, stage, samplerCount, uniformCount) {}
Shader::Shader(SDL_GPUDevice *d, ShaderCode code, SDL_GPUShaderStage stage,
               Uint32 samplerCount, Uint32 uniformCount) : device(d), shader(nullptr) {
    constexpr auto known = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXBC |
        SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB;
    if (!device || code.bytes.empty() || !code.entrypoint || !*code.entrypoint ||
        !std::has_single_bit(code.format) || (code.format & ~known))
        throw std::invalid_argument("GPU shader requires a single format, bytecode and exported entrypoint");
    if (!(SDL_GetGPUShaderFormats(device) & code.format))
        throw std::invalid_argument("GPU device does not accept the supplied shader format");
    SDL_GPUShaderCreateInfo info{};
    info.code = code.bytes.data(); info.code_size = code.bytes.size(); info.entrypoint = code.entrypoint;
    info.format = code.format; info.stage = stage;
    std::vector<Uint32> alignedSpirv;
    if (code.format == SDL_GPU_SHADERFORMAT_DXBC || code.format == SDL_GPU_SHADERFORMAT_DXIL) {
        if (code.bytes.size() < 4 || std::memcmp(code.bytes.data(), "DXBC", 4))
            throw std::invalid_argument("Direct3D shader requires a DXBC container");
    } else if (code.format == SDL_GPU_SHADERFORMAT_SPIRV) {
        const Uint8 magic[] = {3,2,35,7};
        if (code.bytes.size() < 20 || code.bytes.size() % sizeof(Uint32) ||
            std::memcmp(code.bytes.data(), magic, sizeof(magic)))
            throw std::invalid_argument("Vulkan shader requires a SPIR-V module");
        // Embedded uint8 arrays do not promise Vulkan's Uint32 alignment.
        alignedSpirv.resize(code.bytes.size() / sizeof(Uint32));
        std::memcpy(alignedSpirv.data(), code.bytes.data(), code.bytes.size());
        info.code = reinterpret_cast<const Uint8 *>(alignedSpirv.data());
    }
    info.num_samplers = samplerCount; info.num_uniform_buffers = uniformCount;
    shader = SDL_CreateGPUShader(device, &info);
    if (!shader) fail("SDL_CreateGPUShader");
}
Shader::~Shader() { SDL_ReleaseGPUShader(device, shader); }
const char *QuadPipeline::entryPoint(Program program) { return names[index(program)]; }
Uint32 QuadPipeline::samplerCount(Program program) { return samplers[index(program)]; }
namespace {
QuadPipeline::FragmentShaders dxbcFragments(const QuadPipeline::FragmentBytecode &bytes) {
    QuadPipeline::FragmentShaders result{};
    for (size_t i=0;i<result.size();++i)
        result[i] = {bytes[i], SDL_GPU_SHADERFORMAT_DXBC, names[i]};
    return result;
}
}
QuadPipeline::QuadPipeline(SDL_GPUDevice *d, std::span<const Uint8> vertexCode,
    const FragmentBytecode &code, std::span<const Uint8> tilemapVertexCode)
    : QuadPipeline(d, ShaderCode{vertexCode,SDL_GPU_SHADERFORMAT_DXBC,"vertexMain"},
        dxbcFragments(code), ShaderCode{tilemapVertexCode,SDL_GPU_SHADERFORMAT_DXBC,"tilemapVertexMain"}) {}
QuadPipeline::QuadPipeline(SDL_GPUDevice *d, ShaderCode vertexCode,
    const FragmentShaders &code, ShaderCode tilemapVertexCode)
    : device(d), vertex(d, vertexCode, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1) {
    if (!tilemapVertexCode.bytes.empty())
        tilemapVertex = std::make_unique<Shader>(device, tilemapVertexCode, SDL_GPU_SHADERSTAGE_VERTEX, 0, 2);
    for (size_t i=0;i<fragments.size();++i)
        if (!code[i].bytes.empty())
            fragments[i] = std::make_unique<Shader>(device, code[i], SDL_GPU_SHADERSTAGE_FRAGMENT, samplers[i], uniforms[i]);
}
QuadPipeline::~QuadPipeline() {
    for (auto &[key, pipeline] : cache) SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
}
void QuadPipeline::loadWeatherShader() {
    namespace fs = std::filesystem;
    const auto readFile = [](const fs::path &path, size_t limit) {
        const auto size = fs::file_size(path);
        if (!size || size > limit) throw std::runtime_error("Invalid game weather shader file size: " + path.generic_string());
        std::ifstream file(path, std::ios::binary);
        std::vector<Uint8> bytes(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            throw std::runtime_error("Cannot read game weather shader file: " + path.generic_string());
        return bytes;
    };
    try {
        const auto manifestBytes = readFile("shaders/weather.json", 64*1024);
        const auto manifest = json5pp::parse5(std::string(manifestBytes.begin(),manifestBytes.end()));
        if (manifest["version"].as_integer() != 1)
            throw std::runtime_error("Unsupported game weather shader manifest version");
        const auto supported = SDL_GetGPUShaderFormats(device);
        for (const auto &variant : manifest["variants"].as_array()) {
            const auto &name = variant["format"].as_string();
            const auto format = name=="dxbc" ? SDL_GPU_SHADERFORMAT_DXBC :
                name=="dxil" ? SDL_GPU_SHADERFORMAT_DXIL :
                name=="spirv" ? SDL_GPU_SHADERFORMAT_SPIRV :
                name=="msl" ? SDL_GPU_SHADERFORMAT_MSL : SDL_GPU_SHADERFORMAT_INVALID;
            if (format == SDL_GPU_SHADERFORMAT_INVALID)
                throw std::runtime_error("Unknown game weather shader format: " + name);
            if (!(supported & format)) continue;
            const auto path = fs::u8path(variant["file"].as_string());
            if (path.empty() || path.is_absolute() || path.has_root_name())
                throw std::runtime_error("Game weather shader file must be game-relative");
            for (const auto &component : path)
                if (component == "..") throw std::runtime_error("Game weather shader file cannot traverse parent directories");
            const auto bytes = readFile(path,16*1024*1024);
            const auto &entry = variant["entrypoint"].as_string();
            if (entry.empty()) throw std::runtime_error("Game weather shader entrypoint is empty");
            fragments[index(Program::Weather)] = std::make_unique<Shader>(device,
                ShaderCode{bytes,format,entry.c_str()},SDL_GPU_SHADERSTAGE_FRAGMENT,0,1);
            return;
        }
        throw std::runtime_error("Game weather shader has no variant supported by this GPU device");
    } catch (const std::exception &error) {
        throw std::runtime_error(std::string("Game weather shader: ")+error.what());
    }
}
SDL_GPUGraphicsPipeline *QuadPipeline::get(Program program, Blend blend, SDL_GPUTextureFormat format) {
    const auto i = index(program);
    if (program == Program::Weather && !fragments[i]) loadWeatherShader();
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
