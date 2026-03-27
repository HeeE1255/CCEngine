#include "DX11Context.h"
#include <iostream>

// 라이브러리 링크 (premake에서 했지만, 코드에서도 명시해주면 더 안전)
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace CCEngine
{
	// 엔진 전역에서 현재 DirectX 11 컨텍스트에 접근하기 위한 정적 포인터
	// 싱글톤 패턴처럼 사용하기 위해 클래스 외부에 정의
	static DX11Context* s_DX11Context = nullptr;

	DX11Context* DX11Context::Get() 
	{
		return s_DX11Context;
	}

	void DX11Context::Clear(float r, float g, float b, float a)
	{
		float clearColor[4] = { r, g, b, a };
		m_DeviceContext->ClearRenderTargetView(m_RenderTargetView, clearColor);
	}

	DX11Context::DX11Context(HWND hwnd) : m_hWnd(hwnd) 
	{
		//싱글톤 패턴처럼 엔진 전역에서 이 컨텍스트에 접근할 수 있도록 정적 포인터에 자기 자신을 할당
		s_DX11Context = this;
	}

	DX11Context::~DX11Context() 
	{
		//모든 셰이더, 버퍼 강제 해제
		if (m_DeviceContext != nullptr)
		{
			m_DeviceContext->ClearState();
			m_DeviceContext->Flush();
		}

		//메모리 누스 방지를 위해 역순
		if (m_RenderTargetView != nullptr)
		{
			m_RenderTargetView->Release();
		}

		if (m_SwapChain != nullptr)
		{
			m_SwapChain->Release();
		}
		
		if (m_DeviceContext != nullptr)
		{
			m_DeviceContext->Release();
		}
		
		if (m_Device != nullptr) 
		{
			m_Device->Release();
		}
	}

	void DX11Context::Init()
	{
		//스왑 체인 설정
		DXGI_SWAP_CHAIN_DESC scd = {};
		scd.BufferCount = 1;                                // 백 버퍼 1개 (더블 버퍼링)
		scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // 32비트 색상
		scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;  // 렌더 타겟으로 사용
		scd.OutputWindow = m_hWnd;							// 윈도우 핸들 연결
		scd.SampleDesc.Count = 1;                           // 안티앨리어싱 미사용
		scd.Windowed = TRUE;                                // 창 모드

		// 디바이스 및 스왑 체인 생성
		HRESULT hr = D3D11CreateDeviceAndSwapChain(
			nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
			nullptr, 0, D3D11_SDK_VERSION, &scd,
			&m_SwapChain, &m_Device, nullptr, &m_DeviceContext
		);

		if (FAILED(hr)) 
		{
			std::cout << "DirectX 11 초기화 실패!" << std::endl;
			return;
		}

		// 백 버퍼를 가져와서 렌더 타겟 뷰(캔버스) 생성
		ID3D11Texture2D* backBuffer = nullptr;
		m_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);

		if (backBuffer == nullptr)
		{
			std::cout << "백 버퍼 가져오기 실패!" << std::endl;
			return;
		}

		m_Device->CreateRenderTargetView(backBuffer, nullptr, &m_RenderTargetView);
		backBuffer->Release(); // 뷰 생성 후 원본 포인터 해제

		//뷰포트 설정
		RECT clientRect;
		GetClientRect(m_hWnd, &clientRect); // 윈도우 창의 실제 해상도를 가져옴

		D3D11_VIEWPORT viewport = {};
		viewport.TopLeftX = 0.0f;
		viewport.TopLeftY = 0.0f;
		viewport.Width = static_cast<float>(clientRect.right - clientRect.left);
		viewport.Height = static_cast<float>(clientRect.bottom - clientRect.top);
		viewport.MinDepth = 0.0f; // 3D 깊이 최소값
		viewport.MaxDepth = 1.0f; // 3D 깊이 최대값

		// 래스터라이저(Rasterizer)에 뷰포트 설정
		m_DeviceContext->RSSetViewports(1, &viewport);

		// 파이프라인에 렌더 타겟 설정
		m_DeviceContext->OMSetRenderTargets(1, &m_RenderTargetView, nullptr);
		std::cout << "DirectX 11 컨텍스트 생성 완료!" << std::endl;
	}

	void DX11Context::SwapBuffers() 
	{
		// 완성된 백 버퍼를 화면(프론트 버퍼)으로 송출
		m_SwapChain->Present(1, 0); // VSync 켬 (모니터 주사율에 맞춤)
	}

	void DX11Context::ResizeBuffers(uint32_t width, uint32_t height)
	{
		// 스왑체인이나 디바이스가 없거나, 창이 최소화(0,0) 된 상태면 무시합니다.
		if (!m_SwapChain || !m_Device || width == 0 || height == 0) return;

		// ==========================================
		// 1. 기존 렌더 타겟 뷰(RTV) 완벽하게 해제
		// ==========================================
		if (m_RenderTargetView)
		{
			// DX11 파이프라인에서 타겟을 빼버립니다. (안 빼면 리사이즈 실패함)
			m_DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);

			m_RenderTargetView->Release();
			m_RenderTargetView = nullptr;
		}

		// ==========================================
		// 2. 스왑체인 버퍼 크기 진짜로 변경
		// ==========================================
		HRESULT hr = m_SwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
		if (FAILED(hr))
		{
			// 리사이즈 실패 시 로그 출력이나 예외 처리를 해야 합니다.
			return;
		}

		// ==========================================
		// 3. 크기가 바뀐 새로운 백버퍼(도화지) 텍스처를 스왑체인에서 가져오기
		// ==========================================
		ID3D11Texture2D* backBuffer = nullptr;
		hr = m_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
		if (FAILED(hr)) return;

		// ==========================================
		// 4. 가져온 백버퍼로 새로운 렌더 타겟 뷰(RTV) 생성
		// ==========================================
		hr = m_Device->CreateRenderTargetView(backBuffer, nullptr, &m_RenderTargetView);

		// 뷰를 만들었으니 백버퍼 텍스처의 원본 참조 카운트는 깎아줍니다. (메모리 누수 방지!)
		backBuffer->Release();

		if (FAILED(hr)) return;

		// ==========================================
		// 5. 완성된 새 렌더 타겟을 다시 DX11 파이프라인(Output Merger)에 장착!
		// ==========================================
		m_DeviceContext->OMSetRenderTargets(1, &m_RenderTargetView, nullptr);
	}


}