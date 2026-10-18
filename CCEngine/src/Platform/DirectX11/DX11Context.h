#pragma once
#include "Renderer/GraphicsContext.h"
#include "DX11Device.h"
#include "Core.h"
#include <d3d11.h>
#include <Windows.h>
#include <cstdint>
#include <vector>

namespace CCEngine
{
    class DX11RendererAPI;
    class CC_API DX11Context : public GraphicsContext
    {
    public:
        DX11Context(HWND windowHandle, std::shared_ptr<DX11Device> device);
        virtual ~DX11Context();

        virtual void Init() override;
        virtual void SwapBuffers(bool waitForVSync = true) override;
        virtual void Clear(float r, float g, float b, float a) override;


        virtual void MakeCurrent() override;
        virtual void BindBackBuffer() override;
        RendererAPI* GetCommands() override;
        std::shared_ptr<GraphicsDevice> GetGraphicsDevice() const override { return m_Device; }

        virtual void ResizeBuffers(uint32_t width, uint32_t height) override;

        bool ReadBackBuffer(std::vector<uint8_t>& rgbaPixels, uint32_t& width, uint32_t& height) override;

        // Get/Set은 한 줄로 작성
        ID3D11Device* GetDevice() { return m_Device->Native(); }
        ID3D11DeviceContext* GetDeviceContext() { return m_Device->Immediate(); }
        HWND GetWindowHandle() const { return m_hWnd; }
        ID3D11RenderTargetView* GetBackBufferRTV() { return m_RenderTargetView; }
		IDXGISwapChain* GetSwapChain() { return m_SwapChain; }

        static DX11Context* Get();

    private:
        HWND m_hWnd;

        std::shared_ptr<DX11Device> m_Device;
        std::unique_ptr<DX11RendererAPI> m_Commands;
        IDXGISwapChain* m_SwapChain = nullptr; // 스왑 체인 객체 (버퍼 스왑을 관리)
        ID3D11RenderTargetView* m_RenderTargetView = nullptr; // 렌더 타겟 뷰 객체 (렌더링 결과를 출력할 버퍼를 나타냄)
    };
}
