#include "WindowsWindow.h"
#include "Renderer/GraphicsContext.h"
#include "Application.h" 
#include "Core/ApplicationEvent.h"
#include "UI/Widget.h"
#include "Events/MouseEvent.h"
#include <iostream>
#include <windows.h>
#include <windowsx.h>
#include <utility>
//#include "imgui.h"

//extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace CCEngine
{ 
	LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		WindowsWindow* window = reinterpret_cast<WindowsWindow*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

		switch (message)
		{
		case WM_SIZE:
		{
			if (wParam == SIZE_MINIMIZED) return 0;

			UINT width = LOWORD(lParam);
			UINT height = HIWORD(lParam);

			if (window)
			{
				window->SetWidth(width);
				window->SetHeight(height);

				// ✅ [RHI 추상화] DX11Context 캐스팅 없이 인터페이스로 리사이즈 호출
				if (window->GetContext())
				{
					window->GetContext()->ResizeBuffers(width, height);
				}

				// ✅ 메인 창 여부를 Application에게 물어보거나 Window 객체 자체의 상태로 판단
				// 여기서는 간단하게 Application의 메인 윈도우와 주소를 비교합니다.
				if (CCEngine::Application::Get() &&
					window == &(CCEngine::Application::Get()->GetWindow()))
				{
					CCEngine::WindowResizeEvent e(width, height);
					CCEngine::Application::Get()->OnWindowResize(e);
				}
			}
			return 0;
		}
		break;

		case WM_CLOSE:
		{
			if (CCEngine::Application::Get() &&
				window == &(CCEngine::Application::Get()->GetWindow()))
			{
				PostQuitMessage(0);
			}
			else
			{
				if (window)
				{
					window->SetShouldClose(true);
				}
				//DestroyWindow(hWnd);
			}
			return 0;
		}
		break;

		case WM_DESTROY:
		{
			if (CCEngine::Application::Get() &&
				window == &(CCEngine::Application::Get()->GetWindow()))
			{
				PostQuitMessage(0);
			}
			return 0;
		}
		break;

		case WM_NCCREATE:
		{
			LPCREATESTRUCT cs = reinterpret_cast<LPCREATESTRUCT>(lParam);
			SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		break;

		case WM_LBUTTONUP:
		{
			if (window)
			{
				ReleaseCapture();

				float mouseX = static_cast<float>((short)LOWORD(lParam));
				float mouseY = static_cast<float>((short)HIWORD(lParam));

				CCEngine::MouseButtonReleasedEvent e(0, mouseX, mouseY);
				if (window->GetRootUI())
				{
					window->GetRootUI()->OnEvent(e);
				}

				// (참고: Application의 LayerStack 이벤트를 사용 중이라면 아래처럼 전달할 수도 있습니다)
				// if (CCEngine::Application::Get())
				// 	   CCEngine::Application::Get()->OnEvent(e);
			}
			return 0;
		}
		break;

		//
		case WM_LBUTTONDOWN:
		{
			//창 밖으로 드래그할 때 OS가 마우스 이벤트를 끊지 않도록 캡처
			SetCapture(hWnd);

			if (window)
			{
				float mouseX = static_cast<float>((short)LOWORD(lParam));
				float mouseY = static_cast<float>((short)HIWORD(lParam));

				// 0은 좌클릭. 클릭 이벤트를 생성해서 UI로 쏴줍니다!
				CCEngine::MouseButtonPressedEvent e(0, mouseX, mouseY);
				if (window->GetRootUI())
				{
					window->GetRootUI()->OnEvent(e);
				}
			}
			break;
		}
		break;

		case WM_NCHITTEST:  
		{
			// 1. OS의 기본 마우스 위치 판정 (테두리 크기 조절 등)
			LRESULT hit = DefWindowProc(hWnd, message, wParam, lParam);

			// 2. 마우스가 창 내부(Client)에 있을 때만 검사
			if (hit == HTCLIENT)
			{
				POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				ScreenToClient(hWnd, &pt);

				RECT rc;
				GetClientRect(hWnd, &rc);

				bool isMainWindow = (CCEngine::Application::Get() &&
					window == &(CCEngine::Application::Get()->GetWindow()));

				if (isMainWindow)
				{
					if (pt.y >= 0 && pt.y <= 24 && pt.x < (rc.right - 100))
					{
						return HTCAPTION;
					}
				}
			}
			return hit;
		}
		break;

		case WM_NCCALCSIZE:
		{
			// wParam이 TRUE일 때 0을 반환하면, OS가 기본적으로 그리는 창 테두리 영역을 무시하고
			// 창의 전체 크기를 클라이언트 영역(도화지)으로 확장합니다.
			// 이를 통해 상단에 남은 하얀색 선(잔여 테두리)이 완벽하게 제거됩니다.
			if (wParam == TRUE)
			{
				return 0;
			}
		}
		break;

		case WM_MOUSEMOVE:
		{
			if (window)
			{
				float mouseX = static_cast<float>((short)LOWORD(lParam));
				float mouseY = static_cast<float>((short)HIWORD(lParam));

				CCEngine::MouseMovedEvent e(mouseX, mouseY);
				if (window->GetRootUI())
				{
					window->GetRootUI()->OnEvent(e);
				}
			}
			break;
		}
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

	std::pair<float, float> WindowsWindow::GetMousePosition() const
	{
		POINT pt;
		GetCursorPos(&pt);               // 모니터 전역 좌표 획득
		ScreenToClient(m_Window, &pt);   // 현재 윈도우(HWND) 기준 로컬 좌표로 변환
		return { (float)pt.x, (float)pt.y };
	}

	bool WindowsWindow::IsMouseButtonPressed(int button) const
	{
		int vKey = 0;
		if (button == 0) vKey = VK_LBUTTON;
		else if (button == 1) vKey = VK_RBUTTON;
		else if (button == 2) vKey = VK_MBUTTON;

		return (GetAsyncKeyState(vKey) & 0x8000) != 0;
	}

	void WindowsWindow::SetPosition(int x, int y)
	{
		SetWindowPos(m_Window, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
	}

	std::pair<int, int> WindowsWindow::GetScreenMousePosition() const
	{
		POINT pt;
		GetCursorPos(&pt);
		return { (int)pt.x, (int)pt.y };
	}

	void WindowsWindow::SetShouldClose(bool shouldClose)
	{
		m_ShouldClose = shouldClose;
	}

	void WindowsWindow::Init(const WindowProps& props)
	{
		m_Data.Title = props.Title;
		m_Data.Width = props.Width;
		m_Data.Height = props.Height;

		WNDCLASSEX wc = { 0 };
		wc.cbSize = sizeof(WNDCLASSEX);
		wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
		wc.lpfnWndProc = WndProc;
		wc.hInstance = GetModuleHandle(nullptr);
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.lpszClassName = L"CCEngineWindowClass";

		RegisterClassEx(&wc);

		std::wstring titleWide(m_Data.Title.begin(), m_Data.Title.end());

		DWORD windowStyle = WS_POPUP | WS_THICKFRAME;

		m_Window = CreateWindowEx(
			0, wc.lpszClassName, titleWide.c_str(),
			windowStyle,
			CW_USEDEFAULT, CW_USEDEFAULT, m_Data.Width, m_Data.Height,
			nullptr, nullptr, wc.hInstance, this
		);
		ShowWindow(m_Window, SW_SHOW);

		m_Context = GraphicsContext::Create(m_Window);
		m_Context->Init();
	}

	void WindowsWindow::Shutdown()
	{
		if (m_Context)
		{
			delete m_Context;
			m_Context = nullptr;
		}

		if (m_Window)
		{
			DestroyWindow(m_Window);
			m_Window = nullptr;
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