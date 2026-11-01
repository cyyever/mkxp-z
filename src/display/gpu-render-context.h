#pragma once

#include "gpu-frame.h"
#include "gpu-resources.h"
#include "gpu-shader-state.h"
#include <functional>
#include <utility>
#include <unordered_map>

namespace gpu {

// RGSS-thread state shared by engine shaders, CPU geometry and texture adapters.
// SDL window presentation remains on the window thread through FrameHandoff.
class RenderContext {
public:
    RenderContext(Device &, QuadPipeline &, int width, int height);
    ~RenderContext();
    RenderContext(const RenderContext &) = delete;
    RenderContext &operator=(const RenderContext &) = delete;
    static RenderContext &current();
    static void install(RenderContext *);

    Device &device() { return gpuDevice; }
    void setShader(const ShaderState *value) { shader = value; }
    void bindTexture(TEX::ID value) { primaryTexture = std::move(value); }
    void setTarget(TextureView);
    void setDefaultTarget(TextureView);
    void bindDefaultTarget();
    void setViewport(SDL_Rect);
    SDL_Rect viewport() const { return viewportRect; }
    void setScissor(std::optional<SDL_Rect> value) { state.scissor = value; }
    void setBlend(Blend value) { state.blend = value; }
    void setClearColor(SDL_FColor value) { clearColor = value; }
    void setMipmapsEnabled(bool value) { mipmapsEnabled = value; }
    bool mipmapsAllowed() const { return mipmapsEnabled; }
    void copyTexture(const TEX::ID &source, const TEX::ID &destination);
    void clear();
    void draw(std::span<const QuadVertex>);
    [[nodiscard]] Fence submit();
    void flush();
    // Preserve the existing per-shader hires/ping-pong decision at the engine
    // boundary, without coupling GPU primitives to global SharedState.
    using ProjectionResolver = std::function<std::array<int,2>(bool)>;
    void setProjectionResolver(ProjectionResolver value) { projectionResolver = std::move(value); }
    std::array<int,2> projectionSize(bool scalingAllowed) const;

private:
    void ensureTarget();
    SDL_GPUSampler *sampler(const TEX::ID &);
    Device &gpuDevice;
    FrameEncoder encoder;
    const ShaderState *shader = nullptr;
    TEX::ID primaryTexture;
    std::optional<TextureView> target, defaultTarget;
    DrawState state;
    SDL_Rect viewportRect;
    SDL_FColor clearColor{};
    std::array<SDL_GPUSampler *, 8> samplers{};
    ProjectionResolver projectionResolver;
    std::unordered_map<SDL_GPUTexture *, TEX::ID> pendingResources;
    bool recording = false;
    bool targetNeedsBegin = false;
    bool mipmapsEnabled = false;
};

Device &renderDevice();
void flushRenderCommands();
void bindPrimaryTexture(TEX::ID);
void setRenderTarget(TextureView);
void bindDefaultRenderTarget();
void clearRenderTarget();
bool textureMipmapsEnabled();
void bindShaderState(const ShaderState *);
std::array<int,2> shaderProjectionSize(bool scalingAllowed);
void drawVertices(std::span<const QuadVertex>);
}
