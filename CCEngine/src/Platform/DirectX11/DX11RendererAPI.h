#pragma once
#include "Renderer/RendererAPI.h"
#include "DX11Buffer.h"
#include <d3d11.h>


namespace CCEngine
{
    class CC_API DX11RendererAPI : public RendererAPI
    {
    public:
        DX11RendererAPI();
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
        
    private:
        float m_ClearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };

        ID3D11BlendState* m_OpaqueBlendState = nullptr;
        ID3D11BlendState* m_TransparentBlendState = nullptr;
        ID3D11BlendState* m_MRTPickingBlendState = nullptr;
        ID3D11RasterizerState* m_CullBackState = nullptr;
        ID3D11RasterizerState* m_CullNoneState = nullptr;
    };
}
