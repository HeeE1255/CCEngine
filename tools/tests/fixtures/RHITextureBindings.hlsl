Texture2D ZDetail : register(t3);
Texture2D AMask : register(t7);
SamplerState LinearSampler : register(s0);

struct PixelInput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD;
};

PixelInput VSMain(float2 position : POSITION)
{
    PixelInput result;
    result.Position = float4(position, 0.0, 1.0);
    result.UV = position * 0.5 + 0.5;
    return result;
}

float4 PSMain(PixelInput input) : SV_TARGET
{
    return ZDetail.Sample(LinearSampler, input.UV) * AMask.Sample(LinearSampler, input.UV);
}
