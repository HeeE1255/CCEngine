// assets/shaders/GizmoShader.hlsl

// 1. 카메라 행렬 (b0)
cbuffer CameraCB : register(b0)
{
    matrix u_ViewProjection;
};

// ★ 2. C++의 TransformData 구조체와 완벽하게 형태와 순서를 맞춤 (b1)
cbuffer TransformCB : register(b1)
{
    matrix u_Transform;
    float4 u_BaseColor; // 여기서 색상을 제대로 받습니다!
    int u_EntityID;
    int u_HasAnimation;
    float2 u_Padding;
};

struct VS_INPUT
{
    float3 Position : POSITION;
};

struct PS_INPUT
{
    float4 SV_Position : SV_POSITION;
};

// ==========================================
// Vertex Shader
// ==========================================
PS_INPUT VSMain(VS_INPUT input)
{
    PS_INPUT output;
    
    matrix mvp = mul(u_Transform, u_ViewProjection);
    output.SV_Position = mul(float4(input.Position, 1.0f), mvp);
    
    return output;
}

// ==========================================
// Pixel Shader
// ==========================================
float4 PSMain(PS_INPUT input) : SV_TARGET
{
    // C++에서 넘겨준 빨/초/파 색상을 조명 계산 없이 바로 출력
    return u_BaseColor;
}