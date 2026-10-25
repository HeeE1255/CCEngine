#include "OpenGLDevice.h"
#if defined(CC_ENABLE_OPENGL_RHI)
#include <stdexcept>
namespace CCEngine
{
    OpenGLDevice::OpenGLDevice()
    {
        const HDC previousDC = wglGetCurrentDC();
        const HGLRC previousRC = wglGetCurrentContext();
        try
        {
            const HINSTANCE instance = GetModuleHandleW(nullptr);
            WNDCLASSW wc{};
            wc.style = CS_OWNDC;
            wc.lpfnWndProc = DefWindowProcW;
            wc.hInstance = instance;
            wc.lpszClassName = L"CCEngine.OpenGL.ResourceOwner";
            if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                throw std::runtime_error("OpenGL owner window registration failed");
            m_Window = CreateWindowW(wc.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1,
                nullptr, nullptr, instance, nullptr);
            if (!m_Window || !(m_DC = GetDC(m_Window)))
                throw std::runtime_error("OpenGL owner window creation failed");
            PIXELFORMATDESCRIPTOR format{};
            format.nSize = sizeof(format); format.nVersion = 1;
            format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
            format.iPixelType = PFD_TYPE_RGBA; format.cColorBits = 32;
            format.cDepthBits = 24; format.cStencilBits = 8;
            m_Format = ChoosePixelFormat(m_DC, &format);
            if (!m_Format || !SetPixelFormat(m_DC, m_Format, &format))
                throw std::runtime_error("OpenGL owner pixel format failed");
            m_RC = wglCreateContext(m_DC);
            if (!m_RC || !wglMakeCurrent(m_DC, m_RC))
                throw std::runtime_error("OpenGL owner bootstrap failed");
            using CreateContext = HGLRC (WINAPI*)(HDC, HGLRC, const int*);
            auto create = reinterpret_cast<CreateContext>(OpenGLContext::Resolve("wglCreateContextAttribsARB"));
            const int attributes[] = { 0x2091, 4, 0x2092, 5, 0x9126, 1, 0 };
            HGLRC core = create(m_DC, nullptr, attributes);
            if (!core) throw std::runtime_error("OpenGL owner core context failed");
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(m_RC); m_RC = core;
            if (!wglMakeCurrent(m_DC, m_RC)) throw std::runtime_error("OpenGL owner activation failed");
            m_GL.Load();
            if (!wglMakeCurrent(previousDC, previousRC))
                throw std::runtime_error("OpenGL previous context restore failed");
        }
        catch (...)
        {
            Release();
            wglMakeCurrent(previousDC, previousRC);
            throw;
        }
    }
    void OpenGLDevice::CheckThread() const
    {
        if (GetCurrentThreadId() != m_Thread)
            throw std::logic_error("OpenGL device requires its owning render thread");
    }
    OpenGLDevice::~OpenGLDevice() { Release(); }
    void OpenGLDevice::Release() noexcept
    {
        if (wglGetCurrentContext() == m_RC) wglMakeCurrent(nullptr, nullptr);
        if (m_RC) wglDeleteContext(m_RC);
        if (m_DC) ReleaseDC(m_Window, m_DC);
        if (m_Window) DestroyWindow(m_Window);
        m_RC = nullptr; m_DC = nullptr; m_Window = nullptr;
    }
    void OpenGLDevice::Delete(GLuint name, int kind) noexcept
    {
        if (!name) return;
        if (GetCurrentThreadId() != m_Thread)
        {
            OutputDebugStringA("OpenGL resource destroyed outside its render thread\n");
            return;
        }
        const HDC dc = wglGetCurrentDC();
        const HGLRC rc = wglGetCurrentContext();
        // 공유 객체 삭제도 살아 있는 컨텍스트가 필요하다. 소멸자는 창을 역참조하지 않는다.
        if (rc) glFinish();
        if (!wglMakeCurrent(m_DC, m_RC))
        {
            OutputDebugStringA("OpenGL resource cleanup activation failed\n");
            return;
        }
        if (kind == 0) m_GL.DeleteBuffers(1, &name);
        else if (kind == 1) glDeleteTextures(1, &name);
        else m_GL.DeleteProgram(name);
        glFinish();
        if (!wglMakeCurrent(dc, rc)) OutputDebugStringA("OpenGL cleanup context restore failed\n");
    }
    void OpenGLDevice::DeleteBuffer(GLuint name) noexcept { Delete(name, 0); }
    void OpenGLDevice::DeleteTexture(GLuint name) noexcept { Delete(name, 1); }
    void OpenGLDevice::DeleteProgram(GLuint name) noexcept { Delete(name, 2); }
}
#endif
