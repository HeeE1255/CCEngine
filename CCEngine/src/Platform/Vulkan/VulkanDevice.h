#pragma once
#if defined(CC_ENABLE_VULKAN_RHI)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <vulkan/vulkan.h>
#include "Renderer/GraphicsDevice.h"
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace CCEngine
{
    class VulkanContext;
    struct VulkanBufferAllocation;
    struct VulkanRenderTarget;
    class VulkanDevice final : public GraphicsDevice
    {
    public:
        VulkanDevice();
        ~VulkanDevice() override;
        RendererAPI::API GetBackend() const override { return RendererAPI::API::Vulkan; }
        VkInstance Instance() const { return m_Instance; }
        VkDevice Native() const { return m_Device; }
        VkPhysicalDevice Physical() const { return m_Physical; }
        const VkPhysicalDeviceProperties& Properties() const { return m_Properties; }
        VkQueue Queue() const { return m_Queue; }
        uint32_t QueueFamily() const { return m_QueueFamily; }
        void EnsureSurface(VkSurfaceKHR surface);
        void CheckThread() const;
        void RequireReady() const;
        void MarkFailed() noexcept { m_Failed = true; }
        bool Failed() const { return m_Failed; }
        uint64_t NextID();
        void Attach(VulkanContext* context);
        void Detach(VulkanContext* context) noexcept;
        void AcquireRecording(VulkanContext* context);
        void EndRecording(VulkanContext* context) noexcept;
        void Defer(std::function<void()> destroy);
        void ReleaseTarget(VulkanRenderTarget& target);
        void ReplaceBuffer(const std::shared_ptr<VulkanBufferAllocation>& oldBuffer,
            const std::shared_ptr<VulkanBufferAllocation>& replacement);
        RendererHandle RegisterTexture(const VkDescriptorImageInfo& image);
        void UpdateTexture(RendererHandle handle, const VkDescriptorImageInfo& image);
        void UnregisterTexture(RendererHandle handle) noexcept;
        VkDescriptorImageInfo ResolveTexture(RendererHandle handle, const VkDescriptorImageInfo& empty) const;
    private:
        VkInstance m_Instance = VK_NULL_HANDLE;
        VkDevice m_Device = VK_NULL_HANDLE;
        VkPhysicalDevice m_Physical = VK_NULL_HANDLE;
        VkPhysicalDeviceProperties m_Properties{};
        VkQueue m_Queue = VK_NULL_HANDLE;
        uint32_t m_QueueFamily = 0;
        DWORD m_Thread = GetCurrentThreadId();
        uint64_t m_NextID = 0;
        bool m_Failed = false;
        VulkanContext* m_Recording = nullptr;
        std::vector<VulkanContext*> m_Contexts;
        struct TextureBinding { VkDescriptorImageInfo Image{}; bool Active = true; };
        // 삭제한 토큰도 장치 수명까지 남겨 오래된 핸들의 주소가 재사용되지 않게 한다.
        std::unordered_map<RendererHandle, std::unique_ptr<TextureBinding>> m_Textures;
    };
}
#endif
