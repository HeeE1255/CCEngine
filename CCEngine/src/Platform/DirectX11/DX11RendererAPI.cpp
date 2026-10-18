#include "Platform/DirectX11/DX11RendererAPI.h"
#include "Platform/DirectX11/DX11Context.h"
#include <d3d11.h>
#include <algorithm>
#include <stdexcept>

namespace CCEngine
{
    DX11RendererAPI::DX11RendererAPI(DX11Context& context)
        : m_Context(context)
    {
        auto device = m_Context.GetDevice();

        D3D11_BLEND_DESC blendDesc = {};

        blendDesc.AlphaToCoverageEnable = FALSE;
        blendDesc.IndependentBlendEnable = FALSE;
        blendDesc.RenderTarget[0].BlendEnable = FALSE;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(device->CreateBlendState(&blendDesc, m_OpaqueBlendState.GetAddressOf())))
            throw std::runtime_error("CreateBlendState failed.");

        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        if (FAILED(device->CreateBlendState(&blendDesc, m_TransparentBlendState.GetAddressOf())))
            throw std::runtime_error("CreateBlendState failed.");

        blendDesc.IndependentBlendEnable = TRUE;
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[1].BlendEnable = FALSE;
        blendDesc.RenderTarget[1].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(device->CreateBlendState(&blendDesc, m_MRTPickingBlendState.GetAddressOf())))
            throw std::runtime_error("CreateBlendState failed.");

        for (uint32_t cullIndex = 0; cullIndex < 2; ++cullIndex)
        {
            for (uint32_t scissorIndex = 0; scissorIndex < 2; ++scissorIndex)
            {
                D3D11_RASTERIZER_DESC rsDesc = {};
                rsDesc.FillMode = D3D11_FILL_SOLID;
                rsDesc.FrontCounterClockwise = FALSE;
                rsDesc.DepthClipEnable = TRUE;
                rsDesc.CullMode = cullIndex == 0 ? D3D11_CULL_BACK : D3D11_CULL_NONE;
                rsDesc.ScissorEnable = scissorIndex != 0;
                if (FAILED(device->CreateRasterizerState(&rsDesc, m_RasterizerStates[cullIndex][scissorIndex].GetAddressOf())))
                    throw std::runtime_error("CreateRasterizerState failed.");
            }
        }
    }

    DX11RendererAPI::~DX11RendererAPI() = default;

    void DX11RendererAPI::SetClearColor(float r, float g, float b, float a)
    {
        m_ClearColor[0] = r;
        m_ClearColor[1] = g;
        m_ClearColor[2] = b;
        m_ClearColor[3] = a;
    }

    void DX11RendererAPI::Clear()
    {
        //m_Context.Clear(m_ClearColor[0], m_ClearColor[1], m_ClearColor[2], m_ClearColor[3]);
        auto context = m_Context.GetDeviceContext();

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
            m_Context.Clear(m_ClearColor[0], m_ClearColor[1], m_ClearColor[2], m_ClearColor[3]);
        }

        if (currentDSV)
        {
            context->ClearDepthStencilView(currentDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
            currentDSV->Release();
        }
    }

    void DX11RendererAPI::DrawIndexed(IndexBuffer* indexBuffer)
    {
        auto context = m_Context.GetDeviceContext();

        // 정점을 삼각형 리스트로 해석하라고 DX11에 명령!
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // 인덱스 버퍼의 개수만큼 그리라고 드로우 콜!
        context->DrawIndexed(indexBuffer->GetCount(), 0, 0);
    }

    void DX11RendererAPI::DrawIndexed(uint32_t indexCount)
    {
        auto context = m_Context.GetDeviceContext();

        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->DrawIndexed(indexCount, 0, 0);
    }

    void DX11RendererAPI::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        auto context = m_Context.GetDeviceContext();

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
        // DX11 스왑체인의 버퍼 크기를 직접 변경하는 진짜 로직 호출!
        m_Context.ResizeBuffers(width, height);
    }

    void DX11RendererAPI::SetDepthTest(bool enable)
    {
        auto device = m_Context.GetDevice();
        auto context = m_Context.GetDeviceContext();


        if (!m_DepthEnabled || !m_DepthDisabled)
        {
            // 
            CD3D11_DEPTH_STENCIL_DESC desc(D3D11_DEFAULT);
            desc.DepthEnable = TRUE;
            desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
            desc.DepthFunc = D3D11_COMPARISON_LESS;
            if (FAILED(device->CreateDepthStencilState(&desc, m_DepthEnabled.ReleaseAndGetAddressOf())))
                throw std::runtime_error("CreateDepthStencilState failed.");

            desc.DepthEnable = FALSE;
            desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            if (FAILED(device->CreateDepthStencilState(&desc, m_DepthDisabled.ReleaseAndGetAddressOf())))
                throw std::runtime_error("CreateDepthStencilState failed.");
        }

        // 상태가 정상적으로 생성되었을 때만 적용
        if (m_DepthEnabled && m_DepthDisabled)
        {
            context->OMSetDepthStencilState(enable ? m_DepthEnabled.Get() : m_DepthDisabled.Get(), 1);
        }
    }

    void DX11RendererAPI::SetScissorEnable(bool enable)
    {
        m_ScissorEnabled = enable;
        ApplyRasterizerState();
    }

    void DX11RendererAPI::SetScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        // 렌더링 컨텍스트를 가져와서 영역만 던져줍니다.
        auto context = m_Context.GetDeviceContext();

        D3D11_RECT rect;
        rect.left = (LONG)x;
        rect.top = (LONG)y;
        rect.right = (LONG)(x + width);
        rect.bottom = (LONG)(y + height);

        context->RSSetScissorRects(1, &rect);
    }

    void DX11RendererAPI::SetBlendMode(BlendMode mode)
    {
        auto context = m_Context.GetDeviceContext();
        float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

        switch (mode)
        {
        case BlendMode::Opaque:
            context->OMSetBlendState(m_OpaqueBlendState.Get(), blendFactor, 0xffffffff);
            break;
        case BlendMode::Transparent:
            context->OMSetBlendState(m_TransparentBlendState.Get(), blendFactor, 0xffffffff);
            break;
        case BlendMode::MRTPicking:
            context->OMSetBlendState(m_MRTPickingBlendState.Get(), blendFactor, 0xffffffff);
            break;
        }
    }

    void DX11RendererAPI::SetCullMode(CullMode mode)
    {
        m_CullMode = mode;
        ApplyRasterizerState();
    }

    void DX11RendererAPI::ApplyRasterizerState()
    {
        auto context = m_Context.GetDeviceContext();
        if (!context)
            return;

        const uint32_t cullIndex = m_CullMode == CullMode::Back ? 0u : 1u;
        const uint32_t scissorIndex = m_ScissorEnabled ? 1u : 0u;
        ID3D11RasterizerState* state = m_RasterizerStates[cullIndex][scissorIndex].Get();
        if (state)
            context->RSSetState(state);
    }

    void DX11RendererAPI::BindTexture(uint32_t slot, RendererHandle rendererID)
    {
        auto context = m_Context.GetDeviceContext();
        ID3D11ShaderResourceView* srv = static_cast<ID3D11ShaderResourceView*>(rendererID);

        context->PSSetShaderResources(slot, 1, &srv);
    }

    void DX11RendererAPI::UnbindTextures(uint32_t startSlot, uint32_t count)
    {
        if (count == 0)
            return;

        auto context = m_Context.GetDeviceContext();
        ID3D11ShaderResourceView* nullSRVs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
        uint32_t clampedStart = (std::min)(startSlot, (uint32_t)D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
        uint32_t clampedCount = (std::min)(count, (uint32_t)D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - clampedStart);
        if (clampedCount == 0)
            return;

        context->PSSetShaderResources(clampedStart, clampedCount, nullSRVs);
    }
}
