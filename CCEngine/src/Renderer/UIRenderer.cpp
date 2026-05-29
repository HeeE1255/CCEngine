#include "UIRenderer.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Font.h"
#include <algorithm>

namespace CCEngine
{
    // UTF-8 헬퍼 함수
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
    bool UIRenderer::s_ClipEnabled = false;
    float UIRenderer::s_ClipX = 0.0f;
    float UIRenderer::s_ClipY = 0.0f;
    float UIRenderer::s_ClipW = 0.0f;
    float UIRenderer::s_ClipH = 0.0f;

    void UIRenderer::Init()
    {
        s_DefaultFont = new Font("assets/fonts/NotoSansKR-VariableFont_wght.ttf", 24.0f);
    }

    void UIRenderer::Shutdown()
    {
        if (s_DefaultFont) { delete s_DefaultFont; s_DefaultFont = nullptr; }
    }

    void UIRenderer::BeginUI(uint32_t windowWidth, uint32_t windowHeight)
    {
        DirectX::XMMATRIX orthoMatrix = DirectX::XMMatrixOrthographicOffCenterLH(
            0.0f, (float)windowWidth, (float)windowHeight, 0.0f, -1.0f, 1.0f);
        Renderer2D::BeginScene(orthoMatrix);
    }

    void UIRenderer::EndUI() { Renderer2D::EndScene(); }

    void UIRenderer::SetClipRect(float x, float y, float w, float h)
    {
        s_ClipEnabled = true;
        s_ClipX = x; s_ClipY = y; s_ClipW = w; s_ClipH = h;
    }

    void UIRenderer::ClearClipRect() { s_ClipEnabled = false; }

    void UIRenderer::DrawRectFilled(float x, float y, float width, float height, const DirectX::XMFLOAT4& color, int entityID)
    {
        float outX = x, outY = y, outW = width, outH = height;

        if (s_ClipEnabled)
        {
            float clipRight = s_ClipX + s_ClipW;
            float clipBottom = s_ClipY + s_ClipH;

            float newX = (std::max)(x, s_ClipX);
            float newY = (std::max)(y, s_ClipY);
            float newRight = (std::min)(x + width, clipRight);
            float newBottom = (std::min)(y + height, clipBottom);

            outW = newRight - newX;
            outH = newBottom - newY;

            if (outW <= 0.0f || outH <= 0.0f) return;
            outX = newX; outY = newY;
        }

        float centerX = outX + (outW * 0.5f);
        float centerY = outY + (outH * 0.5f);
        DirectX::XMMATRIX transform = DirectX::XMMatrixScaling(outW, outH, 1.0f) * DirectX::XMMatrixTranslation(centerX, centerY, 0.0f);
        Renderer2D::DrawQuad(transform, color, entityID);
    }

    void UIRenderer::DrawRect(float x, float y, float width, float height, const DirectX::XMFLOAT4& color, int entityID)
    {
        DrawRectFilled(x, y, width, height, color, entityID);
    }

    void UIRenderer::DrawRect(const DirectX::XMFLOAT2& position, const DirectX::XMFLOAT2& size, const DirectX::XMFLOAT4& color, int entityID)
    {
        DrawRectFilled(position.x, position.y, size.x, size.y, color, entityID);
    }

    void UIRenderer::DrawImage(float x, float y, float width, float height, RendererHandle textureID, const DirectX::XMFLOAT4& tintColor, int entityID)
    {
        if (!textureID) return;
        float outX = x, outY = y, outW = width, outH = height;
        float uvS0 = 0.0f, uvT0 = 0.0f, uvS1 = 1.0f, uvT1 = 1.0f;

        if (s_ClipEnabled)
        {
            float clipRight = s_ClipX + s_ClipW;
            float clipBottom = s_ClipY + s_ClipH;

            float newX = (std::max)(x, s_ClipX);
            float newY = (std::max)(y, s_ClipY);
            float newRight = (std::min)(x + width, clipRight);
            float newBottom = (std::min)(y + height, clipBottom);

            outW = newRight - newX;
            outH = newBottom - newY;
            if (outW <= 0.0f || outH <= 0.0f) return;

            uvS0 = (newX - x) / width;
            uvT0 = (newY - y) / height;
            uvS1 = 1.0f - ((x + width - newRight) / width);
            uvT1 = 1.0f - ((y + height - newBottom) / height);
            outX = newX; outY = newY;
        }

        float centerX = outX + (outW * 0.5f);
        float centerY = outY + (outH * 0.5f);
        DirectX::XMMATRIX transform = DirectX::XMMatrixScaling(outW, outH, 1.0f) * DirectX::XMMatrixTranslation(centerX, centerY, 0.0f);
        DirectX::XMFLOAT2 uvs[4] = { { uvS0, uvT0 }, { uvS1, uvT0 }, { uvS1, uvT1 }, { uvS0, uvT1 } };
        Renderer2D::DrawQuad(transform, textureID, uvs, tintColor, entityID);
    }

    void UIRenderer::DrawString(const std::string& text, Font* font, float x, float y, const DirectX::XMFLOAT4& color)
    {
        if (!font || !font->GetAtlasTexture()) return;
        float cursorX = x, cursorY = y;
        const char* ptr = text.c_str();

        while (*ptr != '\0')
        {
            uint32_t codepoint = GetNextUTF8Codepoint(&ptr);
            if (codepoint == '\n') { cursorX = x; cursorY += 32.0f; continue; }

            stbtt_aligned_quad q;
            if (!font->GetGlyphInfo(codepoint, &cursorX, &cursorY, q)) continue;

            float charX0 = q.x0, charY0 = q.y0, charX1 = q.x1, charY1 = q.y1;
            float uvS0 = q.s0, uvT0 = q.t0, uvS1 = q.s1, uvT1 = q.t1;

            if (s_ClipEnabled)
            {
                float clipRight = s_ClipX + s_ClipW, clipBottom = s_ClipY + s_ClipH;
                float newX0 = (std::max)(charX0, s_ClipX);
                float newY0 = (std::max)(charY0, s_ClipY);
                float newX1 = (std::min)(charX1, clipRight);
                float newY1 = (std::min)(charY1, clipBottom);

                if (newX1 <= newX0 || newY1 <= newY0) continue;

                float origW = charX1 - charX0, origH = charY1 - charY0;
                float uvW = uvS1 - uvS0, uvH = uvT1 - uvT0;

                uvS0 = q.s0 + ((newX0 - charX0) / origW) * uvW;
                uvT0 = q.t0 + ((newY0 - charY0) / origH) * uvH;
                uvS1 = q.s1 - ((charX1 - newX1) / origW) * uvW;
                uvT1 = q.t1 - ((charY1 - newY1) / origH) * uvH;

                charX0 = newX0; charY0 = newY0; charX1 = newX1; charY1 = newY1;
            }

            float width = charX1 - charX0, height = charY1 - charY0;
            float centerX = charX0 + (width * 0.5f), centerY = charY0 + (height * 0.5f);
            DirectX::XMMATRIX transform = DirectX::XMMatrixScaling(width, height, 1.0f) * DirectX::XMMatrixTranslation(centerX, centerY, 0.0f);
            DirectX::XMFLOAT2 uvs[4] = { { uvS0, uvT0 }, { uvS1, uvT0 }, { uvS1, uvT1 }, { uvS0, uvT1 } };
            Renderer2D::DrawQuad(transform, font->GetAtlasTexture(), uvs, color, -1);
        }
    }

    void UIRenderer::DrawString(const std::string& text, float x, float y, const DirectX::XMFLOAT4& color)
    {
        DrawString(text, s_DefaultFont, x, y, color);
    }
}
