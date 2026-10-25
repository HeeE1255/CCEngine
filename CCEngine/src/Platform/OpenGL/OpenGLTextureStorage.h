#pragma once
#if defined(CC_ENABLE_OPENGL_RHI)
#include "OpenGLContext.h"
#include "Renderer/Texture.h"
namespace CCEngine
{
    class OpenGLTexture2D final : public Texture2D
    {
    public:
        explicit OpenGLTexture2D(const std::string& path);
        OpenGLTexture2D(uint32_t width, uint32_t height);
        OpenGLTexture2D(OpenGLContext& context, uint32_t width, uint32_t height, const void* pixels = nullptr);
        ~OpenGLTexture2D() override;
        OpenGLTexture2D(const OpenGLTexture2D&) = delete;
        OpenGLTexture2D& operator=(const OpenGLTexture2D&) = delete;
        void Bind(uint32_t slot = 0) const override;
        void Unbind(uint32_t slot = 0) const override;
        void SetData(void* data, uint32_t size) override;
        RendererHandle GetRendererID() const override { return reinterpret_cast<RendererHandle>(static_cast<uintptr_t>(m_Texture)); }
    private:
        OpenGLContext& Context() const { return OpenGLContext::CurrentFor(m_Device); }
        std::shared_ptr<OpenGLDevice> m_Device;
        GLuint m_Texture = 0;
        uint32_t m_Width = 0, m_Height = 0;
        void Allocate(const void* pixels);
    };
}
#endif
