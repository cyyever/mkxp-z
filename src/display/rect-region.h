/*
** rect-region.h
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

#ifndef RECTREGION_H
#define RECTREGION_H

#include <SDL3/SDL_rect.h>

#include <algorithm>
#include <cstddef>
#include <vector>

/* A set of pixels stored as disjoint rectangles.
 *
 * Bitmap uses it to track the area that may hold non-transparent pixels.
 * Callers rely only on that being an over-approximation: reporting an
 * overlap that is not there costs a blend, missing one would be wrong.
 * So once the region grows past kMaxRects it collapses to its bounding box,
 * which keeps every operation O(kMaxRects) even under per-pixel updates. */
class RectRegion
{
public:
	void clear() { rects.clear(); }

	bool empty() const { return rects.empty(); }

	std::size_t rectCount() const { return rects.size(); }

	/* Add 'r' (width/height must be non-negative) */
	void add(const SDL_Rect &r)
	{
		if (SDL_RectEmpty(&r))
			return;

		for (const SDL_Rect &e : rects)
			if (contains(e, r))
				return;

		std::vector<SDL_Rect> next;
		next.reserve(rects.size() + 4);
		for (const SDL_Rect &e : rects)
			cut(e, r, next);
		next.push_back(r);
		rects = std::move(next);

		if (rects.size() > kMaxRects)
			collapse();
	}

	/* Remove 'r' from the region */
	void subtract(const SDL_Rect &r)
	{
		if (SDL_RectEmpty(&r) || !intersects(r))
			return;

		std::vector<SDL_Rect> next;
		next.reserve(rects.size() + 4);
		for (const SDL_Rect &e : rects)
			cut(e, r, next);
		rects = std::move(next);

		if (rects.size() > kMaxRects)
			collapse();
	}

	bool intersects(const SDL_Rect &r) const
	{
		if (SDL_RectEmpty(&r))
			return false;

		return std::any_of(rects.begin(), rects.end(),
		                   [&](const SDL_Rect &e) { return SDL_HasRectIntersection(&e, &r); });
	}

private:
	static constexpr std::size_t kMaxRects = 64;

	std::vector<SDL_Rect> rects;

	static bool contains(const SDL_Rect &outer, const SDL_Rect &inner)
	{
		return inner.x >= outer.x && inner.y >= outer.y &&
		       inner.x + inner.w <= outer.x + outer.w &&
		       inner.y + inner.h <= outer.y + outer.h;
	}

	/* Append the parts of 'e' outside 'c' to 'out' (at most four bands) */
	static void cut(const SDL_Rect &e, const SDL_Rect &c, std::vector<SDL_Rect> &out)
	{
		SDL_Rect overlap;
		if (!SDL_GetRectIntersection(&e, &c, &overlap))
		{
			out.push_back(e);
			return;
		}

		const int eRight = e.x + e.w, eBottom = e.y + e.h;
		const int oRight = overlap.x + overlap.w, oBottom = overlap.y + overlap.h;

		if (overlap.y > e.y)
			out.push_back({e.x, e.y, e.w, overlap.y - e.y});
		if (oBottom < eBottom)
			out.push_back({e.x, oBottom, e.w, eBottom - oBottom});
		if (overlap.x > e.x)
			out.push_back({e.x, overlap.y, overlap.x - e.x, overlap.h});
		if (oRight < eRight)
			out.push_back({oRight, overlap.y, eRight - oRight, overlap.h});
	}

	void collapse()
	{
		SDL_Rect bounds = rects.front();
		for (const SDL_Rect &e : rects)
		{
			SDL_Rect merged;
			SDL_GetRectUnion(&bounds, &e, &merged);
			bounds = merged;
		}
		rects.assign(1, bounds);
	}
};

#endif // RECTREGION_H
