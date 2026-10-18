#include "DX12Framebuffer.h"
#if defined(CC_ENABLE_DX12_RHI)
#include <stdexcept>
namespace CCEngine
{
    DX12Framebuffer::DX12Framebuffer(const FramebufferSpecification& spec) : DX12Framebuffer(DX12Context::Current(), spec) {}
    DX12Framebuffer::DX12Framebuffer(DX12Context& context, const FramebufferSpecification& spec) : m_Device(context.SharedDevice())
    {
        Resize(spec.Width, spec.Height);
    }
    DX12Framebuffer::~DX12Framebuffer()
    {
        m_Device->ReleaseTarget(m_Target);
        for (auto handle : m_Handles) if (handle) m_Device->UnregisterTexture(handle);
    }
    void DX12Framebuffer::Resize(uint32_t width, uint32_t height)
    {
        if (!width || !height) return;
        if (m_Target && width == m_Spec.Width && height == m_Spec.Height) return;
        auto next = DX12Context::CurrentFor(m_Device).CreateTarget(width, height);
        std::array<RendererHandle, 2> handles{};
        try
        {
            handles[0] = m_Device->RegisterTexture(next->Colors[0]);
            handles[1] = m_Device->RegisterTexture(next->Colors[1]);
        }
        catch (...) { if (handles[0]) m_Device->UnregisterTexture(handles[0]); throw; }
        const bool bound = m_Target && DX12Context::CurrentFor(m_Device).IsBound(m_Target);
        for (auto handle : m_Handles) if (handle) m_Device->UnregisterTexture(handle);
        m_Device->ReleaseTarget(m_Target);
        m_Target = std::move(next); m_Handles = handles; m_Spec = { width, height };
        if (bound) Bind();
    }
    void DX12Framebuffer::Bind()
    {
        if (!m_Target) throw std::logic_error("Cannot bind zero-size DX12 framebuffer");
        DX12Context::CurrentFor(m_Device).BindTarget(m_Target); DX12Context::CurrentFor(m_Device).Clear(.1f, .1f, .1f, 1); DX12Context::CurrentFor(m_Device).ClearID(m_Target, -1);
    }
    RendererHandle DX12Framebuffer::GetColorAttachmentRendererID(uint32_t index) const
    {
        if (index >= m_Handles.size()) throw std::out_of_range("DX12 color attachment index");
        return m_Handles[index];
    }
    int DX12Framebuffer::ReadPixel(uint32_t x, uint32_t y)
    {
        if (!m_Target || x >= m_Spec.Width || y >= m_Spec.Height) return -1;
        int32_t id = -1; DX12Context::CurrentFor(m_Device).Read(m_Target->Colors[1], x, y, 1, 1, &id); return id;
    }
    bool DX12Framebuffer::ReadColorPixels(std::vector<uint32_t>& pixels)
    {
        pixels.clear(); if (!m_Target) return false;
        pixels.resize(size_t(m_Spec.Width) * m_Spec.Height);
        DX12Context::CurrentFor(m_Device).Read(m_Target->Colors[0], 0, 0, m_Spec.Width, m_Spec.Height, pixels.data()); return true;
    }
    void DX12Framebuffer::ClearAttachment(uint32_t index, int value)
    {
        if (index != 1) throw std::invalid_argument("Integer clear requires DX12 ID attachment");
        DX12Context::CurrentFor(m_Device).ClearID(m_Target, value);
    }
}
#endif
