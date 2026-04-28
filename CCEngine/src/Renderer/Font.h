#pragma once
#include "Core.h"
#include <string>
#include <vector>
#include <map>
#include <stb_truetype.h>
#include "Renderer/Texture.h"

namespace CCEngine {

    struct FontRange {
        uint32_t FirstCodepoint;
        uint32_t CharCount;
        std::vector<stbtt_packedchar> PackedChars;
    };

    class CC_API Font {
    public:
        Font(const std::string& filepath, float fontSize);
        ~Font();

        // 사용자가 실시간으로 폰트를 교체할 때 호출
        bool Load(const std::string& filepath, float fontSize);

        // 특정 글자의 UV 좌표 및 크기 정보를 가져옴
        bool GetGlyphInfo(uint32_t codepoint, float* x, float* y, stbtt_aligned_quad& q);

        Texture2D* GetAtlasTexture() { return m_AtlasTexture; }
		float GetFontSize() const { return m_FontSize; }

    private:
        void Invalidate();

    private:
        std::string m_FilePath;
        float m_FontSize;

        Texture2D* m_AtlasTexture = nullptr;
        std::vector<FontRange> m_Ranges;

        // 유니코드 범위를 관리하기 위한 맵 (빠른 검색용)
        std::map<uint32_t, size_t> m_RangeMap;
    };
}