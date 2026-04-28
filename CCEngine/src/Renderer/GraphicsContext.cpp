// src/Renderer/GraphicsContext.cpp
#include "GraphicsContext.h"
#include "Renderer/RendererAPI.h"

// 구현체 헤더들 포함
#include "Platform/DirectX11/DX11Context.h"
// #include "Platform/OpenGL/OpenGLContext.h" // 나중에 OpenGL 추가 시

// HWND 캐스팅을 위해 포함 (Windows 환경 전용 컴파일 부분)
#include <windows.h> 

namespace CCEngine {

    GraphicsContext* GraphicsContext::Create(void* windowHandle)
    {
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            // CC_CORE_ASSERT(false, "RendererAPI::None은 지원하지 않습니다!");
            return nullptr;

        case RendererAPI::API::DirectX11:
            // 넘어온 void* 포인터를 HWND로 안전하게 캐스팅하여 전달
            return new DX11Context(static_cast<HWND>(windowHandle));

        case RendererAPI::API::OpenGL:
            // return new OpenGLContext(static_cast<GLFWwindow*>(windowHandle));
            return nullptr;
        }

        // CC_CORE_ASSERT(false, "알 수 없는 RendererAPI 입니다!");
        return nullptr;
    }

}