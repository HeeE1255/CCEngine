#include "Renderer/RenderCommand.h"
#include "Platform/DirectX11/DX11RendererAPI.h"

namespace CCEngine
{
    // 렌더 명령을 수행할 실제 객체로 DX11RendererAPI를 생성해서 연결합니다!
    std::unique_ptr<RendererAPI> RenderCommand::s_RendererAPI = std::make_unique<DX11RendererAPI>();
}