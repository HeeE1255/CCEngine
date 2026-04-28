#include "Platform/Vulkan/VulkanTexture.h"
#include <iostream>

namespace CCEngine
{
    VulkanTexture2D::VulkanTexture2D(const std::string& path) : m_Path(path)
    {
        std::cout << "[Vulkan] Texture Created from path: " << path << std::endl;
    }

    VulkanTexture2D::VulkanTexture2D(uint32_t width, uint32_t height) : m_Width(width), m_Height(height)
    {
        std::cout << "[Vulkan] Empty Texture Created. Size: " << width << "x" << height << std::endl;
    }

    VulkanTexture2D::~VulkanTexture2D()
    {
        std::cout << "[Vulkan] Texture Destroyed" << std::endl;
    }

    void VulkanTexture2D::Bind(uint32_t slot) const { /* Vulkan 파이프라인 바인딩 로직 */ }
    void VulkanTexture2D::Unbind(uint32_t slot) const {}
    void VulkanTexture2D::SetData(void* data, uint32_t size) { /* VkBuffer -> VkImage 복사 등 */ }
}