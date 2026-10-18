#pragma once
#if defined(CC_ENABLE_OPENGL_RHI)
#include "OpenGLContext.h"
namespace CCEngine
{
    class OpenGLBufferStorage
    {
    public:
        OpenGLBufferStorage(OpenGLContext& context, uint32_t size, const void* data);
        ~OpenGLBufferStorage();
        OpenGLBufferStorage(const OpenGLBufferStorage&) = delete;
        OpenGLBufferStorage& operator=(const OpenGLBufferStorage&) = delete;
        void Update(const void* data, uint32_t size);
        OpenGLContext& Context;
        GLuint Name = 0;
        uint32_t Size;
    };
    class OpenGLVertexBuffer final : public VertexBuffer
    {
    public:
        OpenGLVertexBuffer(OpenGLContext& context, uint32_t size, const void* data = nullptr) : m_Buffer(context, size, data) {}
        void Bind() const override;
        void Unbind() const override;
        const BufferLayout& GetLayout() const override { return m_Layout; }
        void SetLayout(const BufferLayout& layout) override { m_Layout = layout; }
        void SetData(const void* data, uint32_t size) override { m_Buffer.Update(data, size); }
    private:
        OpenGLBufferStorage m_Buffer;
        BufferLayout m_Layout;
    };
    class OpenGLIndexBuffer final : public IndexBuffer
    {
    public:
        OpenGLIndexBuffer(OpenGLContext& context, const uint32_t* indices, uint32_t count);
        void Bind() const override;
        void Unbind() const override;
        uint32_t GetCount() const override { return m_Count; }
    private:
        OpenGLBufferStorage m_Buffer;
        uint32_t m_Count;
    };
    class OpenGLConstantBuffer final : public ConstantBuffer
    {
    public:
        OpenGLConstantBuffer(OpenGLContext& context, uint32_t size) : m_Buffer(context, size, nullptr) {}
        void Bind(uint32_t slot = 0) const override;
        void SetData(const void* data, uint32_t size) override { m_Buffer.Update(data, size); }
    private:
        OpenGLBufferStorage m_Buffer;
    };
}
#endif
