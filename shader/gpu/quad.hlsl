// SDL_GPU equivalents of the engine's RGSS quad effects.
// Derived from this project's shader/*.vert and shader/*.frag implementations;
// their existing GPL licensing and attribution continue to apply.

cbuffer VertexParameters : register(b0, space1) {
    float4x4 projection;
    float4x4 transform;
    float4 textureScaleTranslation;
};

struct VertexInput {
    float2 position : TEXCOORD0;
    float2 texcoord : TEXCOORD1;
    float4 color : TEXCOORD2;
};
struct FragmentInput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 color : TEXCOORD1;
};

FragmentInput vertexMain(VertexInput input) {
    FragmentInput output;
    output.position = mul(projection, mul(transform,
        float4(input.position + textureScaleTranslation.zw, 0.0, 1.0)));
    output.texcoord = input.texcoord * textureScaleTranslation.xy;
    output.color = input.color;
    return output;
}

Texture2D<float4> sourceTexture : register(t0, space2);
SamplerState sourceSampler : register(s0, space2);
Texture2D<float4> secondTexture : register(t1, space2);
SamplerState secondSampler : register(s1, space2);
Texture2D<float4> transitionTexture : register(t2, space2);
SamplerState transitionSampler : register(s2, space2);

cbuffer FragmentParameters : register(b0, space3) {
    float4 color;
    float4 tone;
    float4 flash;
    // opacity, transition progress, transition vague, hue adjustment
    float4 parameters;
    float4 subrect;
};

float4 simpleMain(FragmentInput input) : SV_Target0 {
    return sourceTexture.Sample(sourceSampler, input.texcoord);
}
float4 flatColorMain(FragmentInput input) : SV_Target0 {
    return color;
}
float4 vertexColorMain(FragmentInput input) : SV_Target0 {
    return input.color;
}
float4 vertexAlphaMain(FragmentInput input) : SV_Target0 {
    float4 result = sourceTexture.Sample(sourceSampler, input.texcoord);
    result.a *= input.color.a;
    return result;
}
float4 uniformAlphaMain(FragmentInput input) : SV_Target0 {
    float4 result = sourceTexture.Sample(sourceSampler, input.texcoord);
    result.a *= parameters.x;
    return result;
}
float4 planeMain(FragmentInput input) : SV_Target0 {
    float4 result = sourceTexture.Sample(sourceSampler, input.texcoord);
    float gray = dot(result.rgb, float3(0.299, 0.587, 0.114));
    result.rgb = lerp(result.rgb, gray.xxx, tone.w) + tone.rgb;
    result.a *= parameters.x;
    result.rgb = lerp(result.rgb, color.rgb, color.a);
    result.rgb = lerp(result.rgb, flash.rgb, flash.a);
    return result;
}
float4 transitionMain(FragmentInput input) : SV_Target0 {
    float progress = parameters.y;
    float vague = parameters.z;
    float threshold = transitionTexture.Sample(transitionSampler, input.texcoord).r;
    float alpha = (clamp(threshold, progress, progress + vague) - progress) / vague;
    return lerp(sourceTexture.Sample(sourceSampler, input.texcoord),
                secondTexture.Sample(secondSampler, input.texcoord), alpha);
}
float4 simpleTransitionMain(FragmentInput input) : SV_Target0 {
    return lerp(sourceTexture.Sample(sourceSampler, input.texcoord),
                secondTexture.Sample(secondSampler, input.texcoord), parameters.y);
}
float4 bitmapBlendMain(FragmentInput input) : SV_Target0 {
    float2 destinationUV = (input.texcoord - subrect.xy) * subrect.zw;
    float4 source = sourceTexture.Sample(sourceSampler, input.texcoord);
    float4 destination = secondTexture.Sample(secondSampler, destinationUV);
    float sourceWeight = source.a * parameters.x;
    float destinationWeight = destination.a * (1.0 - sourceWeight);
    float alpha = sourceWeight + destinationWeight;
    float3 rgb = alpha == 0.0 ? source.rgb :
        (sourceWeight * source.rgb + destinationWeight * destination.rgb) / alpha;
    return float4(rgb, alpha);
}

