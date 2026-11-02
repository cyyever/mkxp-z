/*
** render-ops.cpp
**
** This file is part of mkxp.
**
** Copyright (C) 2014 - 2021 Amaryllis Kulla <ancurio@mapleshrine.eu>
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** mkxp is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "render-ops.h"

#include "sharedstate.h"
#include "display/gpu-render-context.h"
#include "graphics.h"
#include "config.h"
#include "etc.h"
#include "util/exception.h"
#include <cstdint>

namespace RenderOps {
void subRectImageUpload(int srcX, int srcY, int dstX, int dstY, int width, int height,
                        SDL_Surface *surface) {
    if(!surface || srcX<0 || srcY<0 || width<0 || height<0 ||
       std::int64_t(srcX)+width>surface->w || std::int64_t(srcY)+height>surface->h)
        throw Exception(Exception::ArgumentError,"Invalid surface upload rectangle");
    if(!width || !height) return;
    const auto *pixels=static_cast<const std::byte *>(surface->pixels)+
        std::size_t(srcY)*surface->pitch+std::size_t(srcX)*SDL_BYTESPERPIXEL(surface->format);
    TEX::uploadSubImage(dstX,dstY,width,height,pixels,surface->format,surface->pitch);
}
int blitScaleIsSpecial(TEXFBO &target, bool targetPreferHires, const IntRect &targetRect, TEXFBO &source, const IntRect &sourceRect)
{
	int targetWidth = targetRect.w;
	int targetHeight = targetRect.h;

	int sourceWidth = sourceRect.w;
	int sourceHeight = sourceRect.h;

	if (targetPreferHires && target.selfHires != nullptr)
	{
		targetWidth *= target.selfHires->width;
		targetWidth /= target.width;

		targetHeight *= target.selfHires->height;
		targetHeight /= target.height;
	}

	if (source.selfHires != nullptr)
	{
		sourceWidth *= source.selfHires->width;
		sourceWidth /= source.width;

		sourceHeight *= source.selfHires->height;
		sourceHeight /= source.height;
	}

	if (targetWidth == sourceWidth && targetHeight == sourceHeight)
	{
		return SameScale;
	}

	if (targetWidth < sourceWidth && targetHeight < sourceHeight)
	{
		return DownScale;
	}

	return UpScale;
}

int smoothScalingMethod(int scaleIsSpecial)
{
	switch (scaleIsSpecial)
	{
	case SameScale:
		return NearestNeighbor;
	case DownScale:
		return shState->config().smoothScalingDown;
	}

	return shState->config().smoothScaling;
}

void blit(TEXFBO &target, TEXFBO &source, const IntRect &srcRect, const IntRect &dstRect,
          bool preferHires, int scale, bool smooth) {
    auto &output = preferHires && target.selfHires ? *target.selfHires : target;
    auto &input = source.selfHires ? *source.selfHires : source;
    if (target.width <= 0 || target.height <= 0 || source.width <= 0 || source.height <= 0)
        throw Exception(Exception::ArgumentError, "Blit requires positive texture dimensions");
    const SDL_Rect src{srcRect.x * input.width / source.width,
        srcRect.y * input.height / source.height, srcRect.w * input.width / source.width,
        srcRect.h * input.height / source.height};
    IntRect dst(dstRect.x * output.width / target.width, dstRect.y * output.height / target.height,
                dstRect.w * output.width / target.width, dstRect.h * output.height / target.height);
    FBO::bind(output.fbo);
    TEX::bind(input.tex);
    if (smooth) TEX::setSmooth(true);
    float projectionWidth = float(output.width), projectionHeight = float(output.height);
    if (shState->config().enableHires && shState->graphics().isPingPongFramebufferActive()) {
        projectionWidth = float(shState->graphics().width());
        projectionHeight = float(shState->graphics().height());
    }
    const float sx = output.width / projectionWidth, sy = output.height / projectionHeight;
    const SDL_FRect destination{dst.x * sx, dst.y * sy, dst.w * sx, dst.h * sy};
    gpu::Program program = gpu::Program::Simple;
    gpu::EffectParameters effects{};
    switch (smoothScalingMethod(scale)) {
    case Bicubic:
        program = gpu::Program::Bicubic;
        effects.filterParameters[0] = 1.f - shState->config().bicubicSharpness * .01f;
        effects.filterParameters[1] = shState->config().bicubicSharpness * .005f;
        break;
    case Lanczos3: program = gpu::Program::Lanczos; break;
#ifdef MKXPZ_HAVE_EXTRA_SHADERS
    case xBRZ:
        program = gpu::Program::Xbrz;
        effects.filterParameters[2] = effects.filterParameters[3] =
            shState->config().smoothScaling == xBRZ ? float(shState->config().xbrzScalingFactor) : 1.f;
        break;
#endif
    }
    gpu::RenderContext::current().blitTexture(input.tex, output.tex, src, destination,
                                            program, smooth || input.tex.smooth(), effects);
    if (smooth) TEX::setSmooth(false);
}
}
