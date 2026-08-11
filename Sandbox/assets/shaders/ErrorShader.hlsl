cbuffer CameraBuffer : register(b0)
{
    matrix g_ViewProjection;
};

cbuffer TransformBuffer : register(b1)
{
    matrix g_World;
    float4 g_BaseColor;
    int g_EntityID;
    int g_HasAnimation;
    float2 g_Padding;
};

cbuffer BoneBuffer : register(b2)
{
    matrix g_FinalBoneMatrices[512];
};

struct VS_INPUT
{
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD;
    int4 BoneIDs : BONEIDS;
    float4 Weights : WEIGHTS;
};

struct PS_INPUT
{
    float4 SV_Pos : SV_POSITION;
    float2 TexCoord : TEXCOORD;
};

struct PS_OUTPUT
{
    float4 Color : SV_Target0;
    int EntityID : SV_Target1;
};

PS_INPUT VSMain(VS_INPUT input)
{
    PS_INPUT output;

    float4 localPos = float4(input.Pos, 1.0f);
    if (g_HasAnimation == 1)
    {
        localPos = float4(0.0f, 0.0f, 0.0f, 0.0f);
        for (int i = 0; i < 4; i++)
        {
            if (input.BoneIDs[i] == -1)
                continue;

            localPos += mul(float4(input.Pos, 1.0f), g_FinalBoneMatrices[input.BoneIDs[i]]) * input.Weights[i];
        }
    }

    output.SV_Pos = mul(mul(localPos, g_World), g_ViewProjection);
    output.TexCoord = input.TexCoord;
    return output;
}

PS_OUTPUT PSMain(PS_INPUT input)
{
    PS_OUTPUT output;

    float checker = (fmod(floor(input.TexCoord.x * 8.0f) + floor(input.TexCoord.y * 8.0f), 2.0f) == 0.0f) ? 1.0f : 0.45f;
    output.Color = lerp(float4(0.05f, 0.0f, 0.05f, 1.0f), float4(1.0f, 0.0f, 1.0f, 1.0f), checker);
    output.EntityID = g_EntityID;
    return output;
}
