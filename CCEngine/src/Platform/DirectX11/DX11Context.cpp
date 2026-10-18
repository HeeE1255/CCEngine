#include "DX11Context.h"
#include "DX11RendererAPI.h"
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <string>
#include <wrl/client.h>
#include <algorithm>

// 라이브러리 링크 (premake에서 했지만, 코드에서도 명시해주면 더 안전)
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace CCEngine
{
    namespace
    {
        void RequireDX11(HRESULT result, const char* operation)
        {
            if (FAILED(result))
                throw std::runtime_error(std::string(operation) + " failed (HRESULT="
                    + std::to_string(static_cast<unsigned long>(result)) + ").");
        }
    }
	DX11Context* DX11Context::Get() 
	{
		return dynamic_cast<DX11Context*>(GraphicsContext::GetCurrent());
	}

	void DX11Context::MakeCurrent()
	{
		if (!m_Commands || !m_SwapChain)
			throw std::logic_error("DirectX11 context is not initialized.");
		SetCurrentContext();
	}

    RendererAPI* DX11Context::GetCommands() { return m_Commands.get(); }

	void DX11Context::BindBackBuffer()
	{
		if (!m_RenderTargetView)
			throw std::runtime_error("DirectX11 backbuffer is unavailable after resize.");
		GetDeviceContext()->OMSetRenderTargets(1, &m_RenderTargetView, nullptr);

		if (m_hWnd) // m_WindowHandle은 DX11Context가 가지고 있는 HWND 변수입니다.
		{
			RECT rect;
			GetClientRect(m_hWnd, &rect);

			D3D11_VIEWPORT viewport = {};
			viewport.TopLeftX = 0.0f;
			viewport.TopLeftY = 0.0f;
			viewport.Width = static_cast<float>(rect.right - rect.left);
			viewport.Height = static_cast<float>(rect.bottom - rect.top);
			viewport.MinDepth = 0.0f;
			viewport.MaxDepth = 1.0f;

			GetDeviceContext()->RSSetViewports(1, &viewport);
		}

	}

	DX11Context::DX11Context(HWND hwnd, std::shared_ptr<DX11Device> device)
        : m_hWnd(hwnd), m_Device(std::move(device))
	{
		if (!m_Device)
			throw std::invalid_argument("DirectX11 context requires a shared device.");
	}

	DX11Context::~DX11Context() 
	{
		if (m_RenderTargetView != nullptr) m_RenderTargetView->Release();
		if (m_SwapChain != nullptr) m_SwapChain->Release();

		// 공유 장치는 m_Device의 마지막 소유자가 놓을 때 해제된다.
	}

	void DX11Context::Init()
	{
		HRESULT hr;

		if (m_Commands)
			throw std::logic_error("DirectX11 context is already initialized.");

		// 2. 스왑 체인 생성을 위한 팩토리 가져오기
		// 중간 단계 실패도 임시 COM 참조가 자동 해제되도록 소유권을 지역 변수에 둔다.
		Microsoft::WRL::ComPtr<IDXGIFactory> factory;
		Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
		Microsoft::WRL::ComPtr<IDXGIAdapter> dxgiAdapter;
		RequireDX11(GetDevice()->QueryInterface(IID_PPV_ARGS(dxgiDevice.GetAddressOf())), "Query DXGI device");
		RequireDX11(dxgiDevice->GetParent(IID_PPV_ARGS(dxgiAdapter.GetAddressOf())), "Get DXGI adapter");
		RequireDX11(dxgiAdapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf())), "Get DXGI factory");

		// 3. 현재 창(m_hWnd)을 위한 스왑 체인 설정
		DXGI_SWAP_CHAIN_DESC scd = {};
		scd.BufferCount = 1;
		scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		scd.OutputWindow = m_hWnd; // 인스턴스마다 다른 HWND 할당
		scd.SampleDesc.Count = 1;
		scd.Windowed = TRUE;

		hr = factory->CreateSwapChain(GetDevice(), &scd, &m_SwapChain);

		RequireDX11(hr, "CreateSwapChain");

		// 4. 렌더 타겟 뷰(RTV) 생성 (기존 로직과 동일)
		Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
		RequireDX11(m_SwapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf())), "Get swapchain buffer");
		RequireDX11(GetDevice()->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_RenderTargetView), "Create backbuffer view");

		m_Commands = std::make_unique<DX11RendererAPI>(*this);
		MakeCurrent();
		std::cout << "DirectX 11 윈도우 컨텍스트 초기화 완료 (HWND: " << m_hWnd << ")" << std::endl;
	}

	void DX11Context::SwapBuffers(bool waitForVSync)
	{
		// 메인 창은 v-sync로 기준 프레임을 잡고, 보조 창은 즉시 표시한다.
		// 여러 OS 창이 모두 Present(1)을 쓰면 한 프레임 안에서 대기점이 여러 번 생긴다.
		m_SwapChain->Present(waitForVSync ? 1 : 0, 0);
	}

	void DX11Context::Clear(float r, float g, float b, float a)
	{
		float clearColor[4] = { r, g, b, a };
		// 공유된 DeviceContext를 사용하되, 타겟은 자신의 RTV로 설정
		GetDeviceContext()->OMSetRenderTargets(1, &m_RenderTargetView, nullptr);
		GetDeviceContext()->ClearRenderTargetView(m_RenderTargetView, clearColor);
	}

    void DX11Context::ResizeBuffers(uint32_t width, uint32_t height)
    {
        if (!m_SwapChain || width == 0 || height == 0)
            return;

        // 보조 창의 WM_SIZE가 다른 창/피킹 MRT의 출력 바인딩을 덮어쓰면 안 된다.
        constexpr UINT count = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
        ID3D11RenderTargetView* views[count]{};
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> saved[count];
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth;
        bool replace[count]{};
        auto* immediate = GetDeviceContext();
        immediate->OMGetRenderTargets(count, views, depth.GetAddressOf());
        for (UINT i = 0; i < count; ++i)
        {
            replace[i] = m_RenderTargetView && views[i] == m_RenderTargetView;
            saved[i].Attach(views[i]);
            // ResizeBuffers 전에 이 창의 이전 backbuffer를 붙잡는 참조는 모두 놓는다.
            if (replace[i])
                saved[i].Reset();
        }
        immediate->OMSetRenderTargets(0, nullptr, nullptr);
        if (m_RenderTargetView)
        {
            m_RenderTargetView->Release();
            m_RenderTargetView = nullptr;
        }

        const HRESULT resized = m_SwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        // Resize 실패 시에도 기존 backbuffer에서 RTV를 복구한다.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> buffer;
        HRESULT restored = m_SwapChain->GetBuffer(0, IID_PPV_ARGS(buffer.GetAddressOf()));
        if (SUCCEEDED(restored))
            restored = GetDevice()->CreateRenderTargetView(buffer.Get(), nullptr, &m_RenderTargetView);

        for (UINT i = 0; i < count; ++i)
            views[i] = replace[i] ? m_RenderTargetView : saved[i].Get();
        immediate->OMSetRenderTargets(count, views, depth.Get());
        // Win32 메시지 콜백 밖으로 예외를 넘기지 않는다. 복구 실패는 다음 프레임에서 처리한다.
        if (FAILED(resized) || FAILED(restored))
            std::cerr << "DX11 resize failed: " << resized << ", target restore: " << restored << std::endl;
    }

	bool DX11Context::ReadBackBuffer(std::vector<uint8_t>& rgbaPixels, uint32_t& width, uint32_t& height)
	{
		rgbaPixels.clear();
		width = 0;
		height = 0;
		if (!m_SwapChain || !GetDevice() || !GetDeviceContext())
			return false;

		ID3D11Texture2D* backBuffer = nullptr;
		if (FAILED(m_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer))) || !backBuffer)
			return false;

		D3D11_TEXTURE2D_DESC sourceDesc{};
		backBuffer->GetDesc(&sourceDesc);
		D3D11_TEXTURE2D_DESC stagingDesc = sourceDesc;
		stagingDesc.Usage = D3D11_USAGE_STAGING;
		stagingDesc.BindFlags = 0;
		stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		stagingDesc.MiscFlags = 0;

		ID3D11Texture2D* staging = nullptr;
		const HRESULT createResult = GetDevice()->CreateTexture2D(&stagingDesc, nullptr, &staging);
		if (FAILED(createResult) || !staging)
		{
			backBuffer->Release();
			return false;
		}

		GetDeviceContext()->CopyResource(staging, backBuffer);
		D3D11_MAPPED_SUBRESOURCE mapped{};
		const HRESULT mapResult = GetDeviceContext()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
		if (SUCCEEDED(mapResult))
		{
			width = sourceDesc.Width;
			height = sourceDesc.Height;
			rgbaPixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
			for (uint32_t y = 0; y < height; ++y)
			{
				const uint8_t* source = static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
				uint8_t* destination = rgbaPixels.data() + static_cast<size_t>(y) * width * 4u;
				std::memcpy(destination, source, static_cast<size_t>(width) * 4u);
			}
			GetDeviceContext()->Unmap(staging, 0);
		}

		staging->Release();
		backBuffer->Release();
		return SUCCEEDED(mapResult) && !rgbaPixels.empty();
	}


}
