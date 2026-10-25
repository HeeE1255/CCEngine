#include "Build/GameBuildService.h"
#include "Renderer/BackendShaderPackage.h"
#include "Renderer/Shader.h"
#include "Renderer/RendererAPI.h"
#include <Windows.h>
#include <iostream>
#include <cstring>
#if defined(CC_ENABLE_DX12_RHI)
#include "Platform/DirectX12/DX12Buffer.h"
#include "Platform/DirectX12/DX12Framebuffer.h"
#include "Platform/DirectX12/DX12Shader.h"
#endif
#if defined(CC_ENABLE_OPENGL_RHI)
#include "Platform/OpenGL/OpenGLBuffer.h"
#include "Platform/OpenGL/OpenGLFramebufferStorage.h"
#endif
#if defined(CC_ENABLE_VULKAN_RHI)
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanFramebuffer.h"
#endif

namespace CCEngine
{
#if defined(CC_ENABLE_DX12_RHI) || defined(CC_ENABLE_OPENGL_RHI) || defined(CC_ENABLE_VULKAN_RHI)
    namespace
    {
        template<class Context, class Vertex, class Index, class Target, class Load>
        void CheckMapping(HWND window, const char* manifest, Load load)
        {
            Context context(window);
            context.Init();
            context.MakeCurrent();
            auto shader = load(context, manifest, "RHIVertexMapping.hlsl");
            const auto& properties = shader->GetMaterialProperties();
            if (properties.size() != 2 || properties[0].Name != "ZScale" ||
                properties[1].Name != "AScale" || properties[0].DefaultFloat != 2 || properties[1].DefaultFloat != 3)
                throw std::runtime_error("Packaged material declaration order/defaults mismatch");
            Target target(context, {64, 64});
            struct A { float Position[2], Unused[3], Color[4]; int ID; };
            struct B { float Color[4]; int ID; float Unused[3], Position[2]; };
            static_assert(sizeof(A) == 40 && sizeof(B) == 40);
            A first[4]{}; B second[4]{};
            const float positions[4][2] = {{-.8f,-.8f}, {-.8f,.8f}, {.8f,.8f}, {.8f,-.8f}};
            for (int i = 0; i < 4; ++i)
            {
                std::memcpy(first[i].Position, positions[i], sizeof(positions[i]));
                std::memcpy(second[i].Position, positions[i], sizeof(positions[i]));
                first[i].Color[1] = first[i].Color[3] = 1;
                second[i].Color[2] = second[i].Color[3] = 1;
                first[i].ID = 42; second[i].ID = -17;
            }
            Vertex a(context, sizeof(first), first);
            Vertex b(context, sizeof(second), second);
            Vertex invalid(context, sizeof(first), first);
            Vertex wrongType(context, sizeof(first), first);
            a.SetLayout({{ShaderDataType::Float2,"POSITION"}, {ShaderDataType::Float3,"UNUSED"},
                         {ShaderDataType::Float4,"COLOR"}, {ShaderDataType::Int,"ENTITY_ID"}});
            b.SetLayout({{ShaderDataType::Float4,"COLOR"}, {ShaderDataType::Int,"ENTITY_ID"},
                         {ShaderDataType::Float3,"UNUSED"}, {ShaderDataType::Float2,"POSITION"}});
            invalid.SetLayout({{ShaderDataType::Float2,"POSITION"}});
            wrongType.SetLayout({{ShaderDataType::Float2,"POSITION"}, {ShaderDataType::Float3,"UNUSED"},
                {ShaderDataType::Int4,"COLOR"}, {ShaderDataType::Int,"ENTITY_ID"}});
            const uint32_t indices[] = {0,1,2,0,2,3};
            Index index(context, indices, 6);
            auto& commands = *context.GetCommands();
            auto draw = [&](Vertex& vertex, int id, bool blue)
            {
                target.Bind(); commands.SetViewport(0,0,64,64);
                commands.SetDepthTest(false); commands.SetCullMode(RendererAPI::CullMode::None);
                commands.SetBlendMode(RendererAPI::BlendMode::Opaque); commands.SetScissorEnable(false);
                commands.SetClearColor(1,0,0,1); commands.Clear(); target.ClearAttachment(1, -1);
                if (blue) vertex.Bind();
                shader->Bind(); shader->BindLayout(vertex.GetLayout());
                if (!blue) vertex.Bind();
                index.Bind();
                commands.DrawIndexed(6);
                std::vector<uint32_t> pixels;
                if (!target.ReadColorPixels(pixels) || pixels.size() != 4096)
                    throw std::runtime_error("Vertex mapping color readback failed");
                auto color = [&](uint32_t x, uint32_t y, unsigned char r, unsigned char g, unsigned char b)
                {
                    const auto* bytes = reinterpret_cast<const unsigned char*>(&pixels[y * 64 + x]);
                    if (bytes[0] != r || bytes[1] != g || bytes[2] != b || bytes[3] != 255)
                        throw std::runtime_error("Vertex mapping GPU color mismatch");
                };
                color(32,32,0,blue ? 0 : 255,blue ? 255 : 0);
                color(1,1,255,0,0);
                if (target.ReadPixel(32,32) != id || target.ReadPixel(1,1) != -1)
                    throw std::runtime_error("Vertex mapping GPU integer picking mismatch");
            };
            // 원래 배치 → 다른 배치 → 원래 배치로 바꿔 캐시/VAO의 오래된 연결까지 검사한다.
            draw(a,42,false); draw(b,-17,true); draw(a,42,false);
            std::cout << "PASS reordered vertex buffers and signed picking\n";
            for (auto* bad : {&invalid, &wrongType})
            {
                bool rejected = false;
                try { shader->BindLayout(bad->GetLayout()); bad->Bind(); commands.DrawIndexed(6); }
                catch (const std::invalid_argument&) { rejected = true; }
                if (!rejected) throw std::runtime_error("Missing or mistyped vertex attributes were accepted");
                draw(b,-17,true);
            }
        }
    }
#endif
#if defined(CC_ENABLE_OPENGL_RHI) || defined(CC_ENABLE_VULKAN_RHI)
    static std::unique_ptr<Shader> CheckPortableRegistration(const std::string& backend,
        const char* manifest, const char* source)
    {
        struct Cleanup { ~Cleanup() { ClearPortableShaderPackages(); } } cleanup;
        const auto path = std::filesystem::absolute(manifest);
        auto configure = [&] { ConfigurePortableShaderPackages(backend, {path}, path.parent_path()); };
        auto reject = [](auto operation) {
            bool rejected = false;
            try { operation(); } catch (const std::exception&) { rejected = true; }
            if (!rejected) throw std::runtime_error("Invalid portable shader registration was accepted");
        };
        configure();
        reject([&] { ConfigurePortableShaderPackages(backend, {path,path}, path.parent_path()); });
        std::filesystem::path vertex, pixel, unusedVertex, unusedPixel;
        auto resolve = [&] { GetPortablePackageFiles(backend, source, {}, vertex, pixel); };
        resolve();
        reject([&] { LoadRegisteredPortableShader(backend, pixel, vertex); });
        reject([&] { GetPortablePackageFiles(backend, "../outside.hlsl", {}, unusedVertex, unusedPixel); });
        reject([&] { GetPortablePackageFiles(backend, source, {"MISSING_VARIANT"}, unusedVertex, unusedPixel); });
        configure();
        reject([&] { LoadRegisteredPortableShader(backend, vertex, pixel); });
        resolve();
        ClearPortableShaderPackages();
        reject([&] { LoadRegisteredPortableShader(backend, vertex, pixel); });
        configure(); resolve();
        auto shader = LoadRegisteredPortableShader(backend, vertex, pixel);
        std::cout << "PASS portable package lifecycle, failed-config preservation, source bounds, keyword and file-pair rejection\n";
        return shader;
    }
#endif
    int RunVertexMappingSmokeCheck(const char* backend, const char* manifest)
    {
        try
        {
            if (!backend || !manifest || !*manifest) throw std::invalid_argument("--rhi-manifest is required");
            struct Window
            {
                HWND Handle = CreateWindowExW(0,L"STATIC",L"RHI input QA",WS_POPUP,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
                ~Window() { if (Handle) DestroyWindow(Handle); }
            } window;
            if (!window.Handle) throw std::runtime_error("Cannot create hidden QA window");
#if defined(CC_ENABLE_DX12_RHI)
            if (std::string(backend) == "dx12-package")
                CheckMapping<DX12Context,DX12VertexBuffer,DX12IndexBuffer,DX12Framebuffer>(
                    window.Handle, manifest, [](DX12Context& context, const char* package, const char* source) {
                        struct Cleanup { ~Cleanup() { ClearDX12ShaderPackages(); } } cleanup;
                        const auto path = std::filesystem::absolute(package);
                        // 원본 HLSL 파일이 없어도 배포 패키지의 상대 소스 이름으로 찾을 수 있어야 한다.
                        auto configure = [&] { ConfigureDX12ShaderPackages({path}, path.parent_path()); };
                        std::filesystem::path vertex, pixel;
                        auto resolve = [&] { GetDX12PackageBytecode(source, {}, vertex, pixel); };
                        auto expectEmpty = [&] {
                            bool inputRejected = false, textureRejected = false, propertyRejected = false;
                            try { GetDX12VertexInputs(vertex); }
                            catch (const std::runtime_error&) { inputRejected = true; }
                            try { GetDX12TextureBindings(pixel); }
                            catch (const std::runtime_error&) { textureRejected = true; }
                            try { GetDX12MaterialPropertySource(pixel); }
                            catch (const std::runtime_error&) { propertyRejected = true; }
                            if (!inputRejected || !textureRejected || !propertyRejected)
                                throw std::runtime_error("Stale DX12 package metadata survived reset");
                        };
                        configure(); resolve();
                        if (GetDX12VertexInputs(vertex).size() != 3)
                            throw std::runtime_error("DX12 package vertex metadata was not registered");
                        configure(); expectEmpty();
                        resolve();
                        ClearDX12ShaderPackages(); expectEmpty();
                        bool unconfiguredRejected = false;
                        try { resolve(); }
                        catch (const std::runtime_error&) { unconfiguredRejected = true; }
                        if (!unconfiguredRejected)
                            throw std::runtime_error("Cleared DX12 package still resolved bytecode");
                        configure(); resolve();
                        auto shader = DX12Shader::FromBytecodeFiles(context, vertex, pixel);
                        shader->SetVertexInputs(GetDX12VertexInputs(vertex));
                        shader->SetTextureBindings(GetDX12TextureBindings(pixel));
                        shader->SetMaterialPropertySource(GetDX12MaterialPropertySource(pixel));
                        std::cout << "PASS DX12 package registration, reconfiguration, clear and reload\n";
                        return shader;
                    });
            else
            if (std::string(backend) == "dx12")
                CheckMapping<DX12Context,DX12VertexBuffer,DX12IndexBuffer,DX12Framebuffer>(window.Handle,manifest,LoadDX12ShaderPackage);
            else
#endif
#if defined(CC_ENABLE_OPENGL_RHI)
            if (std::string(backend) == "opengl-package")
                CheckMapping<OpenGLContext,OpenGLVertexBuffer,OpenGLIndexBuffer,OpenGLFramebuffer>(window.Handle,manifest,
                    [](auto&, const char* path, const char* source) { return CheckPortableRegistration("opengl",path,source); });
            else
            if (std::string(backend) == "opengl")
                CheckMapping<OpenGLContext,OpenGLVertexBuffer,OpenGLIndexBuffer,OpenGLFramebuffer>(window.Handle,manifest,LoadOpenGLShaderPackage);
            else
#endif
#if defined(CC_ENABLE_VULKAN_RHI)
            if (std::string(backend) == "vulkan-package")
                CheckMapping<VulkanContext,VulkanVertexBuffer,VulkanIndexBuffer,VulkanFramebuffer>(window.Handle,manifest,
                    [](auto&, const char* path, const char* source) { return CheckPortableRegistration("vulkan",path,source); });
            else
            if (std::string(backend) == "vulkan")
                CheckMapping<VulkanContext,VulkanVertexBuffer,VulkanIndexBuffer,VulkanFramebuffer>(window.Handle,manifest,LoadVulkanShaderPackage);
            else
#endif
                throw std::runtime_error("Requested input QA backend is not compiled");
            std::cout << "PASS " << backend << " GPU vertex mapping: reordered buffers, unused fields, color/picking readback, missing/type-mismatched input rejection and recovery\n";
            return 0;
        }
        catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    }
}
