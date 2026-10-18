#pragma once
#include "Core.h"
#include <cstdint>
#include <vector>
#include <memory>

namespace CCEngine
{
    class GraphicsDevice;
    class RendererAPI;
    // 그래픽 컨텍스트 인터페이스: 플랫폼별로 그래픽 API 초기화 및 관리하는 인터페이스 (추상화 헤더)
    // 설명 : 그래픽 컨텍스트는 렌더링을 위한 그래픽 API(예: OpenGL, DirectX, Vulkan 등)를 초기화하고 관리하는 역할을 하는 인터페이스
    // 엔진은 이 인터페이스를 통해 다양한 플랫폼에서 일관된 방식으로 그래픽 API를 사용
    // 스왑 체인, 렌더 타겟, 깊이 버퍼 등과 같은 그래픽 리소스를 관리하는 기능을 포함할 수 있음
    class CC_API GraphicsContext
    {
    public:
        virtual ~GraphicsContext();
        GraphicsContext(const GraphicsContext&) = delete;
        GraphicsContext& operator=(const GraphicsContext&) = delete;

        virtual void Init() = 0;
        virtual void SwapBuffers(bool waitForVSync = true) = 0;
        virtual void Clear(float r, float g, float b, float a) = 0;
        virtual void ResizeBuffers(uint32_t width, uint32_t height) = 0;

        virtual void MakeCurrent() = 0;
        virtual void BindBackBuffer() = 0;
        virtual RendererAPI* GetCommands() { return nullptr; }
        virtual std::shared_ptr<GraphicsDevice> GetGraphicsDevice() const { return {}; }
        static GraphicsContext* GetCurrent();

        // 좌상단 원점의 RGBA8 행을 빈틈없이 반환한다. 미지원/실패는 빈 결과와 false다.
        virtual bool ReadBackBuffer(std::vector<uint8_t>& pixels, uint32_t& width, uint32_t& height)
        {
            pixels.clear();
            width = height = 0;
            return false;
        }

        //  API에 맞는 컨텍스트를 동적으로 생성하는 함수
        static GraphicsContext* Create(void* windowHandle, std::shared_ptr<GraphicsDevice> sharedDevice = {});
    protected:
        GraphicsContext() = default;
        void SetCurrentContext();
    private:
        static thread_local GraphicsContext* s_Current;
    };
}
