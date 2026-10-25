// @property Float ZScale = 2
// @property Float AScale = 3
struct VertexInput
{
    float4 Color : COLOR;
    float2 Position : POSITION;
    int Entity : ENTITY_ID;
};
struct PixelInput
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR;
    nointerpolation int Entity : ENTITY_ID;
};
PixelInput VSMain(VertexInput input)
{
    PixelInput output;
    output.Position = float4(input.Position, 0.0, 1.0);
    output.Color = input.Color;
    output.Entity = input.Entity;
    return output;
}
struct PixelOutput
{
    float4 Color : SV_Target0;
    int Entity : SV_Target1;
};
PixelOutput PSMain(PixelInput input)
{
    PixelOutput output;
    output.Color = input.Color;
    output.Entity = input.Entity;
    return output;
}
