#include "Renderer/Renderer2D.h"
#include "Renderer/Buffer.h"
#include "Renderer/Shader.h"
#include "Platform/DirectX11/DX11Context.h"
#include "Renderer/Texture.h"

namespace CCEngine
{
    // ==========================================
    // 1. 배치 렌더링용 정점 구조체
    // ==========================================
    struct QuadVertex
    {
        DirectX::XMFLOAT3 Position;
        DirectX::XMFLOAT4 Color;
        DirectX::XMFLOAT2 TexCoord;
        float TexIndex; // 텍스처 인덱스. float를 쓰는 이유는 HLSL에서 정수형 텍스처 인덱스를 float로 받아야 하기 때문
    };

    // ==========================================
    // 2. 렌더러가 사용할 내부 데이터 구조체 (사각형 버퍼, 셰이더, 카메라 상수 버퍼 등)
    // ==========================================
    struct Renderer2DData
    {
        static const uint32_t MaxQuads = 10000;          // 한 번에 그릴 최대 사각형 개수
        static const uint32_t MaxVertices = MaxQuads * 4; // 꼭짓점은 4만 개
        static const uint32_t MaxIndices = MaxQuads * 6;  // 인덱스는 6만 개
        static const uint32_t MaxTextureSlots = 8;        // 최대 텍스처 슬롯 개수

        VertexBuffer* QuadVertexBuffer = nullptr;
        IndexBuffer* QuadIndexBuffer = nullptr;
        Shader* TextureShader = nullptr;
        ConstantBuffer* CameraConstantBuffer = nullptr;

        uint32_t QuadIndexCount = 0;                 // 이번 프레임에 칠할 인덱스 개수
        QuadVertex* QuadVertexBufferBase = nullptr;  // CPU 메모리 배열의 첫 시작점
        QuadVertex* QuadVertexBufferPtr = nullptr;   // 현재 데이터를 적고 있는 펜의 위치

        // 텍스처 슬롯 배열 (현재 배치에서 어떤 텍스처들을 쓰고 있는지 기록)
        Texture2D* TextureSlots[MaxTextureSlots];
        uint32_t TextureSlotIndex = 0; // 현재 몇 개의 텍스처가 등록되었는지 (0부터 시작)
    };

    static Renderer2DData s_Data; // 렌더러가 사용할 내부 데이터는 정적 변수로 선언하여 어디서든 접근 가능하도록 합니다.

    void Renderer2D::Init()
    {
        // GPU에 올릴 정점 버퍼 생성 (동적 버퍼로 생성하여 매 프레임마다 데이터를 업데이트)
        s_Data.QuadVertexBuffer = VertexBuffer::Create(s_Data.MaxVertices * sizeof(QuadVertex));

        BufferLayout layout =
        {
            { ShaderDataType::Float3, "POSITION" },
            { ShaderDataType::Float4, "COLOR" },
            { ShaderDataType::Float2, "TEXCOORD" },
            { ShaderDataType::Float,  "TEXINDEX" }
        };
        s_Data.QuadVertexBuffer->SetLayout(layout);

        // CPU 메모리에 사각형 정점 데이터를 쌓을 배열(이 배열에 데이터를 쌓다가 나중에 GPU 버퍼로 한 번에 보냄)
        s_Data.QuadVertexBufferBase = new QuadVertex[s_Data.MaxVertices];

        // 인덱스 버퍼 생성: 사각형 하나당 6개의 인덱스가 필요하므로 최대 인덱스 개수는 MaxQuads * 6
        uint32_t* quadIndices = new uint32_t[s_Data.MaxIndices];
        uint32_t offset = 0;
        for (uint32_t i = 0; i < s_Data.MaxIndices; i += 6)
        {
            // 첫 번째 삼각형 (0, 2, 1 순서)
            quadIndices[i + 0] = offset + 0;
            quadIndices[i + 1] = offset + 2; 
            quadIndices[i + 2] = offset + 1;

            // 두 번째 삼각형 (2, 0, 3 순서)
            quadIndices[i + 3] = offset + 2;
            quadIndices[i + 4] = offset + 0; 
            quadIndices[i + 5] = offset + 3;

            offset += 4; // 다음 사각형은 4번 정점부터 시작
        }

        s_Data.QuadIndexBuffer = IndexBuffer::Create(quadIndices, s_Data.MaxIndices);
        delete[] quadIndices; // GPU로 넘겼으니 임시 배열은 삭제

        // 셰이더 및 상수 버퍼 (카메라용) 세팅
        s_Data.TextureShader = Shader::Create("assets/shaders/test.hlsl");
        s_Data.CameraConstantBuffer = ConstantBuffer::Create(sizeof(DirectX::XMMATRIX));

        // 텍스처 슬롯 초기화 (처음에는 아무 텍스처도 등록되어 있지 않으므로 nullptr로 초기화)
        for (uint32_t i = 0; i < s_Data.MaxTextureSlots; i++)
        {
            s_Data.TextureSlots[i] = nullptr;
        }
    }

