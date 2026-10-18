#include "VulkanContext.h"
#if defined(CC_ENABLE_VULKAN_RHI)
#include "VulkanResources.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace CCEngine
{
    thread_local VulkanContext* VulkanContext::s_Current = nullptr;
    VulkanContext::VulkanContext(void* hwnd) : m_Window(static_cast<HWND>(hwnd)) {}
    VulkanContext::~VulkanContext() { Release(); }
    VulkanContext& VulkanContext::Current()
    {
        if (!s_Current) throw std::logic_error("No current VulkanContext");
        return *s_Current;
    }
    void VulkanContext::MakeCurrent()
    {
        if (!m_Device) throw std::logic_error("Vulkan context is not initialized");
        s_Current = this;
        SetCurrentContext();
    }
    uint32_t VulkanContext::MemoryType(uint32_t bits, VkMemoryPropertyFlags flags) const
    {
        VkPhysicalDeviceMemoryProperties memory{}; vkGetPhysicalDeviceMemoryProperties(m_Physical, &memory);
        for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags) return i;
        throw std::runtime_error("Required Vulkan memory type is unavailable");
    }
    void VulkanContext::Init()
    {
        if (m_Instance || m_Releasing) throw std::logic_error("Vulkan context cannot be initialized again");
        if (!IsWindow(m_Window)) throw std::invalid_argument("Invalid Vulkan window");
        try
        {
            VkApplicationInfo application{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
            application.pApplicationName = "CCEngine"; application.apiVersion = VK_API_VERSION_1_1;
            const char* extensions[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
            VkInstanceCreateInfo instance{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
            instance.pApplicationInfo = &application; instance.enabledExtensionCount = 2; instance.ppEnabledExtensionNames = extensions;
            CheckVulkan(vkCreateInstance(&instance, nullptr, &m_Instance), "vkCreateInstance");
            VkWin32SurfaceCreateInfoKHR surface{ VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
            surface.hinstance = GetModuleHandleW(nullptr); surface.hwnd = m_Window;
            CheckVulkan(vkCreateWin32SurfaceKHR(m_Instance, &surface, nullptr, &m_Surface), "vkCreateWin32SurfaceKHR");
            uint32_t count = 0; CheckVulkan(vkEnumeratePhysicalDevices(m_Instance, &count, nullptr), "vkEnumeratePhysicalDevices");
            std::vector<VkPhysicalDevice> devices(count);
            CheckVulkan(vkEnumeratePhysicalDevices(m_Instance, &count, devices.data()), "vkEnumeratePhysicalDevices");
            for (VkPhysicalDevice candidate : devices)
            {
                VkPhysicalDeviceProperties properties{}; vkGetPhysicalDeviceProperties(candidate, &properties);
                VkPhysicalDeviceFeatures features{}; vkGetPhysicalDeviceFeatures(candidate, &features);
                if (properties.apiVersion < VK_API_VERSION_1_1 || !features.independentBlend) continue;
                uint32_t extensionCount = 0;
                CheckVulkan(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr), "Device extensions");
                std::vector<VkExtensionProperties> available(extensionCount);
                CheckVulkan(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, available.data()), "Device extensions");
                if (std::none_of(available.begin(), available.end(), [](const auto& e) { return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0; })) continue;
                uint32_t queueCount = 0; vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, nullptr);
                std::vector<VkQueueFamilyProperties> queues(queueCount); vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, queues.data());
                for (uint32_t i = 0; i < queueCount; ++i)
                {
                    VkBool32 present = VK_FALSE;
                    CheckVulkan(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, m_Surface, &present), "Surface support");
                    if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present)
                    { m_Physical = candidate; m_QueueFamily = i; m_Properties = properties; break; }
                }
                if (m_Physical) break;
            }
            if (!m_Physical) throw std::runtime_error("No Vulkan 1.1 graphics/present device with independentBlend");
            const float priority = 1;
            VkDeviceQueueCreateInfo queue{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
            queue.queueFamilyIndex = m_QueueFamily; queue.queueCount = 1; queue.pQueuePriorities = &priority;
            VkPhysicalDeviceFeatures features{}; features.independentBlend = VK_TRUE;
            const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
            VkDeviceCreateInfo device{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
            device.queueCreateInfoCount = 1; device.pQueueCreateInfos = &queue; device.pEnabledFeatures = &features;
            device.enabledExtensionCount = 1; device.ppEnabledExtensionNames = deviceExtensions;
            CheckVulkan(vkCreateDevice(m_Physical, &device, nullptr, &m_Device), "vkCreateDevice");
            vkGetDeviceQueue(m_Device, m_QueueFamily, 0, &m_Queue);
            VkCommandPoolCreateInfo pool{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
            pool.queueFamilyIndex = m_QueueFamily; pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            CheckVulkan(vkCreateCommandPool(m_Device, &pool, nullptr, &m_CommandPool), "vkCreateCommandPool");
            VkCommandBufferAllocateInfo command{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            command.commandPool = m_CommandPool; command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; command.commandBufferCount = 1;
            CheckVulkan(vkAllocateCommandBuffers(m_Device, &command, &m_Command), "vkAllocateCommandBuffers");
            VkFenceCreateInfo fence{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            CheckVulkan(vkCreateFence(m_Device, &fence, nullptr, &m_Fence), "vkCreateFence");
            VkSemaphoreCreateInfo semaphore{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
            CheckVulkan(vkCreateSemaphore(m_Device, &semaphore, nullptr, &m_Acquire), "vkCreateSemaphore");
            const auto& limits = m_Properties.limits;
            if (limits.maxPerStageResources <= 8) throw std::runtime_error("Insufficient Vulkan stage resources");
            m_TextureSlots = 32;
            if (limits.maxPerStageDescriptorSampledImages < 32 || limits.maxDescriptorSetSampledImages < 32 ||
                limits.maxPerStageDescriptorSamplers < 2 || limits.maxDescriptorSetSamplers < 2 || limits.maxPerStageResources < 42 ||
                limits.maxDescriptorSetUniformBuffers < 8 || limits.maxPerStageDescriptorUniformBuffers < 8)
                throw std::runtime_error("Vulkan descriptor limits do not meet the backend binding contract");
            std::array<VkDescriptorSetLayoutBinding, 42> bindings{};
            for (uint32_t i = 0; i < 8; ++i) bindings[i] = { i, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
            // 공통 HLSL의 b0~7 / t0~31 / s0~1을 서로 겹치지 않는 set 0 binding으로 옮긴다.
            for (uint32_t i = 0; i < 32; ++i)
                bindings[8 + i] = { 8 + i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
            for (uint32_t i = 0; i < 2; ++i)
                bindings[40 + i] = { 40 + i, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
            VkDescriptorSetLayoutCreateInfo set{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            set.bindingCount = static_cast<uint32_t>(bindings.size()); set.pBindings = bindings.data();
            CheckVulkan(vkCreateDescriptorSetLayout(m_Device, &set, nullptr, &m_SetLayout), "vkCreateDescriptorSetLayout");
            VkPipelineLayoutCreateInfo pipeline{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            pipeline.setLayoutCount = 1; pipeline.pSetLayouts = &m_SetLayout;
            CheckVulkan(vkCreatePipelineLayout(m_Device, &pipeline, nullptr, &m_PipelineLayout), "vkCreatePipelineLayout");
            VkSamplerCreateInfo sampler{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
            sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST; sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler.maxLod = 0;
            CheckVulkan(vkCreateSampler(m_Device, &sampler, nullptr, &m_Sampler), "vkCreateSampler");
            sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
            CheckVulkan(vkCreateSampler(m_Device, &sampler, nullptr, &m_LinearSampler), "vkCreateSampler(linear)");
            m_EmptyUniform = std::make_shared<VulkanBufferAllocation>(*this, 16384, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
            m_EmptyImage = std::make_shared<VulkanImageAllocation>(*this, VkExtent2D{ 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
            const uint32_t black = 0; Upload(*m_EmptyImage, &black, sizeof(black)); Flush();
            VulkanRendererServices services;
            services.TextureSlots = m_TextureSlots;
            services.EmptyTexture = { m_Sampler, m_EmptyImage->View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            services.ResizeSwapchain = [this](uint32_t w, uint32_t h) { ResizeBuffers(w, h); };
            services.ValidateIndexCount = [this](uint32_t count)
                { if (!m_Index || count > m_IndexCount) throw std::out_of_range("Draw exceeds bound index buffer"); };
            services.PrepareDraw = [this](VkCommandBuffer cmd, const VulkanRasterState& state, const std::vector<VkDescriptorImageInfo>& images)
                { return PrepareDraw(cmd, state, images); };
            m_API = std::make_unique<VulkanRendererAPI>(std::move(services));
            MakeCurrent(); RECT rect{}; GetClientRect(m_Window, &rect);
            ResizeBuffers(static_cast<uint32_t>(rect.right), static_cast<uint32_t>(rect.bottom));
        }
        catch (...) { Release(); throw; }
    }
    void VulkanContext::DestroySwapchain()
    {
        m_Target = nullptr; m_Backbuffers.clear();
        for (auto semaphore : m_PresentSemaphores) vkDestroySemaphore(m_Device, semaphore, nullptr);
        m_PresentSemaphores.clear();
        if (m_Swapchain) vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
        m_Swapchain = VK_NULL_HANDLE; m_Acquired = m_WaitAcquire = false;
    }
    void VulkanContext::ResizeBuffers(uint32_t width, uint32_t height)
    {
        Flush(); CheckVulkan(vkDeviceWaitIdle(m_Device), "vkDeviceWaitIdle(resize)");
        DestroySwapchain();
        m_Extent = { width, height };
        if (!width || !height) return;
        VkSurfaceCapabilitiesKHR capabilities{};
        CheckVulkan(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_Physical, m_Surface, &capabilities), "Surface capabilities");
        if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) throw std::runtime_error("Surface cannot be a color target");
        if (capabilities.currentExtent.width != UINT32_MAX) m_Extent = capabilities.currentExtent;
        else m_Extent = { std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height) };
        if (!m_Extent.width || !m_Extent.height) return;
        uint32_t count = 0; CheckVulkan(vkGetPhysicalDeviceSurfaceFormatsKHR(m_Physical, m_Surface, &count, nullptr), "Surface formats");
        std::vector<VkSurfaceFormatKHR> formats(count);
        CheckVulkan(vkGetPhysicalDeviceSurfaceFormatsKHR(m_Physical, m_Surface, &count, formats.data()), "Surface formats");
        if (formats.empty()) throw std::runtime_error("No Vulkan surface format");
        auto format = formats.front();
        for (auto candidate : formats) if (candidate.format == VK_FORMAT_B8G8R8A8_UNORM && candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { format = candidate; break; }
        if (format.format == VK_FORMAT_UNDEFINED) format.format = VK_FORMAT_B8G8R8A8_UNORM;
        VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
        CheckVulkan(vkGetPhysicalDeviceSurfacePresentModesKHR(m_Physical, m_Surface, &count, nullptr), "Present modes");
        std::vector<VkPresentModeKHR> modes(count);
        CheckVulkan(vkGetPhysicalDeviceSurfacePresentModesKHR(m_Physical, m_Surface, &count, modes.data()), "Present modes");
        if (!m_VSync)
        {
            if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end()) mode = VK_PRESENT_MODE_MAILBOX_KHR;
            else if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end()) mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        }
        VkSwapchainCreateInfoKHR info{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
        info.surface = m_Surface; info.minImageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount) info.minImageCount = std::min(info.minImageCount, capabilities.maxImageCount);
        info.imageFormat = format.format; info.imageColorSpace = format.colorSpace; info.imageExtent = m_Extent;
        info.imageArrayLayers = 1; info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = capabilities.currentTransform; info.presentMode = mode; info.clipped = VK_TRUE;
        for (auto alpha : { VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR })
            if (capabilities.supportedCompositeAlpha & alpha) { info.compositeAlpha = alpha; break; }
        try
        {
            CheckVulkan(vkCreateSwapchainKHR(m_Device, &info, nullptr, &m_Swapchain), "vkCreateSwapchainKHR");
            CheckVulkan(vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &count, nullptr), "Swapchain images");
            std::vector<VkImage> images(count); CheckVulkan(vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &count, images.data()), "Swapchain images");
            for (auto image : images)
            {
                auto color = std::make_shared<VulkanImageAllocation>(*this, m_Extent, format.format, 0, VK_IMAGE_ASPECT_COLOR_BIT, image);
                auto depth = std::make_shared<VulkanImageAllocation>(*this, m_Extent, VK_FORMAT_D32_SFLOAT,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
                m_Backbuffers.push_back(std::make_unique<VulkanRenderTarget>(*this, m_Extent,
                    std::vector<std::shared_ptr<VulkanImageAllocation>>{ color }, depth, true));
                VkSemaphore semaphore = VK_NULL_HANDLE; VkSemaphoreCreateInfo create{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
                CheckVulkan(vkCreateSemaphore(m_Device, &create, nullptr, &semaphore), "Present semaphore");
                try { m_PresentSemaphores.push_back(semaphore); }
                catch (...) { vkDestroySemaphore(m_Device, semaphore, nullptr); throw; }
            }
        }
        catch (...) { DestroySwapchain(); throw; }
        m_Recreate = false;
    }
    void VulkanContext::BindBackBuffer()
    {
        MakeCurrent();
        if (!m_Swapchain) return;
        if (!m_Acquired)
        {
            auto result = vkAcquireNextImageKHR(m_Device, m_Swapchain, UINT64_MAX, m_Acquire, VK_NULL_HANDLE, &m_ImageIndex);
            if (result == VK_ERROR_OUT_OF_DATE_KHR)
            {
                RECT rect{}; GetClientRect(m_Window, &rect); ResizeBuffers(rect.right, rect.bottom);
                if (!m_Swapchain) return;
                result = vkAcquireNextImageKHR(m_Device, m_Swapchain, UINT64_MAX, m_Acquire, VK_NULL_HANDLE, &m_ImageIndex);
            }
            if (result != VK_SUBOPTIMAL_KHR) CheckVulkan(result, "vkAcquireNextImageKHR");
            m_Recreate = result == VK_SUBOPTIMAL_KHR; m_Acquired = m_WaitAcquire = true;
        }
        BindTarget(*m_Backbuffers.at(m_ImageIndex));
    }
    void VulkanContext::Clear(float r, float g, float b, float a)
    {
        if (!m_InPass) return;
        m_API->SetClearColor(r, g, b, a); m_API->Clear();
    }
    void VulkanContext::SwapBuffers(bool vsync)
    {
        if (!m_Acquired && m_Swapchain) BindBackBuffer();
        if (!m_Acquired) { Flush(); return; }
        EndPass();
        BeginRecording();
        Transition(m_Command, *m_Backbuffers[m_ImageIndex]->Colors[0], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        Submit(true); m_Target = nullptr;
        if (m_VSync != vsync || m_Recreate)
        {
            m_VSync = vsync; RECT rect{}; GetClientRect(m_Window, &rect); ResizeBuffers(rect.right, rect.bottom);
        }
    }
    void VulkanContext::Release() noexcept
    {
        m_Releasing = true;
        if (m_Device)
        {
            vkDeviceWaitIdle(m_Device);
            m_Recording = m_InPass = false; m_Target = nullptr;
            DestroySwapchain();
            m_Vertex.reset(); m_Index.reset(); m_Shader.reset();
            for (auto& buffer : m_Uniforms) buffer.reset();
            m_EmptyUniform.reset(); m_EmptyImage.reset(); m_API.reset();
            for (auto& destroy : m_Garbage) destroy(); m_Garbage.clear();
            for (auto& entry : m_Pipelines) vkDestroyPipeline(m_Device, entry.second, nullptr);
            m_Pipelines.clear();
            for (auto pool : m_DescriptorPools) vkDestroyDescriptorPool(m_Device, pool, nullptr);
            m_DescriptorPools.clear();
            if (m_PipelineLayout) vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
            if (m_SetLayout) vkDestroyDescriptorSetLayout(m_Device, m_SetLayout, nullptr);
            if (m_Sampler) vkDestroySampler(m_Device, m_Sampler, nullptr);
            if (m_LinearSampler) vkDestroySampler(m_Device, m_LinearSampler, nullptr);
            if (m_Acquire) vkDestroySemaphore(m_Device, m_Acquire, nullptr);
            if (m_Fence) vkDestroyFence(m_Device, m_Fence, nullptr);
            if (m_CommandPool) vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
            vkDestroyDevice(m_Device, nullptr); m_Device = VK_NULL_HANDLE;
        }
        if (m_Surface) vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
        if (m_Instance) vkDestroyInstance(m_Instance, nullptr);
        m_Surface = VK_NULL_HANDLE; m_Instance = VK_NULL_HANDLE;
        if (s_Current == this) s_Current = nullptr;
    }
}
#endif
