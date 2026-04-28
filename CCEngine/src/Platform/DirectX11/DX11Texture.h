#pragma once
#include "Renderer/Texture.h"
#include <d3d11.h>
#include <string>

namespace CCEngine
{
    class DX11Texture2D : public Texture2D
    {
    public:
        DX11Texture2D(const std::string& path);
        DX11Texture2D(uint32_t width, uint32_t height); // 빈 텍스처 생성자
		DX11Texture2D(uint32_t width, uint32_t height, void* data); // 데이터로 텍스처 생성자
        virtual ~DX11Texture2D();

        virtual void Bind(uint32_t slot = 0) const override;
        virtual void Unbind(uint32_t slot = 0) const override;

        // 구현할 가상 함수들
        virtual void SetData(void* data, uint32_t size) override;
        virtual void* GetRendererID() const override { return (void*)m_TextureView; }

    private:
        std::string m_Path;
        uint32_t m_Width = 0;
        uint32_t m_Height = 0; 

        ID3D11Texture2D* m_Texture = nullptr; // [중요] SetData를 위해 원본 텍스처 보관
        ID3D11ShaderResourceView* m_TextureView = nullptr;
    };
}