
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
};

// 2. 출력 구조체 (픽셀 셰이더로 넘어가는 데이터)
struct VertexOutput
{
    float4 Position : SV_POSITION; 
    float4 Color : COLOR; 
    float2 TexCoord : TEXCOORD;
    nointerpolation float TexIndex : TEXINDEX; // 텍스처 인덱스는 보간 없이 전달 (정수로 사용)
};

// 3. 정점 셰이더 (Vertex Shader)
VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    
    
    // 월드 변환을 적용한 후 뷰 프로젝션 변환을 적용하여 최종 위치 계산 (월드 x 뷰 프로젝션)
    //output.Position = mul(mul(float4(input.Position, 1.0f), u_Transform), u_ViewProjection);
    // 월드 변환 없이 뷰 프로젝션 변환만 적용 (월드가 단위 행렬인 경우)
    ///////////////////////////
    output.Position = mul(float4(input.Position, 1.0f), u_ViewProjection);
    output.Color = input.Color;
    output.TexCoord = input.TexCoord;
    output.TexIndex = input.TexIndex;
    
    return output;
}

// 4. 픽셀 셰이더 (Pixel Shader)
float4 PSMain(VertexOutput input) : SV_TARGET
{
    // 텍스처 인덱스를 정수로 변환 (0~7 범위)
    int index = (int) round(input.TexIndex); // 텍스처 인덱스가 0~7 범위를 벗어나지 않도록 클램핑
    
    // 기본색 흰색
    float4 texColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    
    switch (index)
    {
        //case -1:
        //    texColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
        //    break;
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
    
    return texColor * input.Color;
    
}