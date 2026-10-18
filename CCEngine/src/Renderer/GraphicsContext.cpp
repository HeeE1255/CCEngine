// src/Renderer/GraphicsContext.cpp
#include "GraphicsContext.h"
#include "Renderer/RendererAPI.h"

// 구현체 헤더들 포함
#include "Platform/DirectX11/DX11Context.h"
#include "Platform/DirectX11/DX11Device.h"
#if defined(CC_ENABLE_DX12_RHI)
#include "Platform/DirectX12/DX12Context.h"
#endif
#include <stdexcept>
// #include "Platform/OpenGL/OpenGLContext.h" // 나중에 OpenGL 추가 시

// HWND 캐스팅을 위해 포함 (Windows 환경 전용 컴파일 부분)
#include <windows.h> 

namespace CCEngine {

    thread_local GraphicsContext* GraphicsContext::s_Current = nullptr;

    GraphicsContext::~GraphicsContext()
    {
        if (s_Current == this)
            s_Current = nullptr;
    }

    GraphicsContext* GraphicsContext::GetCurrent() { return s_Current; }
    void GraphicsContext::SetCurrentContext() { s_Current = this; }

    GraphicsContext* GraphicsContext::Create(void* windowHandle, std::shared_ptr<GraphicsDevice> sharedDevice)
    {
        RendererAPI::RequireAvailable(RendererAPI::GetAPI());
        if (!windowHandle)
            throw std::invalid_argument("Graphics context requires a native window.");
        if (sharedDevice && sharedDevice->GetBackend() != RendererAPI::GetAPI())
            throw std::invalid_argument("Window and shared graphics device use different backends.");
        switch (RendererAPI::GetAPI())
        {
        case RendererAPI::API::None:
            // CC_CORE_ASSERT(false, "RendererAPI::None은 지원하지 않습니다!");
            return nullptr;

        case RendererAPI::API::DirectX11:
            // 넘어온 void* 포인터를 HWND로 안전하게 캐스팅하여 전달
        {
            auto device = std::dynamic_pointer_cast<DX11Device>(sharedDevice);
            if (sharedDevice && !device)
                throw std::invalid_argument("Invalid DirectX11 device implementation.");
            if (!device)
                device = std::make_shared<DX11Device>();
            return new DX11Context(static_cast<HWND>(windowHandle), std::move(device));
        }

#if defined(CC_ENABLE_DX12_RHI)
        case RendererAPI::API::DirectX12:
        {
            auto device = std::dynamic_pointer_cast<DX12Device>(sharedDevice);
            if (sharedDevice && !device)
                throw std::invalid_argument("Invalid DirectX12 device implementation.");
            return new DX12Context(windowHandle, false, std::move(device));
        }
#endif
        case RendererAPI::API::OpenGL:
            // return new OpenGLContext(static_cast<GLFWwindow*>(windowHandle));
            return nullptr;
        }

        // CC_CORE_ASSERT(false, "알 수 없는 RendererAPI 입니다!");
        return nullptr;
    }

}
