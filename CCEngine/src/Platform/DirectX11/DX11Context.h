#pragma once
#include "Renderer/GraphicsContext.h"
#include "Core.h"
#include <d3d11.h>
#include <Windows.h>
#include <cstdint>

namespace CCEngine
{
    class CC_API DX11Context : public GraphicsContext
    {
    public:
        DX11Context(HWND windowHandle);
        virtual ~DX11Context();

        virtual void Init() override;
        virtual void SwapBuffers() override;
        virtual void Clear(float r, float g, float b, float a) override;


        virtual void MakeCurrent() override;
        virtual void BindBackBuffer() override;

        virtual void ResizeBuffers(uint32_t width, uint32_t height) override;

        // Get/Set은 한 줄로 작성
        ID3D11Device* GetDevice() { return s_Device; }
        ID3D11DeviceContext* GetDeviceContext() { return s_DeviceContext; }
        HWND GetWindowHandle() const { return m_hWnd; }
        ID3D11RenderTargetView* GetBackBufferRTV() { return m_RenderTargetView; }
		IDXGISwapChain* GetSwapChain() { return m_SwapChain; }

        static DX11Context* Get();

    private:
        HWND m_hWnd;

        static ID3D11Device* s_Device; // DirectX 11 디바이스 객체
        static ID3D11DeviceContext* s_DeviceContext; // DirectX 11 디바이스 컨텍스트 객체 (렌더링 명령어를 GPU에 전달하는 역할)
        static DX11Context* s_CurrentContext; // 현재 활성화된 컨텍스트를 추적하기 위한 정적 포인터 (멀티윈도우 지원용)
        IDXGISwapChain* m_SwapChain = nullptr; // 스왑 체인 객체 (버퍼 스왑을 관리)
        ID3D11RenderTargetView* m_RenderTargetView = nullptr; // 렌더 타겟 뷰 객체 (렌더링 결과를 출력할 버퍼를 나타냄)
    };
}