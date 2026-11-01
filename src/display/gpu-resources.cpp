#include "gpu-resources.h"
#include "gpu-device.h"
#include "gpu-frame.h"
#include "util/exception.h"
#include <SDL3/SDL_surface.h>
#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gpu {
Device &renderDevice();
void flushRenderCommands();
void bindPrimaryTexture(TEX::ID);
void setRenderTarget(TextureView);
void bindDefaultRenderTarget();
void clearRenderTarget();
bool textureMipmapsEnabled();
}
namespace TEX {
struct Storage {
    gpu::Texture texture;
    bool alive = true, repeating = false, smoothing = false;
    bool requestedMipmaps = false, dirty = true;
};
}
namespace FBO {
struct Storage { TEX::ID target; bool alive = true; };
thread_local ID boundFramebufferID;
}
namespace gpu_resources {
struct BufferStorage { std::vector<std::byte> data; bool alive = true; };
struct Access {
    static auto &texture(TEX::ID &id) { return id.storage; }
    static const auto &texture(const TEX::ID &id) { return id.storage; }
    static auto &framebuffer(FBO::ID &id) { return id.storage; }
    static const auto &framebuffer(const FBO::ID &id) { return id.storage; }
    template<BufferKind Kind> static auto &buffer(BufferID<Kind> &id) { return id.storage; }
    template<BufferKind Kind> static const auto &buffer(const BufferID<Kind> &id) { return id.storage; }
};
}
namespace {
using gpu_resources::Access;
thread_local TEX::ID activeTexture;
template<class F> decltype(auto) checkedGpu(F &&function) {
    try { return function(); }
    catch (const std::invalid_argument &error) { throw Exception(Exception::ArgumentError, error.what()); }
    catch (const std::exception &error) { throw Exception(Exception::SDLError, error.what()); }
}
[[noreturn]] void invalid(const char *message) { throw Exception(Exception::ArgumentError, message); }
TEX::Storage &textureStorage(const TEX::ID &id) {
    const auto &storage = Access::texture(id);
    if (!storage || !storage->alive) invalid("Texture handle is empty or released");
    return *storage;
}
FBO::Storage &framebufferStorage(const FBO::ID &id) {
    const auto &storage = Access::framebuffer(id);
    if (!storage || !storage->alive) invalid("Render target handle is empty or released");
    return *storage;
}
void refreshTarget(const TEX::ID &texture) {
    if (!FBO::boundFramebufferID.empty() && FBO::boundFramebufferID.valid() &&
        FBO::boundFramebufferID.texture() == texture && texture.valid())
        checkedGpu([&] { gpu::setRenderTarget(gpu::TextureView(texture.resource())); });
}
Uint32 byteSize(int width, int height) {
    if (width <= 0 || height <= 0 || Uint64(width) * Uint64(height) * 4 > std::numeric_limits<Uint32>::max())
        invalid("Invalid RGBA texture dimensions");
    return Uint32(width) * Uint32(height) * 4;
}
Uint32 mipLevels(int width, int height) { return static_cast<Uint32>(std::bit_width(Uint32(std::max(width, height)))); }
std::span<const Uint8> rgbaPixels(int width, int height, const void *data,
                                 SDL_PixelFormat format, int pitch, std::vector<Uint8> &converted) {
    const auto size = byteSize(width, height);
    const int bytesPerPixel = static_cast<int>(SDL_BYTESPERPIXEL(format));
    if (!data || bytesPerPixel <= 0 || SDL_ISPIXELFORMAT_INDEXED(format))
        invalid("Texture upload requires non-paletted pixel data");
    const auto rowBytes = Uint64(width) * Uint32(bytesPerPixel);
    if (rowBytes > Uint32(std::numeric_limits<int>::max()) || Uint64(width) * 4 > Uint32(std::numeric_limits<int>::max()))
        invalid("Texture row pitch is too large");
    if (pitch == 0) pitch = static_cast<int>(rowBytes);
    if (pitch < static_cast<int>(rowBytes)) invalid("Texture row pitch is too small");
    if (format == SDL_PIXELFORMAT_RGBA32 && pitch == width * 4)
        return {static_cast<const Uint8 *>(data), size};
    converted.resize(size);
    if (!SDL_ConvertPixels(width, height, format, data, pitch, SDL_PIXELFORMAT_RGBA32,
                           converted.data(), width * 4))
        throw Exception(Exception::SDLError, "Pixel conversion failed: %s", SDL_GetError());
    return converted;
}
void allocateTexture(const TEX::ID &id, int width, int height) {
    byteSize(width, height);
    auto &storage = textureStorage(id);
    checkedGpu([&] {
        gpu::flushRenderCommands();
        const auto levels = gpu::textureMipmapsEnabled() || storage.requestedMipmaps ? mipLevels(width, height) : 1;
        auto replacement = gpu::renderDevice().createTexture(Uint32(width), Uint32(height),
            SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, levels);
        storage.texture = std::move(replacement);
    });
    storage.dirty = true;
    refreshTarget(id);
}
struct TransferDeleter {
    SDL_GPUDevice *device;
    void operator()(SDL_GPUTransferBuffer *buffer) const { SDL_ReleaseGPUTransferBuffer(device, buffer); }
};
void uploadRegion(const TEX::ID &id, int x, int y, int width, int height, std::span<const Uint8> pixels) {
    checkedGpu([&] {
        gpu::flushRenderCommands();
        auto &device = gpu::renderDevice();
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        info.size = static_cast<Uint32>(pixels.size());
        std::unique_ptr<SDL_GPUTransferBuffer, TransferDeleter> transfer(
            SDL_CreateGPUTransferBuffer(device.native(), &info), {device.native()});
        if (!transfer) throw std::runtime_error(SDL_GetError());
        void *mapped = SDL_MapGPUTransferBuffer(device.native(), transfer.get(), false);
        if (!mapped) throw std::runtime_error(SDL_GetError());
        std::memcpy(mapped, pixels.data(), pixels.size());
        SDL_UnmapGPUTransferBuffer(device.native(), transfer.get());
        gpu::CommandBuffer command(device.native());
        auto *pass = SDL_BeginGPUCopyPass(command.native());
        if (!pass) throw std::runtime_error(SDL_GetError());
        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transfer.get();
        SDL_GPUTextureRegion destination{};
        destination.texture = id.native();
        destination.x = Uint32(x); destination.y = Uint32(y);
        destination.w = Uint32(width); destination.h = Uint32(height); destination.d = 1;
        SDL_UploadToGPUTexture(pass, &source, &destination, false);
        SDL_EndGPUCopyPass(pass);
        command.submitAndWait();
    });
}
template<gpu_resources::BufferKind Kind> auto &activeBuffer() {
    static thread_local gpu_resources::BufferID<Kind> current;
    return current;
}
template<gpu_resources::BufferKind Kind> auto &bufferStorage(const gpu_resources::BufferID<Kind> &id) {
    const auto &storage = Access::buffer(id);
    if (!storage || !storage->alive) invalid("Geometry buffer handle is empty or released");
    return *storage;
}
}

