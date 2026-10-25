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

    RendererAPI::API RendererAPI::ParseAPIName(const std::string& name)
    {
        for (API api : { API::DirectX11, API::DirectX12, API::OpenGL, API::Vulkan })
            if (name == GetAPIName(api)) return api;
        throw std::invalid_argument("Unknown graphics API: " + name);
    }

    const char* RendererAPI::GetAPIArgument(API api)
    {
        switch (api)
        {
        case API::DirectX11: return "-dx11";
        case API::DirectX12: return "-dx12";
        case API::OpenGL: return "-opengl";
        case API::Vulkan: return "-vulkan";
        default: throw std::invalid_argument("Invalid graphics API.");
        }
    }

    const char* RendererAPI::GetUnavailableReason(API api)
    {
        switch (api)
        {
        case API::DirectX11: return nullptr;
        case API::DirectX12:
#if defined(CC_ENABLE_DX12_RHI)
            return nullptr;
#else
            return "DirectX 12 backend was not compiled into this build.";
#endif
        case API::OpenGL:
#if defined(CC_ENABLE_OPENGL_RHI)
            return nullptr;
#else
            return "OpenGL backend was not compiled into this build.";
#endif
        case API::Vulkan:
#if defined(CC_ENABLE_VULKAN_RHI)
            return nullptr;
#else
            return "Vulkan backend was not compiled into this build.";
#endif
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
