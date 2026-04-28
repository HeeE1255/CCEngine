#include "DX11Framebuffer.h"
#include "Platform/DirectX11/DX11Context.h"

namespace CCEngine {

    // ID 버퍼용 포맷: 32비트 부호 있는 정수 (엔티티 ID 저장용)
#define DXGI_FORMAT_ID_BUFFER DXGI_FORMAT_R32_SINT

    DX11Framebuffer::DX11Framebuffer(const FramebufferSpecification& spec)
        : m_Specification(spec)
    {
        Invalidate();
    }

    DX11Framebuffer::~DX11Framebuffer()
    {
        if (m_RenderTargetTexture) m_RenderTargetTexture->Release();
        if (m_RenderTargetView) m_RenderTargetView->Release();
        if (m_ShaderResourceView) m_ShaderResourceView->Release();

        if (m_IDTexture) m_IDTexture->Release();
        if (m_IDView) m_IDView->Release();
        if (m_IDShaderResourceView) m_IDShaderResourceView->Release();

        if (m_DepthStencilBuffer) m_DepthStencilBuffer->Release();
        if (m_DepthStencilView) m_DepthStencilView->Release();
    }

    void DX11Framebuffer::Invalidate()
    {
        // 1. 기존 리소스 해제
        if (m_RenderTargetTexture) { m_RenderTargetTexture->Release(); m_RenderTargetTexture = nullptr; }
        if (m_RenderTargetView) { m_RenderTargetView->Release(); m_RenderTargetView = nullptr; }
        if (m_ShaderResourceView) { m_ShaderResourceView->Release(); m_ShaderResourceView = nullptr; }

        if (m_IDTexture) { m_IDTexture->Release(); m_IDTexture = nullptr; }
        if (m_IDView) { m_IDView->Release(); m_IDView = nullptr; }
        if (m_IDShaderResourceView) { m_IDShaderResourceView->Release(); m_IDShaderResourceView = nullptr; }

        if (m_DepthStencilBuffer) { m_DepthStencilBuffer->Release(); m_DepthStencilBuffer = nullptr; }
        if (m_DepthStencilView) { m_DepthStencilView->Release(); m_DepthStencilView = nullptr; }

        auto device = DX11Context::Get()->GetDevice();
        HRESULT hr; 

        // 2. 색상 버퍼 (Slot 0) 생성
        D3D11_TEXTURE2D_DESC textureDesc = {};
        textureDesc.Width = m_Specification.Width;
        textureDesc.Height = m_Specification.Height;
        textureDesc.MipLevels = 1;
        textureDesc.ArraySize = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Usage = D3D11_USAGE_DEFAULT;
        textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        hr = device->CreateTexture2D(&textureDesc, nullptr, &m_RenderTargetTexture);
        if (SUCCEEDED(hr) && m_RenderTargetTexture != nullptr)
        {
            device->CreateRenderTargetView(m_RenderTargetTexture, nullptr, &m_RenderTargetView);
            device->CreateShaderResourceView(m_RenderTargetTexture, nullptr, &m_ShaderResourceView);
        }

        // 3. ID 버퍼 (Slot 1) 생성 - 정수형 포맷 사용
        D3D11_TEXTURE2D_DESC idDesc = textureDesc;
        idDesc.Format = DXGI_FORMAT_ID_BUFFER;

        hr = device->CreateTexture2D(&idDesc, nullptr, &m_IDTexture);
        if (SUCCEEDED(hr) && m_IDTexture != nullptr)
        {
            device->CreateRenderTargetView(m_IDTexture, nullptr, &m_IDView);
            device->CreateShaderResourceView(m_IDTexture, nullptr, &m_IDShaderResourceView);
        }

        // 4. 깊이 버퍼 (Depth Stencil) 생성
        D3D11_TEXTURE2D_DESC depthDesc = {};
        depthDesc.Width = m_Specification.Width;
        depthDesc.Height = m_Specification.Height;
        depthDesc.MipLevels = 1;
        depthDesc.ArraySize = 1;
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

        device->CreateTexture2D(&depthDesc, nullptr, &m_DepthStencilBuffer);
        device->CreateDepthStencilView(m_DepthStencilBuffer, nullptr, &m_DepthStencilView);
    }

    void DX11Framebuffer::Bind()
    {
        auto context = DX11Context::Get()->GetDeviceContext();

        // 픽셀 셰이더에서 사용 중이던 텍스처(SRV) 강제 해제 (리소스 충돌 방지)
        ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
        context->PSSetShaderResources(0, 2, nullSRVs);

        D3D11_VIEWPORT viewport = { 0.0f, 0.0f, (float)m_Specification.Width, (float)m_Specification.Height, 0.0f, 1.0f };
        context->RSSetViewports(1, &viewport);

        // MRT 장착: 색상 버퍼와 ID 버퍼를 동시에 바인딩
        ID3D11RenderTargetView* rtvs[2] = { m_RenderTargetView, m_IDView };
        context->OMSetRenderTargets(2, rtvs, m_DepthStencilView);

        // 색상 버퍼 지우기
        float clearColor[] = { 0.1f, 0.1f, 0.1f, 1.0f };
        context->ClearRenderTargetView(m_RenderTargetView, clearColor);

        // ID 버퍼 지우기 (-1로 초기화)
        ClearAttachment(1, -1);

        // 깊이 버퍼 지우기
        context->ClearDepthStencilView(m_DepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    }

    void DX11Framebuffer::Unbind()
    {
        auto context = DX11Context::Get();
        auto deviceContext = context->GetDeviceContext();

        // Backbuffer로 다시 복귀
        ID3D11RenderTargetView* backBuffer = context->GetBackBufferRTV();
        deviceContext->OMSetRenderTargets(1, &backBuffer, nullptr);
    }

    void DX11Framebuffer::Resize(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0 || width > 8192 || height > 8192) return;
        m_Specification.Width = width;
        m_Specification.Height = height;
        Invalidate();
    }

    int DX11Framebuffer::ReadPixel(uint32_t x, uint32_t y)
    {
        // 뷰포트 범위를 벗어나면 -1 반환
        if (x >= m_Specification.Width || y >= m_Specification.Height)
            return -1;

        auto device = DX11Context::Get()->GetDevice();
        auto context = DX11Context::Get()->GetDeviceContext();

        // 1. CPU가 읽을 수 있는 1x1 크기의 Staging 텍스처 생성
        D3D11_TEXTURE2D_DESC stagingDesc = {};
        stagingDesc.Width = 1;
        stagingDesc.Height = 1;
        stagingDesc.MipLevels = 1;
        stagingDesc.ArraySize = 1;
        stagingDesc.Format = DXGI_FORMAT_ID_BUFFER;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.Usage = D3D11_USAGE_STAGING;       // 중요: CPU 읽기 전용
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        ID3D11Texture2D* stagingTexture = nullptr;
        HRESULT hr = device->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture);
        if (FAILED(hr)) return -1;

        // 2. ID 텍스처의 마우스 위치(x, y) 1픽셀만 Staging 텍스처로 복사
        D3D11_BOX srcBox;
        srcBox.left = x;
        srcBox.right = x + 1;
        srcBox.top = y;
        srcBox.bottom = y + 1;
        srcBox.front = 0;
        srcBox.back = 1;

        context->CopySubresourceRegion(stagingTexture, 0, 0, 0, 0, m_IDTexture, 0, &srcBox);

        // 3. 복사된 1픽셀 데이터를 CPU로 매핑해서 읽기
        D3D11_MAPPED_SUBRESOURCE mapped;
        context->Map(stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
        int pixelData = *((int*)mapped.pData);
        context->Unmap(stagingTexture, 0);

        // 4. Staging 텍스처 해제 후 반환
        stagingTexture->Release();

        return pixelData;
    }

    void DX11Framebuffer::ClearAttachment(uint32_t attachmentIndex, int value)
    {
        auto context = DX11Context::Get()->GetDeviceContext();

        if (attachmentIndex == 1 && m_IDView)
        {
            float clearColor[4] = { (float)value, 0.0f, 0.0f, 0.0f };
            context->ClearRenderTargetView(m_IDView, clearColor);
        }
    }
}