/*
** render-state.h
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

#ifndef MKXP_RENDER_STATE_H
#define MKXP_RENDER_STATE_H
#include "etc.h"
#include <cassert>
#include <functional>
#include <stack>
#include <utility>
struct Config;

template<class T> class RenderProperty {
public:
    using Apply = std::function<void(const T &)>;
    explicit RenderProperty(Apply callback = {}) : apply(std::move(callback)) {}
    ~RenderProperty() { assert(stack.empty()); }
    void init(const T &value) { current=value; refresh(); }
    void push() { stack.push(current); }
    void pop() { assert(!stack.empty()); set(stack.top()); stack.pop(); }
    const T &get() const { return current; }
    void set(const T &value) { if (!(value==current)) init(value); }
    void pushSet(const T &value) { push(); set(value); }
    void refresh() { if(apply) apply(current); }
private:
    T current{};
    std::stack<T> stack;
    Apply apply;
};
class RenderScissorBox : public RenderProperty<IntRect> {
public:
    using RenderProperty::RenderProperty;
    void setIntersect(const IntRect &);
};
class RenderState {
public:
    RenderProperty<Vec4> clearColor;
    RenderScissorBox scissorBox;
    RenderProperty<bool> scissorTest;
    RenderProperty<BlendType> blendMode;
    RenderProperty<bool> blend;
    RenderProperty<IntRect> viewport;
    struct Caps { int maxTexSize=16384; } caps;
    explicit RenderState(const Config &);
private:
    void updateScissor();
    void updateBlend();
};
#endif
