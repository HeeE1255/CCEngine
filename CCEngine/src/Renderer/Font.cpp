#include "Font.h"
#include <fstream>
#include <iostream>

namespace CCEngine {

    Font::Font(const std::string& filepath, float fontSize)
        : m_FilePath(filepath), m_FontSize(fontSize) 
    {
        Load(filepath, fontSize);
    }

    Font::~Font() 
    {
        Invalidate();
    }

    void Font::Invalidate() 
    {
        if (m_AtlasTexture) delete m_AtlasTexture;
        m_Ranges.clear();
        m_RangeMap.clear();
    }

    bool Font::Load(const std::string& filepath, float fontSize) 
    {
        Invalidate();
        m_FilePath = filepath;
        m_FontSize = fontSize;

        // 1. 폰트 파일 로드
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            std::cout << "[Font Error] 폰트 파일을 찾을 수 없거나 열 수 없습니다: " << filepath << std::endl;
            return false;
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<unsigned char> fontBuffer(size);
        file.read((char*)fontBuffer.data(), size);

        // 2. 아틀라스 설정 (4096 x 4096)
        const int width = 4096;
        const int height = 4096;
        auto bitmap = std::make_unique<unsigned char[]>(width * height);

        stbtt_pack_context pc;
        stbtt_PackBegin(&pc, bitmap.get(), width, height, 0, 1, nullptr);
        stbtt_PackSetOversampling(&pc, 2, 2);

        // 3. 다국어 범위 설정 (나중에 사용자가 추가하기 쉽게 벡터화 가능)
        struct RangeDef { uint32_t Start; uint32_t Count; };
        std::vector<RangeDef> defs = {
            { 0x0020, 95 },    // ASCII
            { 0xAC00, 11172 }, // 한글
            { 0x3040, 96 },    // 히라가나
            { 0x30A0, 96 }     // 가타카나
        };

        std::vector<stbtt_pack_range> stbRanges(defs.size());
        for (size_t i = 0; i < defs.size(); ++i) 
        {
            FontRange range;
            range.FirstCodepoint = defs[i].Start;
            range.CharCount = defs[i].Count;
            range.PackedChars.resize(defs[i].Count);

            stbRanges[i].font_size = m_FontSize;
            stbRanges[i].first_unicode_codepoint_in_range = defs[i].Start;
            stbRanges[i].num_chars = defs[i].Count;
            stbRanges[i].chardata_for_range = range.PackedChars.data();

            m_Ranges.push_back(std::move(range));
            m_RangeMap[defs[i].Start] = i; // 검색용 인덱스 저장
        }

        stbtt_PackFontRanges(&pc, fontBuffer.data(), 0, stbRanges.data(), (int)stbRanges.size());
        stbtt_PackEnd(&pc);

        // 4. DX11 텍스처 생성 (R8_UNORM 권장하나, 기존 구조에 맞춰 RGBA 확장)
        uint32_t* rgba = new uint32_t[width * height];
        for (int i = 0; i < width * height; ++i) 
        {
            uint8_t a = bitmap[i];
            rgba[i] = (a << 24) | 0x00FFFFFF; // 하얀색 글씨 + Alpha
        }

        m_AtlasTexture = Texture2D::Create(width, height, rgba);
        delete[] rgba;

        std::cout << "[Font Info] 다국어 폰트 아틀라스 굽기 완료! (" << width << "x" << height << ")" << std::endl;

        return true;
    }

    bool Font::GetGlyphInfo(uint32_t codepoint, float* x, float* y, stbtt_aligned_quad& q) 
    {
        // 코드포인트가 속한 범위를 찾습니다.
        for (const auto& range : m_Ranges) 
        {
            if (codepoint >= range.FirstCodepoint && codepoint < range.FirstCodepoint + range.CharCount) 
            {
                stbtt_GetPackedQuad(range.PackedChars.data(), 4096, 4096,
                    codepoint - range.FirstCodepoint, x, y, &q, 1);
                return true;
            }
        }
        return false; // 없는 글자
    }
}