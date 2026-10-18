#include "DX12Device.h"
#if defined(CC_ENABLE_DX12_RHI)
#include "DX12Context.h"
#include <algorithm>
#include <stdexcept>

namespace CCEngine
{
    DX12Device::DX12Device(bool debugEnabled)
    {
        UINT flags = 0;
        if (debugEnabled)
        {
            ComPtr<ID3D12Debug> debug;
            CheckDX12(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)), "DX12 debug layer");
            debug->EnableDebugLayer();
            flags = DXGI_CREATE_FACTORY_DEBUG;
        }
        CheckDX12(CreateDXGIFactory2(flags, IID_PPV_ARGS(&m_Factory)), "DXGI factory");
        for (UINT i = 0;; ++i)
        {
            ComPtr<IDXGIAdapter1> adapter;
            const HRESULT result = m_Factory->EnumAdapters1(i, &adapter);
            if (result == DXGI_ERROR_NOT_FOUND) break;
            CheckDX12(result, "Enumerate DX12 adapter");
            DXGI_ADAPTER_DESC1 desc{};
            CheckDX12(adapter->GetDesc1(&desc), "Adapter description");
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_Device)))) break;
        }
        if (!m_Device) throw std::runtime_error("No hardware DirectX 12 device");
        D3D12_COMMAND_QUEUE_DESC queue{};
        queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        CheckDX12(m_Device->CreateCommandQueue(&queue, IID_PPV_ARGS(&m_Queue)), "Create shared DX12 queue");
    }

    void DX12Device::RequireUsable() const
    {
        if (std::this_thread::get_id() != m_Thread)
            throw std::logic_error("DX12 shared device requires its render thread.");
        if (m_Failed) throw std::runtime_error("DX12 shared device has failed; recreate all windows and resources.");
    }

    uint64_t DX12Device::NextID() { RequireUsable(); return ++m_NextID; }
    void DX12Device::Attach(DX12Context* context)
    {
        RequireUsable();
        if (std::find(m_Contexts.begin(), m_Contexts.end(), context) != m_Contexts.end())
            throw std::logic_error("DX12 context already attached.");
        m_Contexts.push_back(context);
    }
    void DX12Device::Detach(DX12Context* context) noexcept
    {
        FinishRecording(context);
        m_Contexts.erase(std::remove(m_Contexts.begin(), m_Contexts.end(), context), m_Contexts.end());
    }
    void DX12Device::AcquireRecording(DX12Context* context)
    {
        RequireUsable();
        // Image의 State는 공유된다. 다른 창의 미제출 barrier를 먼저 실행해야
        // 다음 창이 그 상태를 시작점으로 사용해도 GPU 실행 순서와 일치한다.
        if (m_RecordingContext && m_RecordingContext != context)
            m_RecordingContext->Flush();
        m_RecordingContext = context;
    }
    void DX12Device::FinishRecording(DX12Context* context) noexcept
    {
        if (m_RecordingContext == context) m_RecordingContext = nullptr;
    }
    void DX12Device::ReplaceBuffer(const std::shared_ptr<DX12BufferAllocation>& oldBuffer,
        const std::shared_ptr<DX12BufferAllocation>& next)
    {
        RequireUsable();
        if (!oldBuffer || !next || oldBuffer->Owner.lock().get() != this || next->Owner.lock().get() != this)
            throw std::invalid_argument("Cannot replace a buffer from another DX12 device.");
        for (auto* context : m_Contexts) context->ReplaceBuffer(oldBuffer, next);
    }
    void DX12Device::ReleaseTarget(const std::shared_ptr<DX12Target>& target) noexcept
    {
        for (auto* context : m_Contexts) context->ReleaseTarget(target);
    }
    RendererHandle DX12Device::RegisterTexture(std::shared_ptr<DX12Image> image)
    {
        RequireUsable();
        if (!image || image->Owner.lock().get() != this || !image->SRV)
            throw std::invalid_argument("Invalid shared DX12 texture.");
        auto token = std::make_unique<TextureToken>();
        token->Image = std::move(image);
        auto handle = static_cast<RendererHandle>(token.get());
        m_Textures.emplace(handle, std::move(token));
        return handle;
    }
    void DX12Device::UpdateTexture(RendererHandle handle, std::shared_ptr<DX12Image> image)
    {
        RequireUsable();
        auto found = m_Textures.find(handle);
        if (found == m_Textures.end() || !found->second->Image || !image ||
            image->Owner.lock().get() != this || !image->SRV)
            throw std::invalid_argument("Invalid shared DX12 texture update.");
        found->second->Image = std::move(image);
    }
    void DX12Device::UnregisterTexture(RendererHandle handle) noexcept
    {
        auto found = m_Textures.find(handle);
        // 토큰 주소는 장치 종료까지 보존한다. 오래된 UI 핸들이 새 텍스처로 재해석되지 않는다.
        if (found != m_Textures.end()) found->second->Image.reset();
    }
    std::shared_ptr<DX12Image> DX12Device::ResolveTexture(RendererHandle handle) const
    {
        RequireUsable();
        auto found = m_Textures.find(handle);
        return found == m_Textures.end() ? nullptr : found->second->Image;
    }
}
#endif
