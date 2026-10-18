#include "OpenGLRendererAPI.h"
#if defined(CC_ENABLE_OPENGL_RHI)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

namespace CCEngine
{
    namespace
    {
        template<typename T> T Load(OpenGLRendererAPI::ProcAddress loader, const char* name)
        {
            void* address = loader ? loader(name) : nullptr;
            const auto value = reinterpret_cast<uintptr_t>(address);
            // wglGetProcAddress는 실패 시 nullptr 외에 1, 2, 3, -1도 반환할 수 있다.
            if (!address || value <= 3 || value == std::numeric_limits<uintptr_t>::max())
                throw std::runtime_error(std::string("OpenGL procedure unavailable: ") + name);
            return reinterpret_cast<T>(address);
        }
        GLint Dimension(uint32_t value)
        {
            if (value > static_cast<uint32_t>(std::numeric_limits<GLint>::max()))
                throw std::out_of_range("OpenGL dimension exceeds GLint range");
            return static_cast<GLint>(value);
        }
    }

    struct OpenGLRendererAPI::Functions
    {
        void (APIENTRY* GetIntegerv)(GLenum, GLint*);
        void (APIENTRY* GetBooleanv)(GLenum, GLboolean*);
        void (APIENTRY* GetBooleani_v)(GLenum, GLuint, GLboolean*);
        GLboolean (APIENTRY* IsEnabled)(GLenum);
        void (APIENTRY* Enable)(GLenum);
        void (APIENTRY* Disable)(GLenum);
        void (APIENTRY* Enablei)(GLenum, GLuint);
        void (APIENTRY* Disablei)(GLenum, GLuint);
        void (APIENTRY* DepthMask)(GLboolean);
        void (APIENTRY* DepthFunc)(GLenum);
        void (APIENTRY* ColorMaski)(GLuint, GLboolean, GLboolean, GLboolean, GLboolean);
        void (APIENTRY* ClearBufferfv)(GLenum, GLint, const GLfloat*);
        void (APIENTRY* DrawElements)(GLenum, GLsizei, GLenum, const void*);
        void (APIENTRY* Viewport)(GLint, GLint, GLsizei, GLsizei);
        void (APIENTRY* Scissor)(GLint, GLint, GLsizei, GLsizei);
        void (APIENTRY* BlendFuncSeparatei)(GLuint, GLenum, GLenum, GLenum, GLenum);
        void (APIENTRY* BlendEquationSeparatei)(GLuint, GLenum, GLenum);
        void (APIENTRY* CullFace)(GLenum);
        void (APIENTRY* FrontFace)(GLenum);
        void (APIENTRY* BindTextureUnit)(GLuint, GLuint);
        void (APIENTRY* ClipControl)(GLenum, GLenum);

        explicit Functions(ProcAddress loader)
        {
#define CC_GL_LOAD(name) name = Load<decltype(name)>(loader, "gl" #name)
            CC_GL_LOAD(GetIntegerv); CC_GL_LOAD(GetBooleanv); CC_GL_LOAD(GetBooleani_v);
            CC_GL_LOAD(IsEnabled); CC_GL_LOAD(Enable); CC_GL_LOAD(Disable);
            CC_GL_LOAD(Enablei); CC_GL_LOAD(Disablei); CC_GL_LOAD(DepthMask); CC_GL_LOAD(DepthFunc);
            CC_GL_LOAD(ColorMaski); CC_GL_LOAD(ClearBufferfv); CC_GL_LOAD(DrawElements);
            CC_GL_LOAD(Viewport); CC_GL_LOAD(Scissor); CC_GL_LOAD(BlendFuncSeparatei);
            CC_GL_LOAD(BlendEquationSeparatei); CC_GL_LOAD(CullFace); CC_GL_LOAD(FrontFace);
            CC_GL_LOAD(BindTextureUnit); CC_GL_LOAD(ClipControl);
#undef CC_GL_LOAD
        }
    };

    OpenGLRendererAPI::OpenGLRendererAPI(GraphicsContext& context, ProcAddress load)
        : m_GL(std::make_unique<Functions>(load)), m_Context(context)
    {
        GLint major = 0, minor = 0, slots = 0, targets = 0;
        m_GL->GetIntegerv(0x821B /* GL_MAJOR_VERSION */, &major);
        m_GL->GetIntegerv(0x821C /* GL_MINOR_VERSION */, &minor);
        if (major < 4 || (major == 4 && minor < 5))
            throw std::runtime_error("OpenGL RHI requires a current OpenGL 4.5 core context");
        m_GL->GetIntegerv(0x8872 /* GL_MAX_TEXTURE_IMAGE_UNITS */, &slots);
        m_GL->GetIntegerv(0x8824 /* GL_MAX_DRAW_BUFFERS */, &targets);
        if (slots <= 0 || targets <= 0)
            throw std::runtime_error("OpenGL context limits are unavailable");
        m_TextureSlots = static_cast<uint32_t>(slots);
        m_ColorTargets = static_cast<uint32_t>(targets);
        // 엔진 투영 행렬의 깊이 범위는 0~1이다. UV 반전과 깊이 범위 변환은 별개다.
        m_GL->ClipControl(0x8CA1 /* GL_LOWER_LEFT */, 0x935F /* GL_ZERO_TO_ONE */);
        SetDepthTest(true);
        SetCullMode(CullMode::Back);
        SetBlendMode(BlendMode::Opaque);
        SetScissorEnable(false);
    }
    OpenGLRendererAPI::~OpenGLRendererAPI() = default;

