#include "OpenGLTextureStorage.h"
#if defined(CC_ENABLE_OPENGL_RHI)
#include "OpenGLDevice.h"
#include "stb_image.h"
#include <stdexcept>
#include <limits>
namespace CCEngine
{
    OpenGLTexture2D::OpenGLTexture2D(const std::string& path) : m_Device(OpenGLContext::Current().SharedDevice())
    {
        int width = 0, height = 0, channels = 0;
        stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
        if (!pixels) throw std::runtime_error("Could not load OpenGL texture: " + path);
        m_Width = width; m_Height = height;
        try { Allocate(pixels); } catch (...) { stbi_image_free(pixels); throw; }
        stbi_image_free(pixels);
    }
    OpenGLTexture2D::OpenGLTexture2D(uint32_t width, uint32_t height)
        : OpenGLTexture2D(OpenGLContext::Current(), width, height) {}
    OpenGLTexture2D::OpenGLTexture2D(OpenGLContext& context, uint32_t width, uint32_t height, const void* pixels)
        : m_Device(context.SharedDevice()), m_Width(width), m_Height(height) { Allocate(pixels); }
    void OpenGLTexture2D::Allocate(const void* pixels)
    {
        auto& gl = Context().GL(); GLint limit = 0; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
        if (!m_Width || !m_Height || m_Width > static_cast<uint32_t>(limit) || m_Height > static_cast<uint32_t>(limit) ||
            uint64_t(m_Width) * m_Height * 4 > UINT32_MAX) throw std::invalid_argument("Invalid OpenGL texture size");
        gl.CreateTextures(GL_TEXTURE_2D, 1, &m_Texture);
        gl.TextureStorage2D(m_Texture, 1, 0x8058, m_Width, m_Height);
        gl.TextureParameteri(m_Texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl.TextureParameteri(m_Texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl.TextureParameteri(m_Texture, GL_TEXTURE_WRAP_S, 0x812F);
        gl.TextureParameteri(m_Texture, GL_TEXTURE_WRAP_T, 0x812F);
        try
        {
            if (pixels) SetData(const_cast<void*>(pixels), m_Width * m_Height * 4);
            else gl.ClearTexImage(m_Texture, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        }
        catch (...) { glDeleteTextures(1, &m_Texture); m_Texture = 0; throw; }
        if (glGetError() != GL_NO_ERROR)
        {
            glDeleteTextures(1, &m_Texture); m_Texture = 0;
            throw std::runtime_error("OpenGL texture allocation failed");
        }
    }
    OpenGLTexture2D::~OpenGLTexture2D() { m_Device->DeleteTexture(m_Texture); }
    void OpenGLTexture2D::Bind(uint32_t slot) const { Context().Commands().BindTexture(slot, GetRendererID()); }
    void OpenGLTexture2D::Unbind(uint32_t slot) const { Context().Commands().BindTexture(slot, nullptr); }
    void OpenGLTexture2D::SetData(void* data, uint32_t size)
    {
        if (!data || uint64_t(m_Width) * m_Height * 4 != size) throw std::invalid_argument("RGBA8 texture upload size mismatch");
        auto& gl = Context().GL();
        GLint buffer = 0, alignment = 0, rowLength = 0, skipRows = 0, skipPixels = 0;
        glGetIntegerv(0x88EF, &buffer); glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength); glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skipRows); glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skipPixels);
        gl.BindBuffer(0x88EC, 0); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0); glPixelStorei(GL_UNPACK_SKIP_ROWS, 0); glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        gl.TextureSubImage2D(m_Texture, 0, 0, 0, m_Width, m_Height, GL_RGBA, GL_UNSIGNED_BYTE, data);
        gl.BindBuffer(0x88EC, buffer); glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLength); glPixelStorei(GL_UNPACK_SKIP_ROWS, skipRows); glPixelStorei(GL_UNPACK_SKIP_PIXELS, skipPixels);
        if (glGetError() != GL_NO_ERROR) throw std::runtime_error("OpenGL texture upload failed");
    }
}
#endif
