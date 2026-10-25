#include "VulkanResources.h"
#if defined(CC_ENABLE_VULKAN_RHI)
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace CCEngine
{
    void VulkanContext::BeginRecording()
    {
        m_SharedDevice->AcquireRecording(this);
        if (m_Recording) return;
        CheckVulkan(vkResetCommandBuffer(m_Command, 0), "Reset command buffer");
        VkCommandBufferBeginInfo info{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        CheckVulkan(vkBeginCommandBuffer(m_Command, &info), "Begin command buffer");
        m_Recording = true;
    }
    void VulkanContext::Transition(VkCommandBuffer command, VulkanImageAllocation& image, VkImageLayout next)
    {
        if (image.Owner != m_SharedDevice || command != m_Command || !m_Recording)
            throw std::invalid_argument("Foreign Vulkan image or command buffer");
        if (image.Layout == next) return;
        VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.oldLayout = image.Layout; barrier.newLayout = next;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image.Image;
        barrier.subresourceRange = { image.Aspect, 0, 1, 0, 1 };
        barrier.srcAccessMask = image.Layout == VK_IMAGE_LAYOUT_UNDEFINED || image.Layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
            ? 0 : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        switch (next)
        {
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT; break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT; break;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; break;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: barrier.dstAccessMask = 0; break;
        default: throw std::invalid_argument("Unsupported Vulkan image layout");
        }
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
        image.Layout = next;
    }
    void VulkanContext::BeginPass()
    {
        if (m_InPass || !m_Target) return;
        BeginRecording();
        for (auto& color : m_Target->Colors) Transition(m_Command, *color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        if (m_Target->Depth) Transition(m_Command, *m_Target->Depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        VkRenderPassBeginInfo info{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        info.renderPass = m_Target->Pass; info.framebuffer = m_Target->Framebuffer;
        info.renderArea = { { 0, 0 }, m_Target->Extent };
        vkCmdBeginRenderPass(m_Command, &info, VK_SUBPASS_CONTENTS_INLINE);
        m_InPass = true;
        m_API->BeginPass(m_Command, m_Target->Extent, static_cast<uint32_t>(m_Target->Colors.size()),
            m_Target->Depth ? m_Target->Depth->Aspect : 0);
    }
    void VulkanContext::EndPass()
    {
        if (!m_InPass) return;
        vkCmdEndRenderPass(m_Command);
        m_API->EndPass(); m_InPass = false;
        if (!m_Target->Backbuffer)
            for (auto& color : m_Target->Colors) Transition(m_Command, *color, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    void VulkanContext::BindTarget(VulkanRenderTarget& target)
    {
        if (target.Owner != m_SharedDevice || (target.Backbuffer && target.WindowOwner != this))
            throw std::invalid_argument("Render target belongs to another device or window");
        m_SharedDevice->AcquireRecording(this);
        EndPass(); m_Target = &target;
        m_API->UnbindTextures(0, m_TextureSlots);
        BeginPass();
    }
    void VulkanContext::ReleaseTarget(VulkanRenderTarget& target)
    {
        if (m_Target == &target) { if (!m_Releasing) EndPass(); m_Target = nullptr; }
        const auto prefix = std::to_string(target.ID) + '/';
        for (auto it = m_Pipelines.begin(); it != m_Pipelines.end();)
        {
            if (it->first.compare(0, prefix.size(), prefix) != 0) { ++it; continue; }
            const auto device = m_Device; const auto pipeline = it->second;
            Defer([device, pipeline]() { vkDestroyPipeline(device, pipeline, nullptr); });
            it = m_Pipelines.erase(it);
        }
    }
    void VulkanContext::Execute(const std::function<void(VkCommandBuffer)>& record)
    {
        const bool resume = m_InPass;
        const auto view = resume ? m_API->CaptureView() : VulkanRendererAPI::ViewState{};
        EndPass(); BeginRecording();
        record(m_Command);
        // 업로드/읽기 명령은 render pass 밖에서만 가능하다. 중단 전 viewport와 scissor도 복원한다.
        if (resume) { BeginPass(); m_API->RestoreView(view); }
    }
    void VulkanContext::Defer(std::function<void()> destroy)
    {
        if (m_Recording && !m_Releasing) m_Garbage.push_back(std::move(destroy));
        else destroy();
    }
    void VulkanContext::Submit(bool present)
    {
        EndPass();
        if (!m_Recording) { m_SharedDevice->EndRecording(this); return; }
        CheckVulkan(vkEndCommandBuffer(m_Command), "End command buffer");
        CheckVulkan(vkResetFences(m_Device, 1, &m_Fence), "Reset submit fence");
        VkSubmitInfo info{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        if (m_WaitAcquire) { info.waitSemaphoreCount = 1; info.pWaitSemaphores = &m_Acquire; info.pWaitDstStageMask = &stage; }
        info.commandBufferCount = 1; info.pCommandBuffers = &m_Command;
        if (present) { info.signalSemaphoreCount = 1; info.pSignalSemaphores = &m_PresentSemaphores.at(m_ImageIndex); }
        try
        {
            CheckVulkan(vkQueueSubmit(m_Queue, 1, &info, m_Fence), "Submit graphics commands");
            CheckVulkan(vkWaitForFences(m_Device, 1, &m_Fence, VK_TRUE, 10000000000ull), "Wait graphics fence");
        }
        catch (...) { m_SharedDevice->MarkFailed(); throw; }
        m_Recording = false; m_WaitAcquire = false;
        m_SharedDevice->EndRecording(this);
        if (present)
        {
            VkPresentInfoKHR presentation{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
            presentation.waitSemaphoreCount = 1; presentation.pWaitSemaphores = info.pSignalSemaphores;
            presentation.swapchainCount = 1; presentation.pSwapchains = &m_Swapchain; presentation.pImageIndices = &m_ImageIndex;
            const auto result = vkQueuePresentKHR(m_Queue, &presentation);
            m_Acquired = false;
            if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) m_Recreate = true;
            else CheckVulkan(result, "Present swapchain");
        }
        // 제출 완료 전에는 이전 draw가 참조하는 버퍼나 descriptor를 재사용하면 안 된다.
        auto garbage = std::move(m_Garbage); m_Garbage.clear();
        for (auto& destroy : garbage) destroy();
        for (auto pool : m_DescriptorPools) CheckVulkan(vkResetDescriptorPool(m_Device, pool, 0), "Reset descriptor pool");
    }
    void VulkanContext::Flush()
    {
        try { m_SharedDevice->RequireReady(); Submit(false); }
        catch (...) { m_SharedDevice->MarkFailed(); throw; }
    }
    void VulkanContext::Upload(VulkanImageAllocation& image, const void* data, uint32_t bytes)
    {
        if (!data || image.Format != VK_FORMAT_R8G8B8A8_UNORM ||
            uint64_t(image.Extent.width) * image.Extent.height * 4 != bytes)
            throw std::invalid_argument("Vulkan upload requires complete RGBA8 data");
        auto staging = std::make_shared<VulkanBufferAllocation>(*this, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, data);
        Execute([&](VkCommandBuffer command)
        {
            Transition(command, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkBufferImageCopy copy{}; copy.imageSubresource = { image.Aspect, 0, 0, 1 };
            copy.imageExtent = { image.Extent.width, image.Extent.height, 1 };
            vkCmdCopyBufferToImage(command, staging->Buffer, image.Image, image.Layout, 1, &copy);
            Transition(command, image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        });
    }
    void VulkanContext::ReadImage(VulkanImageAllocation& image, uint32_t x, uint32_t y,
        uint32_t width, uint32_t height, void* destination)
    {
        if (!destination || !width || !height || x > image.Extent.width || y > image.Extent.height ||
            width > image.Extent.width - x || height > image.Extent.height - y ||
            (image.Format != VK_FORMAT_R8G8B8A8_UNORM && image.Format != VK_FORMAT_R32_SINT &&
                image.Format != VK_FORMAT_B8G8R8A8_UNORM && image.Format != VK_FORMAT_B8G8R8A8_SRGB &&
                image.Format != VK_FORMAT_R8G8B8A8_SRGB) ||
            image.Layout == VK_IMAGE_LAYOUT_UNDEFINED)
            throw std::invalid_argument("Invalid Vulkan readback region or format");
        const VkDeviceSize bytes = VkDeviceSize(width) * height * 4;
        if (bytes > SIZE_MAX) throw std::overflow_error("Readback exceeds CPU address space");
        auto staging = std::make_shared<VulkanBufferAllocation>(*this, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        const bool resume = m_InPass;
        const auto view = resume ? m_API->CaptureView() : VulkanRendererAPI::ViewState{};
        Execute([&](VkCommandBuffer command)
        {
            const auto previous = image.Layout;
            Transition(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            VkBufferImageCopy copy{}; copy.imageSubresource = { image.Aspect, 0, 0, 1 };
            copy.imageOffset = { static_cast<int32_t>(x), static_cast<int32_t>(y), 0 };
            copy.imageExtent = { width, height, 1 };
            vkCmdCopyImageToBuffer(command, image.Image, image.Layout, staging->Buffer, 1, &copy);
            VkBufferMemoryBarrier barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = staging->Buffer; barrier.size = bytes;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                0, 0, nullptr, 1, &barrier, 0, nullptr);
            Transition(command, image, previous);
        });
        Flush();
        void* mapped = nullptr;
        CheckVulkan(vkMapMemory(m_Device, staging->Memory, 0, bytes, 0, &mapped), "Map readback");
        std::memcpy(destination, mapped, static_cast<size_t>(bytes));
        vkUnmapMemory(m_Device, staging->Memory);
        if (resume) { BeginPass(); m_API->RestoreView(view); }
    }
    void VulkanContext::BindVertex(std::shared_ptr<VulkanBufferAllocation> buffer, const BufferLayout& layout)
    {
        if (buffer && buffer->Owner != m_SharedDevice) throw std::invalid_argument("Foreign vertex buffer");
        m_Vertex = std::move(buffer); m_VertexLayout = layout;
    }
    void VulkanContext::BindIndex(std::shared_ptr<VulkanBufferAllocation> buffer, uint32_t count)
    {
        if (buffer && (buffer->Owner != m_SharedDevice || uint64_t(count) * 4 > buffer->Size))
            throw std::invalid_argument("Invalid index buffer");
        m_Index = std::move(buffer); m_IndexCount = count;
    }
    void VulkanContext::BindUniform(uint32_t slot, std::shared_ptr<VulkanBufferAllocation> buffer)
    {
        if (slot >= m_Uniforms.size() || (buffer && (buffer->Owner != m_SharedDevice || buffer->Size > m_Properties.limits.maxUniformBufferRange)))
            throw std::out_of_range("Invalid uniform binding");
        m_Uniforms[slot] = std::move(buffer);
    }
    void VulkanContext::BindShader(std::shared_ptr<VulkanShaderProgram> program)
    {
        if (program && program->Owner != m_SharedDevice) throw std::invalid_argument("Foreign shader");
        m_Shader = std::move(program);
    }
    void VulkanContext::ReplaceBuffer(const std::shared_ptr<VulkanBufferAllocation>& oldBuffer,
        const std::shared_ptr<VulkanBufferAllocation>& newBuffer)
    {
        if (m_Vertex == oldBuffer) m_Vertex = newBuffer;
        if (m_Index == oldBuffer) m_Index = newBuffer;
        for (auto& buffer : m_Uniforms) if (buffer == oldBuffer) buffer = newBuffer;
    }
}
#endif
