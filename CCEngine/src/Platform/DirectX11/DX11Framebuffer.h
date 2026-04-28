#pragma once

#include "Renderer/Framebuffer.h"
#include <d3d11.h>

namespace CCEngine {

    class DX11Framebuffer : public Framebuffer
    {
    public:
        DX11Framebuffer(const FramebufferSpecification& spec);
        virtual ~DX11Framebuffer();

        void Invalidate();

        virtual void Bind() override;
        virtual void Unbind() override;
        virtual void Resize(uint32_t width, uint32_t height) override;

        // 슬롯 인덱스에 따라 0번은 색상, 1번은 ID 텍스처를 반환
        virtual void* GetColorAttachmentRendererID(uint32_t index = 0) const override
        {
            if (index == 1) return (void*)m_IDShaderResourceView;
            return (void*)m_ShaderResourceView;
        }

        virtual const FramebufferSpecification& GetSpecification() const override { return m_Specification; }

        // 마우스 좌표 픽셀의 엔티티 ID 읽기
        virtual int ReadPixel(uint32_t x, uint32_t y) override;

        // 특정 버퍼를 원하는 값으로 지우기 (주로 ID 버퍼를 -1로 지울 때 사용)
        virtual void ClearAttachment(uint32_t attachmentIndex, int value) override;

    private:
        FramebufferSpecification m_Specification;

        // 1. 색상 버퍼 (Slot 0)
        ID3D11Texture2D* m_RenderTargetTexture = nullptr;
        ID3D11RenderTargetView* m_RenderTargetView = nullptr;
        ID3D11ShaderResourceView* m_ShaderResourceView = nullptr;

        // 2. ID 버퍼 (Slot 1) - 엔티티 피킹용
        ID3D11Texture2D* m_IDTexture = nullptr;
        ID3D11RenderTargetView* m_IDView = nullptr;
        ID3D11ShaderResourceView* m_IDShaderResourceView = nullptr;

        // 3. 깊이 버퍼
        ID3D11Texture2D* m_DepthStencilBuffer = nullptr;
        ID3D11DepthStencilView* m_DepthStencilView = nullptr;
    };

}