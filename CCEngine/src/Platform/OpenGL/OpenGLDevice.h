#pragma once
#if defined(CC_ENABLE_OPENGL_RHI)
#include "Renderer/GraphicsDevice.h"
#include "OpenGLContext.h"

namespace CCEngine
{
    class OpenGLDevice final : public GraphicsDevice
    {
    public:
        OpenGLDevice();
        ~OpenGLDevice() override;
        RendererAPI::API GetBackend() const override { return RendererAPI::API::OpenGL; }
        HGLRC ShareContext() const { return m_RC; }
        int PixelFormat() const { return m_Format; }
        void CheckThread() const;
        void DeleteBuffer(GLuint name) noexcept;
        void DeleteTexture(GLuint name) noexcept;
        void DeleteProgram(GLuint name) noexcept;
    private:
        // 화면 창이 모두 닫혀도 공유 리소스가 남아 있으면 WGL 공유 그룹을 유지한다.
        HWND m_Window = nullptr;
        HDC m_DC = nullptr;
        HGLRC m_RC = nullptr;
        DWORD m_Thread = GetCurrentThreadId();
        int m_Format = 0;
        OpenGLFunctions m_GL;
        void Release() noexcept;
        void Delete(GLuint name, int kind) noexcept;
    };
}
#endif
