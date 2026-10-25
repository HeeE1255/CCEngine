#pragma once
#if defined(CC_ENABLE_VULKAN_RHI)
#include "VulkanResources.h"
namespace CCEngine
{
    class VulkanBufferStorage
    {
    public:
        VulkanBufferStorage(VulkanContext& context, uint32_t size, VkBufferUsageFlags usage, const void* data = nullptr);
        VulkanBufferStorage(const VulkanBufferStorage&) = delete;
        VulkanBufferStorage& operator=(const VulkanBufferStorage&) = delete;
    protected:
        void Update(const void* data, uint32_t size);
        std::shared_ptr<VulkanDevice> m_Device;
        VulkanContext& Context() const { return VulkanContext::CurrentFor(m_Device); }
        std::shared_ptr<VulkanBufferAllocation> m_Buffer;
    private:
        VkBufferUsageFlags m_Usage;
        std::vector<uint8_t> m_Shadow;
    };
    class VulkanVertexBuffer final : public VertexBuffer, private VulkanBufferStorage
    {
    public:
        VulkanVertexBuffer(VulkanContext& context, uint32_t size, const void* data = nullptr);
        void Bind() const override { Context().BindVertex(m_Buffer, m_Layout); }
        void Unbind() const override { Context().BindVertex(nullptr, {}); }
        const BufferLayout& GetLayout() const override { return m_Layout; }
        void SetLayout(const BufferLayout& layout) override { layout.Validate(); m_Layout = layout; }
        void SetData(const void* data, uint32_t size) override { Update(data, size); }
    private:
        BufferLayout m_Layout;
    };
    class VulkanIndexBuffer final : public IndexBuffer, private VulkanBufferStorage
    {
    public:
        VulkanIndexBuffer(VulkanContext& context, const uint32_t* indices, uint32_t count);
        void Bind() const override { Context().BindIndex(m_Buffer, m_Count); }
        void Unbind() const override { Context().BindIndex(nullptr, 0); }
        uint32_t GetCount() const override { return m_Count; }
    private:
        uint32_t m_Count;
    };
    class VulkanConstantBuffer final : public ConstantBuffer, private VulkanBufferStorage
    {
    public:
        VulkanConstantBuffer(VulkanContext& context, uint32_t size);
        void Bind(uint32_t slot = 0) const override { Context().BindUniform(slot, m_Buffer); }
        void SetData(const void* data, uint32_t size) override { Update(data, size); }
    };
}
#endif
