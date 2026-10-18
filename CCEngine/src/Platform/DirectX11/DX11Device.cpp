#include "DX11Device.h"
#include <stdexcept>
#include <string>

namespace CCEngine
{
    DX11Device::DX11Device()
    {
        UINT flags = 0;
#ifdef CC_DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
        const HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            flags, levels, 1, D3D11_SDK_VERSION, m_Device.GetAddressOf(), nullptr, m_Immediate.GetAddressOf());
        if (FAILED(result))
            throw std::runtime_error("D3D11CreateDevice failed (HRESULT="
                + std::to_string(static_cast<unsigned long>(result)) + ").");
    }

    DX11Device::~DX11Device()
    {
        // 창 하나의 종료가 아니라 공유 소유자가 모두 사라진 시점에 바인딩 참조를 놓는다.
        if (m_Immediate)
        {
            m_Immediate->ClearState();
            m_Immediate->Flush();
        }
    }
}
