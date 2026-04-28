#include "Renderer/RendererAPI.h"

namespace CCEngine
{
    // 초기값은 DirectX11로 설정
    RendererAPI::API RendererAPI::s_API = RendererAPI::API::DirectX11;
}