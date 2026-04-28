#pragma once
#include <d3d11.h>
#include "Core.h"

namespace CCEngine {

    class CC_API RenderState
    {
    public:
        static void Init();
        static void Shutdown();

        // =========================================
        // 블렌드 상태 프리셋들
        // ==========================================
        static ID3D11BlendState* Opaque;       // 불투명 (기본)
        static ID3D11BlendState* Transparent;  // 반투명 (알파 섞기)
        static ID3D11BlendState* MRTPicking;   // 다중 타겟 (0:알파 켜짐, 1:알파 꺼짐)
        static ID3D11RasterizerState* CullBack; // 3D 기본값 (뒷면 숨김)
        static ID3D11RasterizerState* CullNone;
        // 테스트(Depth/Stencil)나 컬링(Rasterizer) 상태도 여기에

    };

}