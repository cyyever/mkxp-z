/*
** quadarray.h
**
** This file is part of mkxp.
**
** Copyright (C) 2013 - 2021 Amaryllis Kulla <ancurio@mapleshrine.eu>
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

#ifndef QUADARRAY_H
#define QUADARRAY_H

#include "vertex.h"
#include "global-ibo.h"
#include "util/exception.h"

#include <vector>

template<class VertexType>
struct QuadArray
{
	std::vector<VertexType> vertices;

	std::vector<gpu::QuadVertex> triangles;
    size_t quadCount = 0;

	void resize(size_t size)
	{
        if (size > static_cast<size_t>(INDEX_T_MAX - 1) / 6)
            throw Exception(Exception::ArgumentError, "Quad batch exceeds index range");
        vertices.resize(size * 4);
		quadCount = size;
	}

	void clear()
	{
		vertices.clear();
        triangles.clear();
		quadCount = 0;
	}

	/* This needs to be called after the final 'append()' call
	 * and previous to the first 'draw()' call. */
	void commit()
    {
        if (quadCount > static_cast<size_t>(INDEX_T_MAX - 1) / 6 || vertices.size() != quadCount * 4)
            throw Exception(Exception::ArgumentError, "Invalid quad batch geometry");
        triangles.resize(quadCount * 6);
        for (size_t quad = 0; quad < quadCount; ++quad)
            expandQuad(vertices.data() + quad * 4, triangles.data() + quad * 6);
    }

    void draw(size_t offset, size_t count)
    {
        if (offset > quadCount || count > quadCount - offset ||
            offset > triangles.size() / 6 || count > triangles.size() / 6 - offset)
            throw Exception(Exception::ArgumentError, "Quad draw range exceeds committed geometry");
        if (count)
            gpu::drawVertices(std::span<const gpu::QuadVertex>(triangles).subspan(offset * 6, count * 6));
    }

	void draw()
	{
		draw(0, quadCount);
	}

	size_t count() const
	{
		return quadCount;
	}
};

typedef QuadArray<Vertex> ColorQuadArray;
typedef QuadArray<SVertex> SimpleQuadArray;

#endif // QUADARRAY_H
