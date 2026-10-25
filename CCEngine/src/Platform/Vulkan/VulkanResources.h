#pragma once
#if defined(CC_ENABLE_VULKAN_RHI)
#include "VulkanContext.h"
#include "VulkanDevice.h"
namespace CCEngine
{
    struct VulkanBufferAllocation
    {
        std::shared_ptr<VulkanDevice> Owner;
        VkBuffer Buffer = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        VkDeviceSize Size = 0;
        VulkanBufferAllocation(VulkanContext& context, VkDeviceSize size, VkBufferUsageFlags usage, const void* data = nullptr);
        ~VulkanBufferAllocation();
        VulkanBufferAllocation(const VulkanBufferAllocation&) = delete;
        VulkanBufferAllocation& operator=(const VulkanBufferAllocation&) = delete;
    };
    struct VulkanImageAllocation
    {
        std::shared_ptr<VulkanDevice> Owner;
        VkImage Image = VK_NULL_HANDLE;
        VkImageView View = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        VkFormat Format;
        VkExtent2D Extent;
        VkImageAspectFlags Aspect;
        VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
        bool Owned = true;
        VulkanImageAllocation(VulkanContext& context, VkExtent2D size, VkFormat format,
            VkImageUsageFlags usage, VkImageAspectFlags aspect, VkImage external = VK_NULL_HANDLE);
        ~VulkanImageAllocation();
        VulkanImageAllocation(const VulkanImageAllocation&) = delete;
        VulkanImageAllocation& operator=(const VulkanImageAllocation&) = delete;
    };
    struct VulkanRenderTarget
    {
        std::shared_ptr<VulkanDevice> Owner;
        uint64_t ID;
        VkExtent2D Extent;
        std::vector<std::shared_ptr<VulkanImageAllocation>> Colors;
        std::shared_ptr<VulkanImageAllocation> Depth;
        VkRenderPass Pass = VK_NULL_HANDLE;
        VkFramebuffer Framebuffer = VK_NULL_HANDLE;
        bool Backbuffer;
        VulkanContext* WindowOwner = nullptr;
        VulkanRenderTarget(VulkanContext& context, VkExtent2D extent,
            std::vector<std::shared_ptr<VulkanImageAllocation>> colors,
            std::shared_ptr<VulkanImageAllocation> depth, bool backbuffer = false);
        ~VulkanRenderTarget();
        VulkanRenderTarget(const VulkanRenderTarget&) = delete;
        VulkanRenderTarget& operator=(const VulkanRenderTarget&) = delete;
    };
    struct VulkanShaderProgram
    {
        std::shared_ptr<VulkanDevice> Owner;
        uint64_t ID;
        VkShaderModule Vertex = VK_NULL_HANDLE, Fragment = VK_NULL_HANDLE;
        std::string VertexEntry, FragmentEntry;
        explicit VulkanShaderProgram(VulkanContext& context) : Owner(context.SharedDevice()), ID(context.NextID()) {}
        ~VulkanShaderProgram();
        VulkanShaderProgram(const VulkanShaderProgram&) = delete;
        VulkanShaderProgram& operator=(const VulkanShaderProgram&) = delete;
    };
}
#endif
