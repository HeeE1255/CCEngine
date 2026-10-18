#pragma once

#if defined(CC_ENABLE_VULKAN_RHI)
#include "Renderer/RendererAPI.h"
#include <vulkan/vulkan.h>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace CCEngine
{
    struct VulkanRasterState
    {
        RendererAPI::BlendMode Blend = RendererAPI::BlendMode::Opaque;
        RendererAPI::CullMode Cull = RendererAPI::CullMode::Back;
        bool DepthTest = true;
    };

    struct VulkanDrawBinding
    {
        VkPipeline Pipeline = VK_NULL_HANDLE;
        VkPipelineLayout Layout = VK_NULL_HANDLE;
        uint32_t FirstSet = 0;
        std::vector<VkDescriptorSet> DescriptorSets;
        std::vector<uint32_t> DynamicOffsets;
    };

    // 파이프라인 캐시는 shader/vertex layout/render pass와 RasterState를 모두 키에 포함해야 한다.
    // descriptor는 draw 시점의 스냅샷이며 해당 프레임 fence 완료 전까지 덮어쓰거나 해제하지 않는다.
    struct VulkanRendererServices
    {
        // 현재 shader와 pass에 호환되는 pipeline 및 새 descriptor 스냅샷을 반환한다.
        // viewport/scissor는 변경하지 않는다. TextureSlots는 물리 장치의 sampler/image 한도 이하여야 한다.
        std::function<VulkanDrawBinding(VkCommandBuffer, const VulkanRasterState&,
            const std::vector<VkDescriptorImageInfo>&)> PrepareDraw;
        std::function<void(uint32_t, uint32_t)> ResizeSwapchain;
        std::function<void(uint32_t)> ValidateIndexCount;
        VkDescriptorImageInfo EmptyTexture{};
        uint32_t TextureSlots = 0;
    };

    class VulkanRendererAPI final : public RendererAPI
    {
    public:
        // Vulkan 1.1 이상. PrepareDraw가 반환하는 pipeline은 dynamic viewport/scissor를 사용한다.
        explicit VulkanRendererAPI(VulkanRendererServices services);
        ~VulkanRendererAPI() override = default;
        VulkanRendererAPI(const VulkanRendererAPI&) = delete;
        VulkanRendererAPI& operator=(const VulkanRendererAPI&) = delete;

        // 호출자는 실제 vkCmdBeginRenderPass/Rendering 뒤에 연결하고 End 전에 해제한다.
        // 전체 target, layer 0 한 장을 대상으로 하는 inline graphics pass 계약이다.
        // color attachment 0은 float/UNORM 색상이며 정수 ID attachment의 clear는 framebuffer가 담당한다.
        void BeginPass(VkCommandBuffer commands, VkExtent2D extent,
            uint32_t colorAttachments, VkImageAspectFlags depthAspects);
        void EndPass();

        RendererHandle RegisterTexture(const VkDescriptorImageInfo& image);
        void UnregisterTexture(RendererHandle handle);
        void UpdateTexture(RendererHandle handle, const VkDescriptorImageInfo& image);
        struct ViewState { VkRect2D Viewport, Scissor; bool ScissorEnabled; };
        ViewState CaptureView() const { return { m_Viewport, m_Scissor, m_ScissorEnabled }; }
        void RestoreView(const ViewState& state);

        static VkPipelineRasterizationStateCreateInfo Rasterization(const VulkanRasterState& state,
            VkFrontFace frontFace = VK_FRONT_FACE_CLOCKWISE);
        static VkPipelineDepthStencilStateCreateInfo DepthStencil(const VulkanRasterState& state);
        // 서로 다른 MRT blend 상태를 쓰는 장치는 independentBlend 기능을 활성화해야 한다.
        static std::vector<VkPipelineColorBlendAttachmentState> ColorBlend(
            const VulkanRasterState& state, uint32_t colorAttachments);

        void SetClearColor(float r, float g, float b, float a) override;
        void Clear() override;
        void DrawIndexed(IndexBuffer* buffer) override;
        void DrawIndexed(uint32_t count) override;
        void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
        void ResizeContext(uint32_t width, uint32_t height) override;
        void SetDepthTest(bool enable) override { m_State.DepthTest = enable; }
        bool IsYAxisFlipped() const override { return false; }
        void SetScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
        void SetScissorEnable(bool enable) override;
        void SetBlendMode(BlendMode mode) override;
        void SetCullMode(CullMode mode) override;
        void BindTexture(uint32_t slot, RendererHandle handle) override;
        void UnbindTextures(uint32_t startSlot, uint32_t count) override;

    private:
        struct TextureBinding { VkDescriptorImageInfo Image; };
        VulkanRendererServices m_Services;
        VulkanRasterState m_State;
        VkCommandBuffer m_Commands = VK_NULL_HANDLE;
        VkExtent2D m_Extent{};
        VkRect2D m_Scissor{};
        VkRect2D m_Viewport{};
        VkClearColorValue m_ClearColor{ { 0, 0, 0, 1 } };
        uint32_t m_ColorAttachments = 0;
        VkImageAspectFlags m_DepthAspects = 0;
        bool m_ScissorEnabled = false;
        bool m_ViewportEmpty = false;
        std::vector<VkDescriptorImageInfo> m_Textures;
        std::vector<RendererHandle> m_TextureHandles;
        std::unordered_map<RendererHandle, std::unique_ptr<TextureBinding>> m_RegisteredTextures;
        // 제거한 토큰 주소를 재사용하지 않아 오래된 RendererHandle이 다른 텍스처로 바뀌는 것을 막는다.
        std::vector<std::unique_ptr<TextureBinding>> m_RetiredTextures;
        void RequirePass() const;
        void ApplyScissor();
    };
}
#endif
