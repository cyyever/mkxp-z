#include "gpu-embedded.h"
#include <string_view>
#include <stdexcept>
#include <string>
#include "vertexMain.dxbc.xxd"
#include "tilemapVertexMain.dxbc.xxd"
#include "simpleMain.dxbc.xxd"
#include "flatColorMain.dxbc.xxd"
#include "vertexColorMain.dxbc.xxd"
#include "vertexAlphaMain.dxbc.xxd"
#include "uniformAlphaMain.dxbc.xxd"
#include "planeMain.dxbc.xxd"
#include "transitionMain.dxbc.xxd"
#include "simpleTransitionMain.dxbc.xxd"
#include "bitmapBlendMain.dxbc.xxd"
#include "hueMain.dxbc.xxd"
#include "spriteMain.dxbc.xxd"
#include "tilemapMain.dxbc.xxd"
#include "flashMapMain.dxbc.xxd"
#include "grayMain.dxbc.xxd"
#include "blurMain.dxbc.xxd"
#include "invertMain.dxbc.xxd"
#include "compressAlphaMain.dxbc.xxd"
#include "subtractMain.dxbc.xxd"
#include "bicubicMain.dxbc.xxd"
#include "lanczosMain.dxbc.xxd"
#include "shadowHorizontalMain.dxbc.xxd"
#include "shadowVerticalMain.dxbc.xxd"
#ifdef MKXPZ_HAVE_EXTRA_SHADERS
#include "xbrzMain.dxbc.xxd"
#endif
namespace gpu {
std::unique_ptr<QuadPipeline> createEnginePipelines(SDL_GPUDevice *device) {
    struct Blob { std::string_view name; std::span<const Uint8> bytes; };
    const Blob blobs[] = {
        {"simpleMain", {mkxp_gpu_simpleMain, mkxp_gpu_simpleMain_len}},
        {"flatColorMain", {mkxp_gpu_flatColorMain, mkxp_gpu_flatColorMain_len}},
        {"vertexColorMain", {mkxp_gpu_vertexColorMain, mkxp_gpu_vertexColorMain_len}},
        {"vertexAlphaMain", {mkxp_gpu_vertexAlphaMain, mkxp_gpu_vertexAlphaMain_len}},
        {"uniformAlphaMain", {mkxp_gpu_uniformAlphaMain, mkxp_gpu_uniformAlphaMain_len}},
        {"planeMain", {mkxp_gpu_planeMain, mkxp_gpu_planeMain_len}},
        {"transitionMain", {mkxp_gpu_transitionMain, mkxp_gpu_transitionMain_len}},
        {"simpleTransitionMain", {mkxp_gpu_simpleTransitionMain, mkxp_gpu_simpleTransitionMain_len}},
        {"bitmapBlendMain", {mkxp_gpu_bitmapBlendMain, mkxp_gpu_bitmapBlendMain_len}},
        {"hueMain", {mkxp_gpu_hueMain, mkxp_gpu_hueMain_len}},
        {"spriteMain", {mkxp_gpu_spriteMain, mkxp_gpu_spriteMain_len}},
        {"tilemapMain", {mkxp_gpu_tilemapMain, mkxp_gpu_tilemapMain_len}},
        {"flashMapMain", {mkxp_gpu_flashMapMain, mkxp_gpu_flashMapMain_len}},
        {"grayMain", {mkxp_gpu_grayMain, mkxp_gpu_grayMain_len}},
        {"blurMain", {mkxp_gpu_blurMain, mkxp_gpu_blurMain_len}},
        {"invertMain", {mkxp_gpu_invertMain, mkxp_gpu_invertMain_len}},
        {"compressAlphaMain", {mkxp_gpu_compressAlphaMain, mkxp_gpu_compressAlphaMain_len}},
        {"subtractMain", {mkxp_gpu_subtractMain, mkxp_gpu_subtractMain_len}},
        {"bicubicMain", {mkxp_gpu_bicubicMain, mkxp_gpu_bicubicMain_len}},
        {"lanczosMain", {mkxp_gpu_lanczosMain, mkxp_gpu_lanczosMain_len}},
        {"shadowHorizontalMain", {mkxp_gpu_shadowHorizontalMain, mkxp_gpu_shadowHorizontalMain_len}},
        {"shadowVerticalMain", {mkxp_gpu_shadowVerticalMain, mkxp_gpu_shadowVerticalMain_len}},
#ifdef MKXPZ_HAVE_EXTRA_SHADERS
        {"xbrzMain", {mkxp_gpu_xbrzMain, mkxp_gpu_xbrzMain_len}},
#endif
    };
    QuadPipeline::FragmentBytecode fragments{};
    for (size_t i=0; i<fragments.size(); ++i) {
        const auto program = static_cast<Program>(i);
        for (const auto &blob : blobs)
            if (blob.name==QuadPipeline::entryPoint(program)) { fragments[i]=blob.bytes; break; }
#ifndef MKXPZ_HAVE_EXTRA_SHADERS
        if (program == Program::Xbrz) continue;
#endif
        if (fragments[i].empty())
            throw std::logic_error(std::string("Missing embedded GPU shader: ") + QuadPipeline::entryPoint(program));
    }
    return std::make_unique<QuadPipeline>(device,
        std::span(mkxp_gpu_vertexMain, mkxp_gpu_vertexMain_len), fragments,
        std::span(mkxp_gpu_tilemapVertexMain, mkxp_gpu_tilemapVertexMain_len));
}
}
