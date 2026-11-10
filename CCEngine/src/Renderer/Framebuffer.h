#pragma once

#include "Core.h"
#include "Renderer/RendererHandle.h"
#include <cstdint>
#include <vector>

namespace CCEngine {

    enum class FramebufferColorFormat { RGBA8, RGBA16Float };

    // 프레임버퍼 생성 시 필요한 설정값 (해상도 등)
    struct FramebufferSpecification
    {
        uint32_t Width = 0;
        uint32_t Height = 0;
        FramebufferColorFormat ColorFormat = FramebufferColorFormat::RGBA8;
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

        virtual const FramebufferSpecification& GetSpecification() const = 0;

        // 특정 슬롯(0:색상, 1:ID)의 텍스처를 렌더러가 사용할 수 있도록 ID를 반환하는 함수
        virtual RendererHandle GetColorAttachmentRendererID(uint32_t index = 0) const = 0;

        // ID 버퍼(슬롯1)에서 마우스 픽셀의 정수 값을 읽어오는 함수
        virtual int ReadPixel(uint32_t x, uint32_t y) = 0;

        // 색상 버퍼 전체를 CPU 메모리로 복사한다.
        // 썸네일처럼 한 번 캡처해서 재사용하는 용도에만 쓰고, 매 프레임 호출하면 GPU 대기가 생긴다.
        virtual bool ReadColorPixels(std::vector<uint32_t>& outPixels) = 0;

        // ID 버퍼를 깨끗하게 지우는 함수
        virtual void ClearAttachment(uint32_t attachmentIndex, int value) = 0;

        // 후처리용 중간 타깃을 사용할 때도 최종 뷰의 피킹 정보가 유지되도록
        // 같은 형식/크기의 첨부 이미지를 GPU에서 직접 복사한다.
        virtual bool CopyAttachmentTo(uint32_t attachmentIndex, Framebuffer& destination) = 0;

        // 팩토리 함수 (플랫폼에 맞는 프레임버퍼 객체 생성)
        static Framebuffer* Create(const FramebufferSpecification& spec);
    };

}
