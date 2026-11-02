/*
** tilemap.h
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

#ifndef TILEMAP_H
#define TILEMAP_H

#include "disposable.h"

#include "util.h"

class Viewport;
class Bitmap;
class Table;

struct Color;
struct Tone;

struct TilemapPrivate;

class Tilemap : public Disposable
{
public:
	class Autotiles
	{
	public:
		void set(int i, Bitmap *bitmap);

	private:
		Autotiles() = default;
		~Autotiles() = default;

		TilemapPrivate *p;
		friend class Tilemap;
		friend struct TilemapPrivate;
	};

	Tilemap(Viewport *viewport = nullptr);
	~Tilemap() override;

	void update();

	Autotiles &getAutotiles();

	void setTileset(Bitmap* value);
	void setMapData(Table* value);
	void setFlashData(Table* value);
	void setPriorities(Table* value);
	DECL_ATTR( Visible,    bool      )
	DECL_ATTR( OX,         int       )
	DECL_ATTR( OY,         int       )

	DECL_ATTR( Opacity,   int     )
	DECL_ATTR( BlendType, int     )
	DECL_ATTR( Color,     Color&  )
	DECL_ATTR( Tone,      Tone&   )

	void initDynAttribs();

private:
	TilemapPrivate *p;
	Autotiles atProxy;

	void releaseResources() override;
	const char *klassName() const override { return "tilemap"; }
};

#endif // TILEMAP_H