namespace TEX {
ID::ID(int zero) { if (zero != 0) invalid("Texture IDs are opaque"); }
SDL_GPUTexture *ID::native() const { return valid() ? storage->texture.native() : nullptr; }
const gpu::Texture &ID::resource() const {
    if (!valid()) invalid("Texture has no allocated storage or was released");
    return storage->texture;
}
int ID::width() const { return valid() ? static_cast<int>(storage->texture.width()) : 0; }
int ID::height() const { return valid() ? static_cast<int>(storage->texture.height()) : 0; }
bool ID::repeat() const { return storage && storage->alive && storage->repeating; }
bool ID::smooth() const { return storage && storage->alive && storage->smoothing; }
bool ID::mipmaps() const {
    return valid() && storage->requestedMipmaps && storage->smoothing && !storage->dirty && storage->texture.mipLevels() > 1;
}
bool ID::valid() const { return storage && storage->alive && storage->texture.native(); }
ID::operator bool() const { return storage && storage->alive; }
ID gen() { ID id; Access::texture(id) = std::make_shared<Storage>(); return id; }
const ID &bound() { return activeTexture; }
void bind(ID id) {
    if (!id.empty()) textureStorage(id);
    checkedGpu([&] { gpu::bindPrimaryTexture(id); });
    activeTexture = std::move(id);
}
void unbind() { bind(ID{}); }
void del(ID id) {
    auto &storage = Access::texture(id);
    if (!storage || !storage->alive) return;
    if (storage->texture.native()) checkedGpu([] { gpu::flushRenderCommands(); });
    if (activeTexture == id) unbind();
    storage->texture = gpu::Texture{};
    storage->alive = false;
    storage->dirty = true;
}
void allocEmpty(int width, int height) { allocateTexture(activeTexture, width, height); }
void uploadImage(int width, int height, const void *pixels, SDL_PixelFormat format, int pitch) {
    textureStorage(activeTexture);
    std::vector<Uint8> converted;
    const auto data = pixels ? rgbaPixels(width, height, pixels, format, pitch, converted) : std::span<const Uint8>{};
    allocateTexture(activeTexture, width, height);
    if (pixels) checkedGpu([&] { gpu::renderDevice().uploadRGBA(activeTexture.resource(), data); });
    if (pixels && activeTexture.smooth()) prepareSampling(activeTexture);
}
void uploadSubImage(int x, int y, int width, int height, const void *pixels, SDL_PixelFormat format, int pitch) {
    if (width < 0 || height < 0) invalid("Negative texture upload region");
    if (!width || !height) return;
    if (!activeTexture.valid() || x < 0 || y < 0 || x > activeTexture.width() || y > activeTexture.height() ||
        width > activeTexture.width() - x || height > activeTexture.height() - y)
        invalid("Texture upload region is out of bounds");
    std::vector<Uint8> converted;
    const auto data = rgbaPixels(width, height, pixels, format, pitch, converted);
    uploadRegion(activeTexture, x, y, width, height, data);
    markModified(activeTexture);
    if (activeTexture.smooth()) prepareSampling(activeTexture);
}
void setRepeat(bool value) { textureStorage(activeTexture).repeating = value; }
void setSmooth(bool value) { setSmooth(value, checkedGpu([] { return gpu::textureMipmapsEnabled(); })); }
void setSmooth(bool value, bool mipmaps) {
    auto &storage = textureStorage(activeTexture);
    storage.smoothing = value;
    storage.requestedMipmaps = value && mipmaps;
    if (activeTexture.valid() && value && mipmaps) prepareSampling(activeTexture);
}
void markModified(const ID &id) {
    const auto &storage = Access::texture(id);
    if (storage && storage->alive) storage->dirty = true;
}
void prepareSampling(const ID &id) {
    auto &storage = textureStorage(id);
    if (!id.valid()) invalid("Texture has no allocated storage");
    if (!storage.smoothing || !storage.requestedMipmaps || !storage.dirty) return;
    const auto levels = mipLevels(id.width(), id.height());
    if (levels == 1) { storage.dirty = false; return; }
    checkedGpu([&] {
        gpu::flushRenderCommands();
        auto &device = gpu::renderDevice();
        if (storage.texture.mipLevels() != levels) {
            auto replacement = device.createTexture(storage.texture.width(), storage.texture.height(),
                SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, levels);
            gpu::CommandBuffer command(device.native());
            device.copy(command, storage.texture, replacement);
            command.submitAndWait();
            storage.texture = std::move(replacement);
        }
        device.generateMipmaps(storage.texture);
    });
    storage.dirty = false;
    refreshTarget(id);
}
}

