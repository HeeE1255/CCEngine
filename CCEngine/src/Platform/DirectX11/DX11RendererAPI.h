#pragma once
#include "Renderer/RendererAPI.h"
#include "DX11Buffer.h"
#include <d3d11.h>
#include <wrl/client.h>


namespace CCEngine
{
    class DX11Context;
    class CC_API DX11RendererAPI : public RendererAPI
    {
    public:
        explicit DX11RendererAPI(DX11Context& context);
        virtual ~DX11RendererAPI();

        virtual void SetClearColor(float r, float g, float b, float a) override;
        virtual void Clear() override;
        virtual void DrawIndexed(IndexBuffer* indexBuffer) override;
        virtual void DrawIndexed(uint32_t indexCount) override;
        virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
        virtual void ResizeContext(uint32_t width, uint32_t height) override;
        virtual void SetDepthTest(bool enable) override;
        virtual bool IsYAxisFlipped() const override { return false; }

        virtual void SetScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
        virtual void SetScissorEnable(bool enable) override;
        virtual void SetBlendMode(BlendMode mode) override;
        virtual void SetCullMode(CullMode mode) override;
        virtual void BindTexture(uint32_t slot, RendererHandle rendererID) override;
        virtual void UnbindTextures(uint32_t startSlot, uint32_t count) override;
        
    private:
        void ApplyRasterizerState();
        DX11Context& m_Context;

        float m_ClearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };

        Microsoft::WRL::ComPtr<ID3D11BlendState> m_OpaqueBlendState;
        Microsoft::WRL::ComPtr<ID3D11BlendState> m_TransparentBlendState;
        Microsoft::WRL::ComPtr<ID3D11BlendState> m_MRTPickingBlendState;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_RasterizerStates[2][2];
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_DepthEnabled;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_DepthDisabled;
        CullMode m_CullMode = CullMode::Back;
        bool m_ScissorEnabled = false;
    };
}
