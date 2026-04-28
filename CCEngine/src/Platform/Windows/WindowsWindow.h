#pragma once
#include "Core/Window.h"
#include "Renderer/GraphicsContext.h" // 컨텍스트 껍데기
#include <Windows.h> //win32 API

namespace CCEngine
{
	class WindowsWindow : public Window
	{
		public :
			WindowsWindow(const WindowProps& props);
			virtual ~WindowsWindow();

			void OnUpdate() override;

			inline unsigned int GetWidth() const override { return m_Data.Width; }
			inline unsigned int GetHeight() const override { return m_Data.Height; }
			inline void SetWidth(unsigned int width) { m_Data.Width = width; }
			inline void SetHeight(unsigned int height) { m_Data.Height = height; }

			// 창이 닫혀야 하는지 여부 반환 (예: 사용자가 닫기 버튼을 눌렀는지)
			virtual bool ShouldClose() const override { return m_ShouldClose;}
			virtual GraphicsContext* GetContext() const { return m_Context; }

			virtual std::pair<float, float> GetMousePosition() const override;
			virtual bool IsMouseButtonPressed(int button) const override;

			virtual void SetRootUI(UI::Widget* rootWidget) override { m_RootUI = rootWidget; }
			virtual UI::Widget* GetRootUI() const override { return m_RootUI; }

			virtual void SetPosition(int x, int y) override;
			virtual std::pair<int, int> GetScreenMousePosition() const override;

			virtual void SetShouldClose(bool shouldClose) override;
			virtual void* GetNativeWindow() const override { return m_Window; }


			// 윈도우핸들을 다이렉트x에 넘겨주기 위한 get함수
			inline HWND GetHWND() const { return m_Window; }

		private:
			virtual void Init(const WindowProps& props);
			virtual void Shutdown();

		private: //이부분은 변수
			HWND m_Window; //윈도우 핸들
			GraphicsContext* m_Context = nullptr; // 그래픽 컨텍스트 포인터
			bool m_ShouldClose = false; // 창이 닫혀야 하는지 여부를 나타내는 플래그

			struct WindowData
			{
                std::string Title;
				unsigned int Width = 0, Height = 0;
				bool VSync = false; //수직동기화 여부
			};

			WindowData m_Data;
			UI::Widget* m_RootUI = nullptr;
	};
}