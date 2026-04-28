#pragma once
#include "Renderer/Buffer.h"
#include "Core.h"
#include <DirectXMath.h>
#include <memory>
#include <vector>

namespace CCEngine
{
    // 3D 렌더링을 위한 표준 정점 데이터 (필수 요소들)
    struct Vertex3D
    {
        DirectX::XMFLOAT3 Position;
        DirectX::XMFLOAT3 Normal;   // 빛(Lighting) 연산을 위한 법선 벡터
        DirectX::XMFLOAT2 TexCoord; // 텍스처(이미지) 매핑 좌표

        int BoneIDs[4] = { -1, -1, -1, -1 };         // 영향을 주는 뼈의 인덱스 (-1이면 영향 없음)
        float Weights[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // 각 뼈가 미치는 가중치 (합치면 1.0)
    };

    class CC_API Mesh
    {
    public:

        std::string Name = "";
        std::string TexturePath = "";

        // 정점 배열과 인덱스 배열을 받아서 버퍼를 생성하는 생성자
        Mesh(const std::vector<Vertex3D>& vertices, const std::vector<uint32_t>& indices);
        ~Mesh() = default;

        void Bind() const;
        void Unbind() const;

        uint32_t GetIndexCount() const { return m_IndexBuffer->GetCount(); }

        std::shared_ptr<VertexBuffer> GetVertexBuffer() const { return m_VertexBuffer; }
        std::shared_ptr<IndexBuffer> GetIndexBuffer() const { return m_IndexBuffer; }

    private:
        std::shared_ptr<VertexBuffer> m_VertexBuffer;
        std::shared_ptr<IndexBuffer> m_IndexBuffer;
    };
}