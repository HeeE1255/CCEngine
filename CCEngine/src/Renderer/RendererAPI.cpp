#include "Renderer/RendererAPI.h"

namespace CCEngine
{
    // 현재 우리 엔진의 렌더링 백엔드는 DirectX 11 입니다!
    RendererAPI::API RendererAPI::s_API = RendererAPI::API::DirectX11;
}