    void OpenGLRendererAPI::SetRenderTargetSize(uint32_t width, uint32_t height)
    {
        Dimension(width); Dimension(height);
        m_Width = width; m_Height = height;
    }
    void OpenGLRendererAPI::RequireTarget() const
    {
        if (!m_Width || !m_Height)
            throw std::logic_error("Bind a nonzero render target before issuing OpenGL commands");
    }
    void OpenGLRendererAPI::SetClearColor(float r, float g, float b, float a)
    {
        m_ClearColor[0] = r; m_ClearColor[1] = g; m_ClearColor[2] = b; m_ClearColor[3] = a;
    }
    void OpenGLRendererAPI::Clear()
    {
        RequireTarget();
        const GLboolean scissor = m_GL->IsEnabled(GL_SCISSOR_TEST);
        GLboolean depthWrite = GL_TRUE, colorWrite[4] = {};
        m_GL->GetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
        m_GL->GetBooleani_v(GL_COLOR_WRITEMASK, 0, colorWrite);
        // UI의 scissor와 쓰기 마스크가 다음 패스의 전체 초기화를 잘라내지 않게 복구한다.
        m_GL->Disable(GL_SCISSOR_TEST);
        m_GL->DepthMask(GL_TRUE);
        m_GL->ColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        const float depth = 1.0f;
        m_GL->ClearBufferfv(0x1800 /* GL_COLOR */, 0, m_ClearColor);
        m_GL->ClearBufferfv(0x1801 /* GL_DEPTH */, 0, &depth);
        m_GL->ColorMaski(0, colorWrite[0], colorWrite[1], colorWrite[2], colorWrite[3]);
        m_GL->DepthMask(depthWrite);
        if (scissor) m_GL->Enable(GL_SCISSOR_TEST);
    }
    void OpenGLRendererAPI::DrawIndexed(IndexBuffer* buffer)
    {
        if (!buffer) throw std::invalid_argument("Index buffer is null");
        DrawIndexed(buffer->GetCount());
    }
    void OpenGLRendererAPI::DrawIndexed(uint32_t count)
    {
        RequireTarget();
        if (count) m_GL->DrawElements(GL_TRIANGLES, Dimension(count), GL_UNSIGNED_INT, nullptr);
    }
    void OpenGLRendererAPI::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        RequireTarget();
        if (x > m_Width || y > m_Height || width > m_Width - x || height > m_Height - y)
            throw std::out_of_range("Viewport exceeds the current OpenGL target");
        m_GL->Viewport(Dimension(x), Dimension(m_Height - y - height), Dimension(width), Dimension(height));
    }
    void OpenGLRendererAPI::ResizeContext(uint32_t width, uint32_t height)
    {
        Dimension(width); Dimension(height);
        m_Context.ResizeBuffers(width, height);
    }
    void OpenGLRendererAPI::SetDepthTest(bool enable)
    {
        if (enable) m_GL->Enable(GL_DEPTH_TEST); else m_GL->Disable(GL_DEPTH_TEST);
        m_GL->DepthMask(enable ? GL_TRUE : GL_FALSE);
        m_GL->DepthFunc(GL_LESS);
    }
    void OpenGLRendererAPI::SetScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        RequireTarget();
        x = std::min(x, m_Width); y = std::min(y, m_Height);
        width = std::min(width, m_Width - x); height = std::min(height, m_Height - y);
        m_GL->Scissor(Dimension(x), Dimension(m_Height - y - height), Dimension(width), Dimension(height));
    }
    void OpenGLRendererAPI::SetScissorEnable(bool enable)
    {
        if (enable) m_GL->Enable(GL_SCISSOR_TEST); else m_GL->Disable(GL_SCISSOR_TEST);
    }
    void OpenGLRendererAPI::SetBlendMode(BlendMode mode)
    {
        if (mode != BlendMode::Opaque && mode != BlendMode::Transparent && mode != BlendMode::MRTPicking)
            throw std::invalid_argument("Unknown blend mode");
        // 정수 Entity ID attachment에는 블렌딩을 적용하지 않는다.
        for (GLuint i = 0; i < m_ColorTargets; ++i)
        {
            m_GL->Disablei(GL_BLEND, i);
            m_GL->ColorMaski(i, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        }
        if (mode != BlendMode::Opaque)
        {
            m_GL->Enablei(GL_BLEND, 0);
            m_GL->BlendEquationSeparatei(0, 0x8006 /* GL_FUNC_ADD */, 0x8006);
            m_GL->BlendFuncSeparatei(0, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
        }
    }
    void OpenGLRendererAPI::SetCullMode(CullMode mode)
    {
        if (mode != CullMode::None && mode != CullMode::Back)
            throw std::invalid_argument("Unknown cull mode");
        if (mode == CullMode::Back) m_GL->Enable(GL_CULL_FACE); else m_GL->Disable(GL_CULL_FACE);
        m_GL->CullFace(GL_BACK);
        // DX11의 좌상단 원점에서 시계 방향인 면은 GL의 좌하단 원점에서는 반시계 방향이다.
        m_GL->FrontFace(GL_CCW);
    }
    void OpenGLRendererAPI::BindTexture(uint32_t slot, RendererHandle handle)
    {
        if (slot >= m_TextureSlots) throw std::out_of_range("OpenGL texture slot exceeds device limit");
        const uintptr_t name = reinterpret_cast<uintptr_t>(handle);
        if (name > std::numeric_limits<GLuint>::max())
            throw std::invalid_argument("RendererHandle is not an OpenGL texture name");
        m_GL->BindTextureUnit(slot, static_cast<GLuint>(name));
    }
    void OpenGLRendererAPI::UnbindTextures(uint32_t startSlot, uint32_t count)
    {
        if (startSlot > m_TextureSlots || count > m_TextureSlots - startSlot)
            throw std::out_of_range("OpenGL texture slot range exceeds device limit");
        for (uint32_t i = 0; i < count; ++i) m_GL->BindTextureUnit(startSlot + i, 0);
    }
}
#endif
