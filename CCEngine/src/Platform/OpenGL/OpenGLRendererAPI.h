#pragma once

// 기본 빌드에서는 SDK 의존성도 생기지 않는다. 팩토리 연결 단계에서만 활성화한다.
#if defined(CC_ENABLE_OPENGL_RHI)
#include "Renderer/RendererAPI.h"
#include "Renderer/GraphicsContext.h"
#include <memory>

namespace CCEngine
{
    // GL 4.5 core 컨텍스트가 현재 스레드에 바인딩된 뒤 생성해야 한다.
    class OpenGLRendererAPI final : public RendererAPI
    {
    public:
        // 로더는 wglGetProcAddress 실패 시 opengl32.dll의 GetProcAddress도 조회해야 한다.
        using ProcAddress = void* (*)(const char*);
        OpenGLRendererAPI(GraphicsContext& context, ProcAddress load);
        ~OpenGLRendererAPI() override;
        OpenGLRendererAPI(const OpenGLRendererAPI&) = delete;
        OpenGLRendererAPI& operator=(const OpenGLRendererAPI&) = delete;

        // BackBuffer/FBO를 바인딩할 때 호출한다. scissor의 Y 변환에는 viewport가 아닌 대상 높이가 필요하다.
        void SetRenderTargetSize(uint32_t width, uint32_t height);
        void SetClearColor(float r, float g, float b, float a) override;
        void Clear() override;
        void DrawIndexed(IndexBuffer* buffer) override;
        void DrawIndexed(uint32_t count) override;
        void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
        void ResizeContext(uint32_t width, uint32_t height) override;
        void SetDepthTest(bool enable) override;
        bool IsYAxisFlipped() const override { return true; }
        void SetScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
        void SetScissorEnable(bool enable) override;
        void SetBlendMode(BlendMode mode) override;
        void SetCullMode(CullMode mode) override;
        void BindTexture(uint32_t slot, RendererHandle handle) override;
        void UnbindTextures(uint32_t startSlot, uint32_t count) override;

    private:
        struct Functions;
        std::unique_ptr<Functions> m_GL;
        GraphicsContext& m_Context;
        uint32_t m_Width = 0, m_Height = 0;
        uint32_t m_TextureSlots = 0, m_ColorTargets = 0;
        float m_ClearColor[4] = { 0, 0, 0, 1 };
        void RequireTarget() const;
    };
}
#endif
