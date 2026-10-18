#pragma once
#include "Core.h"
#include "Renderer/RendererAPI.h"
#include <memory>


namespace CCEngine
{
    class GraphicsDevice;
    class CC_API RenderCommand
    {
    public:
        struct PassState
        {
            RendererAPI::BlendMode Blend = RendererAPI::BlendMode::Opaque;
            RendererAPI::CullMode Cull = RendererAPI::CullMode::Back;
            bool DepthTest = true;
            bool Scissor = false;
        };

        static void Init();
        static void Shutdown();

        inline static void SetClearColor(float r, float g, float b, float a)
        {
            auto* api = CurrentAPI();
            if (!api) return;
            api->SetClearColor(r, g, b, a);
        }

        inline static void Clear()
        {
            auto* api = CurrentAPI();
            if (!api) return;
            api->Clear();
        }

        inline static void DrawIndexed(IndexBuffer* indexBuffer)
        {
            auto* api = CurrentAPI();
            if (!api) return;
            api->DrawIndexed(indexBuffer);
        }

        inline static void DrawIndexed(uint32_t indexCount)
        {
            auto* api = CurrentAPI();
            if (!api) return;
            api->DrawIndexed(indexCount);
        }

        inline static void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
        {
            auto* api = CurrentAPI();
            if (!api) return;
            api->SetViewport(x, y, width, height);
        }

        inline static void ResizeContext(uint32_t width, uint32_t height)
        {
            auto* api = CurrentAPI();
            if (!api) return;
            api->ResizeContext(width, height);
        }

        inline static void SetDepthTest(bool enable)
        {
            auto* api = CurrentAPI();
            if (!api) return;
            api->SetDepthTest(enable);
        }

        inline static void ApplyPassState(const PassState& state)
        {
            auto* api = CurrentAPI();
            if (!api) return;
            // 렌더 패스 시작에서 네 상태를 한 번에 지정한다. 일부만 설정하면 이전 패스의
            // GPU 상태가 남아 Editor와 Player의 그리기 순서 차이가 화면 차이로 번진다.
            api->SetBlendMode(state.Blend);
            api->SetCullMode(state.Cull);
            api->SetDepthTest(state.DepthTest);
            api->SetScissorEnable(state.Scissor);
        }

        inline static void SetScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
        {
            auto* api = CurrentAPI();
            if (!api) return;
            api->SetScissor(x, y, width, height);
        }

        inline static void SetScissorEnable(bool enable)
        {
            auto* api = CurrentAPI();
            if (!api) return;
            api->SetScissorEnable(enable);
        }

        inline static void SetBlendMode(RendererAPI::BlendMode mode)
        {
            auto* api = CurrentAPI();
            if (!api) return;
            api->SetBlendMode(mode);
        }

        inline static void SetCullMode(RendererAPI::CullMode mode)
        {
            auto* api = CurrentAPI();
            if (!api) return;
            api->SetCullMode(mode);
        }

        inline static void BindTexture(uint32_t slot, RendererHandle rendererID)
        {
            auto* api = CurrentAPI();
            if (!api) return;
            api->BindTexture(slot, rendererID);
        }

        inline static void UnbindTextures(uint32_t startSlot, uint32_t count)
        {
            auto* api = CurrentAPI();
            if (!api) return;
            api->UnbindTextures(startSlot, count);
        }

    private:
        static RendererAPI* CurrentAPI();
        static std::shared_ptr<GraphicsDevice> s_Device;
        static bool s_Initialized;
    };
}
