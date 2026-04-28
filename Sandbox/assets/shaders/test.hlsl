// 트렌스폼 버퍼 (Transform Buffer)
cbuffer TransformBuffer : register(b0)
{
    matrix u_ViewProjection; // 뷰 프로젝션 행렬
    //matrix u_Transform;
};

Texture2D u_Textures[8] : register(t0); // t0 슬롯부터 시작하는 텍스처 배열 (최대 8개)
SamplerState u_Sampler : register(s0); // s0 슬롯의 샘플러 (DX11 기본값 사용)

// 1. 입력 구조체 (C++ 버퍼에서 들어오는 데이터)
struct VertexInput
{
    float3 Position : POSITION; // 대문자 P
    float4 Color : COLOR; // 대문자 C
    float2 TexCoord : TEXCOORD;
    float TexIndex : TEXINDEX;
    int EntityID : ENTITY_ID; // C++에서 넘어온 엔티티 ID
};

// 2. 출력 구조체 (픽셀 셰이더로 넘어가는 데이터)
struct VertexOutput
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR;
    float2 TexCoord : TEXCOORD;
    nointerpolation float TexIndex : TEXINDEX; // 텍스처 인덱스는 보간 없이 전달
    nointerpolation int EntityID : ENTITY_ID; // [핵심] 엔티티 아이디도 보간 없이 전달!
};

// 3. 정점 셰이더 (Vertex Shader)
VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    
    // 월드 변환 없이 뷰 프로젝션 변환만 적용 (월드가 단위 행렬인 경우)
    output.Position = mul(float4(input.Position, 1.0f), u_ViewProjection);
    output.Color = input.Color;
    output.TexCoord = input.TexCoord;
    output.TexIndex = input.TexIndex;
    
    // ID 값 그대로 전달
    output.EntityID = input.EntityID;
    
    return output;
}


// ==========================================================
// 다중 렌더 타겟(MRT)을 위한 픽셀 셰이더 출력 구조체
// ==========================================================
struct PSOutput
{
    float4 Color : SV_Target0; // 0번 슬롯: 모니터에 보이는 색상 도화지
    int EntityID : SV_Target1; // 1번 슬롯: 안 보이는 ID 버퍼 도화지
};

// 4. 픽셀 셰이더 (Pixel Shader)
PSOutput PSMain(VertexOutput input)
{
    PSOutput output;

    // 텍스처 인덱스를 정수로 변환 (0~7 범위)
    int index = (int) round(input.TexIndex);
    
    // 기본색 흰색
    float4 texColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    
    switch (index)
    {
        case 0:
            texColor = u_Textures[0].Sample(u_Sampler, input.TexCoord);
            break;
        case 1:
            texColor = u_Textures[1].Sample(u_Sampler, input.TexCoord);
            break;
        case 2:
            texColor = u_Textures[2].Sample(u_Sampler, input.TexCoord);
            break;
        case 3:
            texColor = u_Textures[3].Sample(u_Sampler, input.TexCoord);
            break;
        case 4:
            texColor = u_Textures[4].Sample(u_Sampler, input.TexCoord);
            break;
        case 5:
            texColor = u_Textures[5].Sample(u_Sampler, input.TexCoord);
            break;
        case 6:
            texColor = u_Textures[6].Sample(u_Sampler, input.TexCoord);
            break;
        case 7:
            texColor = u_Textures[7].Sample(u_Sampler, input.TexCoord);
            break;
    }
    
    // 1. 눈에 보이는 색상 출력 (0번 버퍼)
    output.Color = texColor * input.Color;
    
    // 2. [마우스 피킹의 핵심] 이 픽셀의 주인(EntityID)을 출력 (1번 버퍼)
    output.EntityID = input.EntityID;
    
    return output;
}