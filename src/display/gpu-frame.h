#pragma once
#include "gpu-device.h"
#include "gpu-shader.h"
#include <array>
#include <optional>
#include <vector>

namespace gpu {
// Borrowed until submit: never use a swapchain texture acquired on another
// command buffer. Current Bitmap/Device textures use RGBA8; other formats are
// explicit so the pipeline cache matches the attachment format.
class TextureView {
public:
    TextureView(SDL_GPUTexture *texture, Uint32 width, Uint32 height,
                SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM)
        : texture(texture), w(width), h(height), format(format) {}
    explicit TextureView(const Texture &texture)
        : TextureView(texture.native(), texture.width(), texture.height()) {}
    SDL_GPUTexture *native() const { return texture; }
    Uint32 width() const { return w; }
    Uint32 height() const { return h; }
    SDL_GPUTextureFormat pixelFormat() const { return format; }
private:
    SDL_GPUTexture *texture;
    Uint32 w, h;
    SDL_GPUTextureFormat format;
};
struct DrawState {
    Blend blend = Blend::Normal;
    // Empty viewport/scissor mean the full target, resolved when recording.
    std::optional<SDL_GPUViewport> viewport;
    std::optional<SDL_Rect> scissor;
};

// One encoder belongs to the RGSS render thread. Device, pipelines and sampled
// or target textures outlive submit(). SDL defers native resource destruction
// for submitted commands; wait the returned fence before CPU readback/reuse
// requiring completion. Uploads are batched, never performed during a render pass.
class FrameEncoder {
public:
    FrameEncoder(Device &, QuadPipeline &);
    ~FrameEncoder();
    FrameEncoder(const FrameEncoder &) = delete;
    FrameEncoder &operator=(const FrameEncoder &) = delete;
    void beginTarget(TextureView, SDL_GPULoadOp load, SDL_FColor clear = {});
    void draw(Program, std::span<const QuadVertex>,
              std::span<const SDL_GPUTextureSamplerBinding>,
              const VertexParameters &, const FragmentParameters &,
              const DrawState & = {}, const EffectParameters & = {},
              const TilemapParameters & = {});
    [[nodiscard]] Fence submit();
    // Discards only recorded CPU commands; submitted work remains unaffected.
    void reset();
private:
    struct Draw {
        Program program;
        Uint32 first, count, samplerCount;
        std::array<SDL_GPUTextureSamplerBinding, 3> samplers{};
        VertexParameters vertex;
        FragmentParameters fragment;
        EffectParameters effects;
        TilemapParameters tilemap;
        Blend blend;
        SDL_GPUViewport viewport;
        SDL_Rect scissor;
    };
    struct Batch {
        TextureView target;
        SDL_GPULoadOp load;
        SDL_FColor clear;
        std::vector<Draw> draws;
    };
    void ensureBuffer(Uint32 bytes);
    Device &device;
    QuadPipeline &pipelines;
    std::vector<QuadVertex> vertices;
    std::vector<Batch> batches;
    SDL_GPUBuffer *vertexBuffer = nullptr;
    SDL_GPUTransferBuffer *transferBuffer = nullptr;
    Uint32 capacity = 0;
};
}
