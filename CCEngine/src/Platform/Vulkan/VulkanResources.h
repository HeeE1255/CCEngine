#pragma once
#if defined(CC_ENABLE_VULKAN_RHI)
#include "VulkanContext.h"
namespace CCEngine
{
    struct VulkanBufferAllocation
    {
        VulkanContext& Context;
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
        VulkanContext& Context;
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
        VulkanContext& Context;
        uint64_t ID;
        VkExtent2D Extent;
        std::vector<std::shared_ptr<VulkanImageAllocation>> Colors;
        std::shared_ptr<VulkanImageAllocation> Depth;
        VkRenderPass Pass = VK_NULL_HANDLE;
        VkFramebuffer Framebuffer = VK_NULL_HANDLE;
        bool Backbuffer;
        VulkanRenderTarget(VulkanContext& context, VkExtent2D extent,
            std::vector<std::shared_ptr<VulkanImageAllocation>> colors,
            std::shared_ptr<VulkanImageAllocation> depth, bool backbuffer = false);
        ~VulkanRenderTarget();
        VulkanRenderTarget(const VulkanRenderTarget&) = delete;
        VulkanRenderTarget& operator=(const VulkanRenderTarget&) = delete;
    };
    struct VulkanShaderProgram
    {
        VulkanContext& Context;
        uint64_t ID;
        VkShaderModule Vertex = VK_NULL_HANDLE, Fragment = VK_NULL_HANDLE;
        std::string VertexEntry, FragmentEntry;
        explicit VulkanShaderProgram(VulkanContext& context) : Context(context), ID(context.NextID()) {}
        ~VulkanShaderProgram();
        VulkanShaderProgram(const VulkanShaderProgram&) = delete;
        VulkanShaderProgram& operator=(const VulkanShaderProgram&) = delete;
    };
}
#endif