// Preserve the hue conversion used by hue.frag, including its epsilon.
float3 rgbToHsv(float3 value) {
    const float4 k = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    float4 p = lerp(float4(value.bg, k.wz), float4(value.gb, k.xy), step(value.b, value.g));
    float4 q = lerp(float4(p.xyw, value.r), float4(value.r, p.yzx), step(p.x, value.r));
    float d = q.x - min(q.w, q.y);
    const float epsilon = 1.0e-10;
    return float3(abs(q.z + (q.w - q.y) / (6.0 * d + epsilon)),
                  d / (q.x + epsilon), q.x);
}
float3 hsvToRgb(float3 value) {
    const float4 k = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    float3 p = abs(frac(value.xxx + k.xyz) * 6.0 - k.www);
    return value.z * lerp(k.xxx, clamp(p - k.xxx, 0.0, 1.0), value.y);
}
float4 hueMain(FragmentInput input) : SV_Target0 {
    float4 result = sourceTexture.Sample(sourceSampler, input.texcoord);
    float3 hsv = rgbToHsv(result.rgb);
    hsv.x += parameters.w;
    result.rgb = hsvToRgb(hsv);
    return result;
}
// Extended effects preserve the first Vertex/FragmentParameters blocks above.
cbuffer EffectParameters : register(b1, space3) {
    float4 patternScaleOffset; // affine pattern UV transform
    float4 spriteFlags;       // render pattern, blend type, invert, bush Y
    float4 bush;              // slope, intercept, opacity, under
    float4 filterSize;        // source width/height, reciprocal texture size
    float4 filterParameters;  // Mitchell-Netravali B/C, xBRZ target scale
    float4 blurDirection;     // normalized blur offset xy, sprite pattern opacity z
    float4 shadowBounds;      // H: x1,x2,y,wall; V: y1,y2,x,wall
    float4 shadowDimensions;  // width,height,x center,y center
    float4 shadowSlopes;      // slope1,slope2,soft,unused
};

// Pattern blend arithmetic follows shader/sprite.frag and
// https://github.com/jamieowen/glsl-blend (original attribution retained).
float4 spriteMain(FragmentInput input) : SV_Target0 {
    float4 result = sourceTexture.Sample(sourceSampler, input.texcoord);
    if (spriteFlags.x != 0) {
        float2 patternUV = frac(input.texcoord * patternScaleOffset.xy + patternScaleOffset.zw);
        float4 pattern = secondTexture.Sample(secondSampler, patternUV);
        float3 blended = pattern.rgb;
        if (spriteFlags.y == 1) blended = min(result.rgb + pattern.rgb, 1.0);
        else if (spriteFlags.y == 2) blended = max(result.rgb - pattern.rgb, 0.0);
        // Pattern opacity is independent of the sprite's overall opacity.
        result.rgb = lerp(result.rgb, blended, pattern.a * blurDirection.z);
    }
    float gray = dot(result.rgb, float3(.299,.587,.114));
    result.rgb = lerp(result.rgb, gray.xxx, tone.w) + tone.rgb;
    result.a *= parameters.x;
    result.rgb = lerp(result.rgb, color.rgb, color.a);
    if (spriteFlags.z != 0) result.rgb = 1.0 - result.rgb;
    bool yAxis = spriteFlags.w != 0;
    bool under = (yAxis ? input.texcoord.y : input.texcoord.x)
        < bush.x * (yAxis ? input.texcoord.x : input.texcoord.y) + bush.y;
    result.a *= saturate(bush.z + float(under == (bush.w != 0)));
    return result;
}
float4 tilemapMain(FragmentInput input) : SV_Target0 {
    float4 result = sourceTexture.Sample(sourceSampler, input.texcoord);
    float gray = dot(result.rgb, float3(.299,.587,.114));
    result.rgb = lerp(result.rgb, gray.xxx, tone.w) + tone.rgb;
    result.a *= parameters.x;
    result.rgb = lerp(result.rgb, color.rgb, color.a);
    return result;
}
float4 flashMapMain(FragmentInput input) : SV_Target0 {
    return float4(input.color.rgb * parameters.x, 1.0);
}
float4 grayMain(FragmentInput input) : SV_Target0 {
    float4 result = sourceTexture.Sample(sourceSampler, input.texcoord);
    float gray = dot(result.rgb, float3(.299,.587,.114));
    result.rgb = lerp(result.rgb, gray.xxx, tone.w);
    return result;
}
float4 blurMain(FragmentInput input) : SV_Target0 {
    return (sourceTexture.Sample(sourceSampler,input.texcoord)
        + sourceTexture.Sample(sourceSampler,input.texcoord-blurDirection.xy)
        + sourceTexture.Sample(sourceSampler,input.texcoord+blurDirection.xy)) / 3.0;
}
float4 invertMain(FragmentInput input) : SV_Target0 {
    float4 value=sourceTexture.Sample(sourceSampler,input.texcoord);
    return float4(1.0-value.rgb,value.a);
}
float4 compressAlphaMain(FragmentInput input) : SV_Target0 {
    float4 value=sourceTexture.Sample(sourceSampler,input.texcoord);
    return float4(value.a*value.rgb,0.0);
}
float4 subtractMain(FragmentInput input) : SV_Target0 {
    float4 source=sourceTexture.Sample(sourceSampler,input.texcoord);
    float2 destinationUV=(input.texcoord-subrect.xy)*subrect.zw;
    float4 destination=secondTexture.Sample(secondSampler,destinationUV);
    return float4(destination.rgb-parameters.x*source.rgb,1.0);
}

