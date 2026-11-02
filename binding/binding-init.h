/*
** binding-init.h
**
** This file is part of mkxp.
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

#ifndef BINDINGINIT_H
#define BINDINGINIT_H

#include "binding-util.h"

class Bitmap;

/* Module/class registration, called once from the MRI binding */
void tableBindingInit();
void etcBindingInit();
void fontBindingInit();
void bitmapBindingInit();
void spriteBindingInit();
void viewportBindingInit();
void planeBindingInit();
void windowBindingInit();
void tilemapBindingInit();

void inputBindingInit();
void audioBindingInit();
void graphicsBindingInit();

void fileIntBindingInit();

#ifdef MKXPZ_MINIFFI
void MiniFFIBindingInit();
#endif

/* Shared between binding translation units */
void bitmapInitProps(Bitmap *b, VALUE self);
VALUE kernelLoadDataInt(const char *filename, bool rubyExc, bool raw);

#endif // BINDINGINIT_H
