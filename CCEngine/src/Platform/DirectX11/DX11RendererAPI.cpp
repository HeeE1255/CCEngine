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
        //DX11Context::Get()->Clear(m_ClearColor[0], m_ClearColor[1], m_ClearColor[2], m_ClearColor[3]);
        auto context = DX11Context::Get()->GetDeviceContext();

        // 1. 현재 파이프라인에 꽂혀있는 타겟(프레임버퍼 등)을 동적으로 가져옵니다.
        ID3D11RenderTargetView* currentRTV = nullptr;
        ID3D11DepthStencilView* currentDSV = nullptr;
        context->OMGetRenderTargets(1, &currentRTV, &currentDSV);

        // 2. 프레임버퍼가 바인딩되어 있다면 그것을 지웁니다. (메인 화면 탈취 방지!)
        if (currentRTV)
        {
            float clearColor[4] = { m_ClearColor[0], m_ClearColor[1], m_ClearColor[2], m_ClearColor[3] };
            context->ClearRenderTargetView(currentRTV, clearColor);
            currentRTV->Release(); // Get 함수는 참조 카운트를 올리므로 Release 필수
        }
        else
        {
            // 바인딩된 게 없으면 기존처럼 Context(메인 화면) Clear 호출
            DX11Context::Get()->Clear(m_ClearColor[0], m_ClearColor[1], m_ClearColor[2], m_ClearColor[3]);
        }

        if (currentDSV)
        {
            context->ClearDepthStencilView(currentDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
            currentDSV->Release();
        }
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

    void DX11RendererAPI::SetDepthTest(bool enable)
    {
        auto device = DX11Context::Get()->GetDevice();
        auto context = DX11Context::Get()->GetDeviceContext();

        static ID3D11DepthStencilState* enabledState = nullptr;
        static ID3D11DepthStencilState* disabledState = nullptr;

        if (!enabledState)
        {
            // 
            CD3D11_DEPTH_STENCIL_DESC desc(D3D11_DEFAULT);
            desc.DepthEnable = TRUE;
            desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
            desc.DepthFunc = D3D11_COMPARISON_LESS;
            device->CreateDepthStencilState(&desc, &enabledState);

            desc.DepthEnable = FALSE;
            desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            device->CreateDepthStencilState(&desc, &disabledState);
        }

        // 상태가 정상적으로 생성되었을 때만 적용
        if (enabledState && disabledState)
        {
            context->OMSetDepthStencilState(enable ? enabledState : disabledState, 1);
        }
    }
}