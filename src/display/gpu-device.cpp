#include "gpu-device.h"

#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace gpu {
namespace {
[[noreturn]] void fail(const char *operation) {
    throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}
Uint32 byteSize(Uint32 w, Uint32 h) {
    if (!w || !h || Uint64(w) * h > std::numeric_limits<Uint32>::max() / 4)
        throw std::invalid_argument("Invalid RGBA texture dimensions");
    return w * h * 4;
}
struct TransferDeleter {
    SDL_GPUDevice *device;
    void operator()(SDL_GPUTransferBuffer *p) const { SDL_ReleaseGPUTransferBuffer(device, p); }
};
using Transfer = std::unique_ptr<SDL_GPUTransferBuffer, TransferDeleter>;
Transfer transfer(SDL_GPUDevice *device, Uint32 size, SDL_GPUTransferBufferUsage usage) {
    SDL_GPUTransferBufferCreateInfo info{};
    info.usage = usage;
    info.size = size;
    Transfer result(SDL_CreateGPUTransferBuffer(device, &info), {device});
    if (!result) fail("SDL_CreateGPUTransferBuffer");
    return result;
}
SDL_GPUTextureRegion region(const Texture &texture) {
    SDL_GPUTextureRegion result{};
    result.texture = texture.native();
    result.w = texture.width(); result.h = texture.height(); result.d = 1;
    return result;
}
}

Texture::Texture(SDL_GPUDevice *d, SDL_GPUTexture *t, Uint32 width, Uint32 height)
    : device(d), texture(t), w(width), h(height) {}
