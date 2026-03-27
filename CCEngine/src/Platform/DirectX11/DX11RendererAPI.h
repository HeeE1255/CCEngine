#pragma once
#include "Renderer/RendererAPI.h"
#include "DX11Buffer.h"


namespace CCEngine
{
    class CC_API DX11RendererAPI : public RendererAPI
    {
    public:
        virtual void SetClearColor(float r, float g, float b, float a) override;
        virtual void Clear() override;
        virtual void DrawIndexed(IndexBuffer* indexBuffer) override;
        virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
        virtual void ResizeContext(uint32_t width, uint32_t height) override;

    private:
        float m_ClearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
    };
}