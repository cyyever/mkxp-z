#ifndef MKXP_GPU_RESOURCES_H
#define MKXP_GPU_RESOURCES_H

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_pixels.h>
#include <cstddef>
#include <memory>
#include <span>

namespace gpu { class Texture; }
namespace gpu_resources { struct Access; }

// Handles share resource identity across copies. Mutation belongs to the active
// render thread; the Device must outlive every allocated texture handle.
// empty() distinguishes an unset binding from a released/unallocated resource.
namespace TEX {
struct Storage;
class ID {
public:
    ID() = default;
    explicit ID(int zero);
    SDL_GPUTexture *native() const;
    const gpu::Texture &resource() const;
    int width() const;
    int height() const;
    bool repeat() const;
    bool smooth() const;
    bool mipmaps() const;
    bool empty() const { return !storage; }
    bool valid() const;
    bool operator==(const ID &) const = default;
private:
    friend struct gpu_resources::Access;
    std::shared_ptr<Storage> storage;
};
ID gen();
void del(ID);
void bind(ID);
void unbind();
void uploadImage(int width, int height, const void *pixels,
                 SDL_PixelFormat format = SDL_PIXELFORMAT_RGBA32, int pitch = 0);
void uploadSubImage(int x, int y, int width, int height, const void *pixels,
                    SDL_PixelFormat format = SDL_PIXELFORMAT_RGBA32, int pitch = 0);
void allocEmpty(int width, int height);
void setRepeat(bool);
void setSmooth(bool);
void setSmooth(bool, bool mipmaps);
void markModified(const ID &);
void prepareSampling(const ID &);
}

namespace FBO {
struct Storage;
class ID {
public:
    ID() = default;
    explicit ID(int zero);
    TEX::ID texture() const;
    bool empty() const { return !storage; }
    bool valid() const;
    bool operator==(const ID &) const = default;
private:
    friend struct gpu_resources::Access;
    std::shared_ptr<Storage> storage;
};
extern thread_local ID boundFramebufferID;
ID gen();
void del(ID);
void bind(ID);
void unbind();
void setTarget(TEX::ID);
void clear();
}

namespace gpu_resources {
void resetContextBindings() noexcept;
}

struct TEXFBO {
    TEX::ID tex;
    FBO::ID fbo;
    int width = 0, height = 0;
    TEXFBO *selfHires = nullptr;
    bool operator==(const TEXFBO &other) const { return tex == other.tex && fbo == other.fbo; }
    static void init(TEXFBO &);
    static void allocEmpty(TEXFBO &, int width, int height);
    static void linkFBO(TEXFBO &);
    static void fini(TEXFBO &);
};

#endif
