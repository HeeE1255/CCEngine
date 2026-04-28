#pragma once
#include "Renderer/Texture.h"
#include <string>

namespace CCEngine
{
    class OpenGLTexture2D : public Texture2D
    {
    public:
        OpenGLTexture2D(const std::string& path);
        OpenGLTexture2D(uint32_t width, uint32_t height);
        virtual ~OpenGLTexture2D();

        virtual void Bind(uint32_t slot = 0) const override;
        virtual void Unbind(uint32_t slot = 0) const override;

        virtual void SetData(void* data, uint32_t size) override;
        virtual void* GetRendererID() const override { return (void*)(uintptr_t)m_RendererID; }

    private:
        std::string m_Path;
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        uint32_t m_RendererID = 0; // OpenGL에서 텍스처 객체를 식별하는 ID
    };
}