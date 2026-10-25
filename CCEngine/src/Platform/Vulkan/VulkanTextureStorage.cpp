#include "VulkanTextureStorage.h"
#if defined(CC_ENABLE_VULKAN_RHI)
#include "stb_image.h"
#include <stdexcept>
namespace CCEngine
{
    namespace
    {
        uint32_t PixelBytes(VkExtent2D extent)
        {
            const uint64_t bytes = uint64_t(extent.width) * extent.height * 4;
            if (!extent.width || !extent.height || bytes > UINT32_MAX) throw std::invalid_argument("Invalid RGBA8 texture size");
            return static_cast<uint32_t>(bytes);
        }
    }
    VulkanTexture2D::VulkanTexture2D(const std::string& path) : m_Device(VulkanContext::Current().SharedDevice())
    {
        int width = 0, height = 0, channels = 0;
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(stbi_load(path.c_str(), &width, &height, &channels, 4), stbi_image_free);
        if (!pixels || width <= 0 || height <= 0) throw std::runtime_error("Cannot load Vulkan texture: " + path);
        m_Extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
        Replace(pixels.get(), PixelBytes(m_Extent));
    }
    VulkanTexture2D::VulkanTexture2D(uint32_t width, uint32_t height)
        : VulkanTexture2D(VulkanContext::Current(), width, height) {}
    VulkanTexture2D::VulkanTexture2D(VulkanContext& context, uint32_t width, uint32_t height, const void* pixels)
        : m_Device(context.SharedDevice()), m_Extent{ width, height }
    {
        const auto bytes = PixelBytes(m_Extent);
        if (pixels) Replace(pixels, bytes);
        else { std::vector<uint8_t> empty(bytes, 0); Replace(empty.data(), bytes); }
    }
    VulkanTexture2D::~VulkanTexture2D()
    {
        if (m_Handle) m_Device->UnregisterTexture(m_Handle);
    }
    void VulkanTexture2D::Replace(const void* pixels, uint32_t bytes)
    {
        if (!pixels || bytes != PixelBytes(m_Extent)) throw std::invalid_argument("Texture data must cover the complete RGBA8 image");
        auto replacement = std::make_shared<VulkanImageAllocation>(Context(), m_Extent, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        Context().Upload(*replacement, pixels, bytes);
        VkDescriptorImageInfo info{ Context().Sampler(), replacement->View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        // 슬롯의 토큰은 유지하되 이후 draw만 새 이미지로 전환한다. 이전 descriptor는 이전 이미지가 살아 있는 동안 실행된다.
        if (m_Handle) m_Device->UpdateTexture(m_Handle, info);
        else m_Handle = m_Device->RegisterTexture(info);
        m_Image = std::move(replacement);
    }
    void VulkanTexture2D::SetData(void* data, uint32_t size) { Replace(data, size); }
}
#endif
