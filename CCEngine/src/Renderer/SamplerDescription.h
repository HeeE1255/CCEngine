#pragma once
#include <cstdint>
#include <stdexcept>

namespace CCEngine
{
    enum class SamplerAddress : uint8_t { Repeat, Clamp, Mirror };
    enum class SamplerFilter : uint8_t { Nearest, Linear };
    enum class SamplerMipFilter : uint8_t { Disabled, Nearest, Linear };

    // 기존 재질은 mip 0만 사용한다. anisotropy는 선형 min/mag/mip 조합에서만 허용한다.
    struct SamplerDescription
    {
        SamplerAddress U = SamplerAddress::Clamp;
        SamplerAddress V = SamplerAddress::Clamp;
        SamplerFilter Min = SamplerFilter::Linear;
        SamplerFilter Mag = SamplerFilter::Linear;
        SamplerMipFilter Mip = SamplerMipFilter::Disabled;
        uint8_t Anisotropy = 1;

        uint32_t Key() const
        {
            if (uint32_t(U) > 2 || uint32_t(V) > 2 || uint32_t(Min) > 1 || uint32_t(Mag) > 1 ||
                uint32_t(Mip) > 2 || Anisotropy < 1 || Anisotropy > 16 ||
                (Anisotropy > 1 && (Min != SamplerFilter::Linear || Mag != SamplerFilter::Linear || Mip != SamplerMipFilter::Linear)))
                throw std::invalid_argument("Invalid sampler description");
            return uint32_t(U) + 3 * uint32_t(V) + 9 * uint32_t(Min) + 18 * uint32_t(Mag) +
                36 * uint32_t(Mip) + 108 * (Anisotropy - 1);
        }
        void ValidateDeviceLimit(uint32_t maxAnisotropy) const
        {
            Key();
            if (Anisotropy > maxAnisotropy)
                throw std::invalid_argument("Requested anisotropy exceeds device support");
        }
    };
}
