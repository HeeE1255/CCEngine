#include "Platform/OpenGL/OpenGLTexture.h"
#include <iostream>

namespace CCEngine
{
    OpenGLTexture2D::OpenGLTexture2D(const std::string& path) : m_Path(path)
    {
        std::cout << "[OpenGL] Texture Created from path: " << path << std::endl;
        // 나중에 glGenTextures 등 OpenGL 로직 구현
    }

    OpenGLTexture2D::OpenGLTexture2D(uint32_t width, uint32_t height) : m_Width(width), m_Height(height)
    {
        std::cout << "[OpenGL] Empty Texture Created. Size: " << width << "x" << height << std::endl;
    }

    OpenGLTexture2D::~OpenGLTexture2D()
    {
        std::cout << "[OpenGL] Texture Destroyed" << std::endl;
    }

    void OpenGLTexture2D::Bind(uint32_t slot) const { /* glBindTexture */ }
    void OpenGLTexture2D::Unbind(uint32_t slot) const { /* glBindTexture(0) */ }
    void OpenGLTexture2D::SetData(void* data, uint32_t size) { /* glTexSubImage2D */ }
}