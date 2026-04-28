// =====================================================================
// 1. 상수 버퍼 (Constant Buffers)
// =====================================================================

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

// =====================================================================
// 2. 구조체 (Structs)
// =====================================================================

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
    float4 Color : COLOR; // C++에서 보낸 BaseColor를 담을 그릇
};

struct PS_OUTPUT
{
    float4 Color : SV_Target0; // 우리가 보는 실제 색상
    int EntityID : SV_Target1; // 백그라운드 피킹용 ID 버퍼
};

// =====================================================================
// 3. 버텍스 셰이더 (Vertex Shader)
// =====================================================================
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
            {
                continue;
            }

            matrix boneMatrix = g_FinalBoneMatrices[input.BoneIDs[i]];

            float4 localPos = mul(float4(input.Pos, 1.0f), boneMatrix);
            totalLocalPos += localPos * input.Weights[i];

            float3x3 boneRotMatrix = (float3x3) boneMatrix;
            totalNormal += mul(input.Normal, boneRotMatrix) * input.Weights[i];
        }
    }
    else
    {
        // 정적 메쉬(큐브 등)는 로컬 정점 그대로 사용
        totalLocalPos = float4(input.Pos, 1.0f);
        totalNormal = input.Normal;
    }

    float4 worldPos = mul(totalLocalPos, g_World);
    
    output.SV_Pos = mul(worldPos, g_ViewProjection);
    output.WorldPos = worldPos.xyz;
    output.Normal = normalize(mul(totalNormal, (float3x3) g_World));
    output.TexCoord = input.TexCoord;
    
    // 컴포넌트에서 세팅한 색상을 어떤 가공도 없이 그대로 픽셀 셰이더로 토스!
    output.Color = g_BaseColor;

    return output;
}

// =====================================================================
// 4. 픽셀 셰이더 (Pixel Shader)
// =====================================================================
Texture2D g_AlbedoMap : register(t0);
SamplerState g_Sampler : register(s0);

PS_OUTPUT PSMain(PS_INPUT input) : SV_TARGET
{
    PS_OUTPUT output;

    float4 texColor = g_AlbedoMap.Sample(g_Sampler, input.TexCoord);
    float3 totalDiffuse = float3(0.1f, 0.1f, 0.1f);

    for (int j = 0; j < g_LightCount; j++)
    {
        float3 lightDir = normalize(g_Lights[j].Direction);
        float diff = max(dot(input.Normal, -lightDir), 0.0f);
        totalDiffuse += diff * g_Lights[j].Color * g_Lights[j].Intensity;
    }

    output.Color = texColor * float4(totalDiffuse, 1.0f) * input.Color;

     // [핵심!] TransformBuffer에서 받아온 ID를 두 번째 타겟으로 출력!
    output.EntityID = g_EntityID;

    return output;
}