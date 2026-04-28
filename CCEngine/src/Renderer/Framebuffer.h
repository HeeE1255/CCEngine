#pragma once

#include "Core.h"
#include <cstdint>

namespace CCEngine {

    // 프레임버퍼 생성 시 필요한 설정값 (해상도 등)
    struct FramebufferSpecification
    {
        uint32_t Width = 0;
        uint32_t Height = 0;
        // 나중에 멀티샘플링(MSAA)이나 포맷 설정도 여기에 추가
    };

    class CC_API Framebuffer
    {
    public:
        virtual ~Framebuffer() = default;

        // 프레임버퍼를 활성화
        virtual void Bind() = 0;
        // 프레임버퍼 비활성화
        virtual void Unbind() = 0;

        // 뷰포트 창 크기가 바뀔 때 버퍼 해상도도 다시 맞춰주는 함수
        virtual void Resize(uint32_t width, uint32_t height) = 0;

        // DirectX 11에서는 ID3D11ShaderResourceView* 포인터를 반환
        //virtual void* GetColorAttachmentRendererID() const = 0;

        virtual const FramebufferSpecification& GetSpecification() const = 0;

        // 특정 슬롯(0:색상, 1:ID)의 텍스처를 렌더러가 사용할 수 있도록 ID를 반환하는 함수
        virtual void* GetColorAttachmentRendererID(uint32_t index = 0) const = 0;

        // ID 버퍼(슬롯1)에서 마우스 픽셀의 정수 값을 읽어오는 함수
        virtual int ReadPixel(uint32_t x, uint32_t y) = 0;

        // ID 버퍼를 깨끗하게 지우는 함수
        virtual void ClearAttachment(uint32_t attachmentIndex, int value) = 0;

        // 팩토리 함수 (플랫폼에 맞는 프레임버퍼 객체 생성)
        static Framebuffer* Create(const FramebufferSpecification& spec);
    };

}