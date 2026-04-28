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
        static void BeginScene(const DirectX::XMMATRIX& viewProjection);

        static void EndScene();

        static void Flush();
        static void StartBatch();

        // 1. 메인 함수 (Transform 행렬 사용)
        static void DrawQuad(const DirectX::XMMATRIX& transform, const DirectX::XMFLOAT4& color, int entityID = -1);
        static void DrawQuad(const DirectX::XMMATRIX& transform, Texture2D* texture, const DirectX::XMFLOAT4& tintColor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), int entityID = -1);
        static void DrawQuad(const DirectX::XMMATRIX& transform, Texture2D* texture, const DirectX::XMFLOAT2* texCoords, const DirectX::XMFLOAT4& tintColor, int entityID = -1);

        // 2. 편의성 오버로딩 함수 (단색 사각형)
        static void DrawQuad(const DirectX::XMFLOAT2& position, const DirectX::XMFLOAT2& size, const DirectX::XMFLOAT4& color, int entityID = -1);
        static void DrawQuad(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT2& size, const DirectX::XMFLOAT4& color, int entityID = -1);

        // 3. 편의성 오버로딩 함수 (텍스처 사각형)
        static void DrawQuad(const DirectX::XMFLOAT2& position, const DirectX::XMFLOAT2& size, Texture2D* texture, const DirectX::XMFLOAT4& tintColor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), int entityID = -1);
        static void DrawQuad(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT2& size, Texture2D* texture, const DirectX::XMFLOAT4& tintColor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), int entityID = -1);

		// 4. 텍스처의 일부분(UV 지정)만 그리는 함수
        static void DrawQuad(const DirectX::XMFLOAT2& position, const DirectX::XMFLOAT2& size, Texture2D* texture, const DirectX::XMFLOAT2* texCoords, const DirectX::XMFLOAT4& tintColor, int entityID = -1);
        static void DrawQuad(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT2& size, Texture2D* texture, const DirectX::XMFLOAT2* texCoords, const DirectX::XMFLOAT4& tintColor, int entityID = -1);
    };
}