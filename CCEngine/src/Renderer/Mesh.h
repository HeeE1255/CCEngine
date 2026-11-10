#pragma once
#include "Renderer/Buffer.h"
#include "Core.h"
#include <DirectXMath.h>
#include <memory>
#include <vector>
#include <string>
#include <limits>

namespace CCEngine
{
    enum class ImportedTextureSemantic { BaseColor, Normal, Metallic, Roughness };

    struct ImportedEmbeddedTexture
    {
        unsigned int SourceIndex = 0;
        unsigned int Width = 0;
        unsigned int Height = 0; // 0이면 압축 이미지 바이트, 그 외에는 BGRA 픽셀이다.
        std::vector<unsigned char> Bytes;
    };

    struct ImportedTextureReference
    {
        ImportedTextureSemantic Semantic = ImportedTextureSemantic::BaseColor;
        std::string SourceReference;
        std::string FilePath;
        unsigned int UVChannel = 0;
        unsigned int SourceTextureType = 0;
        unsigned int SourceTextureSlot = 0;
        unsigned int Mapping = 0;
        unsigned int AddressU = 0;
        unsigned int AddressV = 0;
        unsigned int Operation = 0;
        unsigned int Flags = 0;
        float Blend = 1.0f;
        DirectX::XMFLOAT2 UVOffset = { 0, 0 };
        DirectX::XMFLOAT2 UVScale = { 1, 1 };
        float UVRotation = 0.0f;
        int MinFilter = -1;
        int MagFilter = -1;
        bool ValidSampling = true;
        bool Embedded = false;
        bool Exists = false;
        std::shared_ptr<const ImportedEmbeddedTexture> EmbeddedData;
    };

    struct ImportedMaterialData
    {
        unsigned int SourceIndex = (std::numeric_limits<unsigned int>::max)();
        std::string Name;
        std::string ModelPath;
        DirectX::XMFLOAT4 BaseColor = { 1, 1, 1, 1 };
        float Metallic = 0.0f;
        float Roughness = 0.5f;
        float Opacity = 1.0f;
        std::vector<ImportedTextureReference> Textures;
    };

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
        std::shared_ptr<const ImportedMaterialData> ImportedMaterial;

        // 정점 배열과 인덱스 배열을 받아서 버퍼를 생성하는 생성자
        Mesh(const std::vector<Vertex3D>& vertices, const std::vector<uint32_t>& indices);
        ~Mesh() = default;

        void Bind() const;
        void Unbind() const;

        uint32_t GetIndexCount() const { return m_IndexBuffer->GetCount(); }

        std::shared_ptr<VertexBuffer> GetVertexBuffer() const { return m_VertexBuffer; }
        std::shared_ptr<IndexBuffer> GetIndexBuffer() const { return m_IndexBuffer; }
        const std::vector<Vertex3D>& GetVertices() const { return m_Vertices; }
        const std::vector<uint32_t>& GetIndices() const { return m_Indices; }
        bool HasSkinWeights() const { return m_HasSkinWeights; }

    private:
        std::shared_ptr<VertexBuffer> m_VertexBuffer;
        std::shared_ptr<IndexBuffer> m_IndexBuffer;
        std::vector<Vertex3D> m_Vertices;
        std::vector<uint32_t> m_Indices;
        bool m_HasSkinWeights = false;
    };
}
