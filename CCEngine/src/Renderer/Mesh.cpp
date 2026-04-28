#pragma once
#include "Mesh.h"

namespace CCEngine
{
    Mesh::Mesh(const std::vector<Vertex3D>& vertices, const std::vector<uint32_t>& indices)
    {
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