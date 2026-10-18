#include "VulkanFramebuffer.h"
#if defined(CC_ENABLE_VULKAN_RHI)
#include <stdexcept>
namespace CCEngine
{
    VulkanFramebuffer::VulkanFramebuffer(const FramebufferSpecification& spec)
        : VulkanFramebuffer(VulkanContext::Current(), spec) {}
    VulkanFramebuffer::VulkanFramebuffer(VulkanContext& context, const FramebufferSpecification& spec)
        : m_Context(context), m_Specification(spec) { Resize(spec.Width, spec.Height); }
    VulkanFramebuffer::~VulkanFramebuffer()
    {
        for (auto handle : m_Handles) if (handle) m_Context.Commands().UnregisterTexture(handle);
    }
    void VulkanFramebuffer::Resize(uint32_t width, uint32_t height)
    {
        if (!width || !height) return;
        if (m_Target && width == m_Specification.Width && height == m_Specification.Height) return;
        if (uint64_t(width) * height > SIZE_MAX / sizeof(uint32_t)) throw std::overflow_error("Framebuffer exceeds CPU readback range");
        const VkExtent2D extent{ width, height };
        const VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        auto color = std::make_shared<VulkanImageAllocation>(m_Context, extent, VK_FORMAT_R8G8B8A8_UNORM, usage, VK_IMAGE_ASPECT_COLOR_BIT);
        auto id = std::make_shared<VulkanImageAllocation>(m_Context, extent, VK_FORMAT_R32_SINT, usage, VK_IMAGE_ASPECT_COLOR_BIT);
        auto depth = std::make_shared<VulkanImageAllocation>(m_Context, extent, VK_FORMAT_D32_SFLOAT,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
        auto replacement = std::make_unique<VulkanRenderTarget>(m_Context, extent,
            std::vector<std::shared_ptr<VulkanImageAllocation>>{ color, id }, depth);
        m_Context.Execute([&](VkCommandBuffer command)
        {
            const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            VkClearColorValue clear{};
            m_Context.Transition(command, *color, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            vkCmdClearColorImage(command, color->Image, color->Layout, &clear, 1, &range);
            clear.int32[0] = -1;
            m_Context.Transition(command, *id, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            vkCmdClearColorImage(command, id->Image, id->Layout, &clear, 1, &range);
            m_Context.Transition(command, *color, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            m_Context.Transition(command, *id, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            m_Context.Transition(command, *depth, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            const VkImageSubresourceRange depthRange{ VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
            const VkClearDepthStencilValue depthClear{ 1.0f, 0 };
            vkCmdClearDepthStencilImage(command, depth->Image, depth->Layout, &depthClear, 1, &depthRange);
            m_Context.Transition(command, *depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        });
        std::array<RendererHandle, 2> handles{};
        try
        {
            handles[0] = m_Context.Commands().RegisterTexture({ m_Context.Sampler(), color->View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
            handles[1] = m_Context.Commands().RegisterTexture({ m_Context.Sampler(), id->View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
        }
        catch (...) { if (handles[0]) m_Context.Commands().UnregisterTexture(handles[0]); throw; }
        const bool rebound = m_Target && m_Context.IsTargetBound(*m_Target);
        if (m_Target) m_Context.ReleaseTarget(*m_Target);
        for (auto handle : m_Handles) if (handle) m_Context.Commands().UnregisterTexture(handle);
        m_Target = std::move(replacement); m_Handles = handles;
        m_Specification.Width = width; m_Specification.Height = height;
        if (rebound) Bind();
    }
    void VulkanFramebuffer::Bind()
    {
        if (!m_Target) throw std::logic_error("Cannot bind a zero-size framebuffer");
        m_Context.BindTarget(*m_Target);
        m_Context.Clear(0.1f, 0.1f, 0.1f, 1.0f);
        ClearAttachment(1, -1);
    }
    RendererHandle VulkanFramebuffer::GetColorAttachmentRendererID(uint32_t index) const
    {
        if (index >= m_Handles.size()) throw std::out_of_range("Framebuffer attachment index");
        return m_Handles[index];
    }
    int VulkanFramebuffer::ReadPixel(uint32_t x, uint32_t y)
    {
        if (!m_Target || x >= m_Specification.Width || y >= m_Specification.Height) return -1;
        int32_t id = -1;
        m_Context.ReadImage(*m_Target->Colors[1], x, y, 1, 1, &id);
        return id;
    }
    bool VulkanFramebuffer::ReadColorPixels(std::vector<uint32_t>& pixels)
    {
        pixels.clear(); if (!m_Target) return false;
        pixels.resize(size_t(m_Specification.Width) * m_Specification.Height);
        m_Context.ReadImage(*m_Target->Colors[0], 0, 0, m_Specification.Width, m_Specification.Height, pixels.data());
        return true;
    }
    void VulkanFramebuffer::ClearAttachment(uint32_t index, int value)
    {
        if (index != 1 || !m_Target) throw std::invalid_argument("Integer clear requires the ID attachment");
        auto& image = *m_Target->Colors[1];
        m_Context.Execute([&](VkCommandBuffer command)
        {
            const auto previous = image.Layout;
            m_Context.Transition(command, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkClearColorValue clear{}; clear.int32[0] = value;
            const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            vkCmdClearColorImage(command, image.Image, image.Layout, &clear, 1, &range);
            m_Context.Transition(command, image, previous);
        });
    }
}
#endif
