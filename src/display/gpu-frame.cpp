#include "gpu-frame.h"
#include <algorithm>
#include <format>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace gpu {
namespace {
[[noreturn]] void fail(const char *operation) {
    throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}
}
FrameEncoder::FrameEncoder(Device &d, QuadPipeline &p) : device(d), pipelines(p) {}
FrameEncoder::~FrameEncoder() {
    if (transferBuffer) SDL_ReleaseGPUTransferBuffer(device.native(), transferBuffer);
    if (vertexBuffer) SDL_ReleaseGPUBuffer(device.native(), vertexBuffer);
}
void FrameEncoder::reset() { vertices.clear(); batches.clear(); }
bool FrameEncoder::Draw::canMerge(const Draw &other) const {
    // Compare uniform bytes conservatively, preserving signed zeros and NaNs.
    // Any padding differences merely prevent merging otherwise compatible draws.
    const auto sameBytes = []<class T>(const T &a, const T &b) {
        static_assert(std::is_trivially_copyable_v<T>);
        return std::memcmp(&a, &b, sizeof(T)) == 0;
    };
    if (program != other.program || blend != other.blend || samplerCount != other.samplerCount ||
        !sameBytes(vertex, other.vertex) || !sameBytes(fragment, other.fragment) ||
        !sameBytes(effects, other.effects) || !sameBytes(tilemap, other.tilemap) ||
        !sameBytes(viewport, other.viewport) || !sameBytes(scissor, other.scissor))
        return false;
    for (Uint32 i = 0; i < samplerCount; ++i)
        if (samplers[i].texture != other.samplers[i].texture || samplers[i].sampler != other.samplers[i].sampler)
            return false;
    return true;
}
void FrameEncoder::beginTarget(TextureView target, SDL_GPULoadOp load, SDL_FColor clear) {
    if (!target.native() || !target.width() || !target.height()
        || target.width() > static_cast<Uint32>(std::numeric_limits<int>::max()) || target.height() > static_cast<Uint32>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("Frame target requires valid texture dimensions");
    if (load != SDL_GPU_LOADOP_LOAD && load != SDL_GPU_LOADOP_CLEAR && load != SDL_GPU_LOADOP_DONT_CARE)
        throw std::invalid_argument("Invalid GPU target load operation");
    batches.push_back({target, load, clear, {}});
}
void FrameEncoder::draw(Program program, std::span<const QuadVertex> geometry,
    std::span<const SDL_GPUTextureSamplerBinding> samplers,
    const VertexParameters &vertex, const FragmentParameters &fragment,
    const DrawState &state, const EffectParameters &effects, const TilemapParameters &tilemap) {
    if (batches.empty()) throw std::logic_error("beginTarget must precede GPU draws");
    if (geometry.empty() || geometry.size() % 3 || samplers.size() > 3)
        throw std::invalid_argument("GPU draw requires triangles and at most three samplers");
    constexpr auto maxVertices = std::numeric_limits<Uint32>::max() / sizeof(QuadVertex);
    if (geometry.size() > maxVertices - vertices.size())
        throw std::length_error("GPU frame vertex buffer exceeds SDL byte limit");
    auto &batch = batches.back();
    for (const auto &binding : samplers) {
        if (!binding.texture || !binding.sampler || binding.texture == batch.target.native())
            throw std::invalid_argument("GPU draw cannot sample its target or an empty binding");
    }
    const auto viewport = state.viewport.value_or(SDL_GPUViewport{0,0,float(batch.target.width()),float(batch.target.height()),0,1});
    if (!std::isfinite(viewport.x) || !std::isfinite(viewport.y) || !std::isfinite(viewport.w)
        || !std::isfinite(viewport.h) || viewport.w <= 0 || viewport.h <= 0
        || !std::isfinite(viewport.min_depth) || !std::isfinite(viewport.max_depth)
        || viewport.min_depth < 0 || viewport.max_depth > 1 || viewport.min_depth > viewport.max_depth)
        throw std::invalid_argument("Invalid GPU viewport");
    if (viewport.x + viewport.w > batch.target.width() ||
        viewport.y + viewport.h > batch.target.height())
        throw std::invalid_argument(std::format(
            "GPU viewport {},{},{}x{} exceeds render target {}x{}",
            viewport.x, viewport.y, viewport.w, viewport.h, batch.target.width(), batch.target.height()));
    auto scissor = state.scissor.value_or(SDL_Rect{0,0,int(batch.target.width()),int(batch.target.height())});
    if (scissor.w < 0 || scissor.h < 0) throw std::invalid_argument("Invalid GPU scissor size");
    // Clip with wide arithmetic, preserving empty intersections safely.
    const Sint64 left = std::clamp<Sint64>(scissor.x, 0, batch.target.width());
    const Sint64 top = std::clamp<Sint64>(scissor.y, 0, batch.target.height());
    const Sint64 right = std::clamp<Sint64>(Sint64(scissor.x) + scissor.w, left, batch.target.width());
    const Sint64 bottom = std::clamp<Sint64>(Sint64(scissor.y) + scissor.h, top, batch.target.height());
    scissor = {int(left),int(top),int(right-left),int(bottom-top)};
    if (scissor.w == 0 || scissor.h == 0) return;
    Draw draw{program, static_cast<Uint32>(vertices.size()), static_cast<Uint32>(geometry.size()),
              static_cast<Uint32>(samplers.size()), {}, vertex, fragment, effects, tilemap, state.blend, viewport, scissor};
    std::copy(samplers.begin(), samplers.end(), draw.samplers.begin());
    vertices.insert(vertices.end(), geometry.begin(), geometry.end());
    if (!batch.draws.empty() && batch.draws.back().canMerge(draw)) {
        batch.draws.back().count += draw.count;
        return;
    }
    batch.draws.push_back(draw);
}
void FrameEncoder::ensureBuffer(Uint32 bytes) {
    if (capacity >= bytes) return;
    const auto grown = std::min<Uint64>(std::numeric_limits<Uint32>::max(), std::max<Uint64>(bytes, Uint64(capacity)*2));
    SDL_GPUBufferCreateInfo info{};
    info.usage = SDL_GPU_BUFFERUSAGE_VERTEX; info.size = static_cast<Uint32>(grown);
    auto *nextVertices = SDL_CreateGPUBuffer(device.native(), &info);
    if (!nextVertices) fail("SDL_CreateGPUBuffer");
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; transferInfo.size = info.size;
    auto *nextTransfer = SDL_CreateGPUTransferBuffer(device.native(), &transferInfo);
    if (!nextTransfer) {
        SDL_ReleaseGPUBuffer(device.native(), nextVertices);
        fail("SDL_CreateGPUTransferBuffer");
    }
    if (vertexBuffer) SDL_ReleaseGPUBuffer(device.native(), vertexBuffer);
    if (transferBuffer) SDL_ReleaseGPUTransferBuffer(device.native(), transferBuffer);
    vertexBuffer = nextVertices; transferBuffer = nextTransfer; capacity = info.size;
}
Fence FrameEncoder::submit() { return submitCommands(true); }
void FrameEncoder::submitWithoutFence() { static_cast<void>(submitCommands(false)); }
Fence FrameEncoder::submitCommands(bool acquireFence) {
    if (batches.empty()) throw std::logic_error("No GPU frame targets were recorded");
    // Pipeline creation can fail/allocate, so do all of it before opening passes.
    for (const auto &batch : batches)
        for (const auto &draw : batch.draws)
            pipelines.get(draw.program, draw.blend, batch.target.pixelFormat());
    const auto bytes = static_cast<Uint32>(vertices.size()*sizeof(QuadVertex));
    if (bytes) {
        ensureBuffer(bytes);
        void *mapped = SDL_MapGPUTransferBuffer(device.native(), transferBuffer, true);
        if (!mapped) fail("SDL_MapGPUTransferBuffer");
        std::memcpy(mapped, vertices.data(), bytes);
        SDL_UnmapGPUTransferBuffer(device.native(), transferBuffer);
    }
    CommandBuffer command(device.native());
    if (bytes) {
        auto *copy = SDL_BeginGPUCopyPass(command.native());
        if (!copy) fail("SDL_BeginGPUCopyPass");
        SDL_GPUTransferBufferLocation source{transferBuffer,0};
        SDL_GPUBufferRegion destination{vertexBuffer,0,bytes};
        SDL_UploadToGPUBuffer(copy, &source, &destination, true);
        SDL_EndGPUCopyPass(copy);
    }
    for (const auto &batch : batches) {
        SDL_GPUColorTargetInfo target{};
        target.texture = batch.target.native(); target.load_op = batch.load;
        target.store_op = SDL_GPU_STOREOP_STORE; target.clear_color = batch.clear;
        RenderPass pass(device, command, target);
        for (const auto &draw : batch.draws) {
            SDL_SetGPUViewport(pass.native(), &draw.viewport);
            SDL_SetGPUScissor(pass.native(), &draw.scissor);
            const SDL_GPUBufferBinding binding{vertexBuffer,draw.first*static_cast<Uint32>(sizeof(QuadVertex))};
            pipelines.bind(command.native(), pass.native(), draw.program, draw.blend, batch.target.pixelFormat(),
                binding, std::span(draw.samplers).first(draw.samplerCount), draw.vertex, draw.fragment, draw.effects, draw.tilemap);
            SDL_DrawGPUPrimitives(pass.native(), draw.count, 1, 0, 0);
        }
    }
    Fence fence;
    if (acquireFence) fence = command.submitWithFence();
    else command.submit();
    reset();
    return fence;
}
}
