#include "UIRenderer.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Font.h"

namespace CCEngine
{
	// 유니코드 코드포인트를 UTF-8 문자열에서 읽어오는 간단한 함수
    static uint32_t GetNextUTF8Codepoint(const char** text)
    {
        uint32_t c = (unsigned char)(**text);
        if (c == 0) return 0;

        int bytes = 0;
        if ((c & 0xE0) == 0xC0) { bytes = 1; c &= 0x1F; }
        else if ((c & 0xF0) == 0xE0) { bytes = 2; c &= 0x0F; }
        else if ((c & 0xF8) == 0xF0) { bytes = 3; c &= 0x07; }

        (*text)++;
        while (bytes > 0)
        {
            c = (c << 6) | ((unsigned char)(**text) & 0x3F);
            (*text)++;
            bytes--;
        }
        return c;
    }

    Font* UIRenderer::s_DefaultFont = nullptr;

    void UIRenderer::Init()
    {
        s_DefaultFont = new Font("assets/fonts/NotoSansKR-VariableFont_wght.ttf", 24.0f);
    }

    void UIRenderer::Shutdown()
    {
        if (s_DefaultFont)
        {
            delete s_DefaultFont;
            s_DefaultFont = nullptr;
        }
    }

    void UIRenderer::BeginUI(uint32_t windowWidth, uint32_t windowHeight)
    {
        // 1.UI 픽셀 좌표계(Ortho) 행렬 생성
        // Left: 0, Right: Width
        // Bottom: Height, Top: 0 (★Y축을 뒤집어서 화면 맨 위가 0이 되게 만듦!)
        // Near: -1.0f, Far: 1.0f
        DirectX::XMMATRIX orthoMatrix = DirectX::XMMatrixOrthographicOffCenterLH(
            0.0f, (float)windowWidth,
            (float)windowHeight, 0.0f,
            -1.0f, 1.0f
        );

        // 2. Renderer2D에게 위에서 만든 특별한 행렬을 주입하며 배치 시작
        Renderer2D::BeginScene(orthoMatrix);
    }

    void UIRenderer::EndUI()
    {
        // 렌더러에 모인 UI들을 한 번에 그리기(Draw Call)
        Renderer2D::EndScene();
    }

    void UIRenderer::DrawRectFilled(float x, float y, float width, float height, const DirectX::XMFLOAT4& color, int entityID)
    {
        // 1. [핵심] Top-Left(x,y) 기준을 Center 기준으로 변환
        float centerX = x + (width * 0.5f);
        float centerY = y + (height * 0.5f);

        // 2. Transform 행렬 생성 (크기 조절 -> 위치 이동)
        DirectX::XMMATRIX transform =
            DirectX::XMMatrixScaling(width, height, 1.0f) * DirectX::XMMatrixTranslation(centerX, centerY, 0.0f);

        // 3. Renderer2D의 쿼드 그리기 호출
        Renderer2D::DrawQuad(transform, color, entityID);
    }

    void UIRenderer::DrawImage(float x, float y, float width, float height, void* textureID, const DirectX::XMFLOAT4& tintColor, int entityID)
    {
        if (!textureID) return;

        float centerX = x + (width * 0.5f);
        float centerY = y + (height * 0.5f);

        DirectX::XMMATRIX transform =
            DirectX::XMMatrixScaling(width, height, 1.0f) * DirectX::XMMatrixTranslation(centerX, centerY, 0.0f);

        // void* 로 넘어온 SRV 포인터를 Texture2D* 로 처리하기 위한 로직
        Texture2D* tex = static_cast<Texture2D*>(textureID);

        Renderer2D::DrawQuad(transform, tex, tintColor, entityID);
    }


    void UIRenderer::DrawString(const std::string& text, Font* font, float x, float y, const DirectX::XMFLOAT4& color)
    {
        if (!font || !font->GetAtlasTexture()) return;

        float cursorX = x;
        float cursorY = y;

        const char* ptr = text.c_str();
        while (*ptr != '\0')
        {
            uint32_t codepoint = GetNextUTF8Codepoint(&ptr);

            if (codepoint == '\n')
            {
                cursorX = x;
                cursorY += 32.0f;
                continue;
            }

            stbtt_aligned_quad q;

            // 좌표 자동 계산 및 커서 전진
            if (!font->GetGlyphInfo(codepoint, &cursorX, &cursorY, q)) continue;

            float width = q.x1 - q.x0;
            float height = q.y1 - q.y0;

            float centerX = q.x0 + (width * 0.5f);
            float centerY = q.y0 + (height * 0.5f);

            DirectX::XMMATRIX transform =
                DirectX::XMMatrixScaling(width, height, 1.0f) * DirectX::XMMatrixTranslation(centerX, centerY, 0.0f);

            DirectX::XMFLOAT2 uvs[4] = {
                { q.s0, q.t0 },
                { q.s1, q.t0 },
                { q.s1, q.t1 },
                { q.s0, q.t1 }
            };

            CCEngine::Renderer2D::DrawQuad(transform, font->GetAtlasTexture(), uvs, color, -1);
        }
    }

    void UIRenderer::DrawString(const std::string& text, float x, float y, const DirectX::XMFLOAT4& color)
    {
        DrawString(text, s_DefaultFont , x, y, color);
    }
}