Texture::~Texture() { if (texture) SDL_ReleaseGPUTexture(device, texture); }
Texture::Texture(Texture &&other) noexcept { *this = std::move(other); }
Texture &Texture::operator=(Texture &&other) noexcept {
    if (this != &other) {
        if (texture) SDL_ReleaseGPUTexture(device, texture);
        device = other.device;
        texture = std::exchange(other.texture, nullptr);
        w = other.w; h = other.h;
    }
    return *this;
}
CommandBuffer::CommandBuffer(SDL_GPUDevice *d)
    : device(d), command(SDL_AcquireGPUCommandBuffer(d)), thread(std::this_thread::get_id()) {
    if (!command) fail("SDL_AcquireGPUCommandBuffer");
}
void CommandBuffer::checkThread() const {
    if (thread != std::this_thread::get_id())
        throw std::logic_error("GPU command buffer used on a different thread");
    if (!command) throw std::logic_error("GPU command buffer already submitted");
}
CommandBuffer::~CommandBuffer() {
    if (!command) return;
    // SDL forbids cancellation once a swapchain texture has been acquired.
    if (acquiredSwapchain) SDL_SubmitGPUCommandBuffer(command);
    else SDL_CancelGPUCommandBuffer(command);
}
void CommandBuffer::submit() {
    checkThread();
    auto *pending = std::exchange(command, nullptr);
    if (!SDL_SubmitGPUCommandBuffer(pending)) fail("SDL_SubmitGPUCommandBuffer");
}
void CommandBuffer::submitAndWait() {
    checkThread();
    auto *pending = std::exchange(command, nullptr);
    auto *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(pending);
    if (!fence) fail("SDL_SubmitGPUCommandBufferAndAcquireFence");
    const bool success = SDL_WaitForGPUFences(device, true, &fence, 1);
    SDL_ReleaseGPUFence(device, fence);
    if (!success) fail("SDL_WaitForGPUFences");
}
Device::Device(SDL_Window *w, SDL_GPUShaderFormat formats, bool debug, const char *driver)
    : window(w), windowThread(std::this_thread::get_id()) {
    if (!window) throw std::invalid_argument("GPU device requires a window");
    device = SDL_CreateGPUDevice(formats, debug, driver);
    if (!device) fail("SDL_CreateGPUDevice");
    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        const std::string error = SDL_GetError();
        SDL_DestroyGPUDevice(device); device = nullptr;
        throw std::runtime_error("SDL_ClaimWindowForGPUDevice: " + error);
    }
}
Device::~Device() {
    SDL_WaitForGPUIdle(device);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
}
void Device::checkWindowThread() const {
    if (windowThread != std::this_thread::get_id())
        throw std::logic_error("GPU window operation requires the window creation thread");
}
SDL_GPUTextureFormat Device::swapchainFormat() const {
    checkWindowThread();
    return SDL_GetGPUSwapchainTextureFormat(device, window);
}
void Device::checkTexture(const Texture &texture) const {
    if (texture.device != device || !texture.native())
        throw std::invalid_argument("GPU texture belongs to a different device or was moved");
}
Texture Device::createTexture(Uint32 w, Uint32 h, SDL_GPUTextureUsageFlags usage) {
    byteSize(w, h);
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = usage;
    info.width = w; info.height = h;
    info.layer_count_or_depth = 1; info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    auto *texture = SDL_CreateGPUTexture(device, &info);
    if (!texture) fail("SDL_CreateGPUTexture");
    return Texture(device, texture, w, h);
}
void Device::uploadRGBA(const Texture &texture, std::span<const Uint8> pixels) {
    checkTexture(texture);
    const auto size = byteSize(texture.width(), texture.height());
    if (pixels.size() != size) throw std::invalid_argument("RGBA upload size does not match texture");
    auto buffer = transfer(device, size, SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD);
    void *mapped = SDL_MapGPUTransferBuffer(device, buffer.get(), false);
    if (!mapped) fail("SDL_MapGPUTransferBuffer");
    std::memcpy(mapped, pixels.data(), size);
    SDL_UnmapGPUTransferBuffer(device, buffer.get());
    CommandBuffer command(device);
    auto *pass = SDL_BeginGPUCopyPass(command.native());
    if (!pass) fail("SDL_BeginGPUCopyPass");
    SDL_GPUTextureTransferInfo src{}; src.transfer_buffer = buffer.get();
    const auto dst = region(texture);
    SDL_UploadToGPUTexture(pass, &src, &dst, false);
    SDL_EndGPUCopyPass(pass);
    command.submitAndWait();
}
std::vector<Uint8> Device::downloadRGBA(const Texture &texture) {
    checkTexture(texture);
    const auto size = byteSize(texture.width(), texture.height());
    auto buffer = transfer(device, size, SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD);
    CommandBuffer command(device);
    auto *pass = SDL_BeginGPUCopyPass(command.native());
    if (!pass) fail("SDL_BeginGPUCopyPass");
    const auto src = region(texture);
    SDL_GPUTextureTransferInfo dst{}; dst.transfer_buffer = buffer.get();
    SDL_DownloadFromGPUTexture(pass, &src, &dst);
    SDL_EndGPUCopyPass(pass);
    command.submitAndWait();
    std::vector<Uint8> pixels(size);
    void *mapped = SDL_MapGPUTransferBuffer(device, buffer.get(), false);
    if (!mapped) fail("SDL_MapGPUTransferBuffer");
    std::memcpy(pixels.data(), mapped, size);
    SDL_UnmapGPUTransferBuffer(device, buffer.get());
    return pixels;
}
void Device::copy(CommandBuffer &command, const Texture &source, const Texture &destination) {
    command.checkThread();
    checkTexture(source); checkTexture(destination);
    if (command.device != device || source.width() != destination.width() || source.height() != destination.height()
        || source.native() == destination.native())
        throw std::invalid_argument("GPU copy requires distinct equal-size textures on one device");
    auto *pass = SDL_BeginGPUCopyPass(command.native());
    if (!pass) fail("SDL_BeginGPUCopyPass");
    SDL_GPUTextureLocation src{}; src.texture = source.native();
    SDL_GPUTextureLocation dst{}; dst.texture = destination.native();
    SDL_CopyGPUTextureToTexture(pass, &src, &dst, source.width(), source.height(), 1, false);
    SDL_EndGPUCopyPass(pass);
}
SDL_GPUTexture *Device::acquireFrame(CommandBuffer &command, Uint32 &w, Uint32 &h) {
    checkWindowThread(); command.checkThread();
    if (command.device != device || command.acquiredSwapchain)
        throw std::invalid_argument("GPU frame requires an unclaimed command buffer on this device");
    SDL_GPUTexture *texture = nullptr;
    w = h = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(command.native(), window, &texture, &w, &h))
        fail("SDL_WaitAndAcquireGPUSwapchainTexture");
    command.acquiredSwapchain = texture != nullptr;
    return texture;
}
}
