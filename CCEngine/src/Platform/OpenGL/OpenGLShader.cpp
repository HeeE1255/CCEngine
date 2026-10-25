#include "OpenGLShader.h"
#if defined(CC_ENABLE_OPENGL_RHI)
#include "OpenGLDevice.h"
#include <stdexcept>
#include <limits>
#include <climits>
#include <fstream>
#include <algorithm>
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
        : m_Device(context.SharedDevice())
    {
        auto& gl = Context().GL();
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
    OpenGLShader::~OpenGLShader() { m_Device->DeleteProgram(m_Program); }
    void OpenGLShader::SetUniformRepack(std::vector<UniformRepackLayout> layouts)
    {
        ValidateUniformRepack(layouts);
        auto& gl = Context().GL();
        GLint count = 0;
        gl.GetProgramiv(m_Program, 0x8A36 /* GL_ACTIVE_UNIFORM_BLOCKS */, &count);
        // 파일의 복사 범위가 정상이더라도 실제 드라이버의 블록 크기보다 작으면 읽기가 유효하지 않다.
        // 메타데이터를 조용히 보정하지 않고 패키지 로딩 단계에서 거부한다.
        for (GLint block = 0; block < count; ++block)
        {
            GLint binding = 0, size = 0;
            gl.GetActiveUniformBlockiv(m_Program, static_cast<GLuint>(block), 0x8A3F /* GL_UNIFORM_BLOCK_BINDING */, &binding);
            gl.GetActiveUniformBlockiv(m_Program, static_cast<GLuint>(block), 0x8A40 /* GL_UNIFORM_BLOCK_DATA_SIZE */, &size);
            const auto found = std::find_if(layouts.begin(), layouts.end(), [binding](const UniformRepackLayout& layout)
                { return binding >= 0 && layout.Binding == static_cast<uint32_t>(binding); });
            if (found == layouts.end() || size <= 0 || found->DestinationSize < static_cast<uint32_t>(size))
                throw std::runtime_error("OpenGL uniform repack does not cover active block binding " + std::to_string(binding));
        }
        m_UniformLayouts = std::make_shared<const std::vector<UniformRepackLayout>>(std::move(layouts));
    }
    void OpenGLShader::Bind() const
    {
        auto& context = Context();
        context.SetShaderInputs(GetVertexInputs()); context.SetUniformLayouts(m_UniformLayouts);
        context.GL().UseProgram(m_Program);
    }
    void OpenGLShader::Unbind() const
    {
        auto& context = Context();
        context.ClearShaderInputs(); context.SetUniformLayouts(nullptr); context.GL().UseProgram(0);
    }
    void OpenGLShader::BindLayout(const BufferLayout& layout) { Context().SetLayout(layout); }
}
#endif
