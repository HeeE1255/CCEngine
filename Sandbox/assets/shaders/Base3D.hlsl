// =====================================================================
// Base3D - 기본 3D Lit 셰이더
// =====================================================================
// 이 셰이더는 엔진의 기본 표시용이다.
// 기존 씬의 색감이 바뀌지 않도록 PBR 계산은 넣지 않고, 가벼운 Lambert 조명만 사용한다.

struct LightInfo
{
    float3 Direction;
    float Intensity;
    float3 Color;
    float Padding;
};

cbuffer CameraBuffer : register(b0)
{
    matrix g_ViewProjection;
    float3 g_CameraPosition;
    float g_CameraPadding;
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
    matrix g_FinalBoneMatrices[100];
};

cbuffer SceneBuffer : register(b3)
{
    LightInfo g_Lights[4];
    int g_LightCount;
    float3 g_ScenePad2;
};

cbuffer MaterialPropertyBuffer : register(b4)
{
    float4 g_AlbedoColor;
    float4 g_PropertyColors[8];
    float4 g_PropertyScalars[4];
    float4 g_PropertyToggles[4];
    float4 g_SurfaceValues;
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
    float3 WorldPos : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD;
    float4 Color : COLOR;
};

struct PS_OUTPUT
{
    float4 Color : SV_Target0;
    int EntityID : SV_Target1;
};

Texture2D g_AlbedoMap : register(t0);
SamplerState g_Sampler : register(s0);

PS_INPUT VSMain(VS_INPUT input)
{
    PS_INPUT output;

    float4 totalLocalPos = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float3 totalNormal = float3(0.0f, 0.0f, 0.0f);

    if (g_HasAnimation == 1)
    {
        for (int i = 0; i < 4; i++)
        {
            if (input.BoneIDs[i] == -1)
                continue;

            matrix boneMatrix = g_FinalBoneMatrices[input.BoneIDs[i]];
            float4 localPos = mul(float4(input.Pos, 1.0f), boneMatrix);
            totalLocalPos += localPos * input.Weights[i];

            float3x3 boneRotMatrix = (float3x3) boneMatrix;
            totalNormal += mul(input.Normal, boneRotMatrix) * input.Weights[i];
        }
    }
    else
    {
        totalLocalPos = float4(input.Pos, 1.0f);
        totalNormal = input.Normal;
    }

    float4 worldPos = mul(totalLocalPos, g_World);

    output.SV_Pos = mul(worldPos, g_ViewProjection);
    output.WorldPos = worldPos.xyz;
    output.Normal = normalize(mul(totalNormal, (float3x3) g_World));
    output.TexCoord = input.TexCoord;
    output.Color = g_BaseColor;
    return output;
}

PS_OUTPUT PSMain(PS_INPUT input) : SV_TARGET
{
    PS_OUTPUT output;

    float4 texColor = g_AlbedoMap.Sample(g_Sampler, input.TexCoord);
    float4 baseColor = texColor * input.Color * g_AlbedoColor;
    float3 normal = normalize(input.Normal);

    float3 litColor = baseColor.rgb * 0.18f;
    for (int i = 0; i < g_LightCount; ++i)
    {
        float3 lightDir = normalize(-g_Lights[i].Direction);
        float ndotl = saturate(dot(normal, lightDir));
        litColor += baseColor.rgb * g_Lights[i].Color * g_Lights[i].Intensity * ndotl;
    }

    output.Color = float4(saturate(litColor), baseColor.a);
    output.EntityID = g_EntityID;
    return output;
}
