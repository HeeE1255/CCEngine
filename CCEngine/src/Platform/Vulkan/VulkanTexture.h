#pragma once
#include "Renderer/Texture.h"
#include <string>

namespace CCEngine
{
    class VulkanTexture2D : public Texture2D
    {
    public:
        VulkanTexture2D(const std::string& path);
        VulkanTexture2D(uint32_t width, uint32_t height);
        virtual ~VulkanTexture2D();

        virtual void Bind(uint32_t slot = 0) const override;
        virtual void Unbind(uint32_t slot = 0) const override;

        virtual void SetData(void* data, uint32_t size) override;
        virtual void* GetRendererID() const override { return nullptr; } // Vulkan은 DescriptorSet 활용

    private:
        std::string m_Path;
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        // 나중에 VkImage, VkDeviceMemory, VkImageView 등 선언
    };
}