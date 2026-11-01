#include "gpu-embedded.h"
#include "gpu-embedded-shaders.h"
#include <stdexcept>
#include <string>

namespace gpu {
SDL_GPUShaderFormat engineShaderFormats() { return embedded::formats; }
namespace {
ShaderCode findCode(std::string_view semantic, SDL_GPUShaderFormat supported) {
    for (const auto &entry : embedded::shaders)
        if (entry.semantic == semantic) return selectShaderCode(entry.variants, supported);
    throw std::logic_error(std::string("Missing embedded GPU shader: ") + std::string(semantic));
}
}
std::unique_ptr<QuadPipeline> createEnginePipelines(SDL_GPUDevice *device) {
    if (!device) throw std::invalid_argument("Engine pipelines require a GPU device");
    const auto supported = SDL_GetGPUShaderFormats(device);
    QuadPipeline::FragmentShaders fragments{};
    for (size_t i=0;i<fragments.size();++i) {
        const auto program = static_cast<Program>(i);
#ifndef MKXPZ_HAVE_EXTRA_SHADERS
        if (program == Program::Xbrz) continue;
#endif
        fragments[i] = findCode(QuadPipeline::entryPoint(program), supported);
    }
    return std::make_unique<QuadPipeline>(device, findCode("vertexMain", supported), fragments,
        findCode("tilemapVertexMain", supported));
}
}
