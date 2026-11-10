#pragma once
#include "Core.h"
#include "Renderer/Texture.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace CCEngine
{
    struct TextureMipOptions
    {
        TextureColorSpace ColorSpace = TextureColorSpace::Linear;
        TextureMipContent Content = TextureMipContent::Color;
        size_t MaxBytes = 256u * 1024u * 1024u;
    };

    struct TextureMipLevel
    {
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint32_t RowPitch = 0;
        size_t Offset = 0;
        size_t Size = 0;
    };

    // GPU의 행 정렬은 backend가 담당한다. 여기서는 RGBA8 원본과 모든 mip을 조밀하게 보관한다.
    struct CC_API TextureMipChain
    {
        std::vector<TextureMipLevel> Levels;
        std::vector<uint8_t> Pixels;

        static TextureMipChain GenerateRGBA8(uint32_t width, uint32_t height,
            const void* pixels, size_t bytes, const TextureMipOptions& options = {});
    };
}
