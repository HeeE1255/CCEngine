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
    struct QuadVertex
    {
        DirectX::XMFLOAT3 Position;
        DirectX::XMFLOAT4 Color;
        DirectX::XMFLOAT2 TexCoord;
        float TexIndex;
        int EntityID;
    };

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

        // ★ [핵심] Texture2D* 배열에서 void*(SRV) 배열로 변경!
        void* TextureSlots[MaxTextureSlots];
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

    void Renderer2D::BeginScene(const PerspectiveCamera& camera)
    {
        RenderCommand::SetDepthTest(false);

        DirectX::XMMATRIX viewProj = DirectX::XMMatrixTranspose(camera.GetViewProjectionMatrix());
        s_Data.CameraConstantBuffer->SetData(&viewProj, sizeof(DirectX::XMMATRIX));
        s_Data.CameraConstantBuffer->Bind(0);

        auto context = DX11Context::Get()->GetDeviceContext();
        float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

        context->OMSetBlendState(RenderState::MRTPicking, blendFactor, 0xffffffff);
        context->RSSetState(RenderState::CullNone);

        StartBatch();
    }

    void Renderer2D::BeginScene(const DirectX::XMMATRIX& viewProjection)
    {
        RenderCommand::SetDepthTest(false);

        DirectX::XMMATRIX transposed = DirectX::XMMatrixTranspose(viewProjection);
        s_Data.CameraConstantBuffer->SetData(&transposed, sizeof(DirectX::XMMATRIX));
        s_Data.CameraConstantBuffer->Bind(0);

        auto context = DX11Context::Get()->GetDeviceContext();
        float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

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

        // =====================================================================
        // ★ [핵심] void* (SRV) 배열을 DX11 파이프라인에 직접 장착합니다.
        // Texture2D 클래스의 Bind()에 의존하지 않으므로 구조가 훨씬 안전해집니다.
        // =====================================================================
        auto context = DX11Context::Get()->GetDeviceContext();
        for (uint32_t i = 0; i < s_Data.TextureSlotIndex; i++)
        {
            if (s_Data.TextureSlots[i])
            {
                ID3D11ShaderResourceView* srv = static_cast<ID3D11ShaderResourceView*>(s_Data.TextureSlots[i]);
                context->PSSetShaderResources(i, 1, &srv);
            }
        }

        s_Data.TextureShader->Bind();
        s_Data.TextureShader->BindLayout(s_Data.QuadVertexBuffer->GetLayout());
        s_Data.QuadVertexBuffer->Bind();
        s_Data.QuadIndexBuffer->Bind();

        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->DrawIndexed(s_Data.QuadIndexCount, 0, 0);
    }

    // =========================================================================
    // 1. 단색 사각형 그리기
    // =========================================================================
    void Renderer2D::DrawQuad(const DirectX::XMMATRIX& transform, const DirectX::XMFLOAT4& color, int entityID)
    {
        if (s_Data.QuadIndexCount >= Renderer2DData::MaxIndices)
        {
            Flush(); StartBatch();
        }

        DirectX::XMVECTOR quadVertexPositions[4] = {
            DirectX::XMVectorSet(-0.5f, -0.5f, 0.0f, 1.0f), DirectX::XMVectorSet(0.5f, -0.5f, 0.0f, 1.0f),
            DirectX::XMVectorSet(0.5f,  0.5f, 0.0f, 1.0f), DirectX::XMVectorSet(-0.5f,  0.5f, 0.0f, 1.0f)
        };

        DirectX::XMFLOAT2 texCoords[4] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };

        for (size_t i = 0; i < 4; i++)
        {
            DirectX::XMVECTOR worldPos = DirectX::XMVector3Transform(quadVertexPositions[i], transform);
            DirectX::XMFLOAT3 finalPos; DirectX::XMStoreFloat3(&finalPos, worldPos);

            s_Data.QuadVertexBufferPtr->Position = finalPos;
            s_Data.QuadVertexBufferPtr->Color = color;
            s_Data.QuadVertexBufferPtr->TexCoord = texCoords[i];
            s_Data.QuadVertexBufferPtr->TexIndex = -1.0f;
            s_Data.QuadVertexBufferPtr->EntityID = entityID;
            s_Data.QuadVertexBufferPtr++;
        }
        s_Data.QuadIndexCount += 6;
    }

    // =========================================================================
    // 2. ★ void* (SRV) 기반 메인 사각형 그리기 (새로 추가됨)
    // =========================================================================
    void Renderer2D::DrawQuad(const DirectX::XMMATRIX& transform, void* textureID, const DirectX::XMFLOAT4& tintColor, int entityID)
    {
        DirectX::XMFLOAT2 texCoords[4] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
        DrawQuad(transform, textureID, texCoords, tintColor, entityID);
    }

    void Renderer2D::DrawQuad(const DirectX::XMMATRIX& transform, void* textureID, const DirectX::XMFLOAT2* texCoords, const DirectX::XMFLOAT4& tintColor, int entityID)
    {
        if (s_Data.QuadIndexCount >= Renderer2DData::MaxIndices || s_Data.TextureSlotIndex >= Renderer2DData::MaxTextureSlots)
        {
            Flush(); StartBatch();
        }

        float textureIndex = -1.0f;
        for (uint32_t i = 0; i < s_Data.TextureSlotIndex; i++)
        {
            if (s_Data.TextureSlots[i] == textureID)
            {
                textureIndex = (float)i;
                break;
            }
        }

        if (textureIndex == -1.0f)
        {
            textureIndex = (float)s_Data.TextureSlotIndex;
            s_Data.TextureSlots[s_Data.TextureSlotIndex] = textureID;
            s_Data.TextureSlotIndex++;
        }

        DirectX::XMVECTOR quadVertexPositions[4] = {
            DirectX::XMVectorSet(-0.5f, -0.5f, 0.0f, 1.0f), DirectX::XMVectorSet(0.5f, -0.5f, 0.0f, 1.0f),
            DirectX::XMVectorSet(0.5f,  0.5f, 0.0f, 1.0f), DirectX::XMVectorSet(-0.5f,  0.5f, 0.0f, 1.0f)
        };

        for (size_t i = 0; i < 4; i++)
        {
            DirectX::XMVECTOR worldPos = DirectX::XMVector3Transform(quadVertexPositions[i], transform);
            DirectX::XMFLOAT3 finalPos; DirectX::XMStoreFloat3(&finalPos, worldPos);

            s_Data.QuadVertexBufferPtr->Position = finalPos;
            s_Data.QuadVertexBufferPtr->Color = tintColor;
            s_Data.QuadVertexBufferPtr->TexCoord = texCoords[i];
            s_Data.QuadVertexBufferPtr->TexIndex = textureIndex;
            s_Data.QuadVertexBufferPtr->EntityID = entityID;
            s_Data.QuadVertexBufferPtr++;
        }
        s_Data.QuadIndexCount += 6;
    }

    // =========================================================================
    // 3. Texture2D* 래퍼 함수 (void* 메인 함수로 포워딩)
    // (이 부분이 있으므로 기존에 Texture2D를 넘기던 폰트, 스프라이트 코드도 그대로 작동합니다)
    // =========================================================================
    void Renderer2D::DrawQuad(const DirectX::XMMATRIX& transform, Texture2D* texture, const DirectX::XMFLOAT4& tintColor, int entityID)
    {
        void* srv = texture ? texture->GetRendererID() : nullptr;
        DrawQuad(transform, srv, tintColor, entityID);
    }

    void Renderer2D::DrawQuad(const DirectX::XMMATRIX& transform, Texture2D* texture, const DirectX::XMFLOAT2* texCoords, const DirectX::XMFLOAT4& tintColor, int entityID)
    {
        void* srv = texture ? texture->GetRendererID() : nullptr;
        DrawQuad(transform, srv, texCoords, tintColor, entityID);
    }

    // (기타 Position, Size를 받는 Wrapper 함수들 - 내부적으로 transform 행렬을 만들어 위 핵심 함수 호출)
    void Renderer2D::DrawQuad(const DirectX::XMFLOAT2& position, const DirectX::XMFLOAT2& size, const DirectX::XMFLOAT4& color, int entityID)
    {
        DrawQuad({ position.x, position.y, 0.0f }, size, color, entityID);
    }

    void Renderer2D::DrawQuad(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT2& size, const DirectX::XMFLOAT4& color, int entityID)
    {
        DirectX::XMMATRIX transform = DirectX::XMMatrixScaling(size.x, size.y, 1.0f) * DirectX::XMMatrixTranslation(position.x, position.y, position.z);
        DrawQuad(transform, color, entityID);
    }

    void Renderer2D::DrawQuad(const DirectX::XMFLOAT2& position, const DirectX::XMFLOAT2& size, Texture2D* texture, const DirectX::XMFLOAT4& tintColor, int entityID)
    {
        DrawQuad({ position.x, position.y, 0.0f }, size, texture, tintColor, entityID);
    }

    void Renderer2D::DrawQuad(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT2& size, Texture2D* texture, const DirectX::XMFLOAT4& tintColor, int entityID)
    {
        DirectX::XMMATRIX transform = DirectX::XMMatrixScaling(size.x, size.y, 1.0f) * DirectX::XMMatrixTranslation(position.x, position.y, position.z);
        DrawQuad(transform, texture, tintColor, entityID);
    }

    void Renderer2D::DrawQuad(const DirectX::XMFLOAT2& position, const DirectX::XMFLOAT2& size, Texture2D* texture, const DirectX::XMFLOAT2* texCoords, const DirectX::XMFLOAT4& tintColor, int entityID)
    {
        DrawQuad({ position.x, position.y, 0.0f }, size, texture, texCoords, tintColor, entityID);
    }

    void Renderer2D::DrawQuad(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT2& size, Texture2D* texture, const DirectX::XMFLOAT2* texCoords, const DirectX::XMFLOAT4& tintColor, int entityID)
    {
        DirectX::XMMATRIX transform = DirectX::XMMatrixScaling(size.x, size.y, 1.0f) * DirectX::XMMatrixTranslation(position.x, position.y, position.z);
        DrawQuad(transform, texture, texCoords, tintColor, entityID);
    }
}