    void Renderer2D::Shutdown()
    {
        delete[] s_Data.QuadVertexBufferBase;
        delete s_Data.CameraConstantBuffer;
        delete s_Data.TextureShader;
        delete s_Data.QuadIndexBuffer;
        delete s_Data.QuadVertexBuffer;
    }

    void Renderer2D::BeginScene(const PerspectiveCamera& camera)
    {
        // 카메라의 뷰 프로젝션 행렬을 계산해서 GPU에 전달 (HLSL에서 행렬을 뒤집어서 사용하기 때문에 Transpose 필수)
        DirectX::XMMATRIX viewProj = DirectX::XMMatrixTranspose(camera.GetViewProjectionMatrix());
        s_Data.CameraConstantBuffer->SetData(&viewProj, sizeof(DirectX::XMMATRIX));
        s_Data.CameraConstantBuffer->Bind(0);

        // 씬이 시작될 때마다 인덱스 카운트와 버퍼 포인터를 초기화하여 새로운 사각형 데이터를 쌓을 준비
        s_Data.QuadIndexCount = 0;
        s_Data.QuadVertexBufferPtr = s_Data.QuadVertexBufferBase;

        // 텍스처 슬롯도 초기화
        s_Data.TextureSlotIndex = 0;
    }

    void Renderer2D::EndScene()
    {
        // 만약 그릴 사각형이 하나도 없다면, 굳이 GPU에 데이터를 보내고 드로우 콜을 할 필요가 없으므로 바로 리턴
        if (s_Data.QuadIndexCount == 0)
        {
            return;
        }

        // [핵심] 지금까지 펜이 이동한 거리를 계산해서, 딱 그만큼만 GPU 정점 버퍼에 덮어씁니다!
        uint32_t dataSize = (uint32_t)((uint8_t*)s_Data.QuadVertexBufferPtr - (uint8_t*)s_Data.QuadVertexBufferBase);
        s_Data.QuadVertexBuffer->SetData(s_Data.QuadVertexBufferBase, dataSize);

        // GPU에 텍스처 슬롯에 등록된 텍스처들을 바인딩
        for (uint32_t i = 0; i < s_Data.TextureSlotIndex; i++)
        {
            s_Data.TextureSlots[i]->Bind(i);
        }

        // 파이프라인 장착 및 대망의 한 방 그리기(Draw Call 1회)!
        s_Data.TextureShader->Bind();
        s_Data.TextureShader->BindLayout(s_Data.QuadVertexBuffer->GetLayout());
        s_Data.QuadVertexBuffer->Bind();
        s_Data.QuadIndexBuffer->Bind();

        auto context = DX11Context::Get()->GetDeviceContext();
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->DrawIndexed(s_Data.QuadIndexCount, 0, 0);
    }

    void Renderer2D::DrawQuad(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT2& size, const DirectX::XMFLOAT4& color)
    {
        // 인덱스 버퍼가 가득 찼다면, 지금까지 쌓은 데이터를 GPU로 보내고 그리는 작업을 먼저 처리한 후에, 다시 버퍼를 초기화해서 새로운 사각형 데이터를 쌓을 준비
        if (s_Data.QuadIndexCount >= Renderer2DData::MaxIndices)
        {
            EndScene();
            s_Data.QuadIndexCount = 0;
            s_Data.QuadVertexBufferPtr = s_Data.QuadVertexBufferBase;
        }

        // 사각형의 절반 크기 (중앙 기준 정렬)
        float halfX = size.x / 2.0f;
        float halfY = size.y / 2.0f;

        // 1번 정점 (왼쪽 아래)
        s_Data.QuadVertexBufferPtr->Position = { position.x - halfX, position.y - halfY, position.z };
        s_Data.QuadVertexBufferPtr->Color = color;
        s_Data.QuadVertexBufferPtr->TexCoord = { 0.0f, 0.0f };
        s_Data.QuadVertexBufferPtr->TexIndex = -1.0f;
        s_Data.QuadVertexBufferPtr++; // 펜 이동

        // 2번 정점 (오른쪽 아래)
        s_Data.QuadVertexBufferPtr->Position = { position.x + halfX, position.y - halfY, position.z };
        s_Data.QuadVertexBufferPtr->Color = color;
        s_Data.QuadVertexBufferPtr->TexCoord = { 1.0f, 0.0f };
        s_Data.QuadVertexBufferPtr->TexIndex = -1.0f;
        s_Data.QuadVertexBufferPtr++;

        // 3번 정점 (오른쪽 위)
        s_Data.QuadVertexBufferPtr->Position = { position.x + halfX, position.y + halfY, position.z };
        s_Data.QuadVertexBufferPtr->Color = color;
        s_Data.QuadVertexBufferPtr->TexCoord = { 1.0f, 1.0f };
        s_Data.QuadVertexBufferPtr->TexIndex = -1.0f;
        s_Data.QuadVertexBufferPtr++;

        // 4번 정점 (왼쪽 위)
        s_Data.QuadVertexBufferPtr->Position = { position.x - halfX, position.y + halfY, position.z };
        s_Data.QuadVertexBufferPtr->Color = color;
        s_Data.QuadVertexBufferPtr->TexCoord = { 0.0f, 1.0f };
        s_Data.QuadVertexBufferPtr->TexIndex = -1.0f;
        s_Data.QuadVertexBufferPtr++;

        // 사각형 1개를 그렸으니, 인덱스는 6개 추가
        s_Data.QuadIndexCount += 6;
    }


