#include "DX12RendererAPI.h"
#if defined(CC_ENABLE_DX12_RHI)
#include <stdexcept>
namespace CCEngine
{
    void DX12RendererAPI::DrawIndexed(IndexBuffer* buffer)
    {
        if (!buffer) throw std::invalid_argument("Null index buffer");
        DrawIndexed(buffer->GetCount());
    }
    void DX12RendererAPI::SetBlendMode(BlendMode mode)
    {
        if (mode != BlendMode::Opaque && mode != BlendMode::Transparent && mode != BlendMode::MRTPicking)
            throw std::invalid_argument("Invalid DX12 blend mode");
        m_Blend = mode;
    }
    void DX12RendererAPI::SetCullMode(CullMode mode)
    {
        if (mode != CullMode::Back && mode != CullMode::None) throw std::invalid_argument("Invalid DX12 cull mode");
        m_Cull = mode;
    }
    void DX12RendererAPI::UnbindTextures(uint32_t start, uint32_t count)
    {
        if (start > 32 || count > 32 - start) throw std::out_of_range("DX12 texture slots");
        for (uint32_t i = 0; i < count; ++i) m_Context.BindTexture(start + i, nullptr);
    }
}
#endif
