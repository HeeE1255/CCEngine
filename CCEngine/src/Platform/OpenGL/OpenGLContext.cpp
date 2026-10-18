#include "OpenGLContext.h"
#if defined(CC_ENABLE_OPENGL_RHI)
#include <limits>
#include <climits>
#include <stdexcept>
#include <string>

namespace CCEngine
{
    thread_local OpenGLContext* OpenGLContext::s_Current = nullptr;
    void* OpenGLContext::Resolve(const char* name)
    {
        auto proc = wglGetProcAddress(name);
        const auto value = reinterpret_cast<uintptr_t>(proc);
        if (!proc || value <= 3 || value == std::numeric_limits<uintptr_t>::max())
            proc = reinterpret_cast<PROC>(GetProcAddress(GetModuleHandleW(L"opengl32.dll"), name));
        if (!proc) throw std::runtime_error(std::string("Missing OpenGL entry point: ") + name);
        return reinterpret_cast<void*>(proc);
    }
    void OpenGLFunctions::Load()
    {
#define CC_LOAD_GL(result, name, ...) name = reinterpret_cast<decltype(name)>(OpenGLContext::Resolve("gl" #name));
        CC_GL_RESOURCE_FUNCTIONS(CC_LOAD_GL)
#undef CC_LOAD_GL
    }
    OpenGLContext::OpenGLContext(void* hwnd, OpenGLContext* share)
        : m_Window(static_cast<HWND>(hwnd)), m_Share(share) {}
    OpenGLContext::~OpenGLContext() { Release(); }
    void OpenGLContext::Release() noexcept
    {
        if (m_RC)
        {
            wglMakeCurrent(m_DC, m_RC);
            m_API.reset();
            if (m_VAO && m_GL.DeleteVertexArrays) m_GL.DeleteVertexArrays(1, &m_VAO);
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(m_RC);
        }
        if (m_DC) ReleaseDC(m_Window, m_DC);
        m_RC = nullptr; m_DC = nullptr; m_VAO = 0;
        if (s_Current == this) s_Current = nullptr;
    }
    void OpenGLContext::Init()
    {
        if (m_RC) throw std::logic_error("OpenGL context is already initialized");
        if (!IsWindow(m_Window)) throw std::invalid_argument("Invalid OpenGL window");
        try
        {
            m_DC = GetDC(m_Window);
            if (!m_DC) throw std::runtime_error("GetDC failed");
            PIXELFORMATDESCRIPTOR format{};
            format.nSize = sizeof(format); format.nVersion = 1;
            format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
            format.iPixelType = PFD_TYPE_RGBA; format.cColorBits = 32;
            format.cDepthBits = 24; format.cStencilBits = 8;
            int index = GetPixelFormat(m_DC);
            if (!index)
            {
                index = ChoosePixelFormat(m_DC, &format);
                if (!index || !SetPixelFormat(m_DC, index, &format))
                    throw std::runtime_error("OpenGL pixel format setup failed");
            }
            else
            {
                DescribePixelFormat(m_DC, index, sizeof(format), &format);
                constexpr DWORD required = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
                if ((format.dwFlags & required) != required || format.cDepthBits < 24 || format.cStencilBits < 8)
                    throw std::runtime_error("Existing window pixel format is incompatible with OpenGL");
            }
            // WGL 확장 함수는 임시 컨텍스트를 current로 만든 뒤에만 조회할 수 있다.
            m_RC = wglCreateContext(m_DC);
            if (!m_RC || !wglMakeCurrent(m_DC, m_RC)) throw std::runtime_error("WGL bootstrap failed");
            using CreateContext = HGLRC (WINAPI*)(HDC, HGLRC, const int*);
            auto create = reinterpret_cast<CreateContext>(Resolve("wglCreateContextAttribsARB"));
            if (m_Share && !m_Share->m_RC) throw std::logic_error("Shared OpenGL context is not initialized");
            const int attributes[] = { 0x2091, 4, 0x2092, 5, 0x9126, 1, 0 };
            HGLRC core = create(m_DC, m_Share ? m_Share->m_RC : nullptr, attributes);
            if (!core) throw std::runtime_error("OpenGL 4.5 core context creation failed");
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(m_RC); m_RC = core;
            MakeCurrent();
            m_GL.Load();
            m_GL.CreateVertexArrays(1, &m_VAO);
            if (!m_VAO || glGetError() != GL_NO_ERROR) throw std::runtime_error("OpenGL vertex array allocation failed");
            m_GL.BindVertexArray(m_VAO);
            try { m_SwapInterval = reinterpret_cast<decltype(m_SwapInterval)>(Resolve("wglSwapIntervalEXT")); }
            catch (const std::runtime_error&) { m_SwapInterval = nullptr; }
            m_API = std::make_unique<OpenGLRendererAPI>(*this, Resolve);
            RECT rect{}; GetClientRect(m_Window, &rect);
            ResizeBuffers(static_cast<uint32_t>(rect.right), static_cast<uint32_t>(rect.bottom));
            if (m_Width && m_Height) BindBackBuffer();
        }
        catch (...) { Release(); throw; }
    }
    void OpenGLContext::MakeCurrent()
    {
        if (!m_RC) throw std::logic_error("OpenGL context is not initialized");
        if (wglGetCurrentContext() != m_RC && !wglMakeCurrent(m_DC, m_RC))
            throw std::runtime_error("wglMakeCurrent failed");
        s_Current = this;
        SetCurrentContext();
    }
    OpenGLContext& OpenGLContext::Current()
    {
        if (!s_Current) throw std::logic_error("No current OpenGLContext");
        s_Current->MakeCurrent(); return *s_Current;
    }
    void OpenGLContext::SwapBuffers(bool vsync)
    {
        MakeCurrent();
        if (!m_Width || !m_Height) return;
        if (m_SwapInterval) m_SwapInterval(vsync ? 1 : 0);
        if (!::SwapBuffers(m_DC)) throw std::runtime_error("OpenGL SwapBuffers failed");
    }
    void OpenGLContext::Clear(float r, float g, float b, float a)
    {
        MakeCurrent(); m_API->SetClearColor(r, g, b, a); m_API->Clear();
    }
    void OpenGLContext::ResizeBuffers(uint32_t width, uint32_t height)
    {
        if (width > INT_MAX || height > INT_MAX) throw std::out_of_range("OpenGL size overflow");
        m_Width = width; m_Height = height;
    }
    void OpenGLContext::BindBackBuffer()
    {
        MakeCurrent(); m_GL.BindFramebuffer(0x8D40, 0);
        m_API->SetRenderTargetSize(m_Width, m_Height);
        if (m_Width && m_Height) m_API->SetViewport(0, 0, m_Width, m_Height);
    }
    void OpenGLContext::SetLayout(const BufferLayout& layout)
    {
        MakeCurrent();
        GLint limit = 0; glGetIntegerv(0x8869, &limit);
        GLint maxStride = 0, maxOffset = 0;
        glGetIntegerv(0x82E5 /* GL_MAX_VERTEX_ATTRIB_STRIDE */, &maxStride);
        glGetIntegerv(0x82D9 /* GL_MAX_VERTEX_ATTRIB_RELATIVE_OFFSET */, &maxOffset);
        uint32_t required = 0;
        for (const auto& e : layout)
        {
            if (e.Type == ShaderDataType::None || e.Type == ShaderDataType::Bool)
                throw std::invalid_argument("Unsupported OpenGL vertex attribute type");
            required += e.Type == ShaderDataType::Mat4 ? 4 : e.Type == ShaderDataType::Mat3 ? 3 : 1;
            const uint32_t columns = e.Type == ShaderDataType::Mat4 ? 4 : e.Type == ShaderDataType::Mat3 ? 3 : 1;
            const uint32_t components = columns > 1 ? columns : e.Size / 4;
            if (!components || components > 4 || e.Offset > layout.GetStride() ||
                components * columns * 4 > layout.GetStride() - e.Offset ||
                e.Offset + (columns - 1) * components * 4 > static_cast<uint32_t>(maxOffset))
                throw std::invalid_argument("OpenGL attribute exceeds layout or device limits");
        }
        if (!layout.GetStride() || layout.GetStride() > static_cast<uint32_t>(maxStride) || required > static_cast<uint32_t>(limit))
            throw std::invalid_argument("Invalid OpenGL vertex layout");
        for (uint32_t i = 0; i < m_Attributes; ++i) m_GL.DisableVertexAttribArray(i);
        uint32_t location = 0;
        for (const auto& e : layout)
        {
            const bool integer = e.Type >= ShaderDataType::Int && e.Type <= ShaderDataType::Int4;
            const uint32_t columns = e.Type == ShaderDataType::Mat4 ? 4 : e.Type == ShaderDataType::Mat3 ? 3 : 1;
            const uint32_t components = columns > 1 ? columns : e.Size / 4;
            for (uint32_t column = 0; column < columns; ++column, ++location)
            {
                const uint32_t offset = e.Offset + column * components * 4;
                m_GL.EnableVertexAttribArray(location);
                m_GL.VertexAttribBinding(location, 0);
                if (integer && !e.Normalized) m_GL.VertexAttribIFormat(location, components, GL_INT, offset);
                else m_GL.VertexAttribFormat(location, components, integer ? GL_INT : GL_FLOAT, e.Normalized, offset);
            }
        }
        m_Attributes = required;
    }
}
#endif
