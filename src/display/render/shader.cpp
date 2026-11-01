/*
** shader.cpp
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

#include "shader.h"
#include "gpu-resources.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
void vector4(float *destination, const Vec4 &value) {
    destination[0]=value.x; destination[1]=value.y; destination[2]=value.z; destination[3]=value.w;
}
void matrix(float *destination, const float *value) { std::copy_n(value,16,destination); }
void sharpness(gpu::ShaderState &state,int value) {
    state.effects.filterParameters[0]=1.f-value*.01f;
    state.effects.filterParameters[1]=value*.005f;
}
}
Shader::Shader(gpu::Program program) {
    state.program=program;
    for(int i=0;i<4;++i) state.vertex.transform[i*5]=1;
}
void Shader::bind() { gpu::bindShaderState(&state); }
void Shader::unbind() { gpu::bindShaderState(nullptr); }
ShaderBase::ShaderBase(gpu::Program program) : Shader(program),projMat(*this) {}
void ShaderBase::Projection::set(const Vec2i &size) {
    if(size.x<=0||size.y<=0) throw std::invalid_argument("Shader projection requires positive dimensions");
    auto &projection=owner.state.vertex.projection;
    std::fill_n(projection,16,0.f);
    // Native GPU texture coordinates/readback are top-down; D3D depth is [0,1].
    projection[0]=2.f/size.x; projection[5]=-2.f/size.y;
    projection[12]=-1; projection[13]=1; projection[15]=1;
}
void ShaderBase::applyViewportProj() {
    const auto size=gpu::shaderProjectionSize(framebufferScalingAllowed());
    projMat.set(Vec2i(size[0],size[1]));
}
bool ShaderBase::framebufferScalingAllowed() { return true; }
void ShaderBase::setTexSize(const Vec2i &size) {
    if(size.x<=0||size.y<=0) throw std::invalid_argument("Shader texture requires positive dimensions");
    state.vertex.textureScaleTranslation[0]=1.f/size.x;
    state.vertex.textureScaleTranslation[1]=1.f/size.y;
    state.effects.filterSize[0]=float(size.x); state.effects.filterSize[1]=float(size.y);
    state.effects.filterSize[2]=1.f/size.x; state.effects.filterSize[3]=1.f/size.y;
    if(blurAxis>=0) {
        state.effects.blurDirection[0]=blurAxis==0?1.f/size.x:0;
        state.effects.blurDirection[1]=blurAxis==1?1.f/size.y:0;
    }
}
void ShaderBase::setTranslation(const Vec2i &value) {
    state.vertex.textureScaleTranslation[2]=float(value.x);
    state.vertex.textureScaleTranslation[3]=float(value.y);
}
#define CONSTRUCTOR(Class,Effect) Class::Class() : ShaderBase(gpu::Program::Effect) {}
CONSTRUCTOR(FlatColorShader,FlatColor)
CONSTRUCTOR(SimpleShader,Simple)
CONSTRUCTOR(SimpleColorShader,VertexColor)
CONSTRUCTOR(SimpleAlphaShader,VertexAlpha)
CONSTRUCTOR(SimpleSpriteShader,Simple)
CONSTRUCTOR(AlphaSpriteShader,UniformAlpha)
CONSTRUCTOR(TransShader,Transition)
CONSTRUCTOR(SimpleTransShader,SimpleTransition)
CONSTRUCTOR(SpriteShader,Sprite)
CONSTRUCTOR(PlaneShader,Plane)
CONSTRUCTOR(GrayShader,Gray)
CONSTRUCTOR(TilemapShader,Tilemap)
CONSTRUCTOR(FlashMapShader,FlashMap)
CONSTRUCTOR(HueShader,Hue)
CONSTRUCTOR(SimpleMatrixShader,VertexAlpha)
CONSTRUCTOR(BltShader,BitmapBlend)
CONSTRUCTOR(KglInvertShader,Invert)
CONSTRUCTOR(KglCompressAlphaShader,CompressAlpha)
CONSTRUCTOR(KglShadowShaderH,ShadowHorizontal)
CONSTRUCTOR(KglShadowShaderV,ShadowVertical)
#undef CONSTRUCTOR
SimpleShader::SimpleShader(const ShaderNoConstructTag &) {}
SimpleSpriteShader::SimpleSpriteShader(const ShaderNoConstructTag &) {}
BltShader::BltShader(const ShaderNoConstructTag &) { state.program=gpu::Program::BitmapBlend; }
Lanczos3Shader::Lanczos3Shader() { state.program=gpu::Program::Lanczos; }
Lanczos3Shader::Lanczos3Shader(const ShaderNoConstructTag &) {}
BicubicShader::BicubicShader() { state.program=gpu::Program::Bicubic; }
Lanczos3SpriteShader::Lanczos3SpriteShader() { state.program=gpu::Program::Lanczos; }
Lanczos3SpriteShader::Lanczos3SpriteShader(const ShaderNoConstructTag &) {}
BicubicSpriteShader::BicubicSpriteShader() { state.program=gpu::Program::Bicubic; }
#ifdef MKXPZ_HAVE_EXTRA_SHADERS
XbrzShader::XbrzShader() { state.program=gpu::Program::Xbrz; }
XbrzSpriteShader::XbrzSpriteShader() { state.program=gpu::Program::Xbrz; }
void XbrzShader::setTargetScale(const Vec2 &value) {
    state.effects.filterParameters[2]=value.x; state.effects.filterParameters[3]=value.y;
}
void XbrzSpriteShader::setTargetScale(const Vec2 &value) {
    state.effects.filterParameters[2]=value.x; state.effects.filterParameters[3]=value.y;
}
#endif
BlurShader::HPass::HPass() : ShaderBase(gpu::Program::Blur) { blurAxis=0; }
BlurShader::VPass::VPass() : ShaderBase(gpu::Program::Blur) { blurAxis=1; }
KglSubtractShader::KglSubtractShader() { state.program=gpu::Program::Subtract; }
#define VEC_SET(Class,Method,Member) void Class::Method(const Vec4 &value) { vector4(state.Member,value); }
VEC_SET(FlatColorShader,setColor,fragment.color)
VEC_SET(SpriteShader,setTone,fragment.tone)
VEC_SET(SpriteShader,setColor,fragment.color)
VEC_SET(PlaneShader,setTone,fragment.tone)
VEC_SET(PlaneShader,setColor,fragment.color)
VEC_SET(PlaneShader,setFlash,fragment.flash)
VEC_SET(TilemapShader,setTone,fragment.tone)
VEC_SET(TilemapShader,setColor,fragment.color)
#undef VEC_SET
#define FLOAT_SET(Class,Method,Member) void Class::Method(float value) { state.Member=value; }
FLOAT_SET(AlphaSpriteShader,setAlpha,fragment.parameters[0])
FLOAT_SET(SpriteShader,setOpacity,fragment.parameters[0])
FLOAT_SET(PlaneShader,setOpacity,fragment.parameters[0])
FLOAT_SET(TilemapShader,setOpacity,fragment.parameters[0])
FLOAT_SET(FlashMapShader,setAlpha,fragment.parameters[0])
FLOAT_SET(BltShader,setOpacity,fragment.parameters[0])
FLOAT_SET(GrayShader,setGray,fragment.tone[3])
FLOAT_SET(HueShader,setHueAdjust,fragment.parameters[3])
FLOAT_SET(TransShader,setProg,fragment.parameters[1])
FLOAT_SET(TransShader,setVague,fragment.parameters[2])
FLOAT_SET(SimpleTransShader,setProg,fragment.parameters[1])
FLOAT_SET(SpriteShader,setBushOpacity,effects.bush[2])
FLOAT_SET(SpriteShader,setPatternOpacity,effects.blurDirection[2])
#undef FLOAT_SET
#define MATRIX_SET(Class,Method) void Class::Method(const float value[16]) { matrix(state.vertex.transform,value); }
MATRIX_SET(SimpleSpriteShader,setSpriteMat)
MATRIX_SET(AlphaSpriteShader,setSpriteMat)
MATRIX_SET(SpriteShader,setSpriteMat)
MATRIX_SET(SimpleMatrixShader,setMatrix)
#undef MATRIX_SET
void TransShader::setCurrentScene(const TEX::ID &value) { state.textures[0]=value; }
void TransShader::setFrozenScene(const TEX::ID &value) { state.textures[1]=value; }
void TransShader::setTransMap(const TEX::ID &value) { state.textures[2]=value; }
// Simple fade interpolates OLD->NEW; masked fade interpolates NEW->OLD.
void SimpleTransShader::setCurrentScene(const TEX::ID &value) { state.textures[1]=value; }
void SimpleTransShader::setFrozenScene(const TEX::ID &value) { state.textures[0]=value; }
void BltShader::setSource() { state.textures[0]=TEX::ID{}; }
void BltShader::setDestination(const TEX::ID &value) { state.textures[1]=value; }
void BltShader::setSubRect(const FloatRect &value) {
    state.fragment.subrect[0]=value.x;state.fragment.subrect[1]=value.y;
    state.fragment.subrect[2]=value.w;state.fragment.subrect[3]=value.h;
}
void BltShader::init() { state.textures[0]=TEX::ID{}; }
bool GrayShader::framebufferScalingAllowed() { return false; }
void TilemapShader::setAniIndex(int value) { state.tilemap.animation[0]=value; }
void TilemapShader::setATFrames(int values[7]) {
    std::copy_n(values,4,state.tilemap.autotileFrames0);
    std::copy_n(values+4,3,state.tilemap.autotileFrames1);
}
void Lanczos3Shader::setTexSize(const Vec2i &value) { ShaderBase::setTexSize(value); }
void Lanczos3SpriteShader::setTexSize(const Vec2i &value) { ShaderBase::setTexSize(value); }
void BicubicShader::setSharpness(int value) { sharpness(state,value); }
void BicubicSpriteShader::setSharpness(int value) { sharpness(state,value); }
void SpriteShader::setBushDepth(bool y,bool under,float slope,float intercept) {
    state.effects.spriteFlags[3]=float(y);state.effects.bush[3]=float(under);
    state.effects.bush[0]=slope;state.effects.bush[1]=intercept;
}
void SpriteShader::setPatternBlendType(int value) { state.effects.spriteFlags[1]=float(value); }
void SpriteShader::setShouldRenderPattern(bool value) {
    state.effects.spriteFlags[0]=float(value);
    // The pipeline still binds its sampler layout when pattern sampling is off.
    // Drop old handles so disposed pattern storage cannot reject an unused slot.
    if (!value) state.textures[1]=TEX::ID{};
}
void SpriteShader::setInvert(bool value) { state.effects.spriteFlags[2]=float(value); }
void SpriteShader::setPattern(const TEX::ID &value,const Vec2 &size) {
    state.textures[1]=value;patternSize=size;updatePattern();
}
void SpriteShader::setPatternTile(bool value) { patternTile=value;updatePattern(); }
void SpriteShader::setPatternScroll(const Vec2 &value) { patternScroll=value;updatePattern(); }
void SpriteShader::setPatternZoom(const Vec2 &value) { patternZoom=value;updatePattern(); }
void SpriteShader::setTexSize(const Vec2i &size) { ShaderBase::setTexSize(size);updatePattern(); }
void SpriteShader::updatePattern() {
    const float inverse[]={state.vertex.textureScaleTranslation[0],state.vertex.textureScaleTranslation[1]};
    const float size[]={patternSize.x,patternSize.y},scroll[]={patternScroll.x,patternScroll.y},zoom[]={patternZoom.x,patternZoom.y};
    for(int i=0;i<2;++i) {
        const float patternInverse=size[i]!=0?1.f/size[i]:0;
        state.effects.patternScaleOffset[i]=zoom[i]!=0?(patternTile?(inverse[i]!=0?patternInverse/(inverse[i]*zoom[i]):0):1.f/zoom[i]):0;
        state.effects.patternScaleOffset[i+2]=patternTile?(inverse[i]!=0?-scroll[i]*patternInverse*patternInverse/inverse[i]:0):-scroll[i]*patternInverse;
    }
}
void KglShadowShaderH::setParams(int x1,int x2,int y,bool soft,int w,int h,int cx,int cy,double slope1,double slope2) {
    auto &e=state.effects;
    e.shadowBounds[0]=float(x1);e.shadowBounds[1]=float(x2);e.shadowBounds[2]=float(y);e.shadowBounds[3]=0;
    e.shadowDimensions[0]=float(w);e.shadowDimensions[1]=float(h);e.shadowDimensions[2]=float(cx);e.shadowDimensions[3]=float(cy);
    e.shadowSlopes[0]=static_cast<float>(slope1);e.shadowSlopes[1]=static_cast<float>(slope2);e.shadowSlopes[2]=float(soft);
}
void KglShadowShaderV::setParams(int y1,int y2,int x,bool wall,bool soft,int w,int h,int cx,int cy,double slope1,double slope2) {
    auto &e=state.effects;
    e.shadowBounds[0]=float(y1);e.shadowBounds[1]=float(y2);e.shadowBounds[2]=float(x);e.shadowBounds[3]=float(wall);
    e.shadowDimensions[0]=float(w);e.shadowDimensions[1]=float(h);e.shadowDimensions[2]=float(cx);e.shadowDimensions[3]=float(cy);
    e.shadowSlopes[0]=static_cast<float>(slope1);e.shadowSlopes[1]=static_cast<float>(slope2);e.shadowSlopes[2]=float(soft);
}