namespace FBO {
ID::ID(int zero) { if (zero != 0) invalid("Render target IDs are opaque"); }
TEX::ID ID::texture() const { return storage && storage->alive ? storage->target : TEX::ID{}; }
bool ID::valid() const { return storage && storage->alive; }
ID::operator bool() const { return valid(); }
ID gen() { ID id; Access::framebuffer(id) = std::make_shared<Storage>(); return id; }
void bind(ID id) {
    if (!id.empty()) framebufferStorage(id);
    if (id.empty()) checkedGpu([] { gpu::bindDefaultRenderTarget(); });
    else if (const auto target = id.texture(); target.valid())
        checkedGpu([&] { gpu::setRenderTarget(gpu::TextureView(target.resource())); });
    boundFramebufferID = std::move(id);
}
void unbind() { bind(ID{}); }
void del(ID id) {
    auto &storage = Access::framebuffer(id);
    if (!storage || !storage->alive) return;
    if (storage->target.valid() || boundFramebufferID == id)
        checkedGpu([] { gpu::flushRenderCommands(); });
    if (boundFramebufferID == id) unbind();
    storage->target = {};
    storage->alive = false;
}
void setTarget(TEX::ID target, unsigned attachment) {
    if (attachment != 0) invalid("RGSS render targets have one color attachment");
    textureStorage(target);
    auto &storage = framebufferStorage(boundFramebufferID);
    checkedGpu([] { gpu::flushRenderCommands(); });
    storage.target = std::move(target);
    if (storage.target.valid())
        checkedGpu([&] { gpu::setRenderTarget(gpu::TextureView(storage.target.resource())); });
}
void clear() {
    if (!boundFramebufferID.empty() && !boundFramebufferID.texture().valid())
        invalid("Render target texture has no allocated storage or was released");
    checkedGpu([] { gpu::clearRenderTarget(); });
    if (!boundFramebufferID.empty()) TEX::markModified(boundFramebufferID.texture());
}
}

