#pragma once
#include "gpu-shader.h"
#include <memory>
namespace gpu {
// Only formats actually compiled and embedded are advertised to SDL.
SDL_GPUShaderFormat engineShaderFormats();
// RGSS owns the returned pipelines; the window-thread Device must outlive them.
std::unique_ptr<QuadPipeline> createEnginePipelines(SDL_GPUDevice *);
}
