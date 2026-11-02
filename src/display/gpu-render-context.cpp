#include "gpu-render-context.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace gpu {
namespace {
thread_local RenderContext *activeContext = nullptr;
[[noreturn]] void fail(const char *operation) {
    throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}
}

RenderContext::RenderContext(Device &device, QuadPipeline &pipelines, int width, int height)
    : gpuDevice(device), encoder(device, pipelines), viewportRect{0,0,width,height} {
    setViewport(viewportRect);
}
RenderContext::~RenderContext() {
    // An exceptional shutdown may discard unsubmitted CPU draws. Normal frame
    // completion calls submit(), and mutation/release calls flush().
    encoder.reset();
    if (activeContext == this) {
        gpu_resources::resetContextBindings();
        activeContext = nullptr;
    }
    for (auto *value : samplers)
        if (value) SDL_ReleaseGPUSampler(gpuDevice.native(), value);
}
RenderContext &RenderContext::current() {
    if (!activeContext) throw std::logic_error("No GPU render context on this thread");
    return *activeContext;
}
void RenderContext::install(RenderContext *value) { activeContext = value; }
void RenderContext::setViewport(SDL_Rect value) {
    if (value.w <= 0 || value.h <= 0)
        throw std::invalid_argument("GPU viewport requires positive dimensions");
    viewportRect = value;
    state.viewport = SDL_GPUViewport{float(value.x),float(value.y),float(value.w),float(value.h),0,1};
}
void RenderContext::setTarget(TextureView value) {
    if (!value.native() || !value.width() || !value.height())
        throw std::invalid_argument("GPU render target is empty");
    target = value;
    if (!FBO::boundFramebufferID.empty()) {
        const auto attachment = FBO::boundFramebufferID.texture();
        if (attachment.valid() && attachment.native() == value.native())
            pendingResources.try_emplace(attachment.native(), attachment);
    }
    encoder.beginTarget(value, SDL_GPU_LOADOP_LOAD);
    recording = true;
    targetNeedsBegin = false;
}
void RenderContext::setDefaultTarget(TextureView value) {
    if (!value.native()) throw std::invalid_argument("Default GPU target is empty");
    defaultTarget = value;
}
void RenderContext::bindDefaultTarget() {
    if (!defaultTarget) {
        target.reset();
        return;
    }
    setTarget(*defaultTarget);
}
void RenderContext::ensureTarget() {
    if (!FBO::boundFramebufferID.empty() && !FBO::boundFramebufferID.valid())
        throw std::logic_error("Bound GPU framebuffer has no live attachment");
    if (!target) throw std::logic_error("No offscreen GPU target is bound");
    if (!recording || targetNeedsBegin) {
        if (!FBO::boundFramebufferID.empty()) {
            const auto attachment = FBO::boundFramebufferID.texture();
            pendingResources.try_emplace(attachment.native(), attachment);
        }
        encoder.beginTarget(*target, SDL_GPU_LOADOP_LOAD);
        recording = true;
        targetNeedsBegin = false;
    }
}
void RenderContext::copyTexture(const TEX::ID &source, const TEX::ID &destination) {
    const auto &src = source.resource();
    const auto &dst = destination.resource();
    pendingResources.try_emplace(source.native(), source);
    pendingResources.try_emplace(destination.native(), destination);
    encoder.copy(src, dst);
    TEX::markModified(destination);
    recording = true;
    targetNeedsBegin = true;
}
void RenderContext::blitTexture(const TEX::ID &source, const TEX::ID &destination,
    SDL_Rect src, SDL_FRect dst, Program program, bool smooth, const EffectParameters &effects) {
    if (!std::isfinite(dst.x) || !std::isfinite(dst.y) || !std::isfinite(dst.w) || !std::isfinite(dst.h))
        throw std::invalid_argument("Non-finite GPU blit rectangle");
    if (!src.w || !src.h || !dst.w || !dst.h) return;
    TEX::prepareSampling(source);
    const auto &input = source.resource();
    const auto &output = destination.resource();
    if (input.native() == output.native())
        throw std::invalid_argument("GPU blit cannot sample its destination");
    pendingResources.try_emplace(input.native(), source);
    pendingResources.try_emplace(output.native(), destination);
    const bool noScissor = !state.scissor || (state.scissor->x == 0 && state.scissor->y == 0 &&
        state.scissor->w == int(output.width()) && state.scissor->h == int(output.height()));
    const auto integral = [](float value) { return std::isfinite(value) && std::trunc(value) == value; };
    if (program == Program::Simple && noScissor && !source.repeat() && !source.mipmaps() &&
        src.x >= 0 && src.y >= 0 && src.w > 0 && src.h > 0 &&
        Sint64(src.x) + src.w <= input.width() && Sint64(src.y) + src.h <= input.height() &&
        dst.x >= 0 && dst.y >= 0 && dst.w > 0 && dst.h > 0 &&
        integral(dst.x) && integral(dst.y) && integral(dst.w) && integral(dst.h) &&
        double(dst.x) + dst.w <= output.width() && double(dst.y) + dst.h <= output.height()) {
        SDL_GPUBlitInfo info{};
        info.source = {input.native(), 0, 0, Uint32(src.x), Uint32(src.y), Uint32(src.w), Uint32(src.h)};
        info.destination = {output.native(), 0, 0, Uint32(dst.x), Uint32(dst.y), Uint32(dst.w), Uint32(dst.h)};
        info.load_op = SDL_GPU_LOADOP_LOAD;
        info.filter = smooth ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
        encoder.blit(TextureView(output), info);
    } else {
        // Fractional/clipped/mirrored regions and custom filters retain exact
        // shader sampling; only ordinary bounded blits use SDL's native path.
        const float x = dst.x, y = dst.y, r = x + dst.w, b = y + dst.h;
        const float u = float(src.x), v = float(src.y), ur = u + src.w, vb = v + src.h;
        const QuadVertex quad[] = {
            {{x,y},{u,v},{1,1,1,1}}, {{r,y},{ur,v},{1,1,1,1}}, {{r,b},{ur,vb},{1,1,1,1}},
            {{r,b},{ur,vb},{1,1,1,1}}, {{x,b},{u,vb},{1,1,1,1}}, {{x,y},{u,v},{1,1,1,1}},
        };
        VertexParameters vp{};
        vp.projection[0] = 2.f / output.width(); vp.projection[5] = -2.f / output.height();
        vp.projection[12] = -1; vp.projection[13] = vp.projection[15] = 1;
        for (int i = 0; i < 4; ++i) vp.transform[i * 5] = 1;
        vp.textureScaleTranslation[0] = 1.f / input.width();
        vp.textureScaleTranslation[1] = 1.f / input.height();
        auto parameters = effects;
        parameters.filterSize[0] = float(input.width()); parameters.filterSize[1] = float(input.height());
        parameters.filterSize[2] = 1.f / input.width(); parameters.filterSize[3] = 1.f / input.height();
        const SDL_GPUTextureSamplerBinding texture{input.native(), sampler(source, smooth)};
        DrawState drawState;
        drawState.blend = Blend::Disabled;
        drawState.scissor = state.scissor;
        encoder.beginTarget(TextureView(output), SDL_GPU_LOADOP_LOAD);
        encoder.draw(program, quad, std::span(&texture, 1), vp, {}, drawState, parameters);
    }
    TEX::markModified(destination);
    recording = true;
    targetNeedsBegin = true;
}
SDL_GPUSampler *RenderContext::sampler(const TEX::ID &texture, bool smooth) {
    const auto index = size_t(texture.repeat()) | (size_t(smooth) << 1) |
                       (size_t(texture.mipmaps()) << 2);
    if (!samplers[index]) {
        SDL_GPUSamplerCreateInfo info{};
        info.min_filter = info.mag_filter = smooth ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
        info.mipmap_mode = texture.mipmaps() ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        info.address_mode_u = info.address_mode_v = info.address_mode_w = texture.repeat() ?
            SDL_GPU_SAMPLERADDRESSMODE_REPEAT : SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        info.max_lod = texture.mipmaps() ? 1000.f : 0.f;
        samplers[index] = SDL_CreateGPUSampler(gpuDevice.native(), &info);
        if (!samplers[index]) fail("SDL_CreateGPUSampler");
    }
    return samplers[index];
}
void RenderContext::draw(std::span<const QuadVertex> vertices) {
    if (vertices.empty()) return;
    if (!shader) throw std::logic_error("No GPU shader is bound");
    std::array<SDL_GPUTextureSamplerBinding,3> textures{};
    const auto count = QuadPipeline::samplerCount(shader->program);
    for (size_t i = 0; i < count; ++i) {
        const auto &texture = shader->textures[i].empty() ? primaryTexture : shader->textures[i];
        if (!texture.valid()) throw std::invalid_argument("Draw references an unavailable GPU texture");
        TEX::prepareSampling(texture);
    }
    for (size_t i = 0; i < count; ++i) {
        const auto &texture = shader->textures[i].empty() ? primaryTexture : shader->textures[i];
        if (!texture.valid()) throw std::invalid_argument("Draw references an unavailable GPU texture");
        textures[i] = {texture.native(), sampler(texture, texture.smooth())};
        pendingResources.try_emplace(texture.native(), texture);
    }
    ensureTarget();
    encoder.draw(shader->program, vertices, std::span(textures).first(count), shader->vertex,
                 shader->fragment, state, shader->effects, shader->tilemap);
    if (!FBO::boundFramebufferID.empty()) TEX::markModified(FBO::boundFramebufferID.texture());
}
void RenderContext::clear() {
    ensureTarget();
    if (!state.scissor) {
        encoder.beginTarget(*target, SDL_GPU_LOADOP_CLEAR, clearColor);
        if (!FBO::boundFramebufferID.empty()) TEX::markModified(FBO::boundFramebufferID.texture());
        return;
    }
    const SDL_Rect bounds{0,0,int(target->width()),int(target->height())};
    SDL_Rect area{};
    if (!SDL_GetRectIntersection(&bounds, &*state.scissor, &area)) return;
    const float x = float(area.x), y = float(area.y), r = x + float(area.w), b = y + float(area.h);
    const QuadVertex vertices[] = {
        {{x,y},{0,0},{1,1,1,1}}, {{r,y},{0,0},{1,1,1,1}}, {{x,b},{0,0},{1,1,1,1}},
        {{x,b},{0,0},{1,1,1,1}}, {{r,y},{0,0},{1,1,1,1}}, {{r,b},{0,0},{1,1,1,1}},
    };
    VertexParameters vp{};
    vp.projection[0] = 2.f/float(target->width()); vp.projection[5] = -2.f/float(target->height());
    vp.projection[12] = -1; vp.projection[13] = vp.projection[15] = 1;
    for (int i = 0; i < 4; ++i) vp.transform[i*5] = 1;
    FragmentParameters fp{};
    fp.color[0]=clearColor.r; fp.color[1]=clearColor.g; fp.color[2]=clearColor.b; fp.color[3]=clearColor.a;
    DrawState clearState;
    clearState.blend = Blend::Disabled;
    encoder.draw(Program::FlatColor, vertices, {}, vp, fp, clearState);
    if (!FBO::boundFramebufferID.empty()) TEX::markModified(FBO::boundFramebufferID.texture());
}
Fence RenderContext::submit() {
    if (!recording) return {};
    auto result = encoder.submit();
    recording = false;
    pendingResources.clear();
    return result;
}
void RenderContext::flush() {
    if (!recording) return;
    encoder.submitWithoutFence();
    recording = false;
    pendingResources.clear();
}
std::array<int,2> RenderContext::projectionSize(bool scalingAllowed) const {
    return projectionResolver ? projectionResolver(scalingAllowed) :
        std::array<int,2>{viewportRect.w, viewportRect.h};
}

Device &renderDevice() { return RenderContext::current().device(); }
void flushRenderCommands() { if (activeContext) activeContext->flush(); }
void bindPrimaryTexture(TEX::ID value) { RenderContext::current().bindTexture(std::move(value)); }
void setRenderTarget(TextureView value) { RenderContext::current().setTarget(value); }
void bindDefaultRenderTarget() { RenderContext::current().bindDefaultTarget(); }
void clearRenderTarget() { RenderContext::current().clear(); }
bool textureMipmapsEnabled() { return RenderContext::current().mipmapsAllowed(); }
void bindShaderState(const ShaderState *value) { RenderContext::current().setShader(value); }
std::array<int,2> shaderProjectionSize(bool value) { return RenderContext::current().projectionSize(value); }
void drawVertices(std::span<const QuadVertex> vertices) { RenderContext::current().draw(vertices); }
}
