#pragma once
#include "Core.h"
#include "Renderer/RendererAPI.h"

namespace CCEngine
{
    // 장치는 창보다 오래 살 수 있다. 창은 출력 surface를, 장치는 공유 GPU 자원을 소유한다.
    class CC_API GraphicsDevice
    {
    public:
        virtual ~GraphicsDevice() = default;
        virtual RendererAPI::API GetBackend() const = 0;
        GraphicsDevice(const GraphicsDevice&) = delete;
        GraphicsDevice& operator=(const GraphicsDevice&) = delete;
    protected:
        GraphicsDevice() = default;
    };
}
