#include "Renderer/TextureMipChain.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace CCEngine
{
    namespace
    {
        uint8_t Encode(double value)
        {
            return static_cast<uint8_t>(std::floor((std::clamp)(value, 0.0, 1.0) * 255.0 + 0.5));
        }

        const std::array<double, 256>& SRGBDecodeTable()
        {
            static const auto table = [] {
                std::array<double, 256> values{};
                for (size_t i = 0; i < values.size(); ++i)
                {
                    const double v = static_cast<double>(i) / 255.0;
                    values[i] = v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
                }
                return values;
            }();
            return table;
        }

        double EncodeSRGB(double linear)
        {
            return linear <= 0.0031308 ? linear * 12.92 : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
        }

        void Downsample(const TextureMipLevel& source, const TextureMipLevel& target,
            std::vector<uint8_t>& bytes, const TextureMipOptions& options)
        {
            const bool color = options.Content == TextureMipContent::Color;
            const bool normal = options.Content == TextureMipContent::Normal;
            const bool srgb = options.ColorSpace == TextureColorSpace::SRGB;
            const auto& decode = SRGBDecodeTable();
            for (uint32_t y = 0; y < target.Height; ++y)
            {
                const double top = double(y) * source.Height / target.Height;
                const double bottom = double(y + 1) * source.Height / target.Height;
                const auto endY = (std::min)(source.Height, static_cast<uint32_t>(std::ceil(bottom)));
                for (uint32_t x = 0; x < target.Width; ++x)
                {
                    const double left = double(x) * source.Width / target.Width;
                    const double right = double(x + 1) * source.Width / target.Width;
                    const auto endX = (std::min)(source.Width, static_cast<uint32_t>(std::ceil(right)));
                    double rgb[3] = {};
                    double alpha = 0.0;
                    const double area = (right - left) * (bottom - top);
                    // 홀수 크기도 끝 행/열을 버리지 않도록 픽셀이 차지하는 면적만큼 가중한다.
                    for (uint32_t sy = static_cast<uint32_t>(top); sy < endY; ++sy)
                    {
                        const double wy = (std::min)(bottom, double(sy + 1)) - (std::max)(top, double(sy));
                        for (uint32_t sx = static_cast<uint32_t>(left); sx < endX; ++sx)
                        {
                            const double weight = wy * ((std::min)(right, double(sx + 1)) - (std::max)(left, double(sx)));
                            const auto* pixel = bytes.data() + source.Offset + size_t(sy) * source.RowPitch + size_t(sx) * 4;
                            const double a = double(pixel[3]) / 255.0;
                            alpha += weight * a;
                            for (size_t c = 0; c < 3; ++c)
                            {
                                double value = srgb ? decode[pixel[c]] : double(pixel[c]) / 255.0;
                                if (normal) value = value * 2.0 - 1.0;
                                // 투명 texel의 숨은 색이 테두리에 번지지 않도록 선형 premultiplied 색으로 축소한다.
                                rgb[c] += weight * value * (color ? a : 1.0);
                            }
                        }
                    }
                    for (auto& value : rgb) value /= color ? (alpha > 0.0 ? alpha : 1.0) : area;
                    if (normal)
                    {
                        const double length = std::sqrt(rgb[0] * rgb[0] + rgb[1] * rgb[1] + rgb[2] * rgb[2]);
                        // 서로 반대인 법선이 상쇄되면 정면 법선으로 복귀한다. NaN을 다음 mip으로 전파하지 않는다.
                        if (length > 1.0e-8)
                            for (auto& value : rgb) value /= length;
                        else { rgb[0] = 0.0; rgb[1] = 0.0; rgb[2] = 1.0; }
                        for (auto& value : rgb) value = value * 0.5 + 0.5;
                    }
                    auto* pixel = bytes.data() + target.Offset + size_t(y) * target.RowPitch + size_t(x) * 4;
                    for (size_t c = 0; c < 3; ++c) pixel[c] = Encode(srgb ? EncodeSRGB(rgb[c]) : rgb[c]);
                    pixel[3] = Encode(alpha / area);
                }
            }
        }
    }

    TextureMipChain TextureMipChain::GenerateRGBA8(uint32_t width, uint32_t height,
        const void* pixels, size_t bytes, const TextureMipOptions& options)
    {
        if (!pixels || !width || !height || width > 16384 || height > 16384)
            throw std::invalid_argument("RGBA8 mip source must have pixels and dimensions in [1, 16384]");
        if (options.ColorSpace != TextureColorSpace::Linear && options.ColorSpace != TextureColorSpace::SRGB)
            throw std::invalid_argument("Invalid mip color space");
        if (options.Content != TextureMipContent::Color && options.Content != TextureMipContent::Data &&
            options.Content != TextureMipContent::Normal)
            throw std::invalid_argument("Invalid mip content type");
        if (options.Content != TextureMipContent::Color && options.ColorSpace != TextureColorSpace::Linear)
            throw std::invalid_argument("Data and normal mip sources must be linear");
        if (uint64_t(width) * height * 4 != bytes)
            throw std::invalid_argument("RGBA8 mip source byte count mismatch");

        TextureMipChain result;
        size_t total = 0;
        // 모든 레벨의 예산을 먼저 검사한다. 중간까지 메모리를 할당한 뒤 제한 초과를 발견하지 않는다.
        for (uint32_t w = width, h = height;; w = (std::max)(1u, w / 2), h = (std::max)(1u, h / 2))
        {
            const size_t size = size_t(w) * h * 4;
            if (total > options.MaxBytes || size > options.MaxBytes - total)
                throw std::length_error("RGBA8 mip chain exceeds memory budget");
            result.Levels.push_back({ w, h, w * 4, total, size });
            total += size;
            if (w == 1 && h == 1) break;
        }
        result.Pixels.resize(total);
        std::memcpy(result.Pixels.data(), pixels, bytes);
        for (size_t level = 1; level < result.Levels.size(); ++level)
            Downsample(result.Levels[level - 1], result.Levels[level], result.Pixels, options);
        return result;
    }
}
