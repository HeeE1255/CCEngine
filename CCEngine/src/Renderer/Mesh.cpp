#pragma once
#include "Mesh.h"

namespace CCEngine
{
    Mesh::Mesh(const std::vector<Vertex3D>& vertices, const std::vector<uint32_t>& indices)
        : m_Vertices(vertices), m_Indices(indices)
    {
        // 디버그 와이어, bounds 계산, 에디터 피킹처럼 CPU에서 다시 읽어야 하는 기능이 있다.
        // GPU 버퍼를 만든 뒤 원본을 버리면 이런 에디터 기능이 매번 임포터를 다시 열어야 해서 느려진다.
        // 1. Vertex Buffer 생성 및 데이터 전송
        m_VertexBuffer.reset(VertexBuffer::Create((void*)vertices.data(), (uint32_t)(vertices.size() * sizeof(Vertex3D))));

        // 2. 버퍼 레이아웃 설정 
        BufferLayout layout = {
            { ShaderDataType::Float3, "POSITION" },
            { ShaderDataType::Float3, "NORMAL" },
            { ShaderDataType::Float2, "TEXCOORD" },
            { ShaderDataType::Int4, "BONEIDS" },
            { ShaderDataType::Float4, "WEIGHTS" }
        };
        m_VertexBuffer->SetLayout(layout);

        // 3. Index Buffer 생성 및 데이터 전송
        m_IndexBuffer.reset(IndexBuffer::Create((uint32_t*)indices.data(), (uint32_t)indices.size()));
    }

    void Mesh::Bind() const
    {
        if (m_VertexBuffer) m_VertexBuffer->Bind();
        if (m_IndexBuffer) m_IndexBuffer->Bind();
    }

    void Mesh::Unbind() const
    {
        if (m_VertexBuffer) m_VertexBuffer->Unbind();
        if (m_IndexBuffer) m_IndexBuffer->Unbind();
    }
}
