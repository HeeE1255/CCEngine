#pragma once
#include "Core.h"
#include "Renderer/Texture.h"
#include <DirectXMath.h>

namespace CCEngine
{
    class Font;

    class CC_API UIRenderer
    {
    public:
        static void Init();
        static void Shutdown();

        // 창의 너비와 높이를 받아서 픽셀 1:1 매칭 행렬을 만듭니다.
        static void BeginUI(uint32_t windowWidth, uint32_t windowHeight);
        static void EndUI();

        // UI 렌더링 함수 (기준점: 왼쪽 위 Top-Left)
        static void DrawRectFilled(float x, float y, float width, float height, const DirectX::XMFLOAT4& color, int entityID = -1);

        // 텍스처를 받는 함수 (void* 를 Texture2D* 로 캐스팅해서 사용)
        static void DrawImage(float x, float y, float width, float height, void* textureID, const DirectX::XMFLOAT4& tintColor = { 1.0f, 1.0f, 1.0f, 1.0f }, int entityID = -1);

		// 텍스트 렌더링 함수 (Font* 타입의 폰트와 텍스트 문자열을 받아서 그립니다)
        static void DrawString(const std::string& text, Font* font, float x, float y, const DirectX::XMFLOAT4& color);
        static void DrawString(const std::string& text, float x, float y, const DirectX::XMFLOAT4& color);

        static Font* GetDefaultFont() { return s_DefaultFont; }

    private:
        static Font* s_DefaultFont; // UIRenderer가 관리할 기본 폰트
    };
}