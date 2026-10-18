#include "DX12Buffer.h"
#if defined(CC_ENABLE_DX12_RHI)
#include <cstring>
#include <stdexcept>
namespace CCEngine
{
    namespace
    {
        uint32_t IndexBytes(const uint32_t* data, uint32_t count)
        {
            if (!data || !count || count > UINT32_MAX / 4) throw std::invalid_argument("Invalid DX12 index data");
            return count * 4;
        }
    }
    DX12BufferStorage::DX12BufferStorage(DX12Context& context, uint32_t size, const void* data, bool constant)
        : m_Device(context.SharedDevice()), m_Constant(constant)
    {
        if (!size || (constant && size > 65536)) throw std::invalid_argument("Invalid DX12 buffer size");
        m_Shadow.resize(size, 0);
        if (data) std::memcpy(m_Shadow.data(), data, size);
        m_Buffer = context.CreateBuffer(size, m_Shadow.data(), constant);
    }
    void DX12BufferStorage::Update(const void* data, uint32_t size)
    {
        if (size > m_Shadow.size() || (size && !data)) throw std::invalid_argument("DX12 buffer update exceeds capacity");
        if (!size) return;
        auto nextData = m_Shadow; std::memcpy(nextData.data(), data, size);
        // 이미 기록된 draw의 값은 유지한다. 새 메모리로 교체하고 이전 할당은 제출 fence까지 보관한다.
        auto& context = DX12Context::CurrentFor(m_Device);
        auto next = context.CreateBuffer(static_cast<uint32_t>(nextData.size()), nextData.data(), m_Constant);
        m_Device->ReplaceBuffer(m_Buffer, next); m_Buffer = std::move(next); m_Shadow.swap(nextData);
    }
    DX12IndexBuffer::DX12IndexBuffer(DX12Context& context, const uint32_t* indices, uint32_t count)
        : DX12BufferStorage(context, IndexBytes(indices, count), indices), m_Count(count) {}
}
#endif
