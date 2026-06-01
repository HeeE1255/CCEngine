#include "WindowsWindow.h"
#include "Renderer/GraphicsContext.h"
#include "Application.h" 
#include "Events/ApplicationEvent.h"
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

				if (window->GetContext())
				{
					window->GetContext()->ResizeBuffers(width, height);
				}

				if (CCEngine::Application::Get() &&
					window == &(CCEngine::Application::Get()->GetWindow()))
				{
					CCEngine::WindowResizeEvent e(width, height);
					CCEngine::Application::Get()->OnEvent(e);
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
				CCEngine::WindowCloseEvent e;
				CCEngine::Application::Get()->OnEvent(e);
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

				bool isMainWindow = (CCEngine::Application::Get() &&
					window == &(CCEngine::Application::Get()->GetWindow()));

				if (isMainWindow)
				{
					CCEngine::Application::Get()->OnEvent(e);
				}

				if (!e.Handled && window->GetRootUI())
				{
					window->GetRootUI()->OnEvent(e);
				}
			}
			return 0;
		}
		break;

		//
		case WM_LBUTTONDOWN:
		{
			SetCapture(hWnd);
			if (window)
			{
				float mouseX = static_cast<float>((short)LOWORD(lParam));
				float mouseY = static_cast<float>((short)HIWORD(lParam));

				CCEngine::MouseButtonPressedEvent e(0, mouseX, mouseY);

				bool isMainWindow = (CCEngine::Application::Get() &&
					window == &(CCEngine::Application::Get()->GetWindow()));

				if (isMainWindow)
				{
					CCEngine::Application::Get()->OnEvent(e);
				}

				if (!e.Handled && window->GetRootUI())
				{
					window->GetRootUI()->OnEvent(e);
				}
			}
			return 0;
		}
		break;

		case WM_MOUSEMOVE:
		{
			if (window)
			{
				float mouseX = static_cast<float>((short)LOWORD(lParam));
				float mouseY = static_cast<float>((short)HIWORD(lParam));

				CCEngine::MouseMovedEvent e(mouseX, mouseY);

				bool isMainWindow = (CCEngine::Application::Get() &&
					window == &(CCEngine::Application::Get()->GetWindow()));

				if (isMainWindow)
				{
					CCEngine::Application::Get()->OnEvent(e);
				}

				if (!e.Handled && window->GetRootUI())
				{
					window->GetRootUI()->OnEvent(e);
				}
			}
			return 0;
		}
		break;


		case WM_NCHITTEST:  
		{
			LRESULT hit = DefWindowProc(hWnd, message, wParam, lParam);

			// 마우스가 창 안에 있을 때
			if (hit == HTCLIENT)
			{
				POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				ScreenToClient(hWnd, &pt);

				RECT rc;
				GetClientRect(hWnd, &rc);

				// ==========================================================
				// ★ 1. 메인 창 8방향 리사이즈 테두리 수동 판정! (두께 8px)
				// WM_NCCALCSIZE로 OS 테두리를 날렸기 때문에 여기서 부활시켜야 합니다.
				// ==========================================================
				int borderWidth = 8;
				bool isLeft = pt.x < borderWidth;
				bool isRight = pt.x >= rc.right - borderWidth;
				bool isTop = pt.y < borderWidth;
				bool isBottom = pt.y >= rc.bottom - borderWidth;

				bool isMainWindow = (CCEngine::Application::Get() &&
					window == &(CCEngine::Application::Get()->GetWindow()));
				bool isDockRootWindow = !isMainWindow && window->GetRootUI() && window->GetRootUI()->GetName() == "DockRoot";
				if (isDockRootWindow)
				{
					if (isBottom && isLeft) return HTBOTTOMLEFT;
					if (isBottom && isRight) return HTBOTTOMRIGHT;
					if (isLeft) return HTLEFT;
					if (isRight) return HTRIGHT;
					if (isBottom) return HTBOTTOM;
					if (pt.y >= 0 && pt.y <= 24 && pt.x < (rc.right - 30))
					{
						return HTCAPTION;
					}
					if (isTop) return HTTOP;
				}

				if (isTop && isLeft) return HTTOPLEFT;
				if (isTop && isRight) return HTTOPRIGHT;
				if (isBottom && isLeft) return HTBOTTOMLEFT;
				if (isBottom && isRight) return HTBOTTOMRIGHT;
				if (isLeft) return HTLEFT;
				if (isRight) return HTRIGHT;
				if (isBottom) return HTBOTTOM;
				if (isTop) return HTTOP;

				// ==========================================================
				// ★ 2. 테두리가 아니라면 커스텀 타이틀 바 판정 (드래그용)
				// ==========================================================
				if (isMainWindow)
				{
					// 상단 24px 영역 (우측 100px의 닫기 버튼 영역 제외)
					if (pt.y >= 0 && pt.y <= 24 && pt.x < (rc.right - 100))
					{
						return HTCAPTION;
					}
				}
			}
			return hit;
		}
		break;

		case WM_MOUSEWHEEL:
		{
			if (window)
			{
				// GET_WHEEL_DELTA_WPARAM은 휠 회전 방향과 크기를 반환합니다. (보통 120 단위)
				// 120으로 나누면 한 틱당 1.0 또는 -1.0이 됩니다.
				float yOffset = (float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA;

				// X축 휠(틸트)은 지금 사용하지 않으므로 0.0f로 줍니다.
				CCEngine::MouseScrolledEvent e(0.0f, yOffset);

				bool isMainWindow = (CCEngine::Application::Get() &&
					window == &(CCEngine::Application::Get()->GetWindow()));

				if (isMainWindow)
				{
					CCEngine::Application::Get()->OnEvent(e);
				}

				if (!e.Handled && window->GetRootUI())
				{
					window->GetRootUI()->OnEvent(e);
				}
			}
			return 0;
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
