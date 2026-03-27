#include "Core/Window.h"

//플렛폼 구분
#ifdef CC_PLATFORM_WINDOWS
	#include "Platform/Windows/WindowsWindow.h"
#endif

namespace CCEngine 
{

    // 팩토리 함수: OS에 맞는 윈도우를 생성해서 반환
    Window* Window::Create(const WindowProps& props)
    {
#ifdef CC_PLATFORM_WINDOWS
        return new WindowsWindow(props);
#else
        // 나중에 리눅스/맥을 지원하면 여기에 추가
        return nullptr;
#endif
    }
}