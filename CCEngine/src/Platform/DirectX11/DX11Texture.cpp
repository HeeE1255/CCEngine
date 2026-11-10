#include "Platform/DirectX11/DX11Texture.h"
#include "Platform/DirectX11/DX11Context.h"
#include "stb_image.h"
#include <system_error>
#include <iostream>
#include "Renderer/TextureMipChain.h"
#include <wrl/client.h>

namespace CCEngine
{
    DX11Texture2D::DX11Texture2D(const std::string& path, TextureColorSpace colorSpace)
        : Texture2D(colorSpace, path), m_Path(path)
    {
        int width, height, channels;

        stbi_uc* data = stbi_load(path.c_str(), &width, &height, &channels, 4);

        if (data == nullptr)
        {
            std::cout << "Failed to load texture: " << path << std::endl;
            return;
        }

        m_Width = width;
        m_Height = height;

        D3D11_TEXTURE2D_DESC textureDesc = {};
        textureDesc.Width = m_Width;
        textureDesc.Height = m_Height;
        textureDesc.MipLevels = 1;
        textureDesc.ArraySize = 1;
        textureDesc.Format = colorSpace == TextureColorSpace::SRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Usage = D3D11_USAGE_DEFAULT;
        textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initialData = {};
        initialData.pSysMem = data;
        initialData.SysMemPitch = m_Width * 4;


        auto device = DX11Context::Get()->GetDevice();

        HRESULT hr = device->CreateTexture2D(&textureDesc, &initialData, &m_Texture);

        if (SUCCEEDED(hr) && m_Texture != nullptr)
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = textureDesc.Format;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels = 1;

            device->CreateShaderResourceView(m_Texture, &srvDesc, &m_TextureView);
        }

