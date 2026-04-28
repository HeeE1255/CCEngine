#include "Renderer/RenderCommand.h"
#include "Platform/DirectX11/DX11RendererAPI.h"

namespace CCEngine
{
    std::unique_ptr<RendererAPI> RenderCommand::s_RendererAPI = std::make_unique<DX11RendererAPI>();
}