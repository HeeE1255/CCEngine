#pragma once
#if defined(CC_ENABLE_OPENGL_RHI)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>
#include "Renderer/GraphicsContext.h"
#include "Renderer/Buffer.h"
#include "Renderer/UniformRepack.h"
#include "OpenGLRendererAPI.h"
#include <memory>
#include <cstddef>
#include <array>

namespace CCEngine
{
    class OpenGLDevice;
    struct OpenGLUniformData;
    struct OpenGLUniformBinding;
    // Windows 기본 GL 헤더에 없는 core 함수를 컨텍스트별로 로드한다.
#define CC_GL_RESOURCE_FUNCTIONS(X) \
    X(void, CreateBuffers, GLsizei, GLuint*) \
    X(void, DeleteBuffers, GLsizei, const GLuint*) \
    X(void, NamedBufferData, GLuint, ptrdiff_t, const void*, GLenum) \
    X(void, NamedBufferSubData, GLuint, ptrdiff_t, ptrdiff_t, const void*) \
    X(void, BindBuffer, GLenum, GLuint) \
    X(void, BindBufferBase, GLenum, GLuint, GLuint) \
    X(void, CreateVertexArrays, GLsizei, GLuint*) \
    X(void, DeleteVertexArrays, GLsizei, const GLuint*) \
    X(void, BindVertexArray, GLuint) \
    X(void, BindVertexBuffer, GLuint, GLuint, ptrdiff_t, GLsizei) \
    X(void, EnableVertexAttribArray, GLuint) \
    X(void, DisableVertexAttribArray, GLuint) \
    X(void, VertexAttribFormat, GLuint, GLint, GLenum, GLboolean, GLuint) \
    X(void, VertexAttribIFormat, GLuint, GLint, GLenum, GLuint) \
    X(void, VertexAttribBinding, GLuint, GLuint) \
    X(GLuint, CreateShader, GLenum) \
    X(void, ShaderSource, GLuint, GLsizei, const char* const*, const GLint*) \
    X(void, CompileShader, GLuint) \
    X(void, GetShaderiv, GLuint, GLenum, GLint*) \
    X(void, GetShaderInfoLog, GLuint, GLsizei, GLsizei*, char*) \
    X(void, DeleteShader, GLuint) \
    X(GLuint, CreateProgram, void) \
    X(void, AttachShader, GLuint, GLuint) \
    X(void, LinkProgram, GLuint) \
    X(void, GetProgramiv, GLuint, GLenum, GLint*) \
    X(void, GetActiveUniformBlockiv, GLuint, GLuint, GLenum, GLint*) \
    X(void, GetProgramInfoLog, GLuint, GLsizei, GLsizei*, char*) \
    X(void, DeleteProgram, GLuint) \
    X(void, UseProgram, GLuint) \
    X(void, CreateTextures, GLenum, GLsizei, GLuint*) \
    X(void, TextureStorage2D, GLuint, GLsizei, GLenum, GLsizei, GLsizei) \
    X(void, TextureParameteri, GLuint, GLenum, GLint) \
    X(void, TextureSubImage2D, GLuint, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*) \
    X(void, ClearTexImage, GLuint, GLint, GLenum, GLenum, const void*) \
    X(void, GetTextureSubImage, GLuint, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, GLsizei, void*) \
    X(void, CreateFramebuffers, GLsizei, GLuint*) \
    X(void, DeleteFramebuffers, GLsizei, const GLuint*) \
    X(void, NamedFramebufferTexture, GLuint, GLenum, GLuint, GLint) \
    X(void, NamedFramebufferDrawBuffers, GLuint, GLsizei, const GLenum*) \
    X(GLenum, CheckNamedFramebufferStatus, GLuint, GLenum) \
    X(void, BindFramebuffer, GLenum, GLuint)

    struct OpenGLFunctions
    {
#define CC_DECLARE_GL(result, name, ...) result (APIENTRY* name)(__VA_ARGS__) = nullptr;
        CC_GL_RESOURCE_FUNCTIONS(CC_DECLARE_GL)
#undef CC_DECLARE_GL
        void Load();
    };

    class OpenGLContext final : public GraphicsContext
    {
    public:
        explicit OpenGLContext(void* hwnd, std::shared_ptr<OpenGLDevice> device = {});
        ~OpenGLContext() override;
        OpenGLContext(const OpenGLContext&) = delete;
        OpenGLContext& operator=(const OpenGLContext&) = delete;
        void Init() override;
        void SwapBuffers(bool vsync = true) override;
        void Clear(float r, float g, float b, float a) override;
        void ResizeBuffers(uint32_t width, uint32_t height) override;
        void MakeCurrent() override;
        void BindBackBuffer() override;
        bool ReadBackBuffer(std::vector<uint8_t>& pixels, uint32_t& width, uint32_t& height) override;
        void SetLayout(const BufferLayout& layout);
        void SetShaderInputs(const std::vector<VertexInput>& inputs) { ValidateVertexInputs(inputs); m_ShaderInputs = inputs; m_HasShaderInputs = true; }
        void ClearShaderInputs() { m_HasShaderInputs = false; }
        void ApplyVertexLayout();
        void SetUniformLayouts(std::shared_ptr<const std::vector<UniformRepackLayout>> layouts) { m_UniformLayouts = std::move(layouts); }
        void BindUniformData(uint32_t slot, const std::shared_ptr<OpenGLUniformData>& data);
        void ApplyUniformLayouts();
        OpenGLFunctions& GL() { MakeCurrent(); return m_GL; }
        OpenGLRendererAPI& Commands() { return *m_API; }
        RendererAPI* GetCommands() override { return m_API.get(); }
        std::shared_ptr<GraphicsDevice> GetGraphicsDevice() const override;
        const std::shared_ptr<OpenGLDevice>& SharedDevice() const { return m_Device; }
        static OpenGLContext& CurrentFor(const std::shared_ptr<OpenGLDevice>& device);
        void BindAttachments(const GLuint* textures, const std::shared_ptr<void>& lifetime);
        bool HasAttachments(const std::shared_ptr<void>& lifetime) const;
        static OpenGLContext& Current();
        static void* Resolve(const char* name);
    private:
        HWND m_Window = nullptr;
        HDC m_DC = nullptr;
        HGLRC m_RC = nullptr;
        std::shared_ptr<OpenGLDevice> m_Device;
        OpenGLFunctions m_GL;
        std::unique_ptr<OpenGLRendererAPI> m_API;
        GLuint m_VAO = 0;
        GLuint m_FBO = 0;
        std::weak_ptr<void> m_AttachmentLifetime;
        uint32_t m_Width = 0, m_Height = 0;
        std::vector<uint32_t> m_Attributes;
        std::vector<VertexInput> m_ShaderInputs;
        BufferLayout m_VertexLayout;
        std::shared_ptr<const std::vector<UniformRepackLayout>> m_UniformLayouts;
        std::array<std::weak_ptr<OpenGLUniformData>, 8> m_UniformData;
        std::array<std::unique_ptr<OpenGLUniformBinding>, 8> m_UniformBindings;
        bool m_HasShaderInputs = false;
        int (WINAPI* m_SwapInterval)(int) = nullptr;
        void Release() noexcept;
    };
}
#endif
