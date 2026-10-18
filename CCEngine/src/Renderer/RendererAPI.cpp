#include "Renderer/RendererAPI.h"
#include <stdexcept>
#include <string>

namespace CCEngine
{
    // 초기값은 DirectX11로 설정
    RendererAPI::API RendererAPI::s_API = RendererAPI::API::DirectX11;

    const char* RendererAPI::GetAPIName(API api)
    {
        switch (api)
        {
        case API::None: return "None";
        case API::DirectX11: return "DirectX11";
        case API::DirectX12: return "DirectX12";
        case API::OpenGL: return "OpenGL";
        case API::Vulkan: return "Vulkan";
        default: return "Unknown";
        }
    }

    const char* RendererAPI::GetUnavailableReason(API api)
    {
        // 소스 파일 존재나 SDK 설치만으로 실행 지원을 켜면 안 된다.
        // 창 공유, 리소스 팩토리, 셰이더 패키지까지 연결된 백엔드만 허용한다.
        switch (api)
        {
        case API::DirectX11: return nullptr;
        case API::DirectX12:
        case API::OpenGL:
        case API::Vulkan:
            return "Backend integration is incomplete (window sharing, resource factories and shader packages).";
        case API::None: return "A graphics backend must be selected.";
        default: return "Unknown graphics backend identifier.";
        }
    }

    void RendererAPI::RequireAvailable(API api)
    {
        if (const char* reason = GetUnavailableReason(api))
            throw std::runtime_error(std::string(GetAPIName(api)) + ": " + reason);
    }

    void RendererAPI::SetAPI(API api)
    {
        RequireAvailable(api);
        s_API = api;
    }
}
