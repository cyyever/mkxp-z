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