    void Renderer2D::DrawQuad(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT2& size, Texture2D* texture)
    {
        if (s_Data.QuadIndexCount >= Renderer2DData::MaxIndices || s_Data.TextureSlotIndex >= Renderer2DData::MaxTextureSlots)
        {
            EndScene(); // 버퍼나 텍스처 슬롯이 꽉 차면 한 번 비우고 다시 시작!

            // 버퍼 초기화
            s_Data.QuadIndexCount = 0;
            s_Data.QuadVertexBufferPtr = s_Data.QuadVertexBufferBase;
            s_Data.TextureSlotIndex = 0;
        }

        // ==========================================
        // [핵심] 이 텍스처가 이미 슬롯에 있는지 확인
        // ==========================================
        float textureIndex = -1.0f;
        for (uint32_t i = 0; i < s_Data.TextureSlotIndex; i++)
        {
            if (s_Data.TextureSlots[i] == texture)
            {
                textureIndex = (float)i; // 이미 등록된 텍스처면 그 번호를 재활용
                break;
            }
        }

        // 만약 처음 보는 텍스처라면 새 슬롯에 등록!
        if (textureIndex == -1.0f)
        {
            textureIndex = (float)s_Data.TextureSlotIndex;
            s_Data.TextureSlots[s_Data.TextureSlotIndex] = texture;
            s_Data.TextureSlotIndex++;
        }

        float halfX = size.x / 2.0f;
        float halfY = size.y / 2.0f;

        // 1번 정점
        s_Data.QuadVertexBufferPtr->Position = { position.x - halfX, position.y - halfY, position.z };
        s_Data.QuadVertexBufferPtr->Color = { 1.0f, 1.0f, 1.0f, 1.0f }; // 텍스처 원본 색상
        s_Data.QuadVertexBufferPtr->TexCoord = { 0.0f, 0.0f };
        s_Data.QuadVertexBufferPtr->TexIndex = textureIndex; // [추가] 나 이 슬롯 번호 쓸게!
        s_Data.QuadVertexBufferPtr++;

        // 2번 정점
        s_Data.QuadVertexBufferPtr->Position = { position.x + halfX, position.y - halfY, position.z };
        s_Data.QuadVertexBufferPtr->Color = { 1.0f, 1.0f, 1.0f, 1.0f };
        s_Data.QuadVertexBufferPtr->TexCoord = { 1.0f, 0.0f };
        s_Data.QuadVertexBufferPtr->TexIndex = textureIndex;
        s_Data.QuadVertexBufferPtr++;

        // 3번 정점
        s_Data.QuadVertexBufferPtr->Position = { position.x + halfX, position.y + halfY, position.z };
        s_Data.QuadVertexBufferPtr->Color = { 1.0f, 1.0f, 1.0f, 1.0f };
        s_Data.QuadVertexBufferPtr->TexCoord = { 1.0f, 1.0f };
        s_Data.QuadVertexBufferPtr->TexIndex = textureIndex;
        s_Data.QuadVertexBufferPtr++;

        // 4번 정점
        s_Data.QuadVertexBufferPtr->Position = { position.x - halfX, position.y + halfY, position.z };
        s_Data.QuadVertexBufferPtr->Color = { 1.0f, 1.0f, 1.0f, 1.0f };
        s_Data.QuadVertexBufferPtr->TexCoord = { 0.0f, 1.0f };
        s_Data.QuadVertexBufferPtr->TexIndex = textureIndex;
        s_Data.QuadVertexBufferPtr++;

        s_Data.QuadIndexCount += 6;
    }
}