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
#include "render-state.h"
#include "quad.h"
#include "shader.h"
#include "global-ibo.h"
#include "config.h"
#include "etc.h"
#include "util/exception.h"
#include <memory>
#include <cstdint>
#include <vector>

namespace RenderOps {
void subRectImageUpload(int, int srcX, int srcY, int dstX, int dstY, int width, int height,
                        SDL_Surface *surface, SDL_PixelFormat) {
    if(!surface || srcX<0 || srcY<0 || width<0 || height<0 ||
       std::int64_t(srcX)+width>surface->w || std::int64_t(srcY)+height>surface->h)
        throw Exception(Exception::ArgumentError,"Invalid surface upload rectangle");
    if(!width || !height) return;
    const auto *pixels=static_cast<const std::byte *>(surface->pixels)+
        std::size_t(srcY)*surface->pitch+std::size_t(srcX)*SDL_BYTESPERPIXEL(surface->format);
    TEX::uploadSubImage(dstX,dstY,width,height,pixels,surface->format,surface->pitch);
}
namespace { thread_local VAO *active = nullptr; }
void vaoInit(VAO &vao,bool keepBound) {
    if(!vao.convert || !vao.vertSize) throw Exception(Exception::ArgumentError,"Missing vertex layout");
    if(keepBound) { VBO::bind(vao.vbo); IBO::bind(vao.ibo); active=&vao; }
}
void vaoFini(VAO &vao) { if(active==&vao) active=nullptr; }
void vaoBind(VAO &vao) { active=&vao; VBO::bind(vao.vbo); IBO::bind(vao.ibo); }
void vaoUnbind(VAO &) { active=nullptr; }
void drawElements(std::size_t count,std::size_t byteOffset) {
    if(!count) return;
    if(!active || !active->convert || !active->vertSize || count%3)
        throw Exception(Exception::ArgumentError,"Invalid indexed geometry");
    const auto indices=active->ibo.bytes(), vertices=active->vbo.bytes();
    if(byteOffset%sizeof(index_t) || byteOffset>indices.size() || count>(indices.size()-byteOffset)/sizeof(index_t))
        throw Exception(Exception::ArgumentError,"Index buffer range exceeded");
    thread_local std::vector<gpu::QuadVertex> converted;
    converted.resize(count);
    for(std::size_t i=0;i<count;++i) {
        index_t index; std::memcpy(&index,indices.data()+byteOffset+i*sizeof(index_t),sizeof(index));
        const auto offset=std::size_t(index)*active->vertSize;
        if(offset>vertices.size() || active->vertSize>vertices.size()-offset)
            throw Exception(Exception::ArgumentError,"Vertex buffer range exceeded");
        converted[i]=active->convert(vertices.data()+offset);
    }
    gpu::drawVertices(converted);
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

static void _blitBegin(FBO::ID fbo, const Vec2i &size, int scaleIsSpecial)
{

		FBO::bind(fbo);
		renderState.viewport.pushSet(IntRect(0, 0, size.x, size.y));

		switch (smoothScalingMethod(scaleIsSpecial))
		{
		case Bicubic:
		{
			BicubicShader &shader = shState->shaders().bicubic;
			shader.bind();
			shader.applyViewportProj();
			shader.setTranslation(Vec2i());
			shader.setTexSize(Vec2i(size.x, size.y));
			shader.setSharpness(shState->config().bicubicSharpness);
		}

			break;
		case Lanczos3:
		{
			Lanczos3Shader &shader = shState->shaders().lanczos3;
			shader.bind();
			shader.applyViewportProj();
			shader.setTranslation(Vec2i());
			shader.setTexSize(Vec2i(size.x, size.y));
		}

			break;
#ifdef MKXPZ_HAVE_EXTRA_SHADERS
		case xBRZ:
		{
			XbrzShader &shader = shState->shaders().xbrz;
			shader.bind();
			shader.applyViewportProj();
			shader.setTranslation(Vec2i());
			shader.setTexSize(Vec2i(size.x, size.y));
			shader.setTargetScale(Vec2(1., 1.));
		}

			break;
#endif
		default:
		{
			SimpleShader &shader = shState->shaders().simple;
			shader.bind();
			shader.applyViewportProj();
			shader.setTranslation(Vec2i());
			shader.setTexSize(Vec2i(size.x, size.y));
		}
		}

}

int blitDstWidthLores = 1;
int blitDstWidthHires = 1;
int blitDstHeightLores = 1;
int blitDstHeightHires = 1;

int blitSrcWidthLores = 1;
int blitSrcWidthHires = 1;
int blitSrcHeightLores = 1;
int blitSrcHeightHires = 1;

void blitBegin(TEXFBO &target, bool preferHires, int scaleIsSpecial)
{
	blitDstWidthLores = target.width;
	blitDstHeightLores = target.height;

	if (preferHires && target.selfHires != nullptr) {
		blitDstWidthHires = target.selfHires->width;
		blitDstHeightHires = target.selfHires->height;
		_blitBegin(target.selfHires->fbo, Vec2i(target.selfHires->width, target.selfHires->height), scaleIsSpecial);
	}
	else {
		blitDstWidthHires = blitDstWidthLores;
		blitDstHeightHires = blitDstHeightLores;
		_blitBegin(target.fbo, Vec2i(target.width, target.height), scaleIsSpecial);
	}
}

void blitBeginScreen(const Vec2i &size, int scaleIsSpecial)
{
	blitDstWidthLores = 1;
	blitDstWidthHires = 1;
	blitDstHeightLores = 1;
	blitDstHeightHires = 1;

	_blitBegin(FBO::ID(0), size, scaleIsSpecial);
}

void blitSource(TEXFBO &source, int scaleIsSpecial)
{
	blitSrcWidthLores = source.width;
	blitSrcHeightLores = source.height;
	if (source.selfHires != nullptr) {
		blitSrcWidthHires = source.selfHires->width;
		blitSrcHeightHires = source.selfHires->height;
	}
	else {
		blitSrcWidthHires = blitSrcWidthLores;
		blitSrcHeightHires = blitSrcHeightLores;
	}


		switch (smoothScalingMethod(scaleIsSpecial))
		{
		case Bicubic:
		{
			BicubicShader &shader = shState->shaders().bicubic;
			shader.bind();
			shader.setTexSize(Vec2i(blitSrcWidthHires, blitSrcHeightHires));
		}

			break;
		case Lanczos3:
		{
			Lanczos3Shader &shader = shState->shaders().lanczos3;
			shader.bind();
			shader.setTexSize(Vec2i(blitSrcWidthHires, blitSrcHeightHires));
		}

			break;
#ifdef MKXPZ_HAVE_EXTRA_SHADERS
		case xBRZ:
		{
			XbrzShader &shader = shState->shaders().xbrz;
			shader.bind();
			shader.setTexSize(Vec2i(blitSrcWidthHires, blitSrcHeightHires));
		}

			break;
#endif
		default:
		{
			SimpleShader &shader = shState->shaders().simple;
			shader.bind();
			shader.setTexSize(Vec2i(blitSrcWidthHires, blitSrcHeightHires));
		}
		}
		if (source.selfHires != nullptr) {
			TEX::bind(source.selfHires->tex);
		}
		else {
			TEX::bind(source.tex);
		}

}

void blitRectangle(const IntRect src, const Vec2i &dstPos)
{
	blitRectangle(src, IntRect(dstPos.x, dstPos.y, src.w, src.h), false);
}

void blitRectangle(const IntRect src, const IntRect &dst, bool smooth)
{
	// Handle high-res dest
	int scaledDstX = dst.x * blitDstWidthHires / blitDstWidthLores;
	int scaledDstY = dst.y * blitDstHeightHires / blitDstHeightLores;
	int scaledDstWidth = dst.w * blitDstWidthHires / blitDstWidthLores;
	int scaledDstHeight = dst.h * blitDstHeightHires / blitDstHeightLores;
	IntRect dstScaled(scaledDstX, scaledDstY, scaledDstWidth, scaledDstHeight);

	// Handle high-res source
	int scaledSrcX = src.x * blitSrcWidthHires / blitSrcWidthLores;
	int scaledSrcY = src.y * blitSrcHeightHires / blitSrcHeightLores;
	int scaledSrcWidth = src.w * blitSrcWidthHires / blitSrcWidthLores;
	int scaledSrcHeight = src.h * blitSrcHeightHires / blitSrcHeightLores;
	IntRect srcScaled(scaledSrcX, scaledSrcY, scaledSrcWidth, scaledSrcHeight);


#ifdef MKXPZ_HAVE_EXTRA_SHADERS
		if (shState->config().smoothScaling == xBRZ)
		{
			XbrzShader &shader = shState->shaders().xbrz;
			shader.setTargetScale(Vec2((float)(shState->config().xbrzScalingFactor), (float)(shState->config().xbrzScalingFactor)));
		}
#endif
		if (smooth)
			TEX::setSmooth(true);

		renderState.blend.pushSet(false);
		Quad &quad = shState->gpQuad();
		quad.setTexPosRect(srcScaled, dstScaled);
		quad.draw();
		renderState.blend.pop();

		if (smooth)
			TEX::setSmooth(false);

}

void blitEnd()
{
	blitDstWidthLores = 1;
	blitDstWidthHires = 1;
	blitDstHeightLores = 1;
	blitDstHeightHires = 1;

	blitSrcWidthLores = 1;
	blitSrcWidthHires = 1;
	blitSrcHeightLores = 1;
	blitSrcHeightHires = 1;

	renderState.viewport.pop();
}

}
