#include "WindowsWindow.h"
#include "Platform/DirectX11/DX11Context.h"
#include "Application.h" 
#include "Core/ApplicationEvent.h"
#include <iostream>
#include <windows.h>
#include "imgui.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace CCEngine
{ 
	//윈도우 프로시저 (이벤트 처리기)
	LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
		{
			return true;
		}

		switch (message)
		{
			case WM_SIZE:
			{
				if (wParam == SIZE_MINIMIZED)
					return 0;

				UINT width = LOWORD(lParam);
				UINT height = HIWORD(lParam);

				CCEngine::WindowResizeEvent e(width, height);

				if (CCEngine::Application::Get()) // 앱이 살아있다면
				{
					CCEngine::Application::Get()->OnWindowResize(e);
				}

				return 0;
			}
			break;

			case WM_CLOSE:
				PostQuitMessage(0);
				return 0;
			break;

			case WM_DESTROY:
				PostQuitMessage(0);
				return 0;
			break;
		}
		return DefWindowProc(hWnd, message, wParam, lParam);
	}



	WindowsWindow::WindowsWindow(const WindowProps& props)
	{
		Init(props);
	}

	WindowsWindow::~WindowsWindow()
	{
		Shutdown();
	}

	void WindowsWindow::Init(const WindowProps& props)
	{
		m_Data.Title = props.Title;
		m_Data.Width = props.Width;
		m_Data.Height = props.Height;

		std::cout << "윈도우 생성 : " << props.Title << " (" << props.Width << "x" << props.Height << ")" << std::endl;

		//윈도우 클래스 등록
		WNDCLASSEX wc = { 0 };
		wc.cbSize = sizeof(WNDCLASSEX);
		wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC; //가로,세로 크기 변경시 다시 그리기, 해당 창 전용의 Device Context 할당
		wc.lpfnWndProc = WndProc; //윈도우 프로시저 설정
		wc.hInstance = GetModuleHandle(nullptr); //현재 모듈의 인스턴스 핸들
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW); //기본 화살표 커서
		wc.lpszClassName = L"CCEngineWindowClass"; //윈도우 클래스 이름

		RegisterClassEx(&wc);

		//윈도우 생성
		std::wstring titleWide(m_Data.Title.begin(), m_Data.Title.end());

		m_Window = CreateWindowEx(
			0, //확장 스타일
			wc.lpszClassName, //윈도우 클래스 이름
			titleWide.c_str(), //윈도우 제목
			WS_OVERLAPPEDWINDOW, //윈도우 스타일
			CW_USEDEFAULT, CW_USEDEFAULT, m_Data.Width, m_Data.Height, //위치와 크기
			nullptr, nullptr, wc.hInstance, nullptr); //부모 창, 메뉴, 인스턴스 핸들, 추가 매개변수

		//윈도우 표시
		ShowWindow(m_Window, SW_SHOW);

		m_Context = new DX11Context(m_Window); // 윈도우 핸들(HWND)을 넘겨줌
		m_Context->Init();                     // DirectX 초기화 실행!
	}

	void WindowsWindow::Shutdown()
	{
		DestroyWindow(m_Window);

		if (m_Context != nullptr)
		{
			delete m_Context;
			m_Context = nullptr;
		}
	}

	void WindowsWindow::OnUpdate()
	{
		// 메시지 펌프: 윈도우 이벤트를 가져와서 처리함
		MSG msg;
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT) // WM_QUIT 메시지가 오면 창을 닫아야 함
			{
				m_ShouldClose = true;
			}

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

	}

}