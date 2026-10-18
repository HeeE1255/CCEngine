#pragma once
#if defined(CC_ENABLE_VULKAN_RHI)
#include "Renderer/Texture.h"
#include "VulkanResources.h"
namespace CCEngine
{
    class VulkanTexture2D final : public Texture2D
    {
    public:
        explicit VulkanTexture2D(const std::string& path);
        VulkanTexture2D(uint32_t width, uint32_t height);
        VulkanTexture2D(VulkanContext& context, uint32_t width, uint32_t height, const void* pixels = nullptr);
        ~VulkanTexture2D() override;
        VulkanTexture2D(const VulkanTexture2D&) = delete;
        VulkanTexture2D& operator=(const VulkanTexture2D&) = delete;
        void Bind(uint32_t slot = 0) const override { m_Context.Commands().BindTexture(slot, m_Handle); }
        void Unbind(uint32_t slot = 0) const override { m_Context.Commands().BindTexture(slot, nullptr); }
        void SetData(void* data, uint32_t size) override;
        RendererHandle GetRendererID() const override { return m_Handle; }
    private:
        VulkanContext& m_Context;
        VkExtent2D m_Extent{};
        std::shared_ptr<VulkanImageAllocation> m_Image;
        RendererHandle m_Handle = nullptr;
        void Replace(const void* pixels, uint32_t bytes);
    };
}
#endif
