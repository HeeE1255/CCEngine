#pragma once
#include "Renderer/PerspectiveCamera.h"
#include "Core.h"
#include "Renderer/Texture.h"
#include <DirectXMath.h>

namespace CCEngine
{
    class CC_API Renderer2D
    {
    public:
        static void Init();
        static void Shutdown();

        // 씬 시작과 끝 (렌더링 세션 관리)
        static void BeginScene(const PerspectiveCamera& camera);
        static void EndScene();

        // 색상만 있는 사각형을 그리는 함수
        static void DrawQuad(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT2& size, const DirectX::XMFLOAT4& color);

        // 텍스처가 있는 사각형을 그리는 함수 
        static void DrawQuad(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT2& size, Texture2D* texture);
    };
}