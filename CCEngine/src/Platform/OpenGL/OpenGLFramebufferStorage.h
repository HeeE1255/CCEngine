#pragma once
#if defined(CC_ENABLE_OPENGL_RHI)
#include "OpenGLContext.h"
#include "Renderer/Framebuffer.h"
namespace CCEngine
{
    class OpenGLFramebuffer final : public Framebuffer
    {
    public:
        explicit OpenGLFramebuffer(const FramebufferSpecification& spec);
        OpenGLFramebuffer(OpenGLContext& context, const FramebufferSpecification& spec);
        ~OpenGLFramebuffer() override;
        OpenGLFramebuffer(const OpenGLFramebuffer&) = delete;
        OpenGLFramebuffer& operator=(const OpenGLFramebuffer&) = delete;
        void Bind() override;
        void Unbind() override;
        void Resize(uint32_t width, uint32_t height) override;
        RendererHandle GetColorAttachmentRendererID(uint32_t index = 0) const override;
        const FramebufferSpecification& GetSpecification() const override { return m_Spec; }
        int ReadPixel(uint32_t x, uint32_t y) override;
        bool ReadColorPixels(std::vector<uint32_t>& out) override;
        void ClearAttachment(uint32_t index, int value) override;
    private:
        struct Storage;
        OpenGLContext& m_Context;
        FramebufferSpecification m_Spec{};
        std::unique_ptr<Storage> m_Storage;
    };
}
#endif
