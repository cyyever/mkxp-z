/*
** render-ops.h
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

#ifndef MKXP_RENDER_OPS_H
#define MKXP_RENDER_OPS_H


#include "render-resources.h"
#include "vertex.h"
#include <cstring>
#include <type_traits>

#include <SDL3/SDL_surface.h>

namespace RenderOps
{

/* EXT_unpack_subimage */
void subRectImageUpload(int srcW, int srcX, int srcY,
                        int dstX, int dstY, int dstW, int dstH,
                        SDL_Surface *src, SDL_PixelFormat format);


/* ARB_vertex_array_object */
struct VAO
{
    std::size_t vertSize = 0;
    VBO::ID vbo;
    IBO::ID ibo;
    gpu::QuadVertex (*convert)(const void *) = nullptr;
};

template<class VertexType>
inline void vaoFillInVertexData(VAO &vao)
{
    static_assert(std::is_trivially_copyable_v<VertexType>);
    vao.vertSize = sizeof(VertexType);
    vao.convert = [](const void *bytes) {
        VertexType vertex;
        std::memcpy(&vertex, bytes, sizeof(vertex));
        return VertexTraits<VertexType>::convert(vertex);
    };
}

void vaoInit(VAO &vao, bool keepBound = false);
void vaoFini(VAO &vao);
void vaoBind(VAO &vao);
void vaoUnbind(VAO &vao);
void drawElements(std::size_t count, std::size_t byteOffset = 0);

/* EXT_framebuffer_blit */
int blitScaleIsSpecial(TEXFBO &target, bool targetPreferHires, const IntRect &targetRect, TEXFBO &source, const IntRect &sourceRect);
int smoothScalingMethod(int scaleIsSpecial);
void blitBegin(TEXFBO &target, bool preferHires = false, int scaleIsOne = 0);
void blitBeginScreen(const Vec2i &size, int scaleIsOne = 0);
void blitSource(TEXFBO &source, int scaleIsOne = 0);
void blitRectangle(const IntRect src, const Vec2i &dstPos);
void blitRectangle(const IntRect src, const IntRect &dst,
                   bool smooth = false);
void blitEnd();

}

#endif // MKXP_RENDER_OPS_H
