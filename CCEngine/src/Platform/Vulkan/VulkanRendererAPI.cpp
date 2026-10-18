#include "VulkanRendererAPI.h"
#if defined(CC_ENABLE_VULKAN_RHI)
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace CCEngine
{
    namespace
    {
        bool IsSampledImage(const VkDescriptorImageInfo& image)
        {
            return image.imageView != VK_NULL_HANDLE && image.sampler != VK_NULL_HANDLE &&
                (image.imageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
                 image.imageLayout == VK_IMAGE_LAYOUT_GENERAL);
        }
    }

    VulkanRendererAPI::VulkanRendererAPI(VulkanRendererServices services)
        : m_Services(std::move(services))
    {
        if (!m_Services.PrepareDraw || !m_Services.ResizeSwapchain ||
            !m_Services.TextureSlots || !IsSampledImage(m_Services.EmptyTexture))
            throw std::invalid_argument("Vulkan RHI requires draw/resize services, texture limits and a valid empty texture");
        // nullDescriptor 확장은 필수가 아니다. 빈 슬롯은 실제 빈 텍스처로 채운다.
        m_Textures.assign(m_Services.TextureSlots, m_Services.EmptyTexture);
        m_TextureHandles.assign(m_Services.TextureSlots, nullptr);
    }

    void VulkanRendererAPI::BeginPass(VkCommandBuffer commands, VkExtent2D extent,
        uint32_t colorAttachments, VkImageAspectFlags depthAspects)
    {
        if (m_Commands) throw std::logic_error("Vulkan pass is already active");
        const auto maxCoordinate = static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
        if (!commands || !extent.width || !extent.height ||
            extent.width > maxCoordinate || extent.height > maxCoordinate ||
            (!colorAttachments && !depthAspects) ||
            (depthAspects & ~(VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)))
            throw std::invalid_argument("Invalid Vulkan pass description");
        m_Commands = commands;
        m_Extent = extent;
        m_ColorAttachments = colorAttachments;
        m_DepthAspects = depthAspects;
        m_Scissor = { { 0, 0 }, extent };
        m_ScissorEnabled = false;
        SetViewport(0, 0, extent.width, extent.height);
        ApplyScissor();
    }
    void VulkanRendererAPI::EndPass()
    {
        RequirePass();
        m_Commands = VK_NULL_HANDLE;
    }
    void VulkanRendererAPI::RequirePass() const
    {
        if (!m_Commands) throw std::logic_error("Vulkan graphics commands require an active pass");
    }
    RendererHandle VulkanRendererAPI::RegisterTexture(const VkDescriptorImageInfo& image)
    {
        if (!IsSampledImage(image)) throw std::invalid_argument("Invalid Vulkan sampled image");
        auto binding = std::make_unique<TextureBinding>();
        binding->Image = image;
        RendererHandle handle = binding.get();
        m_RegisteredTextures.emplace(handle, std::move(binding));
        return handle;
    }
    void VulkanRendererAPI::UnregisterTexture(RendererHandle handle)
    {
        if (!handle) return;
        auto found = m_RegisteredTextures.find(handle);
        if (found == m_RegisteredTextures.end()) throw std::invalid_argument("Unknown Vulkan texture handle");
        // 여기서는 CPU 슬롯만 해제한다. VkImage/View/Sampler 파괴는 소유자가 GPU fence 뒤에 수행한다.
        m_RetiredTextures.push_back(std::move(found->second));
        m_RegisteredTextures.erase(found);
        for (size_t i = 0; i < m_TextureHandles.size(); ++i)
            if (m_TextureHandles[i] == handle)
            {
                m_TextureHandles[i] = nullptr;
                m_Textures[i] = m_Services.EmptyTexture;
            }
    }
    void VulkanRendererAPI::SetClearColor(float r, float g, float b, float a)
    {
        m_ClearColor.float32[0] = r; m_ClearColor.float32[1] = g;
        m_ClearColor.float32[2] = b; m_ClearColor.float32[3] = a;
    }
    void VulkanRendererAPI::UpdateTexture(RendererHandle handle, const VkDescriptorImageInfo& image)
    {
        auto found = m_RegisteredTextures.find(handle);
        if (found == m_RegisteredTextures.end() || !IsSampledImage(image))
            throw std::invalid_argument("Invalid Vulkan texture update");
        found->second->Image = image;
        for (size_t i = 0; i < m_TextureHandles.size(); ++i)
            if (m_TextureHandles[i] == handle) m_Textures[i] = image;
    }
    void VulkanRendererAPI::RestoreView(const ViewState& state)
    {
        SetViewport(state.Viewport.offset.x, state.Viewport.offset.y, state.Viewport.extent.width, state.Viewport.extent.height);
        m_Scissor = state.Scissor;
        m_ScissorEnabled = state.ScissorEnabled;
        ApplyScissor();
    }
    void VulkanRendererAPI::Clear()
    {
        RequirePass();
        VkClearAttachment attachments[2]{};
        uint32_t count = 0;
        if (m_ColorAttachments)
        {
            attachments[count].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            attachments[count].colorAttachment = 0;
            attachments[count++].clearValue.color = m_ClearColor;
        }
        if (m_DepthAspects)
        {
            attachments[count].aspectMask = m_DepthAspects;
            attachments[count++].clearValue.depthStencil = { 1.0f, 0 };
        }
        // vkCmdClearAttachments는 pipeline scissor를 사용하지 않는다. ID attachment는 별도 정수 clear 대상이다.
        VkClearRect rect{};
        rect.rect = { { 0, 0 }, m_Extent };
        rect.layerCount = 1;
        vkCmdClearAttachments(m_Commands, count, attachments, 1, &rect);
    }
    void VulkanRendererAPI::DrawIndexed(IndexBuffer* buffer)
    {
        if (!buffer) throw std::invalid_argument("Index buffer is null");
        DrawIndexed(buffer->GetCount());
    }
    void VulkanRendererAPI::DrawIndexed(uint32_t count)
    {
        RequirePass();
        if (!count || m_ViewportEmpty ||
            (m_ScissorEnabled && (!m_Scissor.extent.width || !m_Scissor.extent.height))) return;
        if (m_Services.ValidateIndexCount) m_Services.ValidateIndexCount(count);
        const VulkanDrawBinding binding = m_Services.PrepareDraw(m_Commands, m_State, m_Textures);
        if (!binding.Pipeline || !binding.Layout)
            throw std::runtime_error("Vulkan draw service returned an incomplete pipeline binding");
        if (binding.DescriptorSets.empty() && !binding.DynamicOffsets.empty())
            throw std::runtime_error("Vulkan dynamic offsets require descriptor sets");
        for (VkDescriptorSet set : binding.DescriptorSets)
            if (!set) throw std::runtime_error("Vulkan draw service returned a null descriptor set");
        vkCmdBindPipeline(m_Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, binding.Pipeline);
        if (!binding.DescriptorSets.empty())
            vkCmdBindDescriptorSets(m_Commands, VK_PIPELINE_BIND_POINT_GRAPHICS, binding.Layout,
                binding.FirstSet, static_cast<uint32_t>(binding.DescriptorSets.size()), binding.DescriptorSets.data(),
                static_cast<uint32_t>(binding.DynamicOffsets.size()), binding.DynamicOffsets.data());
        // 호출 전에 UINT32 index buffer와 vertex buffer가 바인딩되어 있어야 한다. DX11 RHI와 같은 계약이다.
        vkCmdDrawIndexed(m_Commands, count, 1, 0, 0, 0);
    }
    void VulkanRendererAPI::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        RequirePass();
        if (x > m_Extent.width || y > m_Extent.height || width > m_Extent.width - x || height > m_Extent.height - y)
            throw std::out_of_range("Viewport exceeds the current Vulkan target");
        m_ViewportEmpty = !width || !height;
        m_Viewport = { { static_cast<int32_t>(x), static_cast<int32_t>(y) }, { width, height } };
        if (m_ViewportEmpty) return;
        // Vulkan 1.1의 음수 viewport 높이로 엔진의 위쪽 +Y 투영을 유지한다. scissor는 그대로 좌상단 기준이다.
        VkViewport viewport{ static_cast<float>(x), static_cast<float>(y + height),
            static_cast<float>(width), -static_cast<float>(height), 0.0f, 1.0f };
        vkCmdSetViewport(m_Commands, 0, 1, &viewport);
    }
    void VulkanRendererAPI::ResizeContext(uint32_t width, uint32_t height)
    {
        if (m_Commands) throw std::logic_error("Resize the Vulkan swapchain outside an active pass");
        // 최소화의 0 크기는 전달한다. swapchain 소유자가 제출 완료를 기다린 뒤 재생성을 연기한다.
        m_Services.ResizeSwapchain(width, height);
    }
    void VulkanRendererAPI::SetScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        RequirePass();
        x = std::min(x, m_Extent.width); y = std::min(y, m_Extent.height);
        m_Scissor = { { static_cast<int32_t>(x), static_cast<int32_t>(y) },
            { std::min(width, m_Extent.width - x), std::min(height, m_Extent.height - y) } };
        ApplyScissor();
    }
    void VulkanRendererAPI::ApplyScissor()
    {
        RequirePass();
        const VkRect2D rect = m_ScissorEnabled ? m_Scissor : VkRect2D{ { 0, 0 }, m_Extent };
        vkCmdSetScissor(m_Commands, 0, 1, &rect);
    }
    void VulkanRendererAPI::SetScissorEnable(bool enable)
    {
        RequirePass();
        m_ScissorEnabled = enable;
        ApplyScissor();
    }
    void VulkanRendererAPI::SetBlendMode(BlendMode mode)
    {
        if (mode != BlendMode::Opaque && mode != BlendMode::Transparent && mode != BlendMode::MRTPicking)
            throw std::invalid_argument("Unknown blend mode");
        m_State.Blend = mode;
    }
    void VulkanRendererAPI::SetCullMode(CullMode mode)
    {
        if (mode != CullMode::Back && mode != CullMode::None) throw std::invalid_argument("Unknown cull mode");
        m_State.Cull = mode;
    }
    void VulkanRendererAPI::BindTexture(uint32_t slot, RendererHandle handle)
    {
        if (slot >= m_Textures.size()) throw std::out_of_range("Vulkan texture slot exceeds configured limit");
        auto found = m_RegisteredTextures.find(handle);
        if (handle && found == m_RegisteredTextures.end())
            throw std::invalid_argument("Texture belongs to a different backend or has been unregistered");
        m_Textures[slot] = handle ? found->second->Image : m_Services.EmptyTexture;
        m_TextureHandles[slot] = handle;
    }
    void VulkanRendererAPI::UnbindTextures(uint32_t startSlot, uint32_t count)
    {
        if (startSlot > m_Textures.size() || count > m_Textures.size() - startSlot)
            throw std::out_of_range("Vulkan texture slot range exceeds configured limit");
        for (uint32_t i = 0; i < count; ++i) BindTexture(startSlot + i, nullptr);
    }
    VkPipelineRasterizationStateCreateInfo VulkanRendererAPI::Rasterization(
        const VulkanRasterState& state, VkFrontFace frontFace)
    {
        VkPipelineRasterizationStateCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        info.polygonMode = VK_POLYGON_MODE_FILL;
        info.cullMode = state.Cull == CullMode::Back ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
        info.frontFace = frontFace;
        info.lineWidth = 1.0f;
        return info;
    }
    VkPipelineDepthStencilStateCreateInfo VulkanRendererAPI::DepthStencil(const VulkanRasterState& state)
    {
        VkPipelineDepthStencilStateCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        info.depthTestEnable = state.DepthTest ? VK_TRUE : VK_FALSE;
        info.depthWriteEnable = info.depthTestEnable;
        info.depthCompareOp = VK_COMPARE_OP_LESS;
        info.maxDepthBounds = 1.0f;
        return info;
    }
    std::vector<VkPipelineColorBlendAttachmentState> VulkanRendererAPI::ColorBlend(
        const VulkanRasterState& state, uint32_t colorAttachments)
    {
        std::vector<VkPipelineColorBlendAttachmentState> result(colorAttachments);
        for (uint32_t i = 0; i < colorAttachments; ++i)
        {
            auto& target = result[i];
            target.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            target.blendEnable = i == 0 && state.Blend != BlendMode::Opaque;
            target.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            target.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            target.colorBlendOp = VK_BLEND_OP_ADD;
            target.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            target.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            target.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        return result;
    }
}
#endif
