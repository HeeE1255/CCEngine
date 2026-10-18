#include "OpenGLFramebufferStorage.h"
#if defined(CC_ENABLE_OPENGL_RHI)
#include <algorithm>
#include <stdexcept>
#include <limits>
#include <climits>
namespace CCEngine
{
    namespace
    {
        struct PackScope
        {
            OpenGLFunctions& GL;
            GLint Buffer = 0, Alignment = 0, Row = 0, Rows = 0, Pixels = 0;
            explicit PackScope(OpenGLContext& context) : GL(context.GL())
            {
                glGetIntegerv(0x88ED, &Buffer); glGetIntegerv(GL_PACK_ALIGNMENT, &Alignment);
                glGetIntegerv(GL_PACK_ROW_LENGTH, &Row); glGetIntegerv(GL_PACK_SKIP_ROWS, &Rows); glGetIntegerv(GL_PACK_SKIP_PIXELS, &Pixels);
                GL.BindBuffer(0x88EB, 0); glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glPixelStorei(GL_PACK_ROW_LENGTH, 0); glPixelStorei(GL_PACK_SKIP_ROWS, 0); glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
            }
            ~PackScope()
            {
                GL.BindBuffer(0x88EB, Buffer); glPixelStorei(GL_PACK_ALIGNMENT, Alignment);
                glPixelStorei(GL_PACK_ROW_LENGTH, Row); glPixelStorei(GL_PACK_SKIP_ROWS, Rows); glPixelStorei(GL_PACK_SKIP_PIXELS, Pixels);
            }
        };
    }
    struct OpenGLFramebuffer::Storage
    {
        OpenGLContext& Context;
        GLuint FBO = 0, Textures[3]{};
        explicit Storage(OpenGLContext& context) : Context(context) {}
        ~Storage()
        {
            auto& gl = Context.GL();
            if (FBO) gl.DeleteFramebuffers(1, &FBO);
            glDeleteTextures(3, Textures);
        }
    };
    OpenGLFramebuffer::OpenGLFramebuffer(const FramebufferSpecification& spec)
        : OpenGLFramebuffer(OpenGLContext::Current(), spec) {}
    OpenGLFramebuffer::OpenGLFramebuffer(OpenGLContext& context, const FramebufferSpecification& spec)
        : m_Context(context) { Resize(spec.Width, spec.Height); }
    OpenGLFramebuffer::~OpenGLFramebuffer() = default;
    void OpenGLFramebuffer::Resize(uint32_t width, uint32_t height)
    {
        auto& gl = m_Context.GL(); GLint limit = 0; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
        if (!width || !height) return;
        if (width > static_cast<uint32_t>(limit) || height > static_cast<uint32_t>(limit) || uint64_t(width) * height * 4 > INT_MAX)
            throw std::out_of_range("OpenGL framebuffer exceeds size limits");
        if (m_Storage && width == m_Spec.Width && height == m_Spec.Height) return;
        auto next = std::make_unique<Storage>(m_Context);
        gl.CreateFramebuffers(1, &next->FBO);
        gl.CreateTextures(GL_TEXTURE_2D, 3, next->Textures);
        const GLenum formats[3] = { 0x8058 /* RGBA8 */, 0x8235 /* R32I */, 0x88F0 /* D24S8 */ };
        const GLenum attachments[3] = { 0x8CE0, 0x8CE1, 0x821A };
        for (uint32_t i = 0; i < 3; ++i)
        {
            gl.TextureStorage2D(next->Textures[i], 1, formats[i], width, height);
            gl.TextureParameteri(next->Textures[i], GL_TEXTURE_MIN_FILTER, i == 0 ? GL_LINEAR : GL_NEAREST);
            gl.TextureParameteri(next->Textures[i], GL_TEXTURE_MAG_FILTER, i == 0 ? GL_LINEAR : GL_NEAREST);
            gl.TextureParameteri(next->Textures[i], GL_TEXTURE_WRAP_S, 0x812F);
            gl.TextureParameteri(next->Textures[i], GL_TEXTURE_WRAP_T, 0x812F);
            gl.NamedFramebufferTexture(next->FBO, attachments[i], next->Textures[i], 0);
        }
        gl.NamedFramebufferDrawBuffers(next->FBO, 2, attachments);
        if (gl.CheckNamedFramebufferStatus(next->FBO, 0x8D40) != 0x8CD5 || glGetError() != GL_NO_ERROR)
            throw std::runtime_error("OpenGL framebuffer allocation is incomplete");
        GLint current = 0; glGetIntegerv(0x8CA6, &current);
        const bool bound = m_Storage && static_cast<GLuint>(current) == m_Storage->FBO;
        m_Storage.swap(next); m_Spec = { width, height };
        if (bound) Bind();
    }
    void OpenGLFramebuffer::Bind()
    {
        if (!m_Storage) throw std::logic_error("Cannot bind an empty OpenGL framebuffer");
        auto& gl = m_Context.GL(); GLint slots = 0; glGetIntegerv(0x8872, &slots);
        m_Context.Commands().UnbindTextures(0, slots);
        gl.BindFramebuffer(0x8D40, m_Storage->FBO);
        m_Context.Commands().SetRenderTargetSize(m_Spec.Width, m_Spec.Height);
        m_Context.Commands().SetViewport(0, 0, m_Spec.Width, m_Spec.Height);
        const float color[4] = { .1f, .1f, .1f, 1 };
        const uint32_t depthStencil = 0xffffff00u;
        gl.ClearTexImage(m_Storage->Textures[0], 0, GL_RGBA, GL_FLOAT, color);
        gl.ClearTexImage(m_Storage->Textures[2], 0, 0x84F9, 0x84FA, &depthStencil);
        ClearAttachment(1, -1);
    }
    void OpenGLFramebuffer::Unbind() { m_Context.BindBackBuffer(); }
    RendererHandle OpenGLFramebuffer::GetColorAttachmentRendererID(uint32_t index) const
    {
        return m_Storage && index < 2 ? reinterpret_cast<RendererHandle>(static_cast<uintptr_t>(m_Storage->Textures[index])) : nullptr;
    }
    int OpenGLFramebuffer::ReadPixel(uint32_t x, uint32_t y)
    {
        if (!m_Storage || x >= m_Spec.Width || y >= m_Spec.Height) return -1;
        PackScope pack(m_Context); int result = -1;
        // RHI 입력은 좌상단 픽셀 좌표다. GL readback만 좌하단으로 변환한다.
        pack.GL.GetTextureSubImage(m_Storage->Textures[1], 0, x, m_Spec.Height - 1 - y, 0, 1, 1, 1, 0x8D94, GL_INT, sizeof(result), &result);
        return result;
    }
    bool OpenGLFramebuffer::ReadColorPixels(std::vector<uint32_t>& out)
    {
        out.clear(); if (!m_Storage) return false;
        PackScope pack(m_Context); out.resize(size_t(m_Spec.Width) * m_Spec.Height);
        pack.GL.GetTextureSubImage(m_Storage->Textures[0], 0, 0, 0, 0, m_Spec.Width, m_Spec.Height, 1,
            GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(out.size() * 4), out.data());
        if (glGetError() != GL_NO_ERROR) { out.clear(); return false; }
        for (uint32_t y = 0; y < m_Spec.Height / 2; ++y)
            std::swap_ranges(out.begin() + size_t(y) * m_Spec.Width, out.begin() + size_t(y + 1) * m_Spec.Width,
                out.begin() + size_t(m_Spec.Height - 1 - y) * m_Spec.Width);
        return true;
    }
    void OpenGLFramebuffer::ClearAttachment(uint32_t index, int value)
    {
        if (!m_Storage || index != 1) throw std::invalid_argument("Integer clear requires the picking attachment");
        m_Context.GL().ClearTexImage(m_Storage->Textures[1], 0, 0x8D94, GL_INT, &value);
    }
}
#endif
