cbuffer BoneBuffer : register(b2)
{
    matrix g_FinalBoneMatrices[512];
};
cbuffer Probe : register(b5)
{
    float4 Tint;
    int Entity;
    float Depth;
    float NormalMode;
    float Padding;
};
struct Input
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    int4 Bones : BONEIDS;
    float4 Weights : WEIGHTS;
};
struct Varying
{
    float4 Position : SV_POSITION;
    float3 Normal : NORMAL;
};
Varying VSMain(Input input)
{
    float4 position = 0;
    float3 normal = 0;
    for (int i = 0; i < 4; ++i)
        if (input.Bones[i] >= 0)
        {
            position += mul(float4(input.Position, 1), g_FinalBoneMatrices[input.Bones[i]]) * input.Weights[i];
            normal += mul(input.Normal, (float3x3)g_FinalBoneMatrices[input.Bones[i]]) * input.Weights[i];
        }
    position.z = Depth * position.w;
    Varying result;
    result.Position = position;
    result.Normal = normal;
    return result;
}
struct Output
{
    float4 Color : SV_Target0;
    int Entity : SV_Target1;
};
Output PSMain(Varying input)
{
    Output result;
    result.Color = NormalMode > 0 ? float4(normalize(input.Normal) * 0.5 + 0.5, 1) : Tint;
    result.Entity = Entity;
    return result;
}
