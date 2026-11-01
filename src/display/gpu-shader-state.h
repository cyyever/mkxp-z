#pragma once
#include "gpu-shader.h"
#include "gpu-resources.h"
#include <array>

namespace gpu {
// ShaderSet instances retain their state between binds. RenderContext observes
// the active object until draw() snapshots it into FrameEncoder; setters after
// bind therefore affect the next draw, without issuing graphics API calls.
struct ShaderState {
    Program program = Program::Simple;
    VertexParameters vertex{};
    FragmentParameters fragment{};
    EffectParameters effects{};
    TilemapParameters tilemap{};
    // Empty handles fall back to the context's bound primary texture. This also
    // supplies a valid unused pattern binding when sprite patterns are disabled.
    // Handles are resolved per draw, so deleted storage cannot leave stale pointers.
    std::array<TEX::ID,3> textures{};
};
// Implemented by the production RenderContext; adapter tests provide stubs.
void bindShaderState(const ShaderState *);
std::array<int,2> shaderProjectionSize(bool framebufferScalingAllowed);
}
