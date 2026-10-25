#pragma once
#if defined(CC_ENABLE_VULKAN_RHI)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#include "Renderer/GraphicsContext.h"
#include "VulkanRendererAPI.h"
#include <array>
#include <map>
#include <string>

namespace CCEngine
{
    struct VulkanBufferAllocation;
    struct VulkanImageAllocation;
    struct VulkanShaderProgram;
    struct VulkanRenderTarget;
    class VulkanDevice;
    void CheckVulkan(VkResult result, const char* operation);

    // 장치는 리소스와 공유하고 창별 출력/명령만 소유한다. 단일 렌더 스레드 계약이다.
    class VulkanContext final : public GraphicsContext
    {
    public:
        explicit VulkanContext(void* hwnd, std::shared_ptr<VulkanDevice> device = {});
        ~VulkanContext() override;
        VulkanContext(const VulkanContext&) = delete;
        VulkanContext& operator=(const VulkanContext&) = delete;
        void Init() override;
        void SwapBuffers(bool vsync = true) override;
        void Clear(float r, float g, float b, float a) override;
        void ResizeBuffers(uint32_t width, uint32_t height) override;
        void MakeCurrent() override;
        void BindBackBuffer() override;
        bool ReadBackBuffer(std::vector<uint8_t>& pixels, uint32_t& width, uint32_t& height) override;
        static VulkanContext& Current();
        static VulkanContext& CurrentFor(const std::shared_ptr<VulkanDevice>& device);
        std::shared_ptr<GraphicsDevice> GetGraphicsDevice() const override;
        const std::shared_ptr<VulkanDevice>& SharedDevice() const { return m_SharedDevice; }
        VulkanRendererAPI& Commands() { return *m_API; }
        RendererAPI* GetCommands() override { return m_API.get(); }
        VkDevice Device() const { return m_Device; }
        VkPhysicalDevice PhysicalDevice() const { return m_Physical; }
        const VkPhysicalDeviceProperties& Properties() const { return m_Properties; }
        VkSampler Sampler() const { return m_Sampler; }
        uint32_t MemoryType(uint32_t bits, VkMemoryPropertyFlags flags) const;
        uint64_t NextID();
        void Execute(const std::function<void(VkCommandBuffer)>& record);
        // 제출을 완료하고 pass를 닫는다. 직접 호출한 뒤 렌더링하려면 target을 다시 바인딩한다.
        void Flush();
        void Defer(std::function<void()> destroy);
        void Transition(VkCommandBuffer command, VulkanImageAllocation& image, VkImageLayout next);
        void Upload(VulkanImageAllocation& image, const void* data, uint32_t bytes);
        void ReadImage(VulkanImageAllocation& image, uint32_t x, uint32_t y, uint32_t width, uint32_t height, void* destination);
        void BindTarget(VulkanRenderTarget& target);
        void ReleaseTarget(VulkanRenderTarget& target);
        void BindVertex(std::shared_ptr<VulkanBufferAllocation> buffer, const BufferLayout& layout);
        void BindIndex(std::shared_ptr<VulkanBufferAllocation> buffer, uint32_t count);
        void BindUniform(uint32_t slot, std::shared_ptr<VulkanBufferAllocation> buffer);
        void BindShader(std::shared_ptr<VulkanShaderProgram> program);
        void SetVertexLayout(const BufferLayout& layout) { layout.Validate(); m_VertexLayout = layout; }
        void SetShaderInputs(const std::vector<VertexInput>& inputs) { ValidateVertexInputs(inputs); m_ShaderInputs = inputs; }
        bool IsTargetBound(const VulkanRenderTarget& target) const { return m_Target == &target; }
        void ReplaceBuffer(const std::shared_ptr<VulkanBufferAllocation>& oldBuffer,
            const std::shared_ptr<VulkanBufferAllocation>& newBuffer);
    private:
        HWND m_Window;
        std::shared_ptr<VulkanDevice> m_SharedDevice;
        VkInstance m_Instance = VK_NULL_HANDLE;
        VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
        VkPhysicalDevice m_Physical = VK_NULL_HANDLE;
        VkPhysicalDeviceProperties m_Properties{};
        VkDevice m_Device = VK_NULL_HANDLE;
        uint32_t m_QueueFamily = 0;
        VkQueue m_Queue = VK_NULL_HANDLE;
        VkCommandPool m_CommandPool = VK_NULL_HANDLE;
        VkCommandBuffer m_Command = VK_NULL_HANDLE;
        VkFence m_Fence = VK_NULL_HANDLE;
        VkSemaphore m_Acquire = VK_NULL_HANDLE;
        VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
        VkExtent2D m_Extent{};
        std::vector<std::unique_ptr<VulkanRenderTarget>> m_Backbuffers;
        std::vector<VkSemaphore> m_PresentSemaphores;
        VkDescriptorSetLayout m_SetLayout = VK_NULL_HANDLE;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        std::vector<VkDescriptorPool> m_DescriptorPools;
        std::map<std::string, VkPipeline> m_Pipelines;
        VkSampler m_Sampler = VK_NULL_HANDLE;
        VkSampler m_LinearSampler = VK_NULL_HANDLE;
        std::shared_ptr<VulkanImageAllocation> m_EmptyImage;
        std::shared_ptr<VulkanBufferAllocation> m_EmptyUniform, m_Vertex, m_Index;
        std::array<std::shared_ptr<VulkanBufferAllocation>, 8> m_Uniforms;
        std::shared_ptr<VulkanShaderProgram> m_Shader;
        std::vector<VertexInput> m_ShaderInputs;
        BufferLayout m_VertexLayout;
        std::unique_ptr<VulkanRendererAPI> m_API;
        VulkanRenderTarget* m_Target = nullptr;
        std::vector<std::function<void()>> m_Garbage;
        uint32_t m_ImageIndex = 0, m_TextureSlots = 0, m_IndexCount = 0;
        bool m_Recording = false, m_InPass = false, m_Acquired = false, m_WaitAcquire = false;
        bool m_VSync = true, m_Recreate = false, m_Releasing = false;
        bool m_BackbufferReadable = false;
        void BeginRecording();
        void BeginPass();
        void EndPass();
        void Submit(bool present);
        void Release() noexcept;
        void DestroySwapchain();
        VkDescriptorSet AllocateSet();
        VulkanDrawBinding PrepareDraw(VkCommandBuffer command, const VulkanRasterState& state,
            const std::vector<VkDescriptorImageInfo>& images);
    };
}
#endif
