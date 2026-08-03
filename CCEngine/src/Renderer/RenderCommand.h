#pragma once
#include "Core.h"
#include "Renderer/RendererAPI.h"
#include <memory>


namespace CCEngine
{
    class CC_API RenderCommand
    {
    public:
        static void Init();
        static void Shutdown();

        inline static void SetClearColor(float r, float g, float b, float a)
        {
            if (!s_RendererAPI) return;
            s_RendererAPI->SetClearColor(r, g, b, a);
        }

        inline static void Clear()
        {
            if (!s_RendererAPI) return;
            s_RendererAPI->Clear();
        }

        inline static void DrawIndexed(IndexBuffer* indexBuffer)
        {
            if (!s_RendererAPI) return;
            s_RendererAPI->DrawIndexed(indexBuffer);
        }

        inline static void DrawIndexed(uint32_t indexCount)
        {
            if (!s_RendererAPI) return;
            s_RendererAPI->DrawIndexed(indexCount);
        }

        inline static void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
        {
            if (!s_RendererAPI) return;
            s_RendererAPI->SetViewport(x, y, width, height);
        }

        inline static void ResizeContext(uint32_t width, uint32_t height)
        {
            if (!s_RendererAPI) return;
            s_RendererAPI->ResizeContext(width, height);
        }

        inline static void SetDepthTest(bool enable)
        {
            if (!s_RendererAPI) return;
            s_RendererAPI->SetDepthTest(enable);
        }

        inline static void SetScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
        {
            if (!s_RendererAPI) return;
            s_RendererAPI->SetScissor(x, y, width, height);
        }

        inline static void SetScissorEnable(bool enable)
        {
            if (!s_RendererAPI) return;
            s_RendererAPI->SetScissorEnable(enable);
        }

        inline static void SetBlendMode(RendererAPI::BlendMode mode)
        {
            if (!s_RendererAPI) return;
            s_RendererAPI->SetBlendMode(mode);
        }

        inline static void SetCullMode(RendererAPI::CullMode mode)
        {
            if (!s_RendererAPI) return;
            s_RendererAPI->SetCullMode(mode);
        }

        inline static void BindTexture(uint32_t slot, RendererHandle rendererID)
        {
            if (!s_RendererAPI) return;
            s_RendererAPI->BindTexture(slot, rendererID);
        }

        inline static void UnbindTextures(uint32_t startSlot, uint32_t count)
        {
            if (!s_RendererAPI) return;
            s_RendererAPI->UnbindTextures(startSlot, count);
        }

    private:
        static std::unique_ptr<RendererAPI> s_RendererAPI;
    };
}
