// ==========================================
// 1. 상수 버퍼 (카메라 행렬)
// ==========================================
cbuffer Camera : register(b0)
{
    matrix ViewProjection;
};

// ==========================================
// 2. 입력/출력 구조체
// ==========================================
struct VS_INPUT
{
    float3 Pos : POSITION;
    float4 Color : COLOR;
    float2 TexCoord : TEXCOORD;
    float TexIndex : TEXINDEX;
    int EntityID : ENTITY_ID;
};

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float4 Color : COLOR;
    float2 TexCoord : TEXCOORD;
    float TexIndex : TEXINDEX;
    
    // ★ 정수형 데이터는 픽셀 사이에서 보간(Interpolation)되면 값이 망가지므로 반드시 붙여야 합니다.
    nointerpolation int EntityID : ENTITY_ID;
};

// ==========================================
// 3. 버텍스 셰이더
// ==========================================
PS_INPUT VSMain(VS_INPUT input)
{
    PS_INPUT output;
    
    // ★ [핵심] C++에서 XMMatrixTranspose로 넘겼으므로, 벡터가 앞, 행렬이 뒤에 와야 합니다!
    output.Pos = mul(float4(input.Pos, 1.0f), ViewProjection);
    
    output.Color = input.Color;
    output.TexCoord = input.TexCoord;
    output.TexIndex = input.TexIndex;
    output.EntityID = input.EntityID;
    
    return output;
}

// ==========================================
// 4. 픽셀 셰이더
// ==========================================
Texture2D textures[8] : register(t0);
SamplerState g_Sampler : register(s0);

// ★ MRT(다중 렌더 타겟) 출력 구조체
struct PS_OUTPUT
{
    float4 Color : SV_TARGET0; // 화면/색상 버퍼로 나가는 색상
    int EntityID : SV_TARGET1; // 마우스 피킹용 ID 버퍼로 나가는 정수
};

PS_OUTPUT PSMain(PS_INPUT input)
{
    PS_OUTPUT output;
    float4 texColor = input.Color;

    int index = (int) input.TexIndex;
    
    // C++에서 텍스처가 없으면 TexIndex를 -1.0f로 보냅니다.
    // 인덱스가 0 이상일 때만 해당 슬롯의 텍스처를 샘플링합니다.
    if (index == 0)
        texColor *= textures[0].Sample(g_Sampler, input.TexCoord);
    else if (index == 1)
        texColor *= textures[1].Sample(g_Sampler, input.TexCoord);
    else if (index == 2)
        texColor *= textures[2].Sample(g_Sampler, input.TexCoord);
    else if (index == 3)
        texColor *= textures[3].Sample(g_Sampler, input.TexCoord);
    else if (index == 4)
        texColor *= textures[4].Sample(g_Sampler, input.TexCoord);
    else if (index == 5)
        texColor *= textures[5].Sample(g_Sampler, input.TexCoord);
    else if (index == 6)
        texColor *= textures[6].Sample(g_Sampler, input.TexCoord);
    else if (index == 7)
        texColor *= textures[7].Sample(g_Sampler, input.TexCoord);

    output.Color = texColor;
    output.EntityID = input.EntityID;

    return output;
}