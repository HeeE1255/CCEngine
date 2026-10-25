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
struct PixelOutput
{
    float4 Color : SV_Target0;
    int Entity : SV_Target1;
};
PixelOutput PSMain(PixelInput input)
{
    PixelOutput output;
    // 교환 법칙이 없는 혼합으로 슬롯이 뒤바뀐 경우도 검출한다.
    output.Color = ZDetail.Sample(LinearSampler, input.UV) * 0.25 + AMask.Sample(LinearSampler, input.UV) * 0.75;
    output.Entity = 31;
    return output;
}
