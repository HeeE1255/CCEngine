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
        virtual void ResizeBuffers(uint32_t width, uint32_t height) override;

        ID3D11Device* GetDevice()
        {
            return m_Device;
        }

        ID3D11DeviceContext* GetDeviceContext()
        {
            return m_DeviceContext;
        }

        HWND GetWindowHandle() const
        {
            return m_hWnd;
        }

        static DX11Context* Get();

    private:
        HWND m_hWnd;

        ID3D11Device* m_Device = nullptr; // DirectX 11 디바이스 객체
        ID3D11DeviceContext* m_DeviceContext = nullptr; // DirectX 11 디바이스 컨텍스트 객체 (렌더링 명령어를 GPU에 전달하는 역할)
        IDXGISwapChain* m_SwapChain = nullptr; // 스왑 체인 객체 (버퍼 스왑을 관리)
        ID3D11RenderTargetView* m_RenderTargetView = nullptr; // 렌더 타겟 뷰 객체 (렌더링 결과를 출력할 버퍼를 나타냄)
    };
}