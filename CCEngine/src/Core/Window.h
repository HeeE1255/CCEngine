#pragma once
#include "Core.h"
#include "Renderer/GraphicsContext.h"
#include <string>
#include <functional>
#include <utility>

namespace CCEngine 
{
	namespace UI { class Widget; } 

	struct WindowProps
	{
		std::string Title;
		unsigned int Width;
		unsigned int Height;

		WindowProps(const std::string& title = "CCEngine",
					unsigned int width = 1280,
					unsigned int height = 720)
			: Title(title), Width(width), Height(height) {}
	};

	class CC_API Window
	{
	public :
		virtual ~Window() = default;

		virtual void OnUpdate() = 0;

		virtual unsigned int GetWidth() const = 0;
		virtual unsigned int GetHeight() const = 0;
		virtual bool ShouldClose() const = 0; // 창이 닫혀야 하는지 여부 반환 (예: 사용자가 닫기 버튼을 눌렀는지)
		virtual GraphicsContext* GetContext() const = 0;
		virtual void SetRootUI(UI::Widget* rootWidget) = 0;
		virtual UI::Widget* GetRootUI() const = 0;
		virtual void SetPosition(int x, int y) = 0;
		virtual std::pair<int, int> GetScreenMousePosition() const = 0;
		virtual std::pair<float, float> GetMousePosition() const = 0;
		virtual bool IsMouseButtonPressed(int button) const = 0;
		virtual void SetShouldClose(bool shouldClose) = 0;
		virtual void* GetNativeWindow() const = 0;
		static Window* Create(const WindowProps& props = WindowProps());
	};

}
