#pragma once
#include <SDL3/SDL_gpu.h>
// Unit-test resource identity only. Real resource ownership/deletion is covered
// by native renderer tests; the shader adapter only copies this public handle.
namespace TEX {
struct ID {
    ID() = default;
    explicit ID(SDL_GPUTexture *value) : value(value) {}
    SDL_GPUTexture *native() const { return value; }
private:
    SDL_GPUTexture *value = nullptr;
};
}
