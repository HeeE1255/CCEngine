#pragma once
#include "Core.h"
#include "Renderer/Buffer.h"

namespace CCEngine
{
    class CC_API RendererAPI
    {
    public:
        // 우리가 지원할 그래픽스 API 종류
        enum class API
        {
            None = 0, DirectX11 = 1, Vulkan = 2, OpenGL = 3
        };

        virtual ~RendererAPI() = default;

        // 렌더러가 무조건 구현해야 하는 필수 기능들 (순수 가상 함수)
        virtual void SetClearColor(float r, float g, float b, float a) = 0;
        virtual void Clear() = 0;
        virtual void DrawIndexed(IndexBuffer* indexBuffer) = 0;
        virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
        virtual void ResizeContext(uint32_t width, uint32_t height) = 0;

        inline static API GetAPI() { return s_API; }

    private:
        static API s_API;
    };
}