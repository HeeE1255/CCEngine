#include "VulkanDevice.h"
#if defined(CC_ENABLE_VULKAN_RHI)
#include "VulkanResources.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace CCEngine
{
    VulkanDevice::VulkanDevice()
    {
        VkApplicationInfo application{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
        application.pApplicationName = "CCEngine"; application.apiVersion = VK_API_VERSION_1_1;
        const char* extensions[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };
        VkInstanceCreateInfo instance{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        instance.pApplicationInfo = &application; instance.enabledExtensionCount = 2; instance.ppEnabledExtensionNames = extensions;
        CheckVulkan(vkCreateInstance(&instance, nullptr, &m_Instance), "vkCreateInstance");
    }
    VulkanDevice::~VulkanDevice()
    {
        if (m_Device) { vkDeviceWaitIdle(m_Device); vkDestroyDevice(m_Device, nullptr); }
        if (m_Instance) vkDestroyInstance(m_Instance, nullptr);
    }
    void VulkanDevice::CheckThread() const
    {
        if (GetCurrentThreadId() != m_Thread) throw std::logic_error("Vulkan requires the owning render thread");
    }
    void VulkanDevice::RequireReady() const
    {
        CheckThread();
        if (!m_Device || m_Failed) throw std::runtime_error("Vulkan shared device is unavailable");
    }
    void VulkanDevice::EnsureSurface(VkSurfaceKHR surface)
    {
        CheckThread();
        if (m_Failed) throw std::runtime_error("Vulkan shared device has failed");
        if (m_Device)
        {
            VkBool32 present = VK_FALSE;
            CheckVulkan(vkGetPhysicalDeviceSurfaceSupportKHR(m_Physical, m_QueueFamily, surface, &present), "Shared surface support");
            if (!present) throw std::runtime_error("Shared Vulkan queue cannot present to this window");
            return;
        }
        m_Physical = VK_NULL_HANDLE;
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
                CheckVulkan(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &present), "Surface support");
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
    }
    uint64_t VulkanDevice::NextID()
    {
        RequireReady();
        if (m_NextID == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Vulkan resource ID exhausted");
        return ++m_NextID;
    }
    void VulkanDevice::Attach(VulkanContext* context)
    {
        RequireReady();
        m_Contexts.push_back(context);
    }
    void VulkanDevice::Detach(VulkanContext* context) noexcept
    {
        EndRecording(context);
        m_Contexts.erase(std::remove(m_Contexts.begin(), m_Contexts.end(), context), m_Contexts.end());
    }
    void VulkanDevice::AcquireRecording(VulkanContext* context)
    {
        RequireReady();
        // Layout은 CPU에서 기록 즉시 바뀐다. 다른 창의 미제출 명령을 먼저 완료해야 실제 GPU 상태와 일치한다.
        if (m_Recording && m_Recording != context) m_Recording->Flush();
        m_Recording = context;
    }
    void VulkanDevice::EndRecording(VulkanContext* context) noexcept
    {
        if (m_Recording == context) m_Recording = nullptr;
    }
    void VulkanDevice::Defer(std::function<void()> destroy)
    {
        CheckThread();
        if (m_Failed)
        {
            // 제출 실패 뒤에는 기록 중인 명령을 재사용하지 않는다. 파괴 전 남은 GPU 작업을 정리한다.
            vkDeviceWaitIdle(m_Device);
            destroy();
        }
        else if (m_Recording) m_Recording->Defer(std::move(destroy));
        else destroy();
    }
    void VulkanDevice::ReleaseTarget(VulkanRenderTarget& target)
    {
        CheckThread();
        for (auto* context : m_Contexts) context->ReleaseTarget(target);
    }
    void VulkanDevice::ReplaceBuffer(const std::shared_ptr<VulkanBufferAllocation>& oldBuffer,
        const std::shared_ptr<VulkanBufferAllocation>& replacement)
    {
        RequireReady();
        for (auto* context : m_Contexts) context->ReplaceBuffer(oldBuffer, replacement);
    }
    RendererHandle VulkanDevice::RegisterTexture(const VkDescriptorImageInfo& image)
    {
        RequireReady();
        if (!image.imageView || (image.imageLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
            image.imageLayout != VK_IMAGE_LAYOUT_GENERAL)) throw std::invalid_argument("Invalid Vulkan sampled image");
        auto binding = std::make_unique<TextureBinding>();
        binding->Image = image; binding->Image.sampler = VK_NULL_HANDLE;
        auto handle = static_cast<RendererHandle>(binding.get());
        m_Textures.emplace(handle, std::move(binding));
        return handle;
    }
    void VulkanDevice::UpdateTexture(RendererHandle handle, const VkDescriptorImageInfo& image)
    {
        RequireReady();
        auto found = m_Textures.find(handle);
        if (found == m_Textures.end() || !found->second->Active || !image.imageView ||
            (image.imageLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && image.imageLayout != VK_IMAGE_LAYOUT_GENERAL))
            throw std::invalid_argument("Invalid Vulkan texture update");
        found->second->Image = image;
        found->second->Image.sampler = VK_NULL_HANDLE;
    }
    void VulkanDevice::UnregisterTexture(RendererHandle handle) noexcept
    {
        auto found = m_Textures.find(handle);
        if (found != m_Textures.end()) { found->second->Active = false; found->second->Image = {}; }
    }
    VkDescriptorImageInfo VulkanDevice::ResolveTexture(RendererHandle handle, const VkDescriptorImageInfo& empty) const
    {
        RequireReady();
        if (!handle) return empty;
        auto found = m_Textures.find(handle);
        if (found == m_Textures.end()) throw std::invalid_argument("Texture belongs to another Vulkan device");
        if (!found->second->Active) return empty;
        auto image = found->second->Image;
        // Sampler는 창의 descriptor layout에 맞는 것을 사용한다. 생성 창의 sampler 수명에 기대지 않는다.
        image.sampler = empty.sampler;
        return image;
    }
}
#endif