// Ported from bicubic.frag; original arithmetic and edge behavior retained.
// From https://raw.githubusercontent.com/Sentmoraap/doing-sdl-right/f1a0183692abbd5d899fb432ab8dafe228a4929a/assets/bicubic.frag
// Copyright 2020 Lilian Gimenez (Sentmoraap).
// mkxp-z modifications Copyright 2022-2023 Splendide Imaginarius.
// MIT license.



float4 bicubicMain(FragmentInput input) : SV_Target0
{
    float4 result;
    float2 sourceSize=filterSize.xy, texSizeInv=filterSize.zw, bc=filterParameters.xy;

	float2 pixel = input.texcoord * sourceSize + 0.5;
	float2 fractional = frac(pixel);
	float2 frac2 = fractional * fractional;
	float2 frac3 = fractional * frac2;
	float2 onePixel = texSizeInv;
	pixel = floor(pixel) * texSizeInv - onePixel / 2.0;
	float4 colours[4];
	// 16 reads, unoptimized but forks for every Mitchell-Netravali filter
	for(int i = -1; i <= 2; i++)
	{
		float4 p0 = sourceTexture.Sample(sourceSampler, pixel + float2(   -onePixel.x, float(i) * onePixel.y)).rgba;
		float4 p1 = sourceTexture.Sample(sourceSampler, pixel + float2(             0, float(i) * onePixel.y)).rgba;
		float4 p2 = sourceTexture.Sample(sourceSampler, pixel + float2(    onePixel.x, float(i) * onePixel.y)).rgba;
		float4 p3 = sourceTexture.Sample(sourceSampler, pixel + float2(2.0 * onePixel.x, float(i) * onePixel.y)).rgba;
		colours[i + 1] = ((-bc.x / 6.0 - bc . y) * p0 + (- 1.5 * bc.x - bc.y + 2.0) * p1
				+ (1.5 * bc.x + bc.y - 2.0) * p2 + (bc.x / 6.0 + bc.y) * p3) * frac3.x
				+ ((0.5 * bc.x + 2.0 * bc.y) * p0 + (2.0 * bc.x + bc.y - 3.0) * p1
				+ (-2.5 * bc.x - 2.0 * bc.y + 3.0) * p2 - bc.y * p3) * frac2.x
				+ ((-0.5 * bc.x - bc.y) * p0 + (0.5 * bc.x + bc.y) * p2) * fractional.x
				+ p0 * bc.x / 6.0 + (-bc.x / 3.0 + 1.0) * p1 + p2 * bc.x / 6.0;
	}
	result = ((-bc.x / 6.0 - bc . y) * colours[0] + (- 1.5 * bc.x - bc.y + 2.0) * colours[1]
			+ (1.5 * bc.x + bc.y - 2.0) * colours[2] + (bc.x / 6.0 + bc.y) * colours[3]) * frac3.y
			+ ((0.5 * bc.x + 2.0 * bc.y) * colours[0] + (2.0 * bc.x + bc.y - 3.0) * colours[1]
			+ (-2.5 * bc.x - 2.0 * bc.y + 3.0) * colours[2] - bc.y * colours[3]) * frac2.y
			+ ((-0.5 * bc.x - bc.y) * colours[0] + (0.5 * bc.x + bc.y) * colours[2]) * fractional.y
			+ colours[0] * bc.x / 6.0 + (-bc.x / 3.0 + 1.0) * colours[1] + colours[2] * bc.x / 6.0;
    return result;
}

