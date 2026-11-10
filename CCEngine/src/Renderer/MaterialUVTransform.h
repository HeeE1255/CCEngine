#pragma once
#include <DirectXMath.h>
#include <cmath>

namespace CCEngine
{
    struct MaterialUVTransform
    {
        DirectX::XMFLOAT2 Offset = { 0, 0 };
        DirectX::XMFLOAT2 Scale = { 1, 1 };
        float Rotation = 0;

        bool GetRows(DirectX::XMFLOAT4& u, DirectX::XMFLOAT4& v) const
        {
            if (!std::isfinite(Offset.x) || !std::isfinite(Offset.y) || !std::isfinite(Scale.x) ||
                !std::isfinite(Scale.y) || !std::isfinite(Rotation)) return false;
            const double c = std::cos(double(Rotation)), s = std::sin(double(Rotation));
            // Assimp TextureTransformStep의 T(0.5)*S*R*T(-0.5)*T(offset) 규약이다.
            // 이미 후처리된 UV이므로 여기에서 V축을 다시 뒤집지 않는다.
            const double a = Scale.x * c, b = -Scale.x * s, d = Scale.y * s, e = Scale.y * c;
            u = { float(a), float(b), float(0.5 + a * (Offset.x - 0.5) + b * (Offset.y - 0.5)), 0 };
            v = { float(d), float(e), float(0.5 + d * (Offset.x - 0.5) + e * (Offset.y - 0.5)), 0 };
            return std::isfinite(u.x) && std::isfinite(u.y) && std::isfinite(u.z) &&
                std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }
    };
}
