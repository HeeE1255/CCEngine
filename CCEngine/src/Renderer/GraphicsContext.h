#pragma once
#include "Core.h"
#include <cstdint>
#include <cstdint>

    namespace CCEngine
    {
        // 그래픽 컨텍스트 인터페이스: 플랫폼별로 그래픽 API 초기화 및 관리하는 인터페이스 (추상화 헤더)
        // 설명 : 그래픽 컨텍스트는 렌더링을 위한 그래픽 API(예: OpenGL, DirectX, Vulkan 등)를 초기화하고 관리하는 역할을 하는 인터페이스
        // 엔진은 이 인터페이스를 통해 다양한 플랫폼에서 일관된 방식으로 그래픽 API를 사용
        // 스왑 체인, 렌더 타겟, 깊이 버퍼 등과 같은 그래픽 리소스를 관리하는 기능을 포함할 수 있음
        class CC_API GraphicsContext
        {
        public:
            virtual ~GraphicsContext() = default;

            virtual void Init() = 0;
            virtual void SwapBuffers() = 0;
            virtual void Clear(float r, float g, float b, float a) = 0;
            virtual void ResizeBuffers(uint32_t width, uint32_t height) = 0;
        };
    }