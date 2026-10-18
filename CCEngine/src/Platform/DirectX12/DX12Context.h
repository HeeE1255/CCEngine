#pragma once
#if defined(CC_ENABLE_DX12_RHI)
#include "DX12Resources.h"
#include "DX12Device.h"
#include "Renderer/GraphicsContext.h"
#include "Renderer/RendererAPI.h"
#include <map>
namespace CCEngine
{
    class DX12RendererAPI;
    // 장치/리소스는 공유하고 출력/명령 기록은 창별로 소유한다. 단일 렌더 스레드 계약이다.
    class DX12Context final : public GraphicsContext
    {
    public:
        explicit DX12Context(void* hwnd, bool debug = false, std::shared_ptr<DX12Device> device = {});
        ~DX12Context() override;
        DX12Context(const DX12Context&) = delete;
        DX12Context& operator=(const DX12Context&) = delete;
        void Init() override;
        void MakeCurrent() override;
        static DX12Context& Current();
        static DX12Context& CurrentFor(const std::shared_ptr<DX12Device>& device);
        std::shared_ptr<GraphicsDevice> GetGraphicsDevice() const override { return m_SharedDevice; }
        const std::shared_ptr<DX12Device>& SharedDevice() const { return m_SharedDevice; }
        bool ReadBackBuffer(std::vector<uint8_t>& pixels, uint32_t& width, uint32_t& height) override;
        void SwapBuffers(bool vsync = true) override;
        void ResizeBuffers(uint32_t width, uint32_t height) override;
        void Clear(float r, float g, float b, float a) override;
        void BindBackBuffer() override;
        DX12RendererAPI& Commands();
        RendererAPI* GetCommands() override;
        ID3D12Device* Device() const { return m_Device.Get(); }
        uint64_t NextID() { return m_SharedDevice->NextID(); }
        void Flush();
        std::shared_ptr<DX12BufferAllocation> CreateBuffer(uint32_t bytes, const void* data = nullptr, bool constant = false);
        std::shared_ptr<DX12Image> CreateImage(uint32_t width, uint32_t height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags);
        std::shared_ptr<DX12Target> CreateTarget(uint32_t width, uint32_t height);
        void Upload(const std::shared_ptr<DX12Image>& image, const void* pixels, size_t bytes);
        void Read(const std::shared_ptr<DX12Image>& image, uint32_t x, uint32_t y, uint32_t width, uint32_t height, void* pixels);
        void ClearID(const std::shared_ptr<DX12Target>& target, int32_t value);
        void BindTarget(std::shared_ptr<DX12Target> target);
        bool IsBound(const std::shared_ptr<DX12Target>& target) const { return m_Target == target; }
        void ReleaseTarget(const std::shared_ptr<DX12Target>& target) { if (m_Target == target) m_Target.reset(); }
        void BindVertex(std::shared_ptr<DX12BufferAllocation> buffer, const BufferLayout& layout);
        void BindIndex(std::shared_ptr<DX12BufferAllocation> buffer, uint32_t count);
        void BindUniform(uint32_t slot, std::shared_ptr<DX12BufferAllocation> buffer);
        void BindProgram(std::shared_ptr<DX12Program> program);
        void SetLayout(const BufferLayout& layout) { m_Layout = layout; }
        void ReplaceBuffer(const std::shared_ptr<DX12BufferAllocation>& oldBuffer, const std::shared_ptr<DX12BufferAllocation>& next);
        RendererHandle RegisterTexture(std::shared_ptr<DX12Image> image);
        void UpdateTexture(RendererHandle handle, std::shared_ptr<DX12Image> image);
        void UnregisterTexture(RendererHandle handle);
        void BindTexture(uint32_t slot, RendererHandle handle);
        void Viewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
        void Scissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
        void ScissorEnabled(bool enabled) { m_ScissorEnabled = enabled; }
        void Draw(uint32_t count, RendererAPI::BlendMode blend, RendererAPI::CullMode cull, bool depth);
    private:
        HWND m_Window;
        std::shared_ptr<DX12Device> m_SharedDevice;
        bool m_Recording = false, m_Tearing = false, m_Failed = false;
        ComPtr<IDXGIFactory6> m_Factory;
        ComPtr<ID3D12Device> m_Device;
        ComPtr<ID3D12CommandQueue> m_Queue;
        ComPtr<IDXGISwapChain3> m_Swapchain;
        ComPtr<ID3D12CommandAllocator> m_Allocator;
        ComPtr<ID3D12GraphicsCommandList> m_List;
        ComPtr<ID3D12Fence> m_Fence;
        HANDLE m_FenceEvent = nullptr;
        uint64_t m_FenceValue = 0;
        ComPtr<ID3D12RootSignature> m_Root;
        std::array<std::shared_ptr<DX12Target>, 2> m_Backbuffers;
        std::shared_ptr<DX12Target> m_Target;
        std::shared_ptr<DX12BufferAllocation> m_Vertex, m_Index, m_EmptyUniform;
        std::array<std::shared_ptr<DX12BufferAllocation>, 8> m_Uniforms;
        std::shared_ptr<DX12Program> m_Program;
        std::shared_ptr<DX12Image> m_EmptyTexture;
        BufferLayout m_Layout;
        uint32_t m_IndexCount = 0;
        D3D12_VIEWPORT m_Viewport{};
        D3D12_RECT m_Scissor{};
        bool m_ScissorEnabled = false;
        std::array<RendererHandle, 32> m_Textures{};
        std::vector<std::shared_ptr<void>> m_Pending;
        std::vector<ComPtr<ID3D12DescriptorHeap>> m_Pages;
        uint32_t m_PageUsed = 1024;
        std::map<std::string, ComPtr<ID3D12PipelineState>> m_Pipelines;
        std::unique_ptr<DX12RendererAPI> m_API;
        void Begin();
        void Wait();
        void Transition(const std::shared_ptr<DX12Image>& image, D3D12_RESOURCE_STATES state);
        void ApplyTarget();
        std::shared_ptr<DX12Target> MakeTarget(std::vector<std::shared_ptr<DX12Image>> colors, bool backbuffer);
        ID3D12PipelineState* Pipeline(RendererAPI::BlendMode blend, RendererAPI::CullMode cull, bool depth);
        void BindDescriptors();
    };
}
#endif
