/*
** vertex.h
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

#ifndef VERTEX_H
#define VERTEX_H

#include "etc-internal.h"
#include "../gpu-shader.h"

#include <array>
#include <span>

/* Simple Vertex */
struct SVertex
{
	Vec2 pos;
	Vec2 texPos;
};

/* Color Vertex */
struct CVertex
{
	Vec2 pos;
	Vec4 color;

	CVertex();
};

struct Vertex
{
	Vec2 pos;
	Vec2 texPos;
	Vec4 color;

	Vertex();
};

namespace gpu {
// The render context snapshots the submitted geometry before this call returns.
void drawVertices(std::span<const QuadVertex> vertices);
}

template<class VertType>
struct VertexTraits
{
    static gpu::QuadVertex convert(const VertType &vertex)
    {
        gpu::QuadVertex result{{vertex.pos.x, vertex.pos.y}, {0, 0}, {1, 1, 1, 1}};
        if constexpr (requires { vertex.texPos; }) {
            result.texcoord[0] = vertex.texPos.x;
            result.texcoord[1] = vertex.texPos.y;
        }
        if constexpr (requires { vertex.color; }) {
            result.color[0] = vertex.color.x;
            result.color[1] = vertex.color.y;
            result.color[2] = vertex.color.z;
            result.color[3] = vertex.color.w;
        }
        return result;
    }
};

template<class VertType>
inline void expandQuad(const VertType *vertices, gpu::QuadVertex *triangles)
{
    constexpr std::array<unsigned, 6> indices{0, 1, 2, 2, 3, 0};
    for (size_t index = 0; index < indices.size(); ++index)
        triangles[index] = VertexTraits<VertType>::convert(vertices[indices[index]]);
}

#endif // VERTEX_H