namespace gpu_resources {
void resetContextBindings() noexcept {
    activeTexture = {};
    FBO::boundFramebufferID = {};
    VBO::unbind();
    IBO::unbind();
}
template<BufferKind Kind> BufferID<Kind>::BufferID(int zero) { if (zero != 0) invalid("Buffer IDs are opaque"); }
template<BufferKind Kind> bool BufferID<Kind>::valid() const { return storage && storage->alive; }
template<BufferKind Kind> BufferID<Kind>::operator bool() const { return valid(); }
template<BufferKind Kind> std::span<const std::byte> BufferID<Kind>::bytes() const {
    if (empty()) return {};
    return bufferStorage(*this).data;
}
template<BufferKind Kind> typename CpuBuffer<Kind>::ID CpuBuffer<Kind>::gen() {
    ID id; Access::buffer(id) = std::make_shared<BufferStorage>(); return id;
}
template<BufferKind Kind> const typename CpuBuffer<Kind>::ID &CpuBuffer<Kind>::bound() { return activeBuffer<Kind>(); }
template<BufferKind Kind> void CpuBuffer<Kind>::bind(ID id) {
    if (!id.empty()) bufferStorage(id);
    activeBuffer<Kind>() = std::move(id);
}
template<BufferKind Kind> void CpuBuffer<Kind>::unbind() { activeBuffer<Kind>() = {}; }
template<BufferKind Kind> void CpuBuffer<Kind>::del(ID id) {
    auto &storage = Access::buffer(id);
    if (!storage || !storage->alive) return;
    if (activeBuffer<Kind>() == id) unbind();
    std::vector<std::byte>().swap(storage->data);
    storage->alive = false;
}
template<BufferKind Kind> void CpuBuffer<Kind>::uploadData(size_t size, const void *data, BufferUsage) {
    auto &storage = bufferStorage(bound());
    if (size > storage.data.max_size()) invalid("Geometry buffer size is too large");
    std::vector<std::byte> replacement(size);
    if (size && data) std::memcpy(replacement.data(), data, size);
    storage.data.swap(replacement);
}
template<BufferKind Kind> void CpuBuffer<Kind>::uploadData(std::span<const std::byte> data) { uploadData(data.size(), data.data()); }
template<BufferKind Kind> void CpuBuffer<Kind>::uploadSubData(size_t offset, size_t size, const void *data) {
    auto &storage = bufferStorage(bound());
    if (offset > storage.data.size() || size > storage.data.size() - offset || (size && !data))
        invalid("Geometry buffer update is out of bounds");
    if (size) std::memmove(storage.data.data() + offset, data, size);
}
template<BufferKind Kind> void CpuBuffer<Kind>::allocEmpty(size_t size, BufferUsage usage) { uploadData(size, nullptr, usage); }
template class BufferID<BufferKind::Vertex>;
template class BufferID<BufferKind::Index>;
template struct CpuBuffer<BufferKind::Vertex>;
template struct CpuBuffer<BufferKind::Index>;
}

void TEXFBO::init(TEXFBO &value) {
    value.tex = TEX::gen(); value.fbo = FBO::gen();
    TEX::bind(value.tex); TEX::setRepeat(false); TEX::setSmooth(false);
}
void TEXFBO::allocEmpty(TEXFBO &value, int width, int height) {
    TEX::bind(value.tex); TEX::allocEmpty(width,height);
    value.width = width; value.height = height;
}
void TEXFBO::linkFBO(TEXFBO &value) { FBO::bind(value.fbo); FBO::setTarget(value.tex); }
void TEXFBO::fini(TEXFBO &value) { FBO::del(value.fbo); TEX::del(value.tex); }
void TEXFBO::clear(TEXFBO &value) { value.tex = {}; value.fbo = {}; value.width = value.height = 0; }
