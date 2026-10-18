#pragma once
#include "Renderer/GraphicsDevice.h"
#include <d3d11.h>
#include <wrl/client.h>

namespace CCEngine
{
    class CC_API DX11Device final : public GraphicsDevice
    {
    public:
        DX11Device();
        ~DX11Device() override;
        RendererAPI::API GetBackend() const override { return RendererAPI::API::DirectX11; }
        ID3D11Device* Native() const { return m_Device.Get(); }
        ID3D11DeviceContext* Immediate() const { return m_Immediate.Get(); }
    private:
        Microsoft::WRL::ComPtr<ID3D11Device> m_Device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_Immediate;
    };
}
