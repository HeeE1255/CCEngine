#pragma once
#if defined(CC_ENABLE_DX12_RHI)
#include "DX12Context.h"
namespace CCEngine
{
    class DX12BufferStorage
    {
    public:
        DX12BufferStorage(DX12Context& context, uint32_t size, const void* data, bool constant = false);
        DX12BufferStorage(const DX12BufferStorage&) = delete;
        DX12BufferStorage& operator=(const DX12BufferStorage&) = delete;
    protected:
        void Update(const void* data, uint32_t size);
        std::shared_ptr<DX12Device> m_Device;
        std::shared_ptr<DX12BufferAllocation> m_Buffer;
    private:
        bool m_Constant;
        std::vector<uint8_t> m_Shadow;
    };
    class DX12VertexBuffer final : public VertexBuffer, private DX12BufferStorage
    {
    public:
        DX12VertexBuffer(DX12Context& context, uint32_t bytes, const void* data = nullptr) : DX12BufferStorage(context, bytes, data) {}
        void Bind() const override { DX12Context::CurrentFor(m_Device).BindVertex(m_Buffer, m_Layout); }
        void Unbind() const override { DX12Context::CurrentFor(m_Device).BindVertex(nullptr, {}); }
        const BufferLayout& GetLayout() const override { return m_Layout; }
        void SetLayout(const BufferLayout& layout) override { m_Layout = layout; }
        void SetData(const void* data, uint32_t size) override { Update(data, size); }
    private:
        BufferLayout m_Layout;
    };
    class DX12IndexBuffer final : public IndexBuffer, private DX12BufferStorage
    {
    public:
        DX12IndexBuffer(DX12Context& context, const uint32_t* indices, uint32_t count);
        void Bind() const override { DX12Context::CurrentFor(m_Device).BindIndex(m_Buffer, m_Count); }
        void Unbind() const override { DX12Context::CurrentFor(m_Device).BindIndex(nullptr, 0); }
        uint32_t GetCount() const override { return m_Count; }
    private:
        uint32_t m_Count;
    };
    class DX12ConstantBuffer final : public ConstantBuffer, private DX12BufferStorage
    {
    public:
        DX12ConstantBuffer(DX12Context& context, uint32_t size) : DX12BufferStorage(context, size, nullptr, true) {}
        void Bind(uint32_t slot = 0) const override { DX12Context::CurrentFor(m_Device).BindUniform(slot, m_Buffer); }
        void SetData(const void* data, uint32_t size) override { Update(data, size); }
    };
}
#endif
