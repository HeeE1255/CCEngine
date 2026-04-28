#pragma once
#include "Renderer/Framebuffer.h"
#include <vector> // std::vector 사용을 위해 추가

namespace CCEngine {

    class OpenGLFramebuffer : public Framebuffer
    {
    public:
        OpenGLFramebuffer(const FramebufferSpecification& spec) : m_Specification(spec) {}
        virtual ~OpenGLFramebuffer() {}

        void Invalidate() {} // OpenGL 방식의 프레임버퍼 생성 (glGenFramebuffers 등)

        virtual void Bind() override {}
        virtual void Unbind() override {}
        virtual void Resize(uint32_t width, uint32_t height) override {}

        // ====================================================================
        // index를 받는 MRT(다중 렌더 타겟)
        // ====================================================================
        virtual void* GetColorAttachmentRendererID(uint32_t index = 0) const override
        {
            // 배열 범위 초과를 막기 위한 안전 검사
            if (index < m_ColorAttachments.size())
            {
                return (void*)(uintptr_t)m_ColorAttachments[index];
            }
            return nullptr;
        }

        virtual const FramebufferSpecification& GetSpecification() const override { return m_Specification; }

        // ====================================================================
        //  마우스 피킹 및 첨부 파일 초기화 껍데기 함수
        // ====================================================================
        virtual int ReadPixel(uint32_t x, uint32_t y) override
        {
            // 나중에 glReadPixels 로직이 들어갈 자리
            return -1;
        }

        virtual void ClearAttachment(uint32_t attachmentIndex, int value) override
        {
            // 나중에 glClearTexImage 등의 로직이 들어갈 자리
        }

    private:
        uint32_t m_RendererID = 0;

        // 여러 개의 렌더 타겟(Color, EntityID 등)을 저장하기 위해 vector로
        std::vector<uint32_t> m_ColorAttachments;
        uint32_t m_DepthAttachment = 0;

        FramebufferSpecification m_Specification;
    };

}