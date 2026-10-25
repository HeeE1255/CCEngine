cbuffer SmokeColor : register(b0)
{
    float4 Color;
};

float4 VSMain(float2 position : POSITION) : SV_POSITION
{
    return float4(position, 0.5, 1.0);
}

float4 PSMain() : SV_TARGET
{
    return Color;
}
