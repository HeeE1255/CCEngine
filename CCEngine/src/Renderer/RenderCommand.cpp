#include "Renderer/RenderCommand.h"
#include "Renderer/GraphicsContext.h"
#include "Renderer/GraphicsDevice.h"
#include <stdexcept>

namespace CCEngine
{
    std::shared_ptr<GraphicsDevice> RenderCommand::s_Device;
    bool RenderCommand::s_Initialized = false;

    void RenderCommand::Init()
    {
        RendererAPI::RequireAvailable(RendererAPI::GetAPI());
        auto* context = GraphicsContext::GetCurrent();
        if (!context || !context->GetCommands() || !context->GetGraphicsDevice())
            throw std::logic_error("RenderCommand requires an initialized graphics context and device.");
        auto device = context->GetGraphicsDevice();
        if (device->GetBackend() != RendererAPI::GetAPI())
            throw std::logic_error("Current graphics device does not match the selected backend.");
        if (s_Initialized && s_Device != device)
            throw std::logic_error("Cannot replace the graphics device while the renderer is initialized.");
        s_Device = std::move(device);
        s_Initialized = true;
    }

    RendererAPI* RenderCommand::CurrentAPI()
    {
        if (!s_Initialized)
            return nullptr;
        auto* context = GraphicsContext::GetCurrent();
        if (!context || context->GetGraphicsDevice() != s_Device || !context->GetCommands())
            throw std::logic_error("Draw commands require a current window on the renderer's shared device.");
        // 전역 adapter를 재사용하지 않는다. 현재 창의 명령 객체가 그 창의 출력 대상을 결정한다.
        return context->GetCommands();
    }

    void RenderCommand::Shutdown()
    {
        s_Initialized = false;
        // 창이 먼저 닫혀도 렌더러 자원이 정리될 때까지 장치를 유지한다.
        s_Device.reset();
    }
}
