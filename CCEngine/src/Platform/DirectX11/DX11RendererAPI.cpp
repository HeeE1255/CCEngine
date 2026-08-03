#include "Platform/DirectX11/DX11RendererAPI.h"
#include "Platform/DirectX11/DX11Context.h"
#include <d3d11.h>
#include <algorithm>

namespace CCEngine
{
    DX11RendererAPI::DX11RendererAPI()
    {
        auto device = DX11Context::Get()->GetDevice();

        D3D11_BLEND_DESC blendDesc = {};

        blendDesc.AlphaToCoverageEnable = FALSE;
        blendDesc.IndependentBlendEnable = FALSE;
        blendDesc.RenderTarget[0].BlendEnable = FALSE;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        device->CreateBlendState(&blendDesc, &m_OpaqueBlendState);

        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        device->CreateBlendState(&blendDesc, &m_TransparentBlendState);

        blendDesc.IndependentBlendEnable = TRUE;
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[1].BlendEnable = FALSE;
        blendDesc.RenderTarget[1].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        device->CreateBlendState(&blendDesc, &m_MRTPickingBlendState);

        D3D11_RASTERIZER_DESC rsDesc = {};
        rsDesc.FillMode = D3D11_FILL_SOLID;
        rsDesc.FrontCounterClockwise = FALSE;
        rsDesc.DepthClipEnable = TRUE;

        rsDesc.CullMode = D3D11_CULL_BACK;
        device->CreateRasterizerState(&rsDesc, &m_CullBackState);

        rsDesc.CullMode = D3D11_CULL_NONE;
        device->CreateRasterizerState(&rsDesc, &m_CullNoneState);
    }

    DX11RendererAPI::~DX11RendererAPI()
    {
        if (m_OpaqueBlendState) { m_OpaqueBlendState->Release(); m_OpaqueBlendState = nullptr; }
        if (m_TransparentBlendState) { m_TransparentBlendState->Release(); m_TransparentBlendState = nullptr; }
        if (m_MRTPickingBlendState) { m_MRTPickingBlendState->Release(); m_MRTPickingBlendState = nullptr; }
        if (m_CullBackState) { m_CullBackState->Release(); m_CullBackState = nullptr; }
        if (m_CullNoneState) { m_CullNoneState->Release(); m_CullNoneState = nullptr; }
    }

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

    void DX11RendererAPI::DrawIndexed(uint32_t indexCount)
    {
        auto context = DX11Context::Get()->GetDeviceContext();

        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->DrawIndexed(indexCount, 0, 0);
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

    void DX11RendererAPI::SetScissorEnable(bool enable)
    {
        auto device = DX11Context::Get()->GetDevice();
        auto context = DX11Context::Get()->GetDeviceContext();

        // SetDepthTest와 동일하게 static 변수로 RasterizerState를 캐싱합니다.
        static ID3D11RasterizerState* enabledState = nullptr;
        static ID3D11RasterizerState* disabledState = nullptr;

        if (!enabledState)
        {
            D3D11_RASTERIZER_DESC desc = {};
            desc.FillMode = D3D11_FILL_SOLID;
            // UI 렌더링이 주 목적이므로 Culling을 끄거나, 기존 엔진 설정에 맞춥니다.
            desc.CullMode = D3D11_CULL_NONE;
            desc.DepthClipEnable = TRUE;

            // 가위질 켜기 상태 생성
            desc.ScissorEnable = TRUE;
            device->CreateRasterizerState(&desc, &enabledState);

            // 가위질 끄기 상태 생성
            desc.ScissorEnable = FALSE;
            device->CreateRasterizerState(&desc, &disabledState);
        }

        // 상태가 정상적으로 생성되었을 때만 파이프라인에 적용
        if (enabledState && disabledState)
        {
            context->RSSetState(enable ? enabledState : disabledState);
        }
    }

    void DX11RendererAPI::SetScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        // 렌더링 컨텍스트를 가져와서 영역만 던져줍니다.
        auto context = DX11Context::Get()->GetDeviceContext();

        D3D11_RECT rect;
        rect.left = (LONG)x;
        rect.top = (LONG)y;
        rect.right = (LONG)(x + width);
        rect.bottom = (LONG)(y + height);

        context->RSSetScissorRects(1, &rect);
    }

    void DX11RendererAPI::SetBlendMode(BlendMode mode)
    {
        auto context = DX11Context::Get()->GetDeviceContext();
        float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

        switch (mode)
        {
        case BlendMode::Opaque:
            context->OMSetBlendState(m_OpaqueBlendState, blendFactor, 0xffffffff);
            break;
        case BlendMode::Transparent:
            context->OMSetBlendState(m_TransparentBlendState, blendFactor, 0xffffffff);
            break;
        case BlendMode::MRTPicking:
            context->OMSetBlendState(m_MRTPickingBlendState, blendFactor, 0xffffffff);
            break;
        }
    }

    void DX11RendererAPI::SetCullMode(CullMode mode)
    {
        auto context = DX11Context::Get()->GetDeviceContext();

        switch (mode)
        {
        case CullMode::Back:
            context->RSSetState(m_CullBackState);
            break;
        case CullMode::None:
            context->RSSetState(m_CullNoneState);
            break;
        }
    }

    void DX11RendererAPI::BindTexture(uint32_t slot, RendererHandle rendererID)
    {
        auto context = DX11Context::Get()->GetDeviceContext();
        ID3D11ShaderResourceView* srv = static_cast<ID3D11ShaderResourceView*>(rendererID);

        context->PSSetShaderResources(slot, 1, &srv);
    }

    void DX11RendererAPI::UnbindTextures(uint32_t startSlot, uint32_t count)
    {
        if (count == 0)
            return;

        auto context = DX11Context::Get()->GetDeviceContext();
        ID3D11ShaderResourceView* nullSRVs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
        uint32_t clampedStart = (std::min)(startSlot, (uint32_t)D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
        uint32_t clampedCount = (std::min)(count, (uint32_t)D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - clampedStart);
        if (clampedCount == 0)
            return;

        context->PSSetShaderResources(clampedStart, clampedCount, nullSRVs);
    }
}
