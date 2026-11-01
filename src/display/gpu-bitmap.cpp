#include "gpu-bitmap.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace gpu {
namespace {
constexpr Uint32 vertexBytes = sizeof(QuadVertex) * 6;
[[noreturn]] void fail(const char *operation) {
    throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}
bool drawable(SDL_FRect rect) {
    if (!std::isfinite(rect.x) || !std::isfinite(rect.y) ||
        !std::isfinite(rect.w) || !std::isfinite(rect.h))
        throw std::invalid_argument("Non-finite bitmap rectangle");
    return rect.w > 0 && rect.h > 0;
}
SDL_FRect whole(const Texture &texture) {
    return {0, 0, float(texture.width()), float(texture.height())};
}
}

BitmapRenderer::BitmapRenderer(Device &d, QuadPipeline &p) : device(d), pipelines(p) {
    try {
        SDL_GPUBufferCreateInfo buffer{};
        buffer.usage = SDL_GPU_BUFFERUSAGE_VERTEX; buffer.size = vertexBytes;
        vertices = SDL_CreateGPUBuffer(device.native(), &buffer);
        if (!vertices) fail("SDL_CreateGPUBuffer");
        SDL_GPUTransferBufferCreateInfo upload{};
        upload.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; upload.size = vertexBytes;
        transfer = SDL_CreateGPUTransferBuffer(device.native(), &upload);
        if (!transfer) fail("SDL_CreateGPUTransferBuffer");
        SDL_GPUSamplerCreateInfo sampler{};
        sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
        sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w =
            SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        nearest = SDL_CreateGPUSampler(device.native(), &sampler);
        if (!nearest) fail("SDL_CreateGPUSampler");
        sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
        linear = SDL_CreateGPUSampler(device.native(), &sampler);
        if (!linear) fail("SDL_CreateGPUSampler");
    } catch (...) {
        if (nearest) SDL_ReleaseGPUSampler(device.native(), nearest);
        if (transfer) SDL_ReleaseGPUTransferBuffer(device.native(), transfer);
        if (vertices) SDL_ReleaseGPUBuffer(device.native(), vertices);
        throw;
    }
}
BitmapRenderer::~BitmapRenderer() {
    SDL_ReleaseGPUSampler(device.native(), linear);
    SDL_ReleaseGPUSampler(device.native(), nearest);
    SDL_ReleaseGPUTransferBuffer(device.native(), transfer);
    SDL_ReleaseGPUBuffer(device.native(), vertices);
}

void BitmapRenderer::clear(CommandBuffer &command, const Texture &target, SDL_FColor value) {
    SDL_GPUColorTargetInfo color{};
    color.texture = target.native(); color.load_op = SDL_GPU_LOADOP_CLEAR;
    color.store_op = SDL_GPU_STOREOP_STORE; color.clear_color = value;
    auto *pass = SDL_BeginGPURenderPass(command.native(), &color, 1, nullptr);
    if (!pass) fail("SDL_BeginGPURenderPass");
    SDL_EndGPURenderPass(pass);
}

