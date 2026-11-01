#pragma once

#include "gpu-device.h"
#include "gpu-shader.h"
#include <SDL3/SDL_rect.h>
#include <array>

namespace gpu {

// RGSS bitmap operations use offscreen RGBA textures. All coordinates here are
// physical pixels; the existing Bitmap layer retains logical/hires mapping.
class BitmapRenderer {
public:
    BitmapRenderer(Device &, QuadPipeline &);
    ~BitmapRenderer();
    BitmapRenderer(const BitmapRenderer &) = delete;
    BitmapRenderer &operator=(const BitmapRenderer &) = delete;

    void clear(CommandBuffer &, const Texture &, SDL_FColor = {});
    void fill(CommandBuffer &, const Texture &, SDL_FRect, SDL_FColor);
    void blit(CommandBuffer &, const Texture &destination, SDL_FRect destinationRect,
              const Texture &source, SDL_FRect sourceRect, bool linear = false);
    // RMXP bitmap alpha composition samples a snapshot of the destination.
    // Callers retain temporary textures until after the command is submitted.
    [[nodiscard]] Texture blend(CommandBuffer &, const Texture &destination, SDL_FRect destinationRect,
                  const Texture &source, SDL_FRect sourceRect, float opacity,
                  bool linear = false);
    [[nodiscard]] Texture hue(CommandBuffer &, const Texture &, float normalizedRotation);

private:
    void draw(CommandBuffer &, const Texture &, SDL_FRect, SDL_FRect, Program,
              std::span<const SDL_GPUTextureSamplerBinding>, const FragmentParameters &);
    Device &device;
    QuadPipeline &pipelines;
    SDL_GPUBuffer *vertices = nullptr;
    SDL_GPUTransferBuffer *transfer = nullptr;
    SDL_GPUSampler *nearest = nullptr, *linear = nullptr;
};
}
