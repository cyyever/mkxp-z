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


#include "display/gpu-resources.h"
#include "etc-internal.h"

#include <SDL3/SDL_surface.h>

namespace RenderOps
{

/* Upload a surface region without changing its pixel format. */
void subRectImageUpload(int srcX, int srcY,
                        int dstX, int dstY, int dstW, int dstH,
                        SDL_Surface *src);


/* Texture copies use RGSS high-resolution coordinate rules. */
int blitScaleIsSpecial(TEXFBO &target, bool targetPreferHires, const IntRect &targetRect, TEXFBO &source, const IntRect &sourceRect);
int smoothScalingMethod(int scaleIsSpecial);
void blit(TEXFBO &target, TEXFBO &source, const IntRect &sourceRect, const IntRect &targetRect,
          bool preferHires = false, int scale = 0, bool smooth = false);

}

#endif // MKXP_RENDER_OPS_H
