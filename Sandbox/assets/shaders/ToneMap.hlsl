cbuffer ToneMapBuffer : register(b0)
{
    float4 Settings; // exposure multiplier, Reinhard, flip Y, reserved
};
Texture2D SceneColor : register(t0);
SamplerState LinearSampler : register(s0);

struct VSInput { float3 Position : POSITION; };
struct PSInput { float4 Position : SV_POSITION; float2 UV : TEXCOORD0; };
PSInput VSMain(VSInput input)
{
    PSInput output;
    output.Position = float4(input.Position, 1.0f);
    output.UV = input.Position.xy * float2(0.5f, -0.5f) + 0.5f;
    return output;
}
float4 PSMain(PSInput input) : SV_TARGET
{
    float2 uv = input.UV;
    if (Settings.z > 0.5f) uv.y = 1.0f - uv.y;
    float4 source = SceneColor.Sample(LinearSampler, uv);
    // FP16 범위 안에서 노출을 곱한다. 최종 출력 전까지 밝은 값을 saturate하지 않는다.
    float3 color = min(max(source.rgb, 0.0f), 65504.0f) * Settings.x;
    if (Settings.y > 0.5f) color = color / (1.0f + color);
    color = saturate(color);
    color = lerp(1.055f * pow(color, 1.0f / 2.4f) - 0.055f, color * 12.92f, step(color, 0.0031308f));
    return float4(color, saturate(source.a));
}
