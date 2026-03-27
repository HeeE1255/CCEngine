#pragma once
#include "Renderer/Texture.h"
#include <d3d11.h>

namespace CCEngine
{
    class DX11Texture2D : public Texture2D
    {
    public:
        DX11Texture2D(const std::string& path);
        virtual ~DX11Texture2D();

        virtual void Bind(uint32_t slot = 0) const override;
        virtual void Unbind(uint32_t slot = 0) const override;

    private:
        ID3D11ShaderResourceView* m_TextureView = nullptr; // GPU 렌더링용 뷰
        std::string m_Path; // 디버깅용 경로 저장
    };
}