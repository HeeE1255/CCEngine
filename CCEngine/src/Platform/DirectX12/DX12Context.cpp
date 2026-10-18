#include "DX12Context.h"
#if defined(CC_ENABLE_DX12_RHI)
#include "DX12RendererAPI.h"
#include <d3dcompiler.h>
#include <stdexcept>
#include <algorithm>
namespace CCEngine
{
    void CheckDX12(HRESULT result, const char* operation)
    {
        if (FAILED(result)) throw std::runtime_error(std::string(operation) + ": HRESULT=" + std::to_string(static_cast<uint32_t>(result)));
    }
    DX12Context::DX12Context(void* hwnd, bool debug, std::shared_ptr<DX12Device> device)
        : m_Window(static_cast<HWND>(hwnd)), m_SharedDevice(device ? std::move(device) : std::make_shared<DX12Device>(debug)) {}
    DX12Context::~DX12Context()
    {
        // GPU 장애 시 소멸자에서 예외를 던지지 않는다. 정상 종료에서는 마지막 제출까지 기다린다.
        try { if (m_Queue && m_Fence && m_FenceEvent && !m_Failed) Flush(); }
        catch (...) { m_Failed = true; }
        if (m_FenceEvent) CloseHandle(m_FenceEvent);
        m_SharedDevice->Detach(this);
    }
    DX12RendererAPI& DX12Context::Commands() { return *m_API; }
    RendererAPI* DX12Context::GetCommands() { return m_API.get(); }
    DX12Context& DX12Context::Current()
    {
        auto* context = dynamic_cast<DX12Context*>(GraphicsContext::GetCurrent());
        if (!context) throw std::logic_error("No current DX12 context");
        context->m_SharedDevice->RequireUsable();
        return *context;
    }
    DX12Context& DX12Context::CurrentFor(const std::shared_ptr<DX12Device>& device)
    {
        auto& context = Current();
        if (context.m_SharedDevice != device)
            throw std::logic_error("DX12 resource belongs to another device.");
        return context;
    }
    void DX12Context::MakeCurrent()
    {
        if (!m_API || m_Failed) throw std::logic_error("DX12 context unavailable");
        m_SharedDevice->AcquireRecording(this);
        SetCurrentContext();
    }
    void DX12Context::Init()
    {
        if (m_Device || m_Failed || !IsWindow(m_Window)) throw std::logic_error("Invalid DX12 initialization");
        try
        {
            m_SharedDevice->RequireUsable();
            m_Factory = m_SharedDevice->Factory();
            m_Device = m_SharedDevice->Native();
            m_Queue = m_SharedDevice->Queue();
            m_SharedDevice->Attach(this);
            D3D12_COMMAND_QUEUE_DESC queue{}; queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            CheckDX12(m_Device->CreateCommandAllocator(queue.Type, IID_PPV_ARGS(&m_Allocator)), "Create DX12 allocator");
            CheckDX12(m_Device->CreateCommandList(0, queue.Type, m_Allocator.Get(), nullptr, IID_PPV_ARGS(&m_List)), "Create DX12 list");
            CheckDX12(m_List->Close(), "Close initial DX12 list");
            CheckDX12(m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence)), "Create DX12 fence");
            m_FenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!m_FenceEvent) throw std::runtime_error("Create DX12 fence event failed");
            BOOL tearing = FALSE;
            m_Tearing = SUCCEEDED(m_Factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))) && tearing;
            D3D12_ROOT_PARAMETER parameters[9]{};
            for (UINT i = 0; i < 8; ++i)
            {
                parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                parameters[i].Descriptor.ShaderRegister = i;
                parameters[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            }
            D3D12_DESCRIPTOR_RANGE range{};
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; range.NumDescriptors = 32;
            parameters[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[8].DescriptorTable = { 1, &range }; parameters[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            D3D12_STATIC_SAMPLER_DESC samplers[2]{};
            for (UINT i = 0; i < 2; ++i)
            {
                samplers[i].Filter = i ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
                samplers[i].AddressU = samplers[i].AddressV = samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                samplers[i].MaxAnisotropy = 1; samplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
                samplers[i].MaxLOD = D3D12_FLOAT32_MAX; samplers[i].ShaderRegister = i;
                samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            }
            // HLSL 계약: b0~b7, t0~t31, s0(linear), s1(point). 정수 ID 텍스처는 point sampler를 쓴다.
            D3D12_ROOT_SIGNATURE_DESC signature{};
            signature.NumParameters = 9; signature.pParameters = parameters;
            signature.NumStaticSamplers = 2; signature.pStaticSamplers = samplers;
            signature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
            ComPtr<ID3DBlob> blob, error;
            const HRESULT serialized = D3D12SerializeRootSignature(&signature, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
            if (FAILED(serialized) && error) throw std::runtime_error(std::string(static_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize()));
            CheckDX12(serialized, "Serialize DX12 root signature");
            CheckDX12(m_Device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_Root)), "Create root signature");
            m_EmptyUniform = CreateBuffer(65536, nullptr, true);
            m_EmptyTexture = CreateImage(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE);
            const uint32_t zero = 0; Upload(m_EmptyTexture, &zero, sizeof(zero)); Flush();
            m_API = std::make_unique<DX12RendererAPI>(*this);
            MakeCurrent(); RECT client{};
            if (!GetClientRect(m_Window, &client)) throw std::runtime_error("Read DX12 client size failed");
            ResizeBuffers(client.right, client.bottom);
        }
        catch (...)
        {
            // 보조 창의 초기화 실패만으로 정상인 공유 장치까지 폐기하지 않는다.
            // 이미 기록한 전이는 제출해 상태를 맞추고 GPU 자체 실패만 전체에 전파한다.
            try { if (m_Recording) Flush(); } catch (...) { m_SharedDevice->MarkFailed(); }
            if (m_Device && FAILED(m_Device->GetDeviceRemovedReason())) m_SharedDevice->MarkFailed();
            m_Failed = true;
            throw;
        }
    }
    void DX12Context::Begin()
    {
        if (m_Failed) throw std::logic_error("DX12 context is in a failed state");
        if (!m_Allocator || !m_List) throw std::logic_error("DX12 context is not initialized");
        m_SharedDevice->RequireUsable();
        if (m_Recording) return;
        m_SharedDevice->AcquireRecording(this);
        CheckDX12(m_Allocator->Reset(), "Reset DX12 allocator");
        CheckDX12(m_List->Reset(m_Allocator.Get(), nullptr), "Reset DX12 list");
        m_Recording = true;
    }
    void DX12Context::Wait()
    {
        CheckDX12(m_Queue->Signal(m_Fence.Get(), ++m_FenceValue), "Signal DX12 fence");
        CheckDX12(m_Fence->SetEventOnCompletion(m_FenceValue, m_FenceEvent), "Set DX12 fence event");
        // 드라이버 장애에서 무한 대기하지 않는다. timeout 뒤에는 이 Context를 재사용하지 않는다.
        const DWORD result = WaitForSingleObject(m_FenceEvent, 30000);
        if (result != WAIT_OBJECT_0 || m_Fence->GetCompletedValue() == UINT64_MAX)
        {
            m_Failed = true;
            m_SharedDevice->MarkFailed();
            throw std::runtime_error("DX12 GPU wait failed or timed out; recreate context");
        }
    }
    void DX12Context::Flush()
    {
        if (!m_Recording) return;
        try
        {
            m_SharedDevice->RequireUsable();
            CheckDX12(m_List->Close(), "Close DX12 commands");
            ID3D12CommandList* lists[] = { m_List.Get() }; m_Queue->ExecuteCommandLists(1, lists);
            Wait(); m_Recording = false;
            m_SharedDevice->FinishRecording(this);
            m_Pending.clear(); m_Pages.clear(); m_PageUsed = 1024;
        }
        catch (...) { m_Failed = true; m_SharedDevice->MarkFailed(); throw; }
    }
    void DX12Context::ResizeBuffers(uint32_t width, uint32_t height)
    {
        m_SharedDevice->RequireUsable();
        if (m_Failed || !m_Device || !m_Queue || !m_FenceEvent) throw std::logic_error("DX12 context unavailable for resize");
        if (width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION || height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION)
            throw std::out_of_range("DX12 swapchain size");
        Flush();
        if (m_Swapchain) Wait();
        if (m_Target && m_Target->Backbuffer) m_Target.reset();
        m_Backbuffers = {};
        if (!width || !height) return;
        const UINT flags = m_Tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
        if (m_Swapchain) CheckDX12(m_Swapchain->ResizeBuffers(2, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, flags), "Resize DX12 swapchain");
        else
        {
            DXGI_SWAP_CHAIN_DESC1 desc{};
            desc.Width = width; desc.Height = height; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = 2; desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; desc.Flags = flags;
            ComPtr<IDXGISwapChain1> swapchain;
            CheckDX12(m_Factory->CreateSwapChainForHwnd(m_Queue.Get(), m_Window, &desc, nullptr, nullptr, &swapchain), "Create DX12 swapchain");
            CheckDX12(swapchain.As(&m_Swapchain), "Query DX12 swapchain");
            CheckDX12(m_Factory->MakeWindowAssociation(m_Window, DXGI_MWA_NO_ALT_ENTER), "DXGI window association");
        }
        for (UINT i = 0; i < 2; ++i)
        {
            auto image = std::make_shared<DX12Image>(); image->Owner = m_SharedDevice;
            image->Width = width; image->Height = height; image->Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            image->State = D3D12_RESOURCE_STATE_PRESENT;
            CheckDX12(m_Swapchain->GetBuffer(i, IID_PPV_ARGS(&image->Resource)), "Get DX12 backbuffer");
            m_Backbuffers[i] = MakeTarget({ image }, true);
        }
    }
    void DX12Context::BindBackBuffer()
    {
        MakeCurrent();
        if (!m_Swapchain || !m_Backbuffers[0]) { m_Target.reset(); return; }
        BindTarget(m_Backbuffers[m_Swapchain->GetCurrentBackBufferIndex()]);
    }
    void DX12Context::SwapBuffers(bool vsync)
    {
        if (!m_Swapchain || !m_Backbuffers[0]) { Flush(); return; }
        auto backbuffer = m_Backbuffers[m_Swapchain->GetCurrentBackBufferIndex()];
        Transition(backbuffer->Colors[0], D3D12_RESOURCE_STATE_PRESENT);
        Flush();
        const HRESULT result = m_Swapchain->Present(vsync ? 1 : 0, !vsync && m_Tearing ? DXGI_PRESENT_ALLOW_TEARING : 0);
        if (FAILED(result)) { m_Failed = true; m_SharedDevice->MarkFailed(); CheckDX12(result, "Present DX12 swapchain"); }
        if (m_Target && m_Target->Backbuffer) m_Target.reset();
    }
    bool DX12Context::ReadBackBuffer(std::vector<uint8_t>& pixels, uint32_t& width, uint32_t& height)
    {
        pixels.clear(); width = height = 0;
        if (!m_Swapchain || !m_Backbuffers[0] || m_Failed) return false;
        try
        {
            auto image = m_Backbuffers[m_Swapchain->GetCurrentBackBufferIndex()]->Colors[0];
            std::vector<uint8_t> result(size_t(image->Width) * image->Height * 4);
            Read(image, 0, 0, image->Width, image->Height, result.data());
            width = image->Width; height = image->Height; pixels.swap(result);
            return true;
        }
        catch (...) { return false; }
    }
    void DX12Context::Transition(const std::shared_ptr<DX12Image>& image, D3D12_RESOURCE_STATES state)
    {
        if (!image || image->Owner.lock() != m_SharedDevice) throw std::invalid_argument("Foreign DX12 image");
        Begin(); m_Pending.push_back(image);
        if (image->State == state) return;
        D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = { image->Resource.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, image->State, state };
        m_List->ResourceBarrier(1, &barrier); image->State = state;
    }
    void DX12Context::BindTarget(std::shared_ptr<DX12Target> target)
    {
        if (!target || target->Owner.lock() != m_SharedDevice || (target->Backbuffer && target->WindowOwner != this)) throw std::invalid_argument("Foreign DX12 target");
        if (m_Target && m_Target != target && !m_Target->Backbuffer)
            for (const auto& image : m_Target->Colors) Transition(image, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        m_Target = std::move(target); m_Textures.fill(nullptr);
        Viewport(0, 0, m_Target->Width, m_Target->Height);
        Scissor(0, 0, m_Target->Width, m_Target->Height); m_ScissorEnabled = false;
        ApplyTarget();
    }
    void DX12Context::ApplyTarget()
    {
        if (!m_Target) throw std::logic_error("Bind DX12 target before rendering");
        for (auto& image : m_Target->Colors) Transition(image, D3D12_RESOURCE_STATE_RENDER_TARGET);
        Transition(m_Target->Depth, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        m_Pending.push_back(m_Target);
        const auto rtv = m_Target->RTV->GetCPUDescriptorHandleForHeapStart();
        const auto dsv = m_Target->DSV->GetCPUDescriptorHandleForHeapStart();
        m_List->OMSetRenderTargets(static_cast<UINT>(m_Target->Colors.size()), &rtv, TRUE, &dsv);
    }
    void DX12Context::Clear(float r, float g, float b, float a)
    {
        ApplyTarget(); const float color[] = { r, g, b, a };
        m_List->ClearRenderTargetView(m_Target->RTV->GetCPUDescriptorHandleForHeapStart(), color, 0, nullptr);
        m_List->ClearDepthStencilView(m_Target->DSV->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
    }
    void DX12Context::Viewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        if (!m_Target || x > m_Target->Width || y > m_Target->Height || width > m_Target->Width - x || height > m_Target->Height - y)
            throw std::out_of_range("DX12 viewport outside target");
        m_Viewport = { float(x), float(y), float(width), float(height), 0, 1 };
    }
    void DX12Context::Scissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        if (!m_Target) throw std::logic_error("No DX12 scissor target");
        x = std::min(x, m_Target->Width); y = std::min(y, m_Target->Height);
        m_Scissor = { LONG(x), LONG(y), LONG(x + std::min(width, m_Target->Width - x)), LONG(y + std::min(height, m_Target->Height - y)) };
    }
}
#endif
