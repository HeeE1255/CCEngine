#include "OpenGLBuffer.h"
#if defined(CC_ENABLE_OPENGL_RHI)
#include "OpenGLDevice.h"
#include <stdexcept>
#include <limits>
#include <vector>
#include <cstring>
namespace CCEngine
{
    namespace
    {
        uint32_t IndexBytes(const uint32_t* data, uint32_t count)
        {
            if (!data || !count || count > UINT32_MAX / 4) throw std::invalid_argument("Invalid OpenGL index data");
            return count * 4;
        }
    }
    OpenGLBufferStorage::OpenGLBufferStorage(OpenGLContext& context, uint32_t size, const void* data)
        : Device(context.SharedDevice()), Size(size)
    {
        if (!size) throw std::invalid_argument("Zero sized OpenGL buffer");
        std::vector<unsigned char> zeros;
        if (!data) { zeros.resize(size, 0); data = zeros.data(); }
        auto& gl = Context().GL();
        gl.CreateBuffers(1, &Name);
        gl.NamedBufferData(Name, size, data, 0x88E8 /* GL_DYNAMIC_DRAW */);
        if (!Name || glGetError() != GL_NO_ERROR)
        {
            if (Name) gl.DeleteBuffers(1, &Name);
            Name = 0; throw std::runtime_error("OpenGL buffer allocation failed");
        }
    }
    OpenGLBufferStorage::~OpenGLBufferStorage()
    {
        Device->DeleteBuffer(Name);
    }
    void OpenGLBufferStorage::Update(const void* data, uint32_t size)
    {
        if ((!data && size) || size > Size) throw std::out_of_range("OpenGL buffer update exceeds capacity");
        if (size) Context().GL().NamedBufferSubData(Name, 0, size, data);
    }
    void OpenGLVertexBuffer::Bind() const
    {
        m_Buffer.Context().SetLayout(m_Layout);
        m_Buffer.Context().GL().BindVertexBuffer(0, m_Buffer.Name, 0, m_Layout.GetStride());
    }
    void OpenGLVertexBuffer::Unbind() const { m_Buffer.Context().GL().BindVertexBuffer(0, 0, 0, 0); }
    OpenGLIndexBuffer::OpenGLIndexBuffer(OpenGLContext& context, const uint32_t* indices, uint32_t count)
        : m_Buffer(context, IndexBytes(indices, count), indices), m_Count(count) {}
    void OpenGLIndexBuffer::Bind() const { m_Buffer.Context().GL().BindBuffer(0x8893, m_Buffer.Name); }
    void OpenGLIndexBuffer::Unbind() const { m_Buffer.Context().GL().BindBuffer(0x8893, 0); }
    OpenGLConstantBuffer::OpenGLConstantBuffer(OpenGLContext& context, uint32_t size)
        : m_Data(std::make_shared<OpenGLUniformData>(context, size)) {}
    void OpenGLConstantBuffer::SetData(const void* data, uint32_t size)
    {
        if ((!data && size) || size > m_Data->Bytes.size())
            throw std::out_of_range("OpenGL uniform update exceeds capacity");
        if (!size) return;
        m_Data->Original.Update(data, size);
        std::memcpy(m_Data->Bytes.data(), data, size);
        ++m_Data->Revision;
    }
    void OpenGLConstantBuffer::Bind(uint32_t slot) const
    {
        auto& context = m_Data->Original.Context();
        auto& gl = context.GL();
        GLint slots = 0, maxSize = 0;
        glGetIntegerv(0x8A2F, &slots); glGetIntegerv(0x8A30, &maxSize);
        if (slot >= 8 || slot >= static_cast<uint32_t>(slots) || m_Data->Original.Size > static_cast<uint32_t>(maxSize))
            throw std::out_of_range("OpenGL uniform buffer exceeds device limits");
        context.BindUniformData(slot, m_Data);
        gl.BindBufferBase(0x8A11, slot, m_Data->Original.Name);
    }
}
#endif
