#include "OpenGLShader.h"
#if defined(CC_ENABLE_OPENGL_RHI)
#include <stdexcept>
#include <limits>
#include <climits>
#include <fstream>
namespace CCEngine
{
    std::unique_ptr<OpenGLShader> OpenGLShader::FromFiles(OpenGLContext& context,
        const std::filesystem::path& vertex, const std::filesystem::path& fragment)
    {
        auto read = [](const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file) throw std::runtime_error("Cannot open GLSL: " + path.string());
            const std::streamoff size = file.tellg();
            if (size <= 0 || size > 64 * 1024 * 1024) throw std::runtime_error("Invalid GLSL file size");
            std::string source(static_cast<size_t>(size), '\0');
            file.seekg(0);
            if (!file.read(source.data(), static_cast<std::streamsize>(size))) throw std::runtime_error("Cannot read GLSL");
            return source;
        };
        return std::make_unique<OpenGLShader>(context, read(vertex), read(fragment));
    }
    OpenGLShader::OpenGLShader(OpenGLContext& context, const std::string& vertexSource, const std::string& fragmentSource)
        : m_Context(context)
    {
        auto& gl = context.GL();
        GLuint vertex = 0, fragment = 0;
        auto compile = [&](GLenum type, const std::string& source, GLuint& shader)
        {
            if (source.empty() || source.size() > INT_MAX) throw std::invalid_argument("Invalid GLSL source size");
            shader = gl.CreateShader(type);
            const char* text = source.c_str(); const GLint length = static_cast<GLint>(source.size());
            gl.ShaderSource(shader, 1, &text, &length); gl.CompileShader(shader);
            GLint ok = 0; gl.GetShaderiv(shader, 0x8B81, &ok);
            if (!ok)
            {
                GLint size = 0; gl.GetShaderiv(shader, 0x8B84, &size);
                std::string log(static_cast<size_t>(size > 0 ? size : 1), '\0');
                gl.GetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
                throw std::runtime_error("GLSL compilation failed: " + log);
            }
        };
        try
        {
            compile(0x8B31, vertexSource, vertex); compile(0x8B30, fragmentSource, fragment);
            m_Program = gl.CreateProgram(); gl.AttachShader(m_Program, vertex); gl.AttachShader(m_Program, fragment);
            gl.LinkProgram(m_Program);
            GLint ok = 0; gl.GetProgramiv(m_Program, 0x8B82, &ok);
            if (!ok)
            {
                GLint size = 0; gl.GetProgramiv(m_Program, 0x8B84, &size);
                std::string log(static_cast<size_t>(size > 0 ? size : 1), '\0');
                gl.GetProgramInfoLog(m_Program, static_cast<GLsizei>(log.size()), nullptr, log.data());
                throw std::runtime_error("GLSL linking failed: " + log);
            }
        }
        catch (...)
        {
            if (vertex) gl.DeleteShader(vertex); if (fragment) gl.DeleteShader(fragment);
            if (m_Program) gl.DeleteProgram(m_Program); m_Program = 0; throw;
        }
        gl.DeleteShader(vertex); gl.DeleteShader(fragment);
    }
    OpenGLShader::~OpenGLShader() { if (m_Program) m_Context.GL().DeleteProgram(m_Program); }
    void OpenGLShader::Bind() const { m_Context.GL().UseProgram(m_Program); }
    void OpenGLShader::Unbind() const { m_Context.GL().UseProgram(0); }
    void OpenGLShader::BindLayout(const BufferLayout& layout) { m_Context.SetLayout(layout); }
}
#endif
