#pragma once
#if defined(CC_ENABLE_DX12_RHI)
#include "DX12Resources.h"
#include "Renderer/GraphicsDevice.h"
#include "Renderer/RendererHandle.h"
#include <map>
#include <thread>

namespace CCEngine
{
    class DX12Device final : public GraphicsDevice
    {
    public:
        explicit DX12Device(bool debug = false);
        RendererAPI::API GetBackend() const override { return RendererAPI::API::DirectX12; }
        ID3D12Device* Native() const { return m_Device.Get(); }
        IDXGIFactory6* Factory() const { return m_Factory.Get(); }
        ID3D12CommandQueue* Queue() const { return m_Queue.Get(); }
        void RequireUsable() const;
        void MarkFailed() noexcept { m_Failed = true; }
        uint64_t NextID();
        void Attach(DX12Context* context);
        void Detach(DX12Context* context) noexcept;
        void AcquireRecording(DX12Context* context);
        void FinishRecording(DX12Context* context) noexcept;
        void ReplaceBuffer(const std::shared_ptr<DX12BufferAllocation>& oldBuffer,
            const std::shared_ptr<DX12BufferAllocation>& next);
        void ReleaseTarget(const std::shared_ptr<DX12Target>& target) noexcept;
        RendererHandle RegisterTexture(std::shared_ptr<DX12Image> image);
        void UpdateTexture(RendererHandle handle, std::shared_ptr<DX12Image> image);
        void UnregisterTexture(RendererHandle handle) noexcept;
        std::shared_ptr<DX12Image> ResolveTexture(RendererHandle handle) const;
    private:
        ComPtr<IDXGIFactory6> m_Factory;
        ComPtr<ID3D12Device> m_Device;
        ComPtr<ID3D12CommandQueue> m_Queue;
        const std::thread::id m_Thread = std::this_thread::get_id();
        bool m_Failed = false;
        uint64_t m_NextID = 0;
        DX12Context* m_RecordingContext = nullptr;
        std::vector<DX12Context*> m_Contexts;
        struct TextureToken { std::shared_ptr<DX12Image> Image; };
        std::map<RendererHandle, std::unique_ptr<TextureToken>> m_Textures;
    };
}
#endif
