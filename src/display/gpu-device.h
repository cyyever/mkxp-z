#pragma once

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_video.h>
#include <span>
#include <mutex>
#include <thread>
#include <vector>

namespace gpu {

// Resource and command wrappers must be destroyed before their Device. Passes
// opened through native SDL APIs must end before submitting a CommandBuffer.
class Texture {
public:
    Texture() = default;
    ~Texture();
    Texture(Texture &&) noexcept;
    Texture &operator=(Texture &&) noexcept;
    Texture(const Texture &) = delete;
    Texture &operator=(const Texture &) = delete;
    SDL_GPUTexture *native() const { return texture; }
    Uint32 width() const { return w; }
    Uint32 height() const { return h; }
    Uint32 mipLevels() const { return levels; }
private:
    friend class Device;
    Texture(SDL_GPUDevice *, SDL_GPUTexture *, Uint32, Uint32, Uint32, SDL_GPUTextureUsageFlags);
    SDL_GPUDevice *device = nullptr;
    SDL_GPUTexture *texture = nullptr;
    Uint32 w = 0, h = 0, levels = 0;
    SDL_GPUTextureUsageFlags usage = 0;
};

class Fence {
public:
    Fence() = default;
    ~Fence();
    Fence(Fence &&) noexcept;
    Fence &operator=(Fence &&) noexcept;
    Fence(const Fence &) = delete;
    Fence &operator=(const Fence &) = delete;
    // Explicit completion fences finish before release, including on unwind.
    void wait() const;
private:
    friend class CommandBuffer;
    void release() noexcept;
    Fence(SDL_GPUDevice *, SDL_GPUFence *);
    SDL_GPUDevice *device = nullptr;
    SDL_GPUFence *fence = nullptr;
};

class CommandBuffer {
public:
    explicit CommandBuffer(SDL_GPUDevice *);
    ~CommandBuffer();
    CommandBuffer(const CommandBuffer &) = delete;
    CommandBuffer &operator=(const CommandBuffer &) = delete;
    SDL_GPUCommandBuffer *native() const { return command; }
    void submit();
    void submitAndWait();
    [[nodiscard]] Fence submitWithFence();
private:
    friend class Device;
    friend class RenderPass;
    void checkThread() const;
    SDL_GPUDevice *device;
    SDL_GPUCommandBuffer *command;
    std::thread::id thread;
    bool acquiredSwapchain = false;
};

class Device;

// SDL's debug render-pass state is device-wide. Keep recording one pass at a
// time across the window and RGSS threads; submission and GPU waits stay outside.
class RenderPass {
public:
    RenderPass(Device &, CommandBuffer &, const SDL_GPUColorTargetInfo &);
    ~RenderPass();
    RenderPass(const RenderPass &) = delete;
    RenderPass &operator=(const RenderPass &) = delete;
    SDL_GPURenderPass *native() const { return pass; }
private:
    std::unique_lock<std::mutex> lock;
    SDL_GPURenderPass *pass = nullptr;
};

class Device {
public:
    // Claim/release/acquireFrame belong to the SDL window's creation thread.
    // Offscreen commands and texture transfers may run on the RGSS worker.
    explicit Device(SDL_Window *, SDL_GPUShaderFormat, bool debug = false,
                    const char *driver = nullptr);
    ~Device();
    Device(const Device &) = delete;
    Device &operator=(const Device &) = delete;
    SDL_GPUDevice *native() const { return device; }
    SDL_GPUTextureFormat swapchainFormat() const;
    Texture createTexture(Uint32 width, Uint32 height,
        SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
        Uint32 mipLevels = 1);
    // Tight RGBA8 rows, top to bottom. These initial transfer paths wait for
    // completion; renderer hot paths should batch native SDL copy passes.
    void uploadRGBA(const Texture &, std::span<const Uint8>);
    std::vector<Uint8> downloadRGBA(const Texture &, Uint32 mipLevel = 0);
    void generateMipmaps(const Texture &);
    void blit(CommandBuffer &, const SDL_GPUBlitInfo &);
    void copy(CommandBuffer &, const Texture &source, const Texture &destination);
    // Returns null when minimized. The texture is borrowed until submission;
    // never sample/download it. It is owned by the SDL swapchain.
    SDL_GPUTexture *acquireFrame(CommandBuffer &, Uint32 &width, Uint32 &height);
private:
    friend class RenderPass;
    void checkWindowThread() const;
    void checkTexture(const Texture &) const;
    SDL_GPUDevice *device = nullptr;
    SDL_Window *window;
    std::thread::id windowThread;
    std::mutex renderPassMutex;
};
}
