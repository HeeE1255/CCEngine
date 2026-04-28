#include "RenderState.h"
#include "Platform/DirectX11/DX11Context.h"

namespace CCEngine {

    // 정적 변수 메모리 할당
    ID3D11BlendState* RenderState::Opaque = nullptr;
    ID3D11BlendState* RenderState::Transparent = nullptr;
    ID3D11BlendState* RenderState::MRTPicking = nullptr;

    // ★ 래스터라이저 스테이트 메모리 할당
    ID3D11RasterizerState* RenderState::CullBack = nullptr;
    ID3D11RasterizerState* RenderState::CullNone = nullptr;

    void RenderState::Init()
    {
        auto device = DX11Context::Get()->GetDevice();

        // ==========================================
        // 1. 블렌드 스테이트 (Blend States)
        // ==========================================
        D3D11_BLEND_DESC blendDesc = {};

        // Opaque (불투명)
        blendDesc.AlphaToCoverageEnable = FALSE;
        blendDesc.IndependentBlendEnable = FALSE;
        blendDesc.RenderTarget[0].BlendEnable = FALSE;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        device->CreateBlendState(&blendDesc, &Opaque);

        // Transparent (반투명 - UI용)
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        device->CreateBlendState(&blendDesc, &Transparent);

        // MRTPicking (에디터 피킹용)
        blendDesc.IndependentBlendEnable = TRUE;
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[1].BlendEnable = FALSE;
        blendDesc.RenderTarget[1].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        device->CreateBlendState(&blendDesc, &MRTPicking);

        // ==========================================
        // 2. 래스터라이저 스테이트 (Rasterizer States)
        // ==========================================
        D3D11_RASTERIZER_DESC rsDesc = {};
        rsDesc.FillMode = D3D11_FILL_SOLID;
        rsDesc.FrontCounterClockwise = FALSE;
        rsDesc.DepthClipEnable = TRUE; // 깊이 클리핑은 기본적으로 켬

        // CullBack (3D 메인 카메라용 기본값)
        rsDesc.CullMode = D3D11_CULL_BACK;
        device->CreateRasterizerState(&rsDesc, &CullBack);

        // CullNone (UI, 2D 스프라이트용 - 앞뒷면 모두 그림)
        rsDesc.CullMode = D3D11_CULL_NONE;
        device->CreateRasterizerState(&rsDesc, &CullNone);
    }

    void RenderState::Shutdown()
    {
        if (Opaque) { Opaque->Release(); Opaque = nullptr; }
        if (Transparent) { Transparent->Release(); Transparent = nullptr; }
        if (MRTPicking) { MRTPicking->Release(); MRTPicking = nullptr; }

        if (CullBack) { CullBack->Release(); CullBack = nullptr; }
        if (CullNone) { CullNone->Release(); CullNone = nullptr; }
    }

}