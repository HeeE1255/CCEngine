#pragma once
#if defined(CC_ENABLE_VULKAN_RHI)
#include "Renderer/Framebuffer.h"
#include "VulkanResources.h"
namespace CCEngine
{
    class VulkanFramebuffer final : public Framebuffer
    {
    public:
        explicit VulkanFramebuffer(const FramebufferSpecification& spec);
        VulkanFramebuffer(VulkanContext& context, const FramebufferSpecification& spec);
        ~VulkanFramebuffer() override;
        VulkanFramebuffer(const VulkanFramebuffer&) = delete;
        VulkanFramebuffer& operator=(const VulkanFramebuffer&) = delete;
        void Bind() override;
        void Unbind() override { Context().BindBackBuffer(); }
        void Resize(uint32_t width, uint32_t height) override;
        const FramebufferSpecification& GetSpecification() const override { return m_Specification; }
        RendererHandle GetColorAttachmentRendererID(uint32_t index = 0) const override;
        int ReadPixel(uint32_t x, uint32_t y) override;
        bool ReadColorPixels(std::vector<uint32_t>& pixels) override;
        void ClearAttachment(uint32_t index, int value) override;
    private:
        std::shared_ptr<VulkanDevice> m_Device;
        VulkanContext& Context() const { return VulkanContext::CurrentFor(m_Device); }
        FramebufferSpecification m_Specification;
        std::unique_ptr<VulkanRenderTarget> m_Target;
        std::array<RendererHandle, 2> m_Handles{};
    };
}
#endif
