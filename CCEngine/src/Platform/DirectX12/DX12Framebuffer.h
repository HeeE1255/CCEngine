#pragma once
#if defined(CC_ENABLE_DX12_RHI)
#include "DX12Context.h"
#include "Renderer/Framebuffer.h"
namespace CCEngine
{
    class DX12Framebuffer final : public Framebuffer
    {
    public:
        explicit DX12Framebuffer(const FramebufferSpecification& spec);
        DX12Framebuffer(DX12Context& context, const FramebufferSpecification& spec);
        ~DX12Framebuffer() override;
        DX12Framebuffer(const DX12Framebuffer&) = delete;
        DX12Framebuffer& operator=(const DX12Framebuffer&) = delete;
        void Bind() override;
        void Unbind() override { DX12Context::CurrentFor(m_Device).BindBackBuffer(); }
        void Resize(uint32_t width, uint32_t height) override;
        const FramebufferSpecification& GetSpecification() const override { return m_Spec; }
        RendererHandle GetColorAttachmentRendererID(uint32_t index = 0) const override;
        int ReadPixel(uint32_t x, uint32_t y) override;
        bool ReadColorPixels(std::vector<uint32_t>& pixels) override;
        void ClearAttachment(uint32_t index, int value) override;
    private:
        std::shared_ptr<DX12Device> m_Device;
        FramebufferSpecification m_Spec{};
        std::shared_ptr<DX12Target> m_Target;
        std::array<RendererHandle, 2> m_Handles{};
    };
}
#endif
