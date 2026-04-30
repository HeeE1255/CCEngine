#include "UIRenderer.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Font.h"

namespace CCEngine
{
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
        DirectX::XMMATRIX orthoMatrix = DirectX::XMMatrixOrthographicOffCenterLH(
            0.0f, (float)windowWidth,
            (float)windowHeight, 0.0f,
            -1.0f, 1.0f
        );

        Renderer2D::BeginScene(orthoMatrix);
    }

    void UIRenderer::EndUI()
    {
        Renderer2D::EndScene();
    }

    void UIRenderer::DrawRectFilled(float x, float y, float width, float height, const DirectX::XMFLOAT4& color, int entityID)
    {
        float centerX = x + (width * 0.5f);
        float centerY = y + (height * 0.5f);

        DirectX::XMMATRIX transform =
            DirectX::XMMatrixScaling(width, height, 1.0f) * DirectX::XMMatrixTranslation(centerX, centerY, 0.0f);

        Renderer2D::DrawQuad(transform, color, entityID);
    }

    // ★ 위험한 캐스팅 제거! 원시 포인터를 그대로 Renderer2D에 전달합니다.
    void UIRenderer::DrawImage(float x, float y, float width, float height, void* textureID, const DirectX::XMFLOAT4& tintColor, int entityID)
    {
        if (!textureID) return;

        float centerX = x + (width * 0.5f);
        float centerY = y + (height * 0.5f);

        DirectX::XMMATRIX transform =
            DirectX::XMMatrixScaling(width, height, 1.0f) * DirectX::XMMatrixTranslation(centerX, centerY, 0.0f);

        Renderer2D::DrawQuad(transform, textureID, tintColor, entityID);
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
            if (!font->GetGlyphInfo(codepoint, &cursorX, &cursorY, q)) continue;

            float width = q.x1 - q.x0;
            float height = q.y1 - q.y0;
            float centerX = q.x0 + (width * 0.5f);
            float centerY = q.y0 + (height * 0.5f);

            DirectX::XMMATRIX transform =
                DirectX::XMMatrixScaling(width, height, 1.0f) * DirectX::XMMatrixTranslation(centerX, centerY, 0.0f);

            DirectX::XMFLOAT2 uvs[4] = {
                { q.s0, q.t0 }, { q.s1, q.t0 }, { q.s1, q.t1 }, { q.s0, q.t1 }
            };

            // 폰트 클래스의 Texture2D 객체를 그대로 넘기면 
            // Renderer2D가 알아서 내부 SRV를 뽑아내 안전하게 그려줍니다.
            Renderer2D::DrawQuad(transform, font->GetAtlasTexture(), uvs, color, -1);
        }
    }

    void UIRenderer::DrawString(const std::string& text, float x, float y, const DirectX::XMFLOAT4& color)
    {
        DrawString(text, s_DefaultFont, x, y, color);
    }
}