// Ported from lanczos3.frag; original arithmetic and edge behavior retained.
// From https://raw.githubusercontent.com/Sentmoraap/doing-sdl-right/93a52a0db0ff2da5066cce12f5b9a2ac62e6f401/assets/lanczos3.frag
// Copyright 2020 Lilian Gimenez (Sentmoraap).
// mkxp-z modifications Copyright 2022-2023 Splendide Imaginarius.
// MIT license.



float lanczos3(float x)
{
	x = max(abs(x), 0.00001);
	float val = x * 3.141592654;
	return sin(val) * sin(val / 3.0) / (val * val);
}

float4 lanczosMain(FragmentInput input) : SV_Target0
{
    float4 result;
    float2 sourceSize=filterSize.xy, texSizeInv=filterSize.zw, bc=filterParameters.xy;

	float2 pixel = input.texcoord * sourceSize + 0.5;
	float2 fractional = frac(pixel);
	float2 onePixel = texSizeInv;
	pixel = floor(pixel) * texSizeInv - onePixel / 2.0;

	int x, y;
	float lanczosX[6];
	float sum = 0.0;
	for(x = 0; x < 6; x++)
	{
		lanczosX[x] = lanczos3(float(x) - 2.0 - fractional.x);
		sum += lanczosX[x];
	}
	for(x = 0; x < 6; x++) lanczosX[x] /= sum;
	sum = 0.0;
	float lanczosY[6];
	for(y = 0; y < 6; y++)
	{
		lanczosY[y] = lanczos3(float(y) - 2.0 - fractional.y);
		sum += lanczosY[y];
	}
	for(y = 0; y < 6; y++) lanczosY[y] /= sum;
	result = (float4)0;
	for(y = -2; y <= 3; y++)
	{
		float4 colour = (float4)0;
		for(x = -2; x <= 3; x++)
				colour += sourceTexture.Sample(sourceSampler, pixel + float2(float(x) * onePixel.x, float(y) * onePixel.y)).rgba * lanczosX[x + 2];
		result += colour * lanczosY[y + 2];
	}
    return result;
}

// Ported from kglShadowH.frag; original arithmetic and edge behavior retained.
float shadowRoundH(float x)
{
	return x >= 0. ? x + 0.5 : x - 0.5;
}

float4 shadowHorizontalMain(FragmentInput input) : SV_Target0
{
    float4 result;
    int x1=int(shadowBounds.x), x2=int(shadowBounds.y), y=int(shadowBounds.z);
    bool soft=shadowSlopes.z != 0;
    int w=int(shadowDimensions.x), h=int(shadowDimensions.y), x_center=int(shadowDimensions.z), y_center=int(shadowDimensions.w);
    float slope1=shadowSlopes.x, slope2=shadowSlopes.y;

	float4 frag = sourceTexture.Sample(sourceSampler, input.texcoord);

	int x_texel = int(float(w) * input.texcoord.x);
	int y_texel = int(float(h) * input.texcoord.y);

	if ((y < y_center && y_texel < y) || (y > y_center && y_texel >= y)) {
		float x_start_raw = slope1 * float(y_texel - y_center) + float(x_center);
		float x_end_raw = slope2 * float(y_texel - y_center) + float(x_center) + 0.2; // The original shader contains a +0.2 adjustment factor for some reason

		int x_start = int(clamp(shadowRoundH(x_start_raw), 0., float(w) + 3.));
		int x_end = int(clamp(shadowRoundH(x_end_raw), -4., x2 < x_center ? float(x2) - 1. : float(w) - 1.)); // This bounds check is incorrect but is consistent with the original shader

		if (x_texel >= x_start && x_texel <= x_end) {
			frag = float4(0., 0., 0., 0.);
		} else if (soft) {
			if (
				x_texel < x_start
					&& x_start - x_texel <= 3
					&& (x1 < x_center || x_texel >= x1) // This bounds check is incorrect but is consistent with the original shader
					&& (x2 >= x_center || x_texel < x2) // This bounds check is incorrect but is consistent with the original shader
			) {
				frag.rgb *= float(x_start - x_texel) / 4.;
			}
			if (
				x2 != x_center
					&& x_texel > x_end
					&& x_texel - x_end <= 3
					&& (x1 < x_center || x_texel >= x1) // This bounds check is incorrect but is consistent with the original shader
					&& (x2 >= x_center || x_texel < x2) // This bounds check is incorrect but is consistent with the original shader
			) {
				frag.rgb *= float(x_texel - x_end) / 4.;
			}
		}
	}

	result = frag;
    return result;
}

// Ported from kglShadowV.frag; original arithmetic and edge behavior retained.
float shadowRoundV(float x)
{
	return x >= 0. ? x + 0.5 : x - 0.5;
}

float4 shadowVerticalMain(FragmentInput input) : SV_Target0
{
    float4 result;
    int y1=int(shadowBounds.x), y2=int(shadowBounds.y), x=int(shadowBounds.z);
    bool wall=shadowBounds.w != 0;
    bool soft=shadowSlopes.z != 0;
    int w=int(shadowDimensions.x), h=int(shadowDimensions.y), x_center=int(shadowDimensions.z), y_center=int(shadowDimensions.w);
    float slope1=shadowSlopes.x, slope2=shadowSlopes.y;

	float4 frag = sourceTexture.Sample(sourceSampler, input.texcoord);

	int x_texel = int(float(w) * input.texcoord.x);
	int y_texel = int(float(h) * input.texcoord.y);

	if ((x < x_center && x_texel < x) || (x > x_center && x_texel >= x)) {
		float y_start_raw = slope1 * float(x_texel - x_center) + float(y_center);
		float y_end_raw = slope2 * float(x_texel - x_center) + float(y_center) + 0.2; // The original shader contains a +0.2 adjustment factor for some reason

		int y_start = wall && y1 >= y_center ? y1 : int(clamp(shadowRoundV(y_start_raw), 0., float(h) + 3.));
		int y_end = wall && y2 >= y_center ? y2 - 1 : int(clamp(shadowRoundV(y_end_raw), -4., y2 < y_center ? float(y2) - 1. : float(h) - 1.)); // This bounds check is incorrect but is consistent with the original shader

		if (y_texel >= y_start && y_texel <= y_end) {
			frag = float4(0., 0., 0., 0.);
		} else if (soft) {
			if (
				(!wall || y1 < y_center)
					&& y_texel < y_start
					&& y_start - y_texel <= 3
					&& (y1 < y_center || y_texel >= y1) // This bounds check is incorrect but is consistent with the original shader
					&& (y2 >= y_center || y_texel < y2) // This bounds check is incorrect but is consistent with the original shader
			) {
				frag.rgb *= float(y_start - y_texel) / 4.;
			}
			if (
				y2 != y_center
					&& (!wall || y2 < y_center)
					&& y_texel > y_end
					&& y_texel - y_end <= 3
					&& (y1 < y_center || y_texel >= y1) // This bounds check is incorrect but is consistent with the original shader
					&& (y2 >= y_center || y_texel < y2) // This bounds check is incorrect but is consistent with the original shader
			) {
				frag.rgb *= float(y_texel - y_end) / 4.;
			}
		}
	}

	result = frag;
    return result;
}
cbuffer TilemapParameters : register(b1, space1) {
    int4 animation; // animation index, remaining components reserved
    int4 autotileFrames0;
    int4 autotileFrames1;
};
FragmentInput tilemapVertexMain(VertexInput input) {
    float2 tex = input.texcoord;
    // Ordinary atlas tiles must not index the seven-element autotile table.
    if (tex.x <= 96.0 && tex.y < 896.0 && tex.y >= 0.0) {
        int atIndex = int(tex.y / 128.0);
        int frames = atIndex < 4 ? autotileFrames0[atIndex] : autotileFrames1[atIndex-4];
        if (frames > 0) {
            uint frame = uint(animation.x) % uint(frames);
            uint row = frame / 8u;
            uint col = frame - 8u * row;
            tex += float2(96.0 * col, 32.0 * row);
        }
    }
    FragmentInput output;
    output.position = mul(projection, float4(input.position + textureScaleTranslation.zw,0,1));
    output.texcoord = tex * textureScaleTranslation.xy;
    output.color = input.color;
    return output;
}
