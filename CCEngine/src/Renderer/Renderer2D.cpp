#include "Renderer/Renderer2D.h"
#include "Renderer/Buffer.h"
#include "Renderer/Shader.h"
#include "Platform/DirectX11/DX11Context.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Texture.h"
#include "Renderer/RenderState.h" 
#include <d3d11.h>

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
        float TexIndex; // 텍스처 인덱스
        int EntityID;   // 마우스 피킹용 엔티티 ID
    };

    // ==========================================
    // 2. 렌더러 내부 데이터 구조체
    // ==========================================
    struct Renderer2DData
    {
        static const uint32_t MaxQuads = 10000;
        static const uint32_t MaxVertices = MaxQuads * 4;
        static const uint32_t MaxIndices = MaxQuads * 6;
        static const uint32_t MaxTextureSlots = 8;

        VertexBuffer* QuadVertexBuffer = nullptr;
        IndexBuffer* QuadIndexBuffer = nullptr;
        Shader* TextureShader = nullptr;
        ConstantBuffer* CameraConstantBuffer = nullptr;

        uint32_t QuadIndexCount = 0;
        QuadVertex* QuadVertexBufferBase = nullptr;
        QuadVertex* QuadVertexBufferPtr = nullptr;

        Texture2D* TextureSlots[MaxTextureSlots];
        uint32_t TextureSlotIndex = 0;
    };

    static Renderer2DData s_Data;

    void Renderer2D::Init()
    {
        s_Data.QuadVertexBuffer = VertexBuffer::Create(s_Data.MaxVertices * sizeof(QuadVertex));

        BufferLayout layout =
        {
            { ShaderDataType::Float3, "POSITION" },
            { ShaderDataType::Float4, "COLOR" },
            { ShaderDataType::Float2, "TEXCOORD" },
            { ShaderDataType::Float,  "TEXINDEX" },
            { ShaderDataType::Int,    "ENTITY_ID" }
        };
        s_Data.QuadVertexBuffer->SetLayout(layout);

        s_Data.QuadVertexBufferBase = new QuadVertex[s_Data.MaxVertices];

        uint32_t* quadIndices = new uint32_t[s_Data.MaxIndices];
        uint32_t offset = 0;
        for (uint32_t i = 0; i < s_Data.MaxIndices; i += 6)
        {
            quadIndices[i + 0] = offset + 0;
            quadIndices[i + 1] = offset + 2;
            quadIndices[i + 2] = offset + 1;

            quadIndices[i + 3] = offset + 2;
            quadIndices[i + 4] = offset + 0;
            quadIndices[i + 5] = offset + 3;

            offset += 4;
        }

        s_Data.QuadIndexBuffer = IndexBuffer::Create(quadIndices, s_Data.MaxIndices);
        delete[] quadIndices;

        //s_Data.TextureShader = Shader::Create("assets/shaders/test.hlsl");
        s_Data.TextureShader = Shader::Create("assets/shaders/Renderer2D.hlsl");
        s_Data.CameraConstantBuffer = ConstantBuffer::Create(sizeof(DirectX::XMMATRIX));

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

    // =========================================================================
    // 3D 씬(게임/에디터 월드)용 BeginScene
    // =========================================================================
    void Renderer2D::BeginScene(const PerspectiveCamera& camera)
    {
        RenderCommand::SetDepthTest(false);

        DirectX::XMMATRIX viewProj = DirectX::XMMatrixTranspose(camera.GetViewProjectionMatrix());
        s_Data.CameraConstantBuffer->SetData(&viewProj, sizeof(DirectX::XMMATRIX));
        s_Data.CameraConstantBuffer->Bind(0);

        auto context = DX11Context::Get()->GetDeviceContext();
        float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

        // 에디터 피킹을 위해 MRT 유지, 2D 쿼드는 양면에서 보여야 하므로 CullNone
        context->OMSetBlendState(RenderState::MRTPicking, blendFactor, 0xffffffff);
        //context->OMSetBlendState(nullptr, blendFactor, 0xffffffff);
        context->RSSetState(RenderState::CullNone);

        StartBatch();
    }

    // =========================================================================
    // UI 전용 (Ortho 매트릭스) BeginScene
    // =========================================================================
    void Renderer2D::BeginScene(const DirectX::XMMATRIX& viewProjection)
    {
        // 심도(Depth) 테스트 끄기 (Painter's Algorithm)
        RenderCommand::SetDepthTest(false);

        // 전달받은 직교투영 행렬(Ortho)을 셰이더 상수 버퍼에 장착
        DirectX::XMMATRIX transposed = DirectX::XMMatrixTranspose(viewProjection);
        s_Data.CameraConstantBuffer->SetData(&transposed, sizeof(DirectX::XMMATRIX));
        s_Data.CameraConstantBuffer->Bind(0);

        auto context = DX11Context::Get()->GetDeviceContext();
        float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

        // ★ [핵심 수정] UI를 위한 완벽한 반투명(Transparent)과 컬링 끄기(CullNone) 적용
        context->OMSetBlendState(RenderState::Transparent, blendFactor, 0xffffffff);
        context->RSSetState(RenderState::CullNone);

        StartBatch();
    }

    void Renderer2D::EndScene()
    {
        Flush();
    }

    void Renderer2D::StartBatch()
    {
        s_Data.QuadIndexCount = 0;
        s_Data.QuadVertexBufferPtr = s_Data.QuadVertexBufferBase;
        s_Data.TextureSlotIndex = 0;
    }

    void Renderer2D::Flush()
    {
        if (s_Data.QuadIndexCount == 0) return;

        uint32_t dataSize = (uint32_t)((uint8_t*)s_Data.QuadVertexBufferPtr - (uint8_t*)s_Data.QuadVertexBufferBase);
        s_Data.QuadVertexBuffer->SetData(s_Data.QuadVertexBufferBase, dataSize);

        for (uint32_t i = 0; i < s_Data.TextureSlotIndex; i++)
        {
            s_Data.TextureSlots[i]->Bind(i);
        }

        s_Data.TextureShader->Bind();
        s_Data.TextureShader->BindLayout(s_Data.QuadVertexBuffer->GetLayout());
        s_Data.QuadVertexBuffer->Bind();
        s_Data.QuadIndexBuffer->Bind();

        auto context = DX11Context::Get()->GetDeviceContext();
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->DrawIndexed(s_Data.QuadIndexCount, 0, 0);
    }

    // =========================================================================
    // 단색 사각형 그리기 (메인 함수)
    // =========================================================================
    void Renderer2D::DrawQuad(const DirectX::XMMATRIX& transform, const DirectX::XMFLOAT4& color, int entityID)
    {
        if (s_Data.QuadIndexCount >= Renderer2DData::MaxIndices)
        {
            Flush();
            StartBatch();
        }

        DirectX::XMVECTOR quadVertexPositions[4] = {
            DirectX::XMVectorSet(-0.5f, -0.5f, 0.0f, 1.0f),
            DirectX::XMVectorSet(0.5f, -0.5f, 0.0f, 1.0f),
            DirectX::XMVectorSet(0.5f,  0.5f, 0.0f, 1.0f),
            DirectX::XMVectorSet(-0.5f,  0.5f, 0.0f, 1.0f)
        };

        DirectX::XMFLOAT2 texCoords[4] = {
            { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f }
        };

        for (size_t i = 0; i < 4; i++)
        {
            DirectX::XMVECTOR worldPos = DirectX::XMVector3Transform(quadVertexPositions[i], transform);
            DirectX::XMFLOAT3 finalPos;
            DirectX::XMStoreFloat3(&finalPos, worldPos);

            s_Data.QuadVertexBufferPtr->Position = finalPos;
            s_Data.QuadVertexBufferPtr->Color = color;
            s_Data.QuadVertexBufferPtr->TexCoord = texCoords[i];
            s_Data.QuadVertexBufferPtr->TexIndex = -1.0f; // 텍스처 없음
            s_Data.QuadVertexBufferPtr->EntityID = entityID;
            s_Data.QuadVertexBufferPtr++;
        }

        s_Data.QuadIndexCount += 6;
    }

    // =========================================================================
    // 텍스처 사각형 그리기 (메인 함수)
    // =========================================================================
    void Renderer2D::DrawQuad(const DirectX::XMMATRIX& transform, Texture2D* texture, const DirectX::XMFLOAT4& tintColor, int entityID)
    {
        if (s_Data.QuadIndexCount >= Renderer2DData::MaxIndices || s_Data.TextureSlotIndex >= Renderer2DData::MaxTextureSlots)
        {
            Flush();
            StartBatch();
        }

        float textureIndex = -1.0f;
        for (uint32_t i = 0; i < s_Data.TextureSlotIndex; i++)
        {
            if (s_Data.TextureSlots[i] == texture)
            {
                textureIndex = (float)i;
                break;
            }
        }

        if (textureIndex == -1.0f)
        {
            textureIndex = (float)s_Data.TextureSlotIndex;
            s_Data.TextureSlots[s_Data.TextureSlotIndex] = texture;
            s_Data.TextureSlotIndex++;
        }

        DirectX::XMVECTOR quadVertexPositions[4] = {
            DirectX::XMVectorSet(-0.5f, -0.5f, 0.0f, 1.0f),
            DirectX::XMVectorSet(0.5f, -0.5f, 0.0f, 1.0f),
            DirectX::XMVectorSet(0.5f,  0.5f, 0.0f, 1.0f),
            DirectX::XMVectorSet(-0.5f,  0.5f, 0.0f, 1.0f)
        };

        DirectX::XMFLOAT2 texCoords[4] = {
            { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f }
        };

        for (size_t i = 0; i < 4; i++)
        {
            DirectX::XMVECTOR worldPos = DirectX::XMVector3Transform(quadVertexPositions[i], transform);
            DirectX::XMFLOAT3 finalPos;
            DirectX::XMStoreFloat3(&finalPos, worldPos);

            s_Data.QuadVertexBufferPtr->Position = finalPos;
            s_Data.QuadVertexBufferPtr->Color = tintColor;
            s_Data.QuadVertexBufferPtr->TexCoord = texCoords[i];
            s_Data.QuadVertexBufferPtr->TexIndex = textureIndex; // 텍스처 슬롯 인덱스 할당!
            s_Data.QuadVertexBufferPtr->EntityID = entityID;
            s_Data.QuadVertexBufferPtr++;
        }

        s_Data.QuadIndexCount += 6;
    }

	// =========================================================================
	// 텍스처 사각형 그리기 (UV 지정 버전)
	// =========================================================================
    void Renderer2D::DrawQuad(const DirectX::XMMATRIX& transform, Texture2D* texture, const DirectX::XMFLOAT2* texCoords, const DirectX::XMFLOAT4& tintColor, int entityID)
    {
        // 1. 배치 렌더링 한계치 검사
        if (s_Data.QuadIndexCount >= Renderer2DData::MaxIndices || s_Data.TextureSlotIndex >= Renderer2DData::MaxTextureSlots)
        {
            Flush();
            StartBatch();
        }

        // 2. 텍스처 슬롯 찾기 또는 등록
        float textureIndex = -1.0f;
        for (uint32_t i = 0; i < s_Data.TextureSlotIndex; i++)
        {
            if (s_Data.TextureSlots[i] == texture)
            {
                textureIndex = (float)i;
                break;
            }
        }

        if (textureIndex == -1.0f)
        {
            textureIndex = (float)s_Data.TextureSlotIndex;
            s_Data.TextureSlots[s_Data.TextureSlotIndex] = texture;
            s_Data.TextureSlotIndex++;
        }

        // 3. 로컬 쿼드 정점 위치 (중앙 기준 1x1 사각형)
        DirectX::XMVECTOR quadVertexPositions[4] = {
            DirectX::XMVectorSet(-0.5f, -0.5f, 0.0f, 1.0f),
            DirectX::XMVectorSet(0.5f, -0.5f, 0.0f, 1.0f),
            DirectX::XMVectorSet(0.5f,  0.5f, 0.0f, 1.0f),
            DirectX::XMVectorSet(-0.5f,  0.5f, 0.0f, 1.0f)
        };

        // 4. 버텍스 버퍼에 데이터 밀어넣기
        for (size_t i = 0; i < 4; i++)
        {
            DirectX::XMVECTOR worldPos = DirectX::XMVector3Transform(quadVertexPositions[i], transform);
            DirectX::XMFLOAT3 finalPos;
            DirectX::XMStoreFloat3(&finalPos, worldPos);

            s_Data.QuadVertexBufferPtr->Position = finalPos;
            s_Data.QuadVertexBufferPtr->Color = tintColor;

            // ★ [핵심] 하드코딩된 UV가 아니라 인자로 넘어온 배열(texCoords[i])을 사용합니다!
            s_Data.QuadVertexBufferPtr->TexCoord = texCoords[i];

            s_Data.QuadVertexBufferPtr->TexIndex = textureIndex;
            s_Data.QuadVertexBufferPtr->EntityID = entityID;
            s_Data.QuadVertexBufferPtr++;
        }

        s_Data.QuadIndexCount += 6;
    }

    // =========================================================================
    // 단색 래퍼 함수
    // =========================================================================
    void Renderer2D::DrawQuad(const DirectX::XMFLOAT2& position, const DirectX::XMFLOAT2& size, const DirectX::XMFLOAT4& color, int entityID)
    {
        DrawQuad({ position.x, position.y, 0.0f }, size, color, entityID);
    }

    void Renderer2D::DrawQuad(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT2& size, const DirectX::XMFLOAT4& color, int entityID)
    {
        DirectX::XMMATRIX transform = DirectX::XMMatrixScaling(size.x, size.y, 1.0f) * DirectX::XMMatrixTranslation(position.x, position.y, position.z);
        DrawQuad(transform, color, entityID);
    }

    // =========================================================================
    // 텍스처 래퍼 함수
    // =========================================================================
    void Renderer2D::DrawQuad(const DirectX::XMFLOAT2& position, const DirectX::XMFLOAT2& size, Texture2D* texture, const DirectX::XMFLOAT4& tintColor, int entityID)
    {
        DrawQuad({ position.x, position.y, 0.0f }, size, texture, tintColor, entityID);
    }

    void Renderer2D::DrawQuad(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT2& size, Texture2D* texture, const DirectX::XMFLOAT4& tintColor, int entityID)
    {
        DirectX::XMMATRIX transform = DirectX::XMMatrixScaling(size.x, size.y, 1.0f) * DirectX::XMMatrixTranslation(position.x, position.y, position.z);
        DrawQuad(transform, texture, tintColor, entityID);
    }


    // =========================================================================
    // 텍스처 영역(UV) 지정 래퍼 함수
    // =========================================================================
    void Renderer2D::DrawQuad(const DirectX::XMFLOAT2& position, const DirectX::XMFLOAT2& size, Texture2D* texture, const DirectX::XMFLOAT2* texCoords, const DirectX::XMFLOAT4& tintColor, int entityID)
    {
        // 2D 위치를 3D(z=0)로 변환해서 호출
        DrawQuad({ position.x, position.y, 0.0f }, size, texture, texCoords, tintColor, entityID);
    }

    void Renderer2D::DrawQuad(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT2& size, Texture2D* texture, const DirectX::XMFLOAT2* texCoords, const DirectX::XMFLOAT4& tintColor, int entityID)
    {
        // 위치와 크기를 기반으로 변환 행렬(Transform Matrix) 자동 생성
        DirectX::XMMATRIX transform = DirectX::XMMatrixScaling(size.x, size.y, 1.0f) * DirectX::XMMatrixTranslation(position.x, position.y, position.z);
        DrawQuad(transform, texture, texCoords, tintColor, entityID);
    }
}