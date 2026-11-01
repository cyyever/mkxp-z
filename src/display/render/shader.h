/*
** shader.h
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

#ifndef SHADER_H
#define SHADER_H

#include "etc-internal.h"
#include "gpu-shader-state.h"


class ShaderNoConstructTag {};

class Shader {
public:
    void bind();
    static void unbind();
    enum Attribute { Position = 0, TexCoord = 1, Color = 2 };
    const gpu::ShaderState &gpuState() const { return state; }
    Shader(const Shader &) = delete;
    Shader &operator=(const Shader &) = delete;
protected:
    explicit Shader(gpu::Program program = gpu::Program::Simple);
    ~Shader() = default;
    gpu::ShaderState state;
};

class ShaderBase : public Shader {
public:
    class Projection {
    public:
        explicit Projection(ShaderBase &owner) : owner(owner) {}
        void set(const Vec2i &);
    private:
        ShaderBase &owner;
    };
    Projection projMat;
    void applyViewportProj();
    virtual void setTexSize(const Vec2i &);
    void setTranslation(const Vec2i &);
protected:
    explicit ShaderBase(gpu::Program program = gpu::Program::Simple);
    virtual bool framebufferScalingAllowed();
    int blurAxis = -1;
};

class FlatColorShader : public ShaderBase
{
public:
	FlatColorShader();

	void setColor(const Vec4 &value);

};

class SimpleShader : public ShaderBase
{
public:
	SimpleShader();
	SimpleShader(const ShaderNoConstructTag &);

};

class SimpleColorShader : public ShaderBase
{
public:
	SimpleColorShader();
};

class SimpleAlphaShader : public ShaderBase
{
public:
	SimpleAlphaShader();
};

class SimpleSpriteShader : public ShaderBase
{
public:
	SimpleSpriteShader();
	SimpleSpriteShader(const ShaderNoConstructTag &);

	void setSpriteMat(const float value[16]);

};

class AlphaSpriteShader : public ShaderBase
{
public:
	AlphaSpriteShader();

	void setSpriteMat(const float value[16]);
	void setAlpha(float value);

};

class TransShader : public ShaderBase
{
public:
	TransShader();

	void setCurrentScene(const TEX::ID &tex);
	void setFrozenScene(const TEX::ID &tex);
	void setTransMap(const TEX::ID &tex);
	void setProg(float value);
	void setVague(float value);

};

class SimpleTransShader : public ShaderBase
{
public:
	SimpleTransShader();

	void setCurrentScene(const TEX::ID &tex);
	void setFrozenScene(const TEX::ID &tex);
	void setProg(float value);

};

class SpriteShader : public ShaderBase
{
public:
	SpriteShader();

	void setSpriteMat(const float value[16]);
	void setTone(const Vec4 &value);
	void setColor(const Vec4 &value);
	void setOpacity(float value);
	void setBushDepth(bool bushY, bool bushUnder, float bushSlope, float bushIntercept);
	void setBushOpacity(float value);
    void setPattern(const TEX::ID &pattern, const Vec2 &dimensions);
    void setPatternBlendType(int blendType);
    void setPatternTile(bool value);
    void setShouldRenderPattern(bool value);
    void setPatternOpacity(float value);
    void setPatternScroll(const Vec2 &scroll);
    void setPatternZoom(const Vec2 &zoom);
    void setInvert(bool value);

    void setTexSize(const Vec2i &value) override;
private:
    void updatePattern();
    Vec2 patternSize{1,1}, patternScroll, patternZoom{1,1};
    bool patternTile = false;
};

class PlaneShader : public ShaderBase
{
public:
	PlaneShader();

	void setTone(const Vec4 &value);
	void setColor(const Vec4 &value);
	void setFlash(const Vec4 &value);
	void setOpacity(float value);

};

class GrayShader : public ShaderBase
{
public:
	GrayShader();

	void setGray(float value);

protected:
	virtual bool framebufferScalingAllowed();

};

class TilemapShader : public ShaderBase
{
public:
	TilemapShader();

	void setAniIndex(int value);

	void setTone(const Vec4 &value);
	void setColor(const Vec4 &value);
	void setOpacity(float value);

	void setATFrames(int values[7]);

};

class FlashMapShader : public ShaderBase
{
public:
	FlashMapShader();

	void setAlpha(float value);

};

class HueShader : public ShaderBase
{
public:
	HueShader();

	void setHueAdjust(float value);

};

class SimpleMatrixShader : public ShaderBase
{
public:
	SimpleMatrixShader();

	void setMatrix(const float value[16]);

};

/* Gaussian blur */
struct BlurShader
{
	class HPass : public ShaderBase
	{
	public:
		HPass();
	};