void BitmapRenderer::draw(CommandBuffer &command, const Texture &target, SDL_FRect destination,
                          SDL_FRect uv, Program program,
                          std::span<const SDL_GPUTextureSamplerBinding> textures,
                          const FragmentParameters &parameters) {
    if (!drawable(destination)) return;
    const float x = destination.x, y = destination.y;
    const float right = x + destination.w, bottom = y + destination.h;
    const float u = uv.x, v = uv.y, ur = u + uv.w, vb = v + uv.h;
    const QuadVertex quad[] = {
        {{x,y},{u,v},{1,1,1,1}}, {{right,y},{ur,v},{1,1,1,1}}, {{x,bottom},{u,vb},{1,1,1,1}},
        {{x,bottom},{u,vb},{1,1,1,1}}, {{right,y},{ur,v},{1,1,1,1}}, {{right,bottom},{ur,vb},{1,1,1,1}},
    };
    void *mapping = SDL_MapGPUTransferBuffer(device.native(), transfer, true);
    if (!mapping) fail("SDL_MapGPUTransferBuffer");
    std::memcpy(mapping, quad, sizeof(quad));
    SDL_UnmapGPUTransferBuffer(device.native(), transfer);
    auto *copy = SDL_BeginGPUCopyPass(command.native());
    if (!copy) fail("SDL_BeginGPUCopyPass");
    SDL_GPUTransferBufferLocation from{transfer, 0};
    SDL_GPUBufferRegion to{vertices, 0, vertexBytes};
    SDL_UploadToGPUBuffer(copy, &from, &to, true);
    SDL_EndGPUCopyPass(copy);

    VertexParameters vp{};
    vp.projection[0] = 2.f / target.width();
    vp.projection[5] = -2.f / target.height();
    vp.projection[10] = vp.projection[15] = 1;
    vp.projection[12] = -1; vp.projection[13] = 1;
    for (int i = 0; i < 4; ++i) vp.transform[i * 5] = 1;
    vp.textureScaleTranslation[0] = vp.textureScaleTranslation[1] = 1;
    // Create/cache pipelines before opening a render pass: C++ unwinding must
    // never leave a live pass in a command buffer.
    pipelines.get(program, Blend::Disabled, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    SDL_GPUColorTargetInfo color{};
    color.texture = target.native(); color.load_op = SDL_GPU_LOADOP_LOAD;
    color.store_op = SDL_GPU_STOREOP_STORE;
    auto *pass = SDL_BeginGPURenderPass(command.native(), &color, 1, nullptr);
    if (!pass) fail("SDL_BeginGPURenderPass");
    try {
        SDL_GPUBufferBinding binding{vertices, 0};
        pipelines.bind(command.native(), pass, program, Blend::Disabled,
                       SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, binding, textures, vp, parameters);
        SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
    } catch (...) { SDL_EndGPURenderPass(pass); throw; }
    SDL_EndGPURenderPass(pass);
}

void BitmapRenderer::fill(CommandBuffer &command, const Texture &target,
                          SDL_FRect rect, SDL_FColor value) {
    FragmentParameters parameters{};
    parameters.color[0] = value.r; parameters.color[1] = value.g;
    parameters.color[2] = value.b; parameters.color[3] = value.a;
    draw(command, target, rect, {}, Program::FlatColor, {}, parameters);
}
void BitmapRenderer::blit(CommandBuffer &command, const Texture &destination, SDL_FRect destinationRect,
                          const Texture &source, SDL_FRect sourceRect, bool smooth) {
    if (source.native() == destination.native())
        throw std::invalid_argument("Self blits require a source snapshot");
    if (!drawable(sourceRect)) return;
    SDL_GPUTextureSamplerBinding binding{source.native(), smooth ? linear : nearest};
    const SDL_FRect uv{sourceRect.x / source.width(), sourceRect.y / source.height(),
                      sourceRect.w / source.width(), sourceRect.h / source.height()};
    draw(command, destination, destinationRect, uv, Program::Simple, std::span(&binding, 1), {});
}
Texture BitmapRenderer::blend(CommandBuffer &command, const Texture &destination, SDL_FRect destinationRect,
                              const Texture &source, SDL_FRect sourceRect, float opacity, bool smooth) {
    if (!std::isfinite(opacity)) throw std::invalid_argument("Non-finite bitmap opacity");
    auto snapshot = device.createTexture(destination.width(), destination.height());
    device.copy(command, destination, snapshot);
    if (!drawable(destinationRect) || !drawable(sourceRect)) return snapshot;
    const float sourceWidth = float(source.width()), sourceHeight = float(source.height());
    SDL_FRect uv{sourceRect.x / sourceWidth, sourceRect.y / sourceHeight,
                sourceRect.w / sourceWidth, sourceRect.h / sourceHeight};
    FragmentParameters parameters{};
    parameters.parameters[0] = std::clamp(opacity, 0.f, 1.f);
    // The second sampler uses destination pixels, including clipping offsets.
    parameters.subrect[0] = uv.x - destinationRect.x * uv.w / destinationRect.w;
    parameters.subrect[1] = uv.y - destinationRect.y * uv.h / destinationRect.h;
    parameters.subrect[2] = destinationRect.w / (uv.w * destination.width());
    parameters.subrect[3] = destinationRect.h / (uv.h * destination.height());
    const SDL_GPUTextureSamplerBinding bindings[] = {
        {source.native() == destination.native() ? snapshot.native() : source.native(), smooth ? linear : nearest},
        {snapshot.native(), nearest}
    };
    draw(command, destination, destinationRect, uv, Program::BitmapBlend, bindings, parameters);
    return snapshot;
}
Texture BitmapRenderer::hue(CommandBuffer &command, const Texture &target, float rotation) {
    if (!std::isfinite(rotation)) throw std::invalid_argument("Non-finite hue rotation");
    auto snapshot = device.createTexture(target.width(), target.height());
    device.copy(command, target, snapshot);
    const SDL_GPUTextureSamplerBinding binding{snapshot.native(), nearest};
    FragmentParameters parameters{}; parameters.parameters[3] = rotation;
    draw(command, target, whole(target), {0,0,1,1}, Program::Hue, std::span(&binding, 1), parameters);
    return snapshot;
}
}
