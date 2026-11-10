// =====================================================================
// Base3D - 기본 3D Lit 셰이더
// =====================================================================
// 이 셰이더는 엔진의 기본 표시용이다.
// 기존 씬의 색감이 바뀌지 않도록 PBR 계산은 넣지 않고, 가벼운 Lambert 조명만 사용한다.

// @property Toggle LinearColorSpace = false

// 색상 입력만 sRGB에서 선형으로 바꾼다. 알파, 노멀, 거칠기 등의 수치는 그대로 사용한다.
float3 SRGBToLinear(float3 value)
{
    value = max(value, 0.0f);
    return float3(value.r <= 0.04045f ? value.r / 12.92f : pow((value.r + 0.055f) / 1.055f, 2.4f),
        value.g <= 0.04045f ? value.g / 12.92f : pow((value.g + 0.055f) / 1.055f, 2.4f),
        value.b <= 0.04045f ? value.b / 12.92f : pow((value.b + 0.055f) / 1.055f, 2.4f));
}

float3 LinearToSRGB(float3 value)
{
    value = max(value, 0.0f);
    return float3(value.r <= 0.0031308f ? value.r * 12.92f : 1.055f * pow(value.r, 1.0f / 2.4f) - 0.055f,
        value.g <= 0.0031308f ? value.g * 12.92f : 1.055f * pow(value.g, 1.0f / 2.4f) - 0.055f,
        value.b <= 0.0031308f ? value.b * 12.92f : 1.055f * pow(value.b, 1.0f / 2.4f) - 0.055f);
}

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
    matrix g_FinalBoneMatrices[512];
};

cbuffer SceneBuffer : register(b3)
{
    LightInfo g_Lights[4];
    int g_LightCount;
    float g_ScenePad0;
    float g_ScenePad1;
    float g_ScenePad2;
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

PS_OUTPUT PSMain(PS_INPUT input)
{
    PS_OUTPUT output;

    float4 texColor = g_AlbedoMap.Sample(g_Sampler, input.TexCoord);
    bool linearColor = ((uint)g_SurfaceValues.w & 2u) != 0u;
    float4 tint = input.Color;
    float4 materialTint = g_AlbedoColor;
    if (linearColor)
    {
        tint.rgb = SRGBToLinear(tint.rgb);
        materialTint.rgb = SRGBToLinear(materialTint.rgb);
    }
    float4 baseColor = texColor * tint * materialTint;
    float3 normal = normalize(input.Normal);

    float3 litColor = baseColor.rgb * 0.18f;
    for (int i = 0; i < g_LightCount; ++i)
    {
        float3 lightDir = normalize(-g_Lights[i].Direction);
        float ndotl = saturate(dot(normal, lightDir));
        float3 lightColor = linearColor ? SRGBToLinear(g_Lights[i].Color) : g_Lights[i].Color;
        litColor += baseColor.rgb * lightColor * g_Lights[i].Intensity * ndotl;
    }

    // 현재 UNORM 타깃과 UI 합성은 표시 색을 받는다. HDR 출력 단계가 생기기 전까지 여기서 한 번만 인코딩한다.
    bool hdrOutput = ((uint)g_SurfaceValues.w & 4u) != 0u;
    // HDR 합성 중에는 선형 광량을 보존한다. 화면용 변환은 ToneMap 패스에서 한 번만 한다.
    output.Color = float4(hdrOutput ? min(max(litColor, 0.0f), 65504.0f) : saturate(linearColor ? LinearToSRGB(litColor) : litColor), baseColor.a);
    output.EntityID = g_EntityID;
    return output;
}
