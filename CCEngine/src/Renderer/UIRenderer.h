#pragma once
#include "Core.h"
#include "Renderer/RendererHandle.h"
#include <DirectXMath.h>
#include <string>
#include <vector>

namespace CCEngine
{
    class Font;

    class CC_API UIRenderer
    {
    public:
        static void Init();
        static void Shutdown();

        static void BeginUI(uint32_t windowWidth, uint32_t windowHeight);
        static void EndUI();

        // ★ 클리핑 제어
        static void SetClipRect(float x, float y, float w, float h);
        static void PushClipRect(float x, float y, float w, float h);
        static void PopClipRect();
        static void ClearClipRect();

        // 사각형 그리기
        static void DrawRectFilled(float x, float y, float width, float height, const DirectX::XMFLOAT4& color, int entityID = -1);
        static void DrawRect(float x, float y, float width, float height, const DirectX::XMFLOAT4& color, int entityID = -1);
        static void DrawRect(const DirectX::XMFLOAT2& position, const DirectX::XMFLOAT2& size, const DirectX::XMFLOAT4& color, int entityID = -1);

        // 이미지 그리기
        static void DrawImage(float x, float y, float width, float height, RendererHandle textureID, const DirectX::XMFLOAT4& tintColor = { 1.0f, 1.0f, 1.0f, 1.0f }, int entityID = -1);

        // 텍스트 그리기
        static void DrawString(const std::string& text, Font* font, float x, float y, const DirectX::XMFLOAT4& color);
        static void DrawString(const std::string& text, float x, float y, const DirectX::XMFLOAT4& color);

        static Font* GetDefaultFont() { return s_DefaultFont; }

    private:
        static Font* s_DefaultFont;

        // ★ 클리핑 상태 변수 (DLL 에러 방지를 위해 개별 선언)
        static bool s_ClipEnabled;
        static float s_ClipX;
        static float s_ClipY;
        static float s_ClipW;
        static float s_ClipH;
        // 내부 영역을 그릴 때 부모 영역의 잘림 상태를 잃지 않도록 클립을 스택으로 저장한다.
        // 그래프, 팝업, 리스트처럼 겹쳐 그리는 UI는 Push/Pop으로 자기 영역만 임시 적용한다.
        static std::vector<DirectX::XMFLOAT4> s_ClipStack;
    };
}
