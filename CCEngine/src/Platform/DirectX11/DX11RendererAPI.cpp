#include "Platform/DirectX11/DX11RendererAPI.h"
#include "Platform/DirectX11/DX11Context.h"
#include <d3d11.h>

namespace CCEngine
{
    void DX11RendererAPI::SetClearColor(float r, float g, float b, float a)
    {
        m_ClearColor[0] = r;
        m_ClearColor[1] = g;
        m_ClearColor[2] = b;
        m_ClearColor[3] = a;
    }

    void DX11RendererAPI::Clear()
    {
        DX11Context::Get()->Clear(m_ClearColor[0], m_ClearColor[1], m_ClearColor[2], m_ClearColor[3]);
    }

    void DX11RendererAPI::DrawIndexed(IndexBuffer* indexBuffer)
    {
        auto context = DX11Context::Get()->GetDeviceContext();

        // 정점을 삼각형 리스트로 해석하라고 DX11에 명령!
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // 인덱스 버퍼의 개수만큼 그리라고 드로우 콜!
        context->DrawIndexed(indexBuffer->GetCount(), 0, 0);
    }

    void DX11RendererAPI::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        if (DX11Context::Get() == nullptr)
        {
            return;
        }

        auto context = DX11Context::Get()->GetDeviceContext();

        D3D11_VIEWPORT viewport;
        ZeroMemory(&viewport, sizeof(D3D11_VIEWPORT));

        viewport.TopLeftX = (float)x;
        viewport.TopLeftY = (float)y;
        viewport.Width = (float)width;
        viewport.Height = (float)height;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        // 창 크기가 변했을 때 DX11 뷰포트를 새로운 크기로 덮어씌웁니다.
        context->RSSetViewports(1, &viewport);
    }

    void DX11RendererAPI::ResizeContext(uint32_t width, uint32_t height)
    {
        if (DX11Context::Get() == nullptr)
        {
            return;
        }

        // DX11 스왑체인의 버퍼 크기를 직접 변경하는 진짜 로직 호출!
        DX11Context::Get()->ResizeBuffers(width, height);
    }
}