#include "Build/FactorySmoke.h"

#include "Renderer/Buffer.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/GraphicsContext.h"
#include "Renderer/RendererAPI.h"
#include "Renderer/Texture.h"

#define NOMINMAX
#include <Windows.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace CCEngine
{
    namespace
    {
        RendererAPI::API ParseBackend(const std::string& backend)
        {
            if (backend == "dx11") return RendererAPI::API::DirectX11;
            if (backend == "dx12") return RendererAPI::API::DirectX12;
            if (backend == "opengl") return RendererAPI::API::OpenGL;
            if (backend == "vulkan") return RendererAPI::API::Vulkan;
            throw std::invalid_argument("Unknown factory smoke backend: " + backend);
        }

        template<class Action>
        void RequireRejected(Action action, const char* message)
        {
            try
            {
                action();
            }
            catch (const std::invalid_argument&)
            {
                return;
            }
            throw std::runtime_error(message);
        }

        void CheckBuffers()
        {
            std::array<float, 8> vertices = { -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f, -1.0f };
            std::array<uint32_t, 6> indices = { 0, 1, 2, 0, 2, 3 };
            std::array<float, 4> constants = { 1.0f, 0.5f, 0.25f, 1.0f };

            std::unique_ptr<VertexBuffer> initialized(VertexBuffer::Create(vertices.data(), sizeof(vertices)));
            std::unique_ptr<VertexBuffer> dynamic(VertexBuffer::Create(static_cast<uint32_t>(sizeof(vertices))));
            std::unique_ptr<IndexBuffer> index(IndexBuffer::Create(indices.data(), static_cast<uint32_t>(indices.size())));
            std::unique_ptr<ConstantBuffer> constant(ConstantBuffer::Create(static_cast<uint32_t>(sizeof(constants))));
            if (!initialized || !dynamic || !index || !constant || index->GetCount() != indices.size())
                throw std::runtime_error("Common buffer factory returned an invalid object.");

            initialized->SetLayout({ { ShaderDataType::Float2, "POSITION" } });
            dynamic->SetLayout({ { ShaderDataType::Float2, "POSITION" } });
            dynamic->SetData(vertices.data(), sizeof(vertices));
            constant->SetData(constants.data(), sizeof(constants));
            initialized->Bind();
            dynamic->Bind();
            index->Bind();
            constant->Bind(0);

            RequireRejected([] { std::unique_ptr<VertexBuffer> invalid(VertexBuffer::Create(0)); },
                "Zero-sized vertex buffer was accepted.");
            RequireRejected([] { std::unique_ptr<ConstantBuffer> invalid(ConstantBuffer::Create(0)); },
                "Zero-sized constant buffer was accepted.");
            RequireRejected([] { std::unique_ptr<ConstantBuffer> invalid(ConstantBuffer::Create(65537)); },
                "Oversized constant buffer was accepted.");
        }

        std::filesystem::path WriteProbeBitmap()
        {
            // 1x1 BGRA BMP다. 파일 생성 overload도 외부 테스트 에셋에 의존하지 않고 검사한다.
            constexpr std::array<unsigned char, 58> bitmap = {
                0x42,0x4d, 0x3a,0,0,0, 0,0,0,0, 0x36,0,0,0,
                0x28,0,0,0, 1,0,0,0, 1,0,0,0, 1,0, 32,0,
                0,0,0,0, 4,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
                0,0,255,255
            };
            const auto path = std::filesystem::temp_directory_path() /
                ("cc-rhi-factory-" + std::to_string(GetCurrentProcessId()) + ".bmp");
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(bitmap.data()), bitmap.size());
            if (!output)
                throw std::runtime_error("Could not create the texture factory probe file.");
            return path;
        }

        void CheckTextures()
        {
            std::array<unsigned char, 16> pixels = {
                255,0,0,255, 0,255,0,255,
                0,0,255,255, 255,255,255,255
            };
            const auto file = WriteProbeBitmap();
            struct RemoveFile
            {
                std::filesystem::path Path;
                ~RemoveFile() { std::error_code error; std::filesystem::remove(Path, error); }
            } remove{ file };

            std::unique_ptr<Texture2D> fromFile(Texture2D::Create(file.string()));
            std::unique_ptr<Texture2D> empty(Texture2D::Create(2, 2));
            std::unique_ptr<Texture2D> initialized(Texture2D::Create(2, 2, pixels.data()));
            if (!fromFile || !empty || !initialized || !fromFile->GetRendererID() ||
                !empty->GetRendererID() || !initialized->GetRendererID())
                throw std::runtime_error("Common texture factory returned an invalid object.");
            empty->SetData(pixels.data(), static_cast<uint32_t>(pixels.size()));
            for (Texture2D* texture : { fromFile.get(), empty.get(), initialized.get() })
            {
                texture->Bind(3);
                texture->Unbind(3);
            }

            RequireRejected([] { std::unique_ptr<Texture2D> invalid(Texture2D::Create(0, 1)); },
                "Zero-width texture was accepted.");
            RequireRejected([] { std::unique_ptr<Texture2D> invalid(Texture2D::Create(std::string{})); },
                "Empty texture path was accepted.");
        }

        void CheckFramebuffer(GraphicsContext& context)
        {
            std::unique_ptr<Framebuffer> framebuffer(Framebuffer::Create({ 16, 16 }));
            if (!framebuffer || framebuffer->GetSpecification().Width != 16 ||
                framebuffer->GetSpecification().Height != 16 ||
                !framebuffer->GetColorAttachmentRendererID())
                throw std::runtime_error("Common framebuffer factory returned an invalid object.");

            auto* commands = context.GetCommands();
            if (!commands)
                throw std::runtime_error("Factory smoke context has no render command interface.");
            framebuffer->Bind();
            commands->SetViewport(0, 0, 16, 16);
            commands->SetClearColor(0.25f, 0.5f, 0.75f, 1.0f);
            commands->Clear();
            framebuffer->ClearAttachment(1, -37);
            if (framebuffer->ReadPixel(8, 8) != -37)
                throw std::runtime_error("Framebuffer integer attachment clear/readback mismatch.");
            std::vector<uint32_t> colors;
            if (!framebuffer->ReadColorPixels(colors) || colors.size() != 16 * 16)
                throw std::runtime_error("Framebuffer color readback failed.");

            framebuffer->Resize(32, 8);
            if (framebuffer->GetSpecification().Width != 32 || framebuffer->GetSpecification().Height != 8)
                throw std::runtime_error("Framebuffer resize did not update its specification.");
            framebuffer->Bind();
            framebuffer->ClearAttachment(1, 91);
            if (framebuffer->ReadPixel(16, 4) != 91)
                throw std::runtime_error("Resized framebuffer picking attachment mismatch.");
            framebuffer->Unbind();

            RequireRejected([] { std::unique_ptr<Framebuffer> invalid(Framebuffer::Create({ 0, 8 })); },
                "Zero-width framebuffer was accepted.");
        }

        void CheckBackBuffer(GraphicsContext& context)
        {
            auto read = [&](const char* phase, int r, int g, int b)
            {
                std::vector<uint8_t> pixels;
                uint32_t width = 0, height = 0;
                if (!context.ReadBackBuffer(pixels, width, height) || width != 64 || height != 64 ||
                    pixels.size() != size_t(width) * height * 4)
                    throw std::runtime_error(std::string(phase) + ": backbuffer readback failed.");
                const auto check = [&](size_t offset, int expected)
                {
                    return std::abs(int(pixels[offset]) - expected) <= 2;
                };
                if (!check(0, r) || !check(1, g) || !check(2, b) || pixels[3] != 255)
                    throw std::runtime_error(std::string(phase) + ": RGBA readback mismatch.");
            };

            context.BindBackBuffer();
            context.Clear(0.25f, 0.5f, 0.75f, 1.0f);
            read("initial", 64, 128, 191);
            context.SwapBuffers(false);

            context.ResizeBuffers(0, 0);
            std::vector<uint8_t> minimized;
            uint32_t width = 1, height = 1;
            if (context.ReadBackBuffer(minimized, width, height) || !minimized.empty() || width || height)
                throw std::runtime_error("Zero-sized backbuffer returned stale capture data.");
            context.SwapBuffers(false);

            context.ResizeBuffers(64, 64);
            context.BindBackBuffer();
            context.Clear(0.0f, 1.0f, 0.25f, 1.0f);
            read("restored", 0, 255, 64);
            context.SwapBuffers(false);
        }
    }

    int RunFactorySmokeCheck(const char* backendValue, const char* scopeValue)
    {
        try
        {
            const std::string backend = backendValue ? backendValue : "";
            const std::string scope = scopeValue && *scopeValue ? scopeValue : "all";
            if (scope != "all" && scope != "buffer" && scope != "texture" &&
                scope != "framebuffer" && scope != "backbuffer")
                throw std::invalid_argument("Factory smoke scope must be buffer, texture, framebuffer, backbuffer or all.");

            RendererAPI::SetAPI(ParseBackend(backend));
            struct Window
            {
                HWND Handle = CreateWindowExW(0, L"STATIC", L"RHI factory QA", WS_POPUP,
                    0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
                ~Window() { if (Handle) DestroyWindow(Handle); }
            } window;
            if (!window.Handle)
                throw std::runtime_error("Cannot create hidden factory QA window.");

            std::unique_ptr<GraphicsContext> context(GraphicsContext::Create(window.Handle));
            if (!context)
                throw std::runtime_error("Common graphics context factory returned null.");
            context->Init();
            context->MakeCurrent();

            // 리소스가 컨텍스트보다 먼저 파괴되도록 각 검사를 이 scope 안에서 끝낸다.
            if (scope == "all" || scope == "buffer") CheckBuffers();
            if (scope == "all" || scope == "texture") CheckTextures();
            if (scope == "all" || scope == "framebuffer") CheckFramebuffer(*context);
            if (scope == "all" || scope == "backbuffer") CheckBackBuffer(*context);

            std::cout << "PASS " << backend << " common " << scope
                << " factory: creation, update, bind, readback, resize and invalid input rejection\n";
            return 0;
        }
        catch (const std::exception& error)
        {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
}
