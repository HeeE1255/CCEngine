#pragma once
#include "Core.h"
#include <string>
#include <functional>

namespace CCEngine 
{

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

		static Window* Create(const WindowProps& props = WindowProps());

		 
	};

}
