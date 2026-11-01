/*
** render-state.cpp
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

#include "render-state.h"
#include "gpu-render-context.h"
#include "config.h"
#include "graphics.h"
#include "sharedstate.h"
#include <cmath>

void RenderScissorBox::setIntersect(const IntRect &value) {
    const auto &current=get();
    SDL_Rect a{current.x,current.y,current.w,current.h}, b{value.x,value.y,value.w,value.h}, result{};
    SDL_GetRectIntersection(&a,&b,&result);
    set(IntRect(result.x,result.y,result.w,result.h));
}
void RenderState::updateScissor() {
    if(!scissorTest.get()) { gpu::RenderContext::current().setScissor(std::nullopt); return; }
    const auto &value=scissorBox.get();
    double scale=1;
    if(shState && shState->config().enableHires && shState->graphics().isPingPongFramebufferActive())
        scale=shState->config().framebufferScalingFactor;
    gpu::RenderContext::current().setScissor(SDL_Rect{int(std::lround(scale*value.x)),int(std::lround(scale*value.y)),
                                                    int(std::lround(scale*value.w)),int(std::lround(scale*value.h))});
}
void RenderState::updateBlend() {
    gpu::Blend result=gpu::Blend::Disabled;
    if(blend.get()) switch(blendMode.get()) {
        case BlendNormal: result=gpu::Blend::Normal; break;
        case BlendAddition: result=gpu::Blend::Addition; break;
        case BlendSubstraction: result=gpu::Blend::Subtraction; break;
        case BlendKeepDestAlpha: result=gpu::Blend::KeepDestinationAlpha; break;
    }
    gpu::RenderContext::current().setBlend(result);
}
RenderState::RenderState(const Config &conf)
    : clearColor([](const Vec4 &v){gpu::RenderContext::current().setClearColor({v.x,v.y,v.z,v.w});}),
      scissorBox([this](const IntRect &){updateScissor();}),
      scissorTest([this](const bool &){updateScissor();}),
      blendMode([this](const BlendType &){updateBlend();}),
      blend([this](const bool &){updateBlend();}),
      viewport([](const IntRect &v){gpu::RenderContext::current().setViewport({v.x,v.y,v.w,v.h});}) {
    // D3D12's texture2D dimension limit; the Windows GPU backend uses D3D12.
    // Keep the user-provided lower/override limit used by mega-bitmap handling.
    if(conf.maxTextureSize>0) caps.maxTexSize=conf.maxTextureSize;
    clearColor.init(Vec4(0,0,0,1)); blendMode.init(BlendNormal); blend.init(true);
    scissorTest.init(false); scissorBox.init(IntRect(0,0,conf.defScreenW,conf.defScreenH));
    viewport.init(IntRect(0,0,conf.defScreenW,conf.defScreenH));
}
