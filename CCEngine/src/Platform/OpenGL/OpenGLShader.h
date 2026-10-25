#pragma once
#if defined(CC_ENABLE_OPENGL_RHI)
#include "OpenGLContext.h"
#include "Renderer/Shader.h"
namespace CCEngine
{
    class OpenGLShader final : public Shader
    {
    public:
        // GLSL 450 core. UBO와 sampler 슬롯은 layout(binding=...)으로 지정한다.
        OpenGLShader(OpenGLContext& context, const std::string& vertexSource, const std::string& fragmentSource);
        static std::unique_ptr<OpenGLShader> FromFiles(OpenGLContext& context,
            const std::filesystem::path& vertex, const std::filesystem::path& fragment);
        ~OpenGLShader() override;
        OpenGLShader(const OpenGLShader&) = delete;
        OpenGLShader& operator=(const OpenGLShader&) = delete;
        void Bind() const override;
        void Unbind() const override;
        void BindLayout(const BufferLayout& layout) override;
        bool IsValid() const override { return m_Program != 0; }
        void SetUniformRepack(std::vector<UniformRepackLayout> layouts);
    private:
        OpenGLContext& Context() const { return OpenGLContext::CurrentFor(m_Device); }
        std::shared_ptr<OpenGLDevice> m_Device;
        GLuint m_Program = 0;
        std::shared_ptr<const std::vector<UniformRepackLayout>> m_UniformLayouts;
    };
}
#endif
