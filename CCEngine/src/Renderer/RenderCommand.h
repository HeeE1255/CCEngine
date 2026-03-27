#pragma once
#include "Core.h"
#include "Renderer/RendererAPI.h"
#include <memory>


namespace CCEngine
{
    class CC_API RenderCommand
    {
    public:
        inline static void SetClearColor(float r, float g, float b, float a)
        {
            s_RendererAPI->SetClearColor(r, g, b, a);
        }

        inline static void Clear()
        {
            s_RendererAPI->Clear();
        }

        inline static void DrawIndexed(IndexBuffer* indexBuffer)
        {
            s_RendererAPI->DrawIndexed(indexBuffer);
        }

        inline static void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
        {
            s_RendererAPI->SetViewport(x, y, width, height);
        }

        inline static void ResizeContext(uint32_t width, uint32_t height)
        {
            s_RendererAPI->ResizeContext(width, height);
        }

    private:
        static std::unique_ptr<RendererAPI> s_RendererAPI;
    };
}