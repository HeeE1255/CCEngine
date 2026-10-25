#include "VulkanResources.h"
#if defined(CC_ENABLE_VULKAN_RHI)
#include <cstring>
#include <stdexcept>
namespace CCEngine
{
    void CheckVulkan(VkResult result, const char* operation)
    {
        if (result != VK_SUCCESS) throw std::runtime_error(std::string(operation) + ": VkResult=" + std::to_string(result));
    }
    VulkanBufferAllocation::VulkanBufferAllocation(VulkanContext& context, VkDeviceSize size, VkBufferUsageFlags usage, const void* data)
        : Owner(context.SharedDevice()), Size(size)
    {
        if (!size || size > SIZE_MAX) throw std::invalid_argument("Invalid Vulkan buffer size");
        if (!Owner) throw std::logic_error("Vulkan resource requires an initialized device");
        Owner->RequireReady();
        const VkDevice device = context.Device();
        try
        {
            VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            info.size = size; info.usage = usage; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            CheckVulkan(vkCreateBuffer(device, &info, nullptr, &Buffer), "vkCreateBuffer");
            VkMemoryRequirements requirements{}; vkGetBufferMemoryRequirements(device, Buffer, &requirements);
            VkMemoryAllocateInfo memory{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            memory.allocationSize = requirements.size;
            memory.memoryTypeIndex = context.MemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            CheckVulkan(vkAllocateMemory(device, &memory, nullptr, &Memory), "vkAllocateMemory(buffer)");
            CheckVulkan(vkBindBufferMemory(device, Buffer, Memory, 0), "vkBindBufferMemory");
            void* mapped = nullptr; CheckVulkan(vkMapMemory(device, Memory, 0, size, 0, &mapped), "vkMapMemory");
            if (data) std::memcpy(mapped, data, static_cast<size_t>(size));
            else std::memset(mapped, 0, static_cast<size_t>(size));
            vkUnmapMemory(device, Memory);
        }
        catch (...)
        {
            if (Buffer) vkDestroyBuffer(device, Buffer, nullptr);
            if (Memory) vkFreeMemory(device, Memory, nullptr);
            throw;
        }
    }
    VulkanBufferAllocation::~VulkanBufferAllocation()
    {
        const auto device = Owner->Native(); const auto buffer = Buffer; const auto memory = Memory;
        Owner->Defer([device, buffer, memory]() { vkDestroyBuffer(device, buffer, nullptr); vkFreeMemory(device, memory, nullptr); });
    }
    VulkanImageAllocation::VulkanImageAllocation(VulkanContext& context, VkExtent2D size, VkFormat format,
        VkImageUsageFlags usage, VkImageAspectFlags aspect, VkImage external)
        : Owner(context.SharedDevice()), Image(external), Format(format), Extent(size), Aspect(aspect), Owned(!external)
    {
        if (!Owner) throw std::logic_error("Vulkan resource requires an initialized device");
        Owner->RequireReady();
        if (!size.width || !size.height || size.width > context.Properties().limits.maxImageDimension2D || size.height > context.Properties().limits.maxImageDimension2D)
            throw std::invalid_argument("Invalid Vulkan image size");
        const VkDevice device = context.Device();
        try
        {
            if (Owned)
            {
                VkImageFormatProperties supported{};
                CheckVulkan(vkGetPhysicalDeviceImageFormatProperties(context.PhysicalDevice(), format, VK_IMAGE_TYPE_2D,
                    VK_IMAGE_TILING_OPTIMAL, usage, 0, &supported), "Image format support");
                if (size.width > supported.maxExtent.width || size.height > supported.maxExtent.height)
                    throw std::out_of_range("Image exceeds format-specific device limits");
                VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
                info.imageType = VK_IMAGE_TYPE_2D; info.format = format; info.extent = { size.width, size.height, 1 };
                info.mipLevels = 1; info.arrayLayers = 1; info.samples = VK_SAMPLE_COUNT_1_BIT;
                info.tiling = VK_IMAGE_TILING_OPTIMAL; info.usage = usage; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                CheckVulkan(vkCreateImage(device, &info, nullptr, &Image), "vkCreateImage");
                VkMemoryRequirements requirements{}; vkGetImageMemoryRequirements(device, Image, &requirements);
                VkMemoryAllocateInfo memory{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
                memory.allocationSize = requirements.size;
                memory.memoryTypeIndex = context.MemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                CheckVulkan(vkAllocateMemory(device, &memory, nullptr, &Memory), "vkAllocateMemory(image)");
                CheckVulkan(vkBindImageMemory(device, Image, Memory, 0), "vkBindImageMemory");
            }
            VkImageViewCreateInfo view{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            view.image = Image; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = format;
            view.subresourceRange = { aspect, 0, 1, 0, 1 };
            CheckVulkan(vkCreateImageView(device, &view, nullptr, &View), "vkCreateImageView");
        }
        catch (...)
        {
            if (Owned && Image) vkDestroyImage(device, Image, nullptr);
            if (Memory) vkFreeMemory(device, Memory, nullptr);
            throw;
        }
    }
    VulkanImageAllocation::~VulkanImageAllocation()
    {
        const auto device = Owner->Native(); const auto image = Image; const auto view = View; const auto memory = Memory; const bool owned = Owned;
        Owner->Defer([device, image, view, memory, owned]()
        {
            vkDestroyImageView(device, view, nullptr);
            if (owned) vkDestroyImage(device, image, nullptr);
            if (memory) vkFreeMemory(device, memory, nullptr);
        });
    }
    VulkanRenderTarget::VulkanRenderTarget(VulkanContext& context, VkExtent2D extent,
        std::vector<std::shared_ptr<VulkanImageAllocation>> colors, std::shared_ptr<VulkanImageAllocation> depth, bool backbuffer)
        : Owner(context.SharedDevice()), ID(context.NextID()), Extent(extent), Colors(std::move(colors)), Depth(std::move(depth)), Backbuffer(backbuffer), WindowOwner(backbuffer ? &context : nullptr)
    {
        std::vector<VkAttachmentDescription> attachments;
        const auto& limits = context.Properties().limits;
        if (!extent.width || !extent.height || extent.width > limits.maxFramebufferWidth ||
            extent.height > limits.maxFramebufferHeight || Colors.empty() || Colors.size() > limits.maxColorAttachments)
            throw std::invalid_argument("Render target exceeds device limits");
        auto validate = [&](const std::shared_ptr<VulkanImageAllocation>& image)
        {
            if (!image || image->Owner != context.SharedDevice() || image->Extent.width != extent.width || image->Extent.height != extent.height)
                throw std::invalid_argument("Render target attachment mismatch");
        };
        for (const auto& color : Colors) validate(color);
        if (Depth) validate(Depth);
        std::vector<VkAttachmentReference> references;
        std::vector<VkImageView> views;
        for (const auto& color : Colors)
        {
            VkAttachmentDescription a{}; a.format = color->Format; a.samples = VK_SAMPLE_COUNT_1_BIT;
            a.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            a.initialLayout = a.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            references.push_back({ static_cast<uint32_t>(attachments.size()), a.initialLayout });
            attachments.push_back(a); views.push_back(color->View);
        }
        VkAttachmentReference depthRef{};
        if (Depth)
        {
            VkAttachmentDescription a{}; a.format = Depth->Format; a.samples = VK_SAMPLE_COUNT_1_BIT;
            a.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            a.initialLayout = a.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthRef = { static_cast<uint32_t>(attachments.size()), a.initialLayout };
            attachments.push_back(a); views.push_back(Depth->View);
        }
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = static_cast<uint32_t>(references.size()); subpass.pColorAttachments = references.data();
        subpass.pDepthStencilAttachment = Depth ? &depthRef : nullptr;
        VkRenderPassCreateInfo pass{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        // 같은 depth attachment를 다음 pass에서도 LOAD할 때 layout이 같아도 쓰기 의존성은 필요하다.
        VkSubpassDependency dependencies[2]{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL; dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0; dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = dependencies[0].dstStageMask;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        pass.dependencyCount = 2; pass.pDependencies = dependencies;
        pass.attachmentCount = static_cast<uint32_t>(attachments.size()); pass.pAttachments = attachments.data();
        pass.subpassCount = 1; pass.pSubpasses = &subpass;
        CheckVulkan(vkCreateRenderPass(context.Device(), &pass, nullptr, &Pass), "vkCreateRenderPass");
        VkFramebufferCreateInfo fb{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fb.renderPass = Pass; fb.attachmentCount = static_cast<uint32_t>(views.size()); fb.pAttachments = views.data();
        fb.width = extent.width; fb.height = extent.height; fb.layers = 1;
        const auto result = vkCreateFramebuffer(context.Device(), &fb, nullptr, &Framebuffer);
        if (result != VK_SUCCESS) { vkDestroyRenderPass(context.Device(), Pass, nullptr); Pass = VK_NULL_HANDLE; CheckVulkan(result, "vkCreateFramebuffer"); }
    }
    VulkanRenderTarget::~VulkanRenderTarget()
    {
        Owner->ReleaseTarget(*this);
        const auto device = Owner->Native(); const auto framebuffer = Framebuffer; const auto pass = Pass;
        Owner->Defer([device, framebuffer, pass]() { vkDestroyFramebuffer(device, framebuffer, nullptr); vkDestroyRenderPass(device, pass, nullptr); });
    }
    VulkanShaderProgram::~VulkanShaderProgram()
    {
        const auto device = Owner->Native(); const auto vertex = Vertex; const auto fragment = Fragment;
        Owner->Defer([device, vertex, fragment]() { if (vertex) vkDestroyShaderModule(device, vertex, nullptr); if (fragment) vkDestroyShaderModule(device, fragment, nullptr); });
    }
}
#endif
