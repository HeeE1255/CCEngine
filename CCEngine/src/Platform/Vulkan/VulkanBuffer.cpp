#include "VulkanBuffer.h"
#if defined(CC_ENABLE_VULKAN_RHI)
#include <cstring>
#include <stdexcept>
namespace CCEngine
{
    namespace
    {
        uint32_t IndexBytes(const uint32_t* indices, uint32_t count)
        {
            if (!indices || !count || count > UINT32_MAX / 4) throw std::invalid_argument("Invalid index data");
            return count * 4;
        }
        uint32_t UniformBytes(VulkanContext& context, uint32_t size)
        {
            if (!size || size > context.Properties().limits.maxUniformBufferRange)
                throw std::invalid_argument("Uniform buffer exceeds device limit");
            return size;
        }
    }
    VulkanBufferStorage::VulkanBufferStorage(VulkanContext& context, uint32_t size, VkBufferUsageFlags usage, const void* data)
        : m_Context(context), m_Usage(usage), m_Shadow(size, 0)
    {
        if (!size) throw std::invalid_argument("Empty GPU buffer");
        if (data) std::memcpy(m_Shadow.data(), data, size);
        m_Buffer = std::make_shared<VulkanBufferAllocation>(context, size, usage, m_Shadow.data());
    }
    void VulkanBufferStorage::Update(const void* data, uint32_t size)
    {
        if (size > m_Shadow.size() || (size && !data)) throw std::invalid_argument("Buffer update exceeds allocation");
        if (!size) return;
        auto shadow = m_Shadow;
        std::memcpy(shadow.data(), data, size);
        // 아직 제출되지 않은 draw도 이전 값을 참조한다. 덮어쓰기 대신 새 할당을 바인딩하고 이전 할당은 fence 뒤에 해제한다.
        auto replacement = std::make_shared<VulkanBufferAllocation>(m_Context, shadow.size(), m_Usage, shadow.data());
        m_Context.ReplaceBuffer(m_Buffer, replacement);
        m_Buffer = std::move(replacement); m_Shadow.swap(shadow);
    }
    VulkanVertexBuffer::VulkanVertexBuffer(VulkanContext& context, uint32_t size, const void* data)
        : VulkanBufferStorage(context, size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, data) {}
    VulkanIndexBuffer::VulkanIndexBuffer(VulkanContext& context, const uint32_t* indices, uint32_t count)
        : VulkanBufferStorage(context, IndexBytes(indices, count), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices), m_Count(count) {}
    VulkanConstantBuffer::VulkanConstantBuffer(VulkanContext& context, uint32_t size)
        : VulkanBufferStorage(context, UniformBytes(context, size), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) {}
}
#endif
