Texture2D modelTexture : register(t0);
Texture2D opacityTexture : register(t1);
SamplerState linearSampler : register(s0);

struct PSInput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float alpha : COLOR0;
    float cutoff : TEXCOORD1;
};

float4 main(PSInput input) : SV_TARGET {
    float4 color = modelTexture.Sample(linearSampler, input.uv);
    float opacitySample = opacityTexture.Sample(linearSampler, input.uv).r;
    if (input.cutoff < 0.0f) {
        opacitySample = 1.0f - opacitySample;
    }
    const float opacityScale = saturate(abs(input.alpha));
    const float finalAlpha = opacityScale * saturate(opacitySample);
    if (input.alpha < 0.0f && finalAlpha < saturate(abs(input.cutoff))) {
        discard;
    }
    color.a *= finalAlpha;
    return color;
}
