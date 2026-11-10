#pragma once
#if defined(CC_ENABLE_DX12_RHI)
#include "DX12Context.h"
namespace CCEngine
{
    class DX12RendererAPI final : public RendererAPI
    {
    public:
        uint32_t GetMaxSamplerAnisotropy() const override { return 16; }
        explicit DX12RendererAPI(DX12Context& context) : m_Context(context) {}
        void SetClearColor(float r, float g, float b, float a) override { m_Color = { r, g, b, a }; }
        void Clear() override { m_Context.Clear(m_Color[0], m_Color[1], m_Color[2], m_Color[3]); }
        void DrawIndexed(IndexBuffer* buffer) override;
        void DrawIndexed(uint32_t count) override { m_Context.Draw(count, m_Blend, m_Cull, m_Depth, m_DepthWrite); }
        void SetViewport(uint32_t x, uint32_t y, uint32_t w, uint32_t h) override { m_Context.Viewport(x, y, w, h); }
        void ResizeContext(uint32_t w, uint32_t h) override { m_Context.ResizeBuffers(w, h); }
        void SetDepthTest(bool enabled) override { m_Depth = enabled; }
        void SetDepthWrite(bool enabled) override { m_DepthWrite = enabled; }
        bool IsYAxisFlipped() const override { return false; }
        void SetScissor(uint32_t x, uint32_t y, uint32_t w, uint32_t h) override { m_Context.Scissor(x, y, w, h); }
        void SetScissorEnable(bool enabled) override { m_Context.ScissorEnabled(enabled); }
        void SetBlendMode(BlendMode mode) override;
        void SetCullMode(CullMode mode) override;
        void BindTexture(uint32_t slot, RendererHandle handle) override { m_Context.BindTexture(slot, handle); }
        void UnbindTextures(uint32_t start, uint32_t count) override;
        void SetCommonSampler(const SamplerDescription* description) override { m_Context.SetCommonSampler(description); }
        void SetMaterialSampler(uint32_t slot, const SamplerDescription* description) override { m_Context.SetMaterialSampler(slot, description); }
    private:
        DX12Context& m_Context;
        std::array<float, 4> m_Color{ 0, 0, 0, 1 };
        BlendMode m_Blend = BlendMode::Opaque;
        CullMode m_Cull = CullMode::Back;
        bool m_Depth = true;
        bool m_DepthWrite = true;
    };
}
#endif
