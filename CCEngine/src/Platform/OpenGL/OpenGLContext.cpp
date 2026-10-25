#include "OpenGLContext.h"
#if defined(CC_ENABLE_OPENGL_RHI)
#include "OpenGLDevice.h"
#include "OpenGLBuffer.h"
#include <algorithm>
#include <limits>
#include <climits>
#include <stdexcept>
#include <string>

namespace CCEngine
{
    void* OpenGLContext::Resolve(const char* name)
    {
        auto proc = wglGetProcAddress(name);
        const auto value = reinterpret_cast<uintptr_t>(proc);
        if (!proc || value <= 3 || value == std::numeric_limits<uintptr_t>::max())
            proc = reinterpret_cast<PROC>(GetProcAddress(GetModuleHandleW(L"opengl32.dll"), name));
        if (!proc) throw std::runtime_error(std::string("Missing OpenGL entry point: ") + name);
        return reinterpret_cast<void*>(proc);
    }
    void OpenGLFunctions::Load()
    {
#define CC_LOAD_GL(result, name, ...) name = reinterpret_cast<decltype(name)>(OpenGLContext::Resolve("gl" #name));
        CC_GL_RESOURCE_FUNCTIONS(CC_LOAD_GL)
#undef CC_LOAD_GL
    }
    OpenGLContext::OpenGLContext(void* hwnd, std::shared_ptr<OpenGLDevice> device)
        : m_Window(static_cast<HWND>(hwnd)), m_Device(std::move(device)) {}
    std::shared_ptr<GraphicsDevice> OpenGLContext::GetGraphicsDevice() const { return m_Device; }
    OpenGLContext::~OpenGLContext() { Release(); }
    void OpenGLContext::Release() noexcept
    {
        for (auto& binding : m_UniformBindings) binding.reset();
        for (auto& data : m_UniformData) data.reset();
        m_UniformLayouts.reset();
        const HDC previousDC = wglGetCurrentDC();
        const HGLRC previousRC = wglGetCurrentContext();
        if (m_RC)
        {
            m_API.reset();
            if (wglMakeCurrent(m_DC, m_RC))
            {
                glFinish();
                if (m_VAO && m_GL.DeleteVertexArrays) m_GL.DeleteVertexArrays(1, &m_VAO);
                if (m_FBO && m_GL.DeleteFramebuffers) m_GL.DeleteFramebuffers(1, &m_FBO);
            }
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(m_RC);
        }
        if (previousRC && previousRC != m_RC) wglMakeCurrent(previousDC, previousRC);
        if (m_DC) ReleaseDC(m_Window, m_DC);
        m_RC = nullptr; m_DC = nullptr; m_VAO = 0; m_FBO = 0;
    }
    void OpenGLContext::Init()
    {
        if (m_RC) throw std::logic_error("OpenGL context is already initialized");
        if (!IsWindow(m_Window)) throw std::invalid_argument("Invalid OpenGL window");
        if (!m_Device) m_Device = std::make_shared<OpenGLDevice>();
        m_Device->CheckThread();
        auto* previous = GraphicsContext::GetCurrent();
        const HDC previousDC = wglGetCurrentDC();
        const HGLRC previousRC = wglGetCurrentContext();
        try
        {
            m_DC = GetDC(m_Window);
            if (!m_DC) throw std::runtime_error("GetDC failed");
            PIXELFORMATDESCRIPTOR format{};
            format.nSize = sizeof(format); format.nVersion = 1;
            format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
            format.iPixelType = PFD_TYPE_RGBA; format.cColorBits = 32;
            format.cDepthBits = 24; format.cStencilBits = 8;
            int index = GetPixelFormat(m_DC);
            if (!index)
            {
                index = m_Device->PixelFormat();
                if (!DescribePixelFormat(m_DC, index, sizeof(format), &format))
                    throw std::runtime_error("OpenGL shared pixel format unavailable");
                if (!index || !SetPixelFormat(m_DC, index, &format))
                    throw std::runtime_error("OpenGL pixel format setup failed");
            }
            else
            {
                if (index != m_Device->PixelFormat())
                    throw std::runtime_error("OpenGL shared windows require the same pixel format");
                DescribePixelFormat(m_DC, index, sizeof(format), &format);
                constexpr DWORD required = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
                if ((format.dwFlags & required) != required || format.cDepthBits < 24 || format.cStencilBits < 8)
                    throw std::runtime_error("Existing window pixel format is incompatible with OpenGL");
            }
            // WGL 확장 함수는 임시 컨텍스트를 current로 만든 뒤에만 조회할 수 있다.
            if (wglGetCurrentContext()) glFinish();
            m_RC = wglCreateContext(m_DC);
            if (!m_RC || !wglMakeCurrent(m_DC, m_RC)) throw std::runtime_error("WGL bootstrap failed");
            using CreateContext = HGLRC (WINAPI*)(HDC, HGLRC, const int*);
            auto create = reinterpret_cast<CreateContext>(Resolve("wglCreateContextAttribsARB"));
            const int attributes[] = { 0x2091, 4, 0x2092, 5, 0x9126, 1, 0 };
            HGLRC core = create(m_DC, m_Device->ShareContext(), attributes);
            if (!core) throw std::runtime_error("OpenGL 4.5 core context creation failed");
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(m_RC); m_RC = core;
            MakeCurrent();
            m_GL.Load();
            m_GL.CreateVertexArrays(1, &m_VAO);
            if (!m_VAO || glGetError() != GL_NO_ERROR) throw std::runtime_error("OpenGL vertex array allocation failed");
            m_GL.BindVertexArray(m_VAO);
            try { m_SwapInterval = reinterpret_cast<decltype(m_SwapInterval)>(Resolve("wglSwapIntervalEXT")); }
            catch (const std::runtime_error&) { m_SwapInterval = nullptr; }
            m_API = std::make_unique<OpenGLRendererAPI>(*this, Resolve);
            RECT rect{}; GetClientRect(m_Window, &rect);
            ResizeBuffers(static_cast<uint32_t>(rect.right), static_cast<uint32_t>(rect.bottom));
            if (m_Width && m_Height) BindBackBuffer();
        }
        catch (...)
        {
            Release();
            if (previous && previous != this)
            {
                try { previous->MakeCurrent(); }
                catch (...) { OutputDebugStringA("OpenGL initialization context restore failed\n"); }
            }
            else wglMakeCurrent(previousDC, previousRC);
            throw;
        }
    }
    void OpenGLContext::MakeCurrent()
    {
        if (!m_RC || !m_Device) throw std::logic_error("OpenGL context is not initialized");
        m_Device->CheckThread();
        if (wglGetCurrentContext() != m_RC)
        {
            // 첫 연결은 단일 렌더 스레드 기준이다. 다른 창이 쓴 공유 텍스처의 완료를 보장한다.
            if (wglGetCurrentContext()) glFinish();
            if (!wglMakeCurrent(m_DC, m_RC)) throw std::runtime_error("wglMakeCurrent failed");
        }
        SetCurrentContext();
        if (m_FBO && m_AttachmentLifetime.expired())
        {
            m_GL.DeleteFramebuffers(1, &m_FBO); m_FBO = 0;
            if (m_API)
            {
                m_API->SetRenderTargetSize(m_Width, m_Height);
                if (m_Width && m_Height) m_API->SetViewport(0, 0, m_Width, m_Height);
            }
        }
    }
    OpenGLContext& OpenGLContext::Current()
    {
        auto* context = dynamic_cast<OpenGLContext*>(GraphicsContext::GetCurrent());
        if (!context) throw std::logic_error("No current OpenGLContext");
        context->MakeCurrent(); return *context;
    }
    OpenGLContext& OpenGLContext::CurrentFor(const std::shared_ptr<OpenGLDevice>& device)
    {
        auto& context = Current();
        if (context.m_Device != device) throw std::logic_error("OpenGL resource belongs to another share group");
        return context;
    }
    bool OpenGLContext::HasAttachments(const std::shared_ptr<void>& lifetime) const
    {
        return m_FBO && m_AttachmentLifetime.lock() == lifetime;
    }
    void OpenGLContext::BindAttachments(const GLuint* textures, const std::shared_ptr<void>& lifetime)
    {
        if (!textures || !lifetime) throw std::invalid_argument("OpenGL attachments require a lifetime owner");
        MakeCurrent();
        // FBO와 VAO는 WGL 공유 대상이 아니다. 텍스처만 공유하고 현재 창의 FBO에 다시 연결한다.
        if (!m_FBO) m_GL.CreateFramebuffers(1, &m_FBO);
        if (!m_FBO) throw std::runtime_error("OpenGL window framebuffer allocation failed");
        const GLenum attachments[] = { 0x8CE0, 0x8CE1, 0x821A };
        for (int i = 0; i < 3; ++i) m_GL.NamedFramebufferTexture(m_FBO, attachments[i], textures[i], 0);
        m_GL.NamedFramebufferDrawBuffers(m_FBO, 2, attachments);
        if (m_GL.CheckNamedFramebufferStatus(m_FBO, 0x8D40) != 0x8CD5 || glGetError() != GL_NO_ERROR)
            throw std::runtime_error("OpenGL window framebuffer is incomplete");
        m_AttachmentLifetime = lifetime;
        m_GL.BindFramebuffer(0x8D40, m_FBO);
    }
    void OpenGLContext::SwapBuffers(bool vsync)
    {
        MakeCurrent();
        if (!m_Width || !m_Height) return;
        if (m_SwapInterval) m_SwapInterval(vsync ? 1 : 0);
        if (!::SwapBuffers(m_DC)) throw std::runtime_error("OpenGL SwapBuffers failed");
    }
    void OpenGLContext::Clear(float r, float g, float b, float a)
    {
        MakeCurrent(); m_API->SetClearColor(r, g, b, a); m_API->Clear();
    }
    bool OpenGLContext::ReadBackBuffer(std::vector<uint8_t>& pixels, uint32_t& width, uint32_t& height)
    {
        pixels.clear(); width = height = 0;
        if (!m_RC || !m_Width || !m_Height ||
            size_t(m_Width) > std::numeric_limits<size_t>::max() / 4 / m_Height) return false;
        try
        {
            MakeCurrent();
            std::vector<uint8_t> result(size_t(m_Width) * m_Height * 4);
            // 캡처는 그리기 대상과 무관하게 이 창의 back buffer를 읽는다.
            // PBO가 바인딩돼 있으면 CPU 주소가 버퍼 오프셋으로 해석되므로 함께 보존/해제한다.
            struct ReadScope
            {
                OpenGLFunctions& GL;
                GLint Framebuffer = 0, DefaultReadBuffer = 0, Buffer = 0;
                GLint Alignment = 0, Row = 0, Rows = 0, Pixels = 0;
                explicit ReadScope(OpenGLFunctions& gl) : GL(gl)
                {
                    glGetIntegerv(0x8CAA /* GL_READ_FRAMEBUFFER_BINDING */, &Framebuffer);
                    glGetIntegerv(0x88ED /* GL_PIXEL_PACK_BUFFER_BINDING */, &Buffer);
                    glGetIntegerv(GL_PACK_ALIGNMENT, &Alignment);
                    glGetIntegerv(GL_PACK_ROW_LENGTH, &Row);
                    glGetIntegerv(GL_PACK_SKIP_ROWS, &Rows);
                    glGetIntegerv(GL_PACK_SKIP_PIXELS, &Pixels);
                    GL.BindFramebuffer(0x8CA8 /* GL_READ_FRAMEBUFFER */, 0);
                    glGetIntegerv(GL_READ_BUFFER, &DefaultReadBuffer);
                    glReadBuffer(GL_BACK);
                    GL.BindBuffer(0x88EB /* GL_PIXEL_PACK_BUFFER */, 0);
                    glPixelStorei(GL_PACK_ALIGNMENT, 1);
                    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
                    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
                    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
                }
                ~ReadScope()
                {
                    glReadBuffer(DefaultReadBuffer);
                    GL.BindFramebuffer(0x8CA8, Framebuffer);
                    GL.BindBuffer(0x88EB, Buffer);
                    glPixelStorei(GL_PACK_ALIGNMENT, Alignment);
                    glPixelStorei(GL_PACK_ROW_LENGTH, Row);
                    glPixelStorei(GL_PACK_SKIP_ROWS, Rows);
                    glPixelStorei(GL_PACK_SKIP_PIXELS, Pixels);
                }
            } scope(m_GL);
            // 앞선 GL 오류까지 있는 경우 성공으로 보고하지 않는다.
            if (glGetError() != GL_NO_ERROR) return false;
            glReadPixels(0, 0, m_Width, m_Height, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
            if (glGetError() != GL_NO_ERROR) return false;
            // GL 읽기는 아래쪽 행부터 반환한다. 공통 RHI 계약은 좌상단 원점이다.
            const size_t rowBytes = size_t(m_Width) * 4;
            for (uint32_t y = 0; y < m_Height / 2; ++y)
                std::swap_ranges(result.begin() + size_t(y) * rowBytes,
                    result.begin() + size_t(y + 1) * rowBytes,
                    result.begin() + size_t(m_Height - 1 - y) * rowBytes);
            pixels.swap(result); width = m_Width; height = m_Height;
            return true;
        }
        catch (...) { return false; }
    }
    void OpenGLContext::ResizeBuffers(uint32_t width, uint32_t height)
    {
        if (width > INT_MAX || height > INT_MAX) throw std::out_of_range("OpenGL size overflow");
        m_Width = width; m_Height = height;
    }
    void OpenGLContext::BindBackBuffer()
    {
        MakeCurrent(); m_GL.BindFramebuffer(0x8D40, 0);
        m_AttachmentLifetime.reset();
        m_API->SetRenderTargetSize(m_Width, m_Height);
        if (m_Width && m_Height) m_API->SetViewport(0, 0, m_Width, m_Height);
    }
    void OpenGLContext::SetLayout(const BufferLayout& layout)
    {
        layout.Validate();
        m_VertexLayout = layout;
    }
    void OpenGLContext::BindUniformData(uint32_t slot, const std::shared_ptr<OpenGLUniformData>& data)
    {
        if (slot >= m_UniformData.size() || !data || data->Original.Device != m_Device)
            throw std::invalid_argument("Invalid OpenGL uniform binding");
        m_UniformData[slot] = data;
    }
    void OpenGLContext::ApplyUniformLayouts()
    {
        MakeCurrent();
        GLint maxSize = 0; glGetIntegerv(0x8A30, &maxSize);
        for (uint32_t slot = 0; slot < m_UniformData.size(); ++slot)
        {
            const UniformRepackLayout* layout = nullptr;
            if (m_UniformLayouts)
                for (const auto& candidate : *m_UniformLayouts)
                    if (candidate.Binding == slot) { layout = &candidate; break; }
            auto data = m_UniformData[slot].lock();
            if (!data)
            {
                m_UniformBindings[slot].reset();
                m_GL.BindBufferBase(0x8A11, slot, 0);
                if (layout) throw std::logic_error("OpenGL shader requires an unbound uniform buffer at slot " + std::to_string(slot));
                continue;
            }
            if (!layout)
            {
                // 재배치 없는 셰이더로 바꿀 때 이전 변환 버퍼가 남지 않게 원본을 복원한다.
                m_UniformBindings[slot].reset();
                m_GL.BindBufferBase(0x8A11, slot, data->Original.Name);
                continue;
            }
            if (layout->DestinationSize > static_cast<uint32_t>(maxSize))
                throw std::out_of_range("Repacked OpenGL uniform exceeds device limit");
            auto& cache = m_UniformBindings[slot];
            if (!cache || cache->Source.lock() != data || cache->Revision != data->Revision ||
                cache->Layouts != m_UniformLayouts)
            {
                auto bytes = RepackUniformBytes(*layout, data->Bytes);
                // 창/슬롯별로 소유하므로 같은 원본을 서로 다른 배치로 동시에 바인딩할 수 있다.
                if (cache && cache->Converted->Size == bytes.size())
                    cache->Converted->Update(bytes.data(), static_cast<uint32_t>(bytes.size()));
                else
                {
                    auto replacement = std::make_unique<OpenGLUniformBinding>();
                    replacement->Converted = std::make_unique<OpenGLBufferStorage>(*this,
                        static_cast<uint32_t>(bytes.size()), bytes.data());
                    cache = std::move(replacement);
                }
                cache->Source = data; cache->Revision = data->Revision;
                cache->Layouts = m_UniformLayouts;
            }
            m_GL.BindBufferBase(0x8A11, slot, cache->Converted->Name);
        }
    }
    void OpenGLContext::ApplyVertexLayout()
    {
        if (!m_HasShaderInputs) throw std::logic_error("OpenGL shader input mapping is not bound");
        const auto& layout = m_VertexLayout;
        layout.Validate();
        MakeCurrent();
        GLint limit = 0; glGetIntegerv(0x8869, &limit);
        GLint maxStride = 0, maxOffset = 0;
        glGetIntegerv(0x82E5 /* GL_MAX_VERTEX_ATTRIB_STRIDE */, &maxStride);
        glGetIntegerv(0x82D9 /* GL_MAX_VERTEX_ATTRIB_RELATIVE_OFFSET */, &maxOffset);
        const auto inputs = ResolveVertexInputs(layout, m_ShaderInputs);
        for (const auto& input : inputs)
        {
            if (input.Location >= static_cast<uint32_t>(limit) || input.Offset > static_cast<uint32_t>(maxOffset))
                throw std::invalid_argument("OpenGL attribute exceeds layout or device limits");
        }
        if (layout.GetStride() > static_cast<uint32_t>(maxStride))
            throw std::invalid_argument("Invalid OpenGL vertex layout");
        // 비연속 location도 있으므로 개수가 아니라 실제 활성 위치를 추적한다.
        for (const auto location : m_Attributes) m_GL.DisableVertexAttribArray(location);
        m_Attributes.clear();
        for (const auto& input : inputs)
        {
            const bool integer = input.Type >= ShaderDataType::Int && input.Type <= ShaderDataType::Int4;
            const uint32_t components = ShaderDataTypeSize(input.Type) / 4;
            m_GL.EnableVertexAttribArray(input.Location);
            m_GL.VertexAttribBinding(input.Location, 0);
            if (integer) m_GL.VertexAttribIFormat(input.Location, components, GL_INT, input.Offset);
            else m_GL.VertexAttribFormat(input.Location, components, GL_FLOAT, false, input.Offset);
            m_Attributes.push_back(input.Location);
        }
    }
}
#endif
