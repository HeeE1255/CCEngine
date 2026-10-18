#pragma once
#if defined(CC_ENABLE_DX12_RHI)
#include "DX12Context.h"
#include "Renderer/Texture.h"
namespace CCEngine
{
    class DX12Texture2D final : public Texture2D
    {
    public:
        explicit DX12Texture2D(const std::string& path);
        DX12Texture2D(uint32_t width, uint32_t height);
        DX12Texture2D(DX12Context& context, uint32_t width, uint32_t height, const void* pixels = nullptr);
        ~DX12Texture2D() override;
        DX12Texture2D(const DX12Texture2D&) = delete;
        DX12Texture2D& operator=(const DX12Texture2D&) = delete;
        void Bind(uint32_t slot = 0) const override { DX12Context::CurrentFor(m_Device).BindTexture(slot, m_Handle); }
        void Unbind(uint32_t slot = 0) const override { DX12Context::CurrentFor(m_Device).BindTexture(slot, nullptr); }
        void SetData(void* data, uint32_t size) override;
        RendererHandle GetRendererID() const override { return m_Handle; }
    private:
        std::shared_ptr<DX12Device> m_Device;
        uint32_t m_Width = 0, m_Height = 0;
        RendererHandle m_Handle = nullptr;
        void Replace(const void* data, uint32_t size);
    };
}
#endif
