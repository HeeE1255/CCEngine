#include "Platform/DirectX11/DX11Texture.h"
#include "Platform/DirectX11/DX11Context.h"
#include "stb_image.h"
#include <iostream>

namespace CCEngine
{
    // 텍스처 생성 함수: 파일 경로를 받아서 텍스처 객체를 생성하는 정적 함수
    Texture2D* Texture2D::Create(const std::string& path)
    {
        return new DX11Texture2D(path);
    }

    DX11Texture2D::DX11Texture2D(const std::string& path)
        : m_Path(path)
    {
        int width, height, channels;

        // 이미지 파일을 로드하여 CPU 메모리에 픽셀 데이터 가져오기 (RGBA 4채널로 강제 변환)
        stbi_set_flip_vertically_on_load(1); // 다이렉트X는 UV 좌표가 y축 반대라서 플립 <>
        stbi_uc* data = stbi_load(path.c_str(), &width, &height, &channels, 4);

        if (data == nullptr)
        {
            std::cout << "Failed to load texture: " << path << std::endl;
            return;
        }

        // 다이렉트X 텍스처 리소스 만들기
        D3D11_TEXTURE2D_DESC textureDesc = {}; 
        textureDesc.Width = width; 
        textureDesc.Height = height;
        textureDesc.MipLevels = 1;  // 밉맵 생략 (나중에 성능 최적화할 때 추가 가능)
        textureDesc.ArraySize = 1;  // 2D 텍스처는 배열 크기가 1
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // RGBA 8비트 표준 포맷
        textureDesc.SampleDesc.Count = 1; // 멀티샘플링 없음
        textureDesc.Usage = D3D11_USAGE_DEFAULT; // GPU에서 읽고 쓰는 용도로 사용
        textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE; // 셰이더에서 읽겠다고 표시

        D3D11_SUBRESOURCE_DATA initialData = {};
        initialData.pSysMem = data; // CPU 메모리에 있는 픽셀 데이터 포인터
        initialData.SysMemPitch = width * 4; // 한 줄의 데이터 크기 (픽셀 수 * 4바이트 RGBA)

        ID3D11Texture2D* texture = nullptr;
        DX11Context::Get()->GetDevice()->CreateTexture2D(&textureDesc, &initialData, &texture);

        // 텍스처 뷰 만들기: 셰이더에서 텍스처를 읽을 때 사용하는 뷰 객체
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = textureDesc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;

        DX11Context::Get()->GetDevice()->CreateShaderResourceView(texture, &srvDesc, &m_TextureView);

        // 해제: 텍스처는 뷰가 참조하므로 뷰가 생성된 후에 해제해도 됩니다. CPU 메모리의 픽셀 데이터도 이제 필요 없으므로 해제합니다
        texture->Release();
        stbi_image_free(data);
    }

    DX11Texture2D::~DX11Texture2D()
    {
        if (m_TextureView) m_TextureView->Release();
    }

    void DX11Texture2D::Bind(uint32_t slot) const
    {
        // 셰이더에서 텍스처를 읽을 때 사용하는 슬롯 번호에 텍스처 뷰를 바인딩합니다. 슬롯 번호는 셰이더 코드에서 Texture2D가 선언된 순서에 따라 결정됩니다 (예: slot0, slot1 등)
        DX11Context::Get()->GetDeviceContext()->PSSetShaderResources(slot, 1, &m_TextureView);
    }

    void DX11Texture2D::Unbind(uint32_t slot) const
    {
        ID3D11ShaderResourceView* nullSRV = nullptr;
        DX11Context::Get()->GetDeviceContext()->PSSetShaderResources(slot, 1, &nullSRV);
    }
}