        stbi_image_free(data);
    }

    // =========================================================================
    // 가로, 세로 크기만 받아 메모리에 빈 텍스처 생성
    // =========================================================================
    DX11Texture2D::DX11Texture2D(uint32_t width, uint32_t height, TextureColorSpace colorSpace)
        : Texture2D(colorSpace), m_Width(width), m_Height(height)
    {
        D3D11_TEXTURE2D_DESC textureDesc = {};
        textureDesc.Width = m_Width;
        textureDesc.Height = m_Height;
        textureDesc.MipLevels = 1;
        textureDesc.ArraySize = 1;
        textureDesc.Format = colorSpace == TextureColorSpace::SRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Usage = D3D11_USAGE_DEFAULT;
        textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        // pInitialData 없이 빈 텍스처를 생성
        HRESULT hr = DX11Context::Get()->GetDevice()->CreateTexture2D(&textureDesc, nullptr, &m_Texture);

        if (SUCCEEDED(hr))
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = textureDesc.Format;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels = 1;

            DX11Context::Get()->GetDevice()->CreateShaderResourceView(m_Texture, &srvDesc, &m_TextureView);
        }
    }

    DX11Texture2D::DX11Texture2D(uint32_t width, uint32_t height, void* data, TextureColorSpace colorSpace)
        : Texture2D(colorSpace)
    {
        m_Width = width;
        m_Height = height;

        if (m_Width == 0 || m_Height == 0)
        {
            std::cout << "[DX11] 에러: 텍스처 너비나 높이가 0입니다!" << std::endl;
            return;
        }

        D3D11_TEXTURE2D_DESC textureDesc = {};
        textureDesc.Width = m_Width;
        textureDesc.Height = m_Height;
        textureDesc.MipLevels = 1;
        textureDesc.ArraySize = 1;               // ★ 필수!
        textureDesc.Format = colorSpace == TextureColorSpace::SRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.SampleDesc.Count = 1;        // ★ 필수! (0이면 무조건 실패)
        textureDesc.Usage = D3D11_USAGE_DEFAULT;
        textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        // ★ 파라미터로 넘어온 데이터 포인터를 연결!
        D3D11_SUBRESOURCE_DATA initialData = {};
        initialData.pSysMem = data;
        initialData.SysMemPitch = m_Width * 4; // RGBA 4채널이므로 픽셀당 4바이트

        auto device = DX11Context::Get()->GetDevice();
        HRESULT hr = device->CreateTexture2D(&textureDesc, &initialData, &m_Texture);

        if (FAILED(hr))
        {
            std::cout << "[DX11] 텍스처 생성 실패! 에러 코드: 0x" << std::hex << hr
                << " / 상세: " << std::system_category().message(hr) << std::endl;
            return;
        }

        // 5. 셰이더 리소스 뷰(SRV) 생성
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = textureDesc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;

        device->CreateShaderResourceView(m_Texture, &srvDesc, &m_TextureView);

    }

    DX11Texture2D::~DX11Texture2D()
    {
        if (m_TextureView) m_TextureView->Release();

        // 소멸될 때 원본 텍스처도 함께 해제
        if (m_Texture) m_Texture->Release();
    }

    void DX11Texture2D::Bind(uint32_t slot) const
    {
        DX11Context::Get()->GetDeviceContext()->PSSetShaderResources(slot, 1, &m_TextureView);
    }

    void DX11Texture2D::Unbind(uint32_t slot) const
    {
        ID3D11ShaderResourceView* nullSRV = nullptr;
        DX11Context::Get()->GetDeviceContext()->PSSetShaderResources(slot, 1, &nullSRV);
    }

    // =========================================================================
    // CPU 메모리의 픽셀 데이터를 GPU 텍스처에 덮어쓰기
    // =========================================================================
    void DX11Texture2D::SetData(void* data, uint32_t size)
    {
        if (UpdateMipData(m_Width, m_Height, data, size)) return;
        uint32_t bpp = 4; // R, G, B, A (각 1바이트)
        if (size != m_Width * m_Height * bpp)
        {
            std::cout << "Data size must be entire texture!" << std::endl;
            return;
        }

        // UpdateSubresource를 사용하여 텍스처 데이터 덮어쓰기
        DX11Context::Get()->GetDeviceContext()->UpdateSubresource(m_Texture, 0, nullptr, data, m_Width * bpp, 0);
    }

    void DX11Texture2D::UploadMipChain(const TextureMipChain& chain)
    {
        if (m_Texture && m_TextureView && m_MipLevelCount == chain.Levels.size())
        {
            // SetData에서는 SRV 주소를 유지한다. UI가 보관한 기존 핸들이 끊기지 않게 한다.
            auto* context = DX11Context::Get()->GetDeviceContext();
            for (UINT i = 0; i < chain.Levels.size(); ++i)
            {
                const auto& level = chain.Levels[i];
                context->UpdateSubresource(m_Texture, i, nullptr, chain.Pixels.data() + level.Offset, level.RowPitch, 0);
            }
            return;
        }
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = m_Width; desc.Height = m_Height;
        desc.MipLevels = static_cast<UINT>(chain.Levels.size()); desc.ArraySize = 1;
        desc.Format = m_ColorSpace == TextureColorSpace::SRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        std::vector<D3D11_SUBRESOURCE_DATA> data;
        for (const auto& level : chain.Levels)
            data.push_back({ chain.Pixels.data() + level.Offset, level.RowPitch, 0 });
        Microsoft::WRL::ComPtr<ID3D11Texture2D> image;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        auto* device = DX11Context::Get()->GetDevice();
        if (FAILED(device->CreateTexture2D(&desc, data.data(), &image)) ||
            FAILED(device->CreateShaderResourceView(image.Get(), nullptr, &view)))
            throw std::runtime_error("DX11 mip texture creation failed");
        // 전 레벨 생성이 성공한 뒤 교체한다. 실패하면 기존 텍스처는 그대로 남는다.
        if (m_TextureView) m_TextureView->Release();
        if (m_Texture) m_Texture->Release();
        m_Texture = image.Detach(); m_TextureView = view.Detach();
    }
}
