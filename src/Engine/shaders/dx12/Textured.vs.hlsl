struct VSInput {
    float4 position : POSITION;
    float2 uv : TEXCOORD0;
    float alpha : COLOR0;
    float cutoff : TEXCOORD1;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float alpha : COLOR0;
    float cutoff : TEXCOORD1;
};

VSOutput main(VSInput input) {
    VSOutput output;
    output.position = input.position;
    output.uv = input.uv;
    output.alpha = input.alpha;
    output.cutoff = input.cutoff;
    return output;
}
