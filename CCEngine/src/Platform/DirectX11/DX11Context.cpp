#include "DX11Context.h"
#include <iostream>

// 라이브러리 링크 (premake에서 했지만, 코드에서도 명시해주면 더 안전)
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace CCEngine
{
	// 엔진 전역에서 현재 DirectX 11 컨텍스트에 접근하기 위한 정적 포인터
	// 싱글톤 패턴처럼 사용하기 위해 클래스 외부에 정의
	// 정적 변수 초기화
	ID3D11Device* DX11Context::s_Device = nullptr;
	ID3D11DeviceContext* DX11Context::s_DeviceContext = nullptr;
	static DX11Context* s_MainContext = nullptr; // 메인 윈도우 참조용
	DX11Context* DX11Context::s_CurrentContext = nullptr; // 현재 활성화된 컨텍스트를 추적하기 위한 정적 포인터 (멀티윈도우 지원용)

	DX11Context* DX11Context::Get() 
	{
		return s_CurrentContext ? s_CurrentContext : s_MainContext;
	}

	void DX11Context::MakeCurrent()
	{
		s_CurrentContext = this;
	}

	void DX11Context::BindBackBuffer()
	{
		s_DeviceContext->OMSetRenderTargets(1, &m_RenderTargetView, nullptr);

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

			s_DeviceContext->RSSetViewports(1, &viewport);
		}

	}

	DX11Context::DX11Context(HWND hwnd) : m_hWnd(hwnd) 
	{
		if (s_MainContext == nullptr)
		{
			s_MainContext = this;
		}
	}

	DX11Context::~DX11Context() 
	{
		if (m_RenderTargetView != nullptr) m_RenderTargetView->Release();
		if (m_SwapChain != nullptr) m_SwapChain->Release();

		if (this == s_MainContext)
		{
			if (s_DeviceContext != nullptr)
			{
				s_DeviceContext->ClearState();
				s_DeviceContext->Flush();
				s_DeviceContext->Release();
			}
			if (s_Device != nullptr)
			{
				s_Device->Release();
			}
			s_MainContext = nullptr;
		}

		if (this == s_CurrentContext) s_CurrentContext = nullptr;
	}

	void DX11Context::Init()
	{
		HRESULT hr;

		// 1. 디바이스가 아직 없다면 생성 (엔진 실행 후 최초 1회)
		if (s_Device == nullptr)
		{
			UINT createDeviceFlags = 0;
#ifdef CC_DEBUG
			createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
			D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };

			hr = D3D11CreateDevice(
				nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
				featureLevels, 1, D3D11_SDK_VERSION,
				&s_Device, nullptr, &s_DeviceContext
			);

			if (FAILED(hr))
			{
				std::cout << "DirectX 11 Device 생성 실패!" << std::endl;
				return;
			}
		}

		// 2. 스왑 체인 생성을 위한 팩토리 가져오기
		IDXGIFactory* factory = nullptr;
		IDXGIDevice* dxgiDevice = nullptr;
		IDXGIAdapter* dxgiAdapter = nullptr;

		s_Device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
		dxgiDevice->GetParent(__uuidof(IDXGIAdapter), (void**)&dxgiAdapter);
		dxgiAdapter->GetParent(__uuidof(IDXGIFactory), (void**)&factory);

		// 3. 현재 창(m_hWnd)을 위한 스왑 체인 설정
		DXGI_SWAP_CHAIN_DESC scd = {};
		scd.BufferCount = 1;
		scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		scd.OutputWindow = m_hWnd; // 인스턴스마다 다른 HWND 할당
		scd.SampleDesc.Count = 1;
		scd.Windowed = TRUE;

		hr = factory->CreateSwapChain(s_Device, &scd, &m_SwapChain);

		// 사용한 임시 객체 해제
		factory->Release();
		dxgiAdapter->Release();
		dxgiDevice->Release();

		if (FAILED(hr))
		{
			std::cout << "스왑 체인 생성 실패! HWND: " << m_hWnd << std::endl;
			return;
		}

		// 4. 렌더 타겟 뷰(RTV) 생성 (기존 로직과 동일)
		ID3D11Texture2D* backBuffer = nullptr;
		m_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
		s_Device->CreateRenderTargetView(backBuffer, nullptr, &m_RenderTargetView);
		backBuffer->Release();

		std::cout << "DirectX 11 윈도우 컨텍스트 초기화 완료 (HWND: " << m_hWnd << ")" << std::endl;
	}

	void DX11Context::SwapBuffers() 
	{
		// 각 인스턴스의 스왑 체인을 개별적으로 Present
		m_SwapChain->Present(1, 0);
	}

	void DX11Context::Clear(float r, float g, float b, float a)
	{
		float clearColor[4] = { r, g, b, a };
		// 공유된 DeviceContext를 사용하되, 타겟은 자신의 RTV로 설정
		s_DeviceContext->OMSetRenderTargets(1, &m_RenderTargetView, nullptr);
		s_DeviceContext->ClearRenderTargetView(m_RenderTargetView, clearColor);
	}

	void DX11Context::ResizeBuffers(uint32_t width, uint32_t height)
	{
		// 스왑체인이나 디바이스가 없거나, 창이 최소화(0,0) 된 상태면 무시
		if (!m_SwapChain || !s_Device || width == 0 || height == 0) return;

		// ==========================================
		// 1. 기존 렌더 타겟 뷰(RTV) 완벽하게 해제
		// ==========================================
		if (m_RenderTargetView)
		{
			// DX11 파이프라인에서 타겟을 해제
			s_DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);

			m_RenderTargetView->Release();
			m_RenderTargetView = nullptr;
		}

		// ==========================================
		// 2. 스왑체인 버퍼 크기 진짜로 변경
		// ==========================================
		HRESULT hr = m_SwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
		if (FAILED(hr))
		{
			// 리사이즈 실패 시 로그 출력이나 예외 처리
			return;
		}

		// ==========================================
		// 3. 크기가 바뀐 새로운 백버퍼(버퍼) 텍스처를 스왑체인에서 가져오기
		// ==========================================
		ID3D11Texture2D* backBuffer = nullptr;
		hr = m_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
		if (FAILED(hr)) return;

		// ==========================================
		// 4. 가져온 백버퍼로 새로운 렌더 타겟 뷰(RTV) 생성
		// ==========================================
		hr = s_Device->CreateRenderTargetView(backBuffer, nullptr, &m_RenderTargetView);

		backBuffer->Release();

		if (FAILED(hr)) return;

		// ==========================================
		// 5. 완성된 새 렌더 타겟을 다시 DX11 파이프라인(Output Merger)에 장착!
		// ==========================================
		s_DeviceContext->OMSetRenderTargets(1, &m_RenderTargetView, nullptr);
	}


}