	class VPass : public ShaderBase
	{
	public:
		VPass();
	};

	HPass pass1;
	VPass pass2;
};

/* Bitmap blit */
class BltShader : public ShaderBase
{
public:
	BltShader();
	BltShader(const ShaderNoConstructTag &);

	void init();

	void setSource();
	void setDestination(const TEX::ID &value);
	void setSubRect(const FloatRect &value);
	void setOpacity(float value);

};

class KglInvertShader : public ShaderBase
{
public:
	KglInvertShader();
};

class KglCompressAlphaShader : public ShaderBase
{
public:
	KglCompressAlphaShader();
};

class KglSubtractShader : public BltShader
{
public:
	KglSubtractShader();
};

class KglShadowShaderH : public ShaderBase
{
public:
	KglShadowShaderH();

	void setParams(int x1, int x2, int y, bool soft, int w, int h, int x_center, int y_center, double slope1, double slope2);

};

class KglShadowShaderV : public ShaderBase
{
public:
	KglShadowShaderV();

	void setParams(int y1, int y2, int x, bool wall, bool soft, int w, int h, int x_center, int y_center, double slope1, double slope2);

};

class Lanczos3Shader : public SimpleShader
{
public:
	Lanczos3Shader();
	Lanczos3Shader(const ShaderNoConstructTag &);

	void setTexSize(const Vec2i &value);

};

class BicubicShader : public Lanczos3Shader
{
public:
	BicubicShader();

	void setSharpness(int sharpness);

};

#ifdef MKXPZ_HAVE_EXTRA_SHADERS
class XbrzShader : public Lanczos3Shader
{
public:
	XbrzShader();

	void setTargetScale(const Vec2 &value);

};
#endif

class Lanczos3SpriteShader : public SimpleSpriteShader
{
public:
	Lanczos3SpriteShader();
	Lanczos3SpriteShader(const ShaderNoConstructTag &);

	void setTexSize(const Vec2i &value);

};

class BicubicSpriteShader : public Lanczos3SpriteShader
{
public:
	BicubicSpriteShader();

	void setSharpness(int sharpness);

};

class XbrzSpriteShader : public Lanczos3SpriteShader
{
public:
	XbrzSpriteShader();

	void setTargetScale(const Vec2 &value);

};

/* Global object containing all available shaders */
struct ShaderSet
{
	FlatColorShader flatColor;
	SimpleShader simple;
	SimpleColorShader simpleColor;
	SimpleAlphaShader simpleAlpha;
	SimpleSpriteShader simpleSprite;
	AlphaSpriteShader alphaSprite;
	SpriteShader sprite;
	PlaneShader plane;
	GrayShader gray;
	TilemapShader tilemap;
	FlashMapShader flashMap;
	TransShader trans;
	SimpleTransShader simpleTrans;
	HueShader hue;
	BltShader blt;
	SimpleMatrixShader simpleMatrix;
	BlurShader blur;
	KglInvertShader kglInvert;
	KglCompressAlphaShader kglCompressAlpha;
	KglSubtractShader kglSubtract;
	KglShadowShaderH kglShadowH;
	KglShadowShaderV kglShadowV;
	BicubicShader bicubic;
	Lanczos3Shader lanczos3;
#ifdef MKXPZ_HAVE_EXTRA_SHADERS
	XbrzShader xbrz;
#endif
	Lanczos3SpriteShader lanczos3Sprite;
	BicubicSpriteShader bicubicSprite;
#ifdef MKXPZ_HAVE_EXTRA_SHADERS
	XbrzSpriteShader xbrzSprite;
#endif
};

#endif // SHADER_H
