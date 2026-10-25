#include "Build/TextureBindingSmoke.h"
#include "Renderer/BackendShaderPackage.h"
#include "Renderer/MaterialTextureBinding.h"
#include <Windows.h>
#include <iostream>
#include <unordered_map>
#include <cstdlib>
#if defined(CC_ENABLE_DX12_RHI)
#include "Platform/DirectX12/DX12Buffer.h"
#include "Platform/DirectX12/DX12Texture.h"
#include "Platform/DirectX12/DX12Framebuffer.h"
#endif
#if defined(CC_ENABLE_OPENGL_RHI)
#include "Platform/OpenGL/OpenGLBuffer.h"
#include "Platform/OpenGL/OpenGLTextureStorage.h"
#include "Platform/OpenGL/OpenGLFramebufferStorage.h"
#endif
#if defined(CC_ENABLE_VULKAN_RHI)
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanTextureStorage.h"
#include "Platform/Vulkan/VulkanFramebuffer.h"
#endif

namespace CCEngine
{
#if defined(CC_ENABLE_DX12_RHI) || defined(CC_ENABLE_OPENGL_RHI) || defined(CC_ENABLE_VULKAN_RHI)
    namespace
    {
        template<class Context, class Vertex, class Index, class Target, class Texture, class Load>
        void CheckTextures(HWND window, const char* manifest, Load load)
        {
            Context context(window);
            context.Init(); context.MakeCurrent();
            auto shader = load(context, manifest, "RHITextureSampling.hlsl");
            if (shader->GetTextureBindings() != Shader::TextureBindings{{"ZDetail",3},{"AMask",7}})
                throw std::runtime_error("Texture reflection did not preserve t3/t7");
            Target target(context, {64,64});
            const float positions[] = {-.8f,-.8f, -.8f,.8f, .8f,.8f, .8f,-.8f};
            const uint32_t indices[] = {0,1,2,0,2,3};
            Vertex vertex(context, sizeof(positions), positions);
            vertex.SetLayout({{ShaderDataType::Float2,"POSITION"}});
            Index index(context, indices, 6);
            unsigned char white[] = {255,255,255,255}, red[] = {255,0,0,255};
            unsigned char green[] = {0,255,0,255}, blue[] = {0,0,255,255};
            auto fallback = std::make_shared<Texture>(context,1,1,white);
            auto detail = std::make_shared<Texture>(context,1,1,red);
            auto mask = std::make_shared<Texture>(context,1,1,green);
            std::unordered_map<std::string,std::shared_ptr<Texture2D>> properties;
            properties.emplace("AMask",mask); properties.emplace("ZDetail",detail);
            auto& commands = *context.GetCommands();
            auto begin = [&]
            {
                target.Bind(); commands.SetViewport(0,0,64,64);
                commands.SetDepthTest(false); commands.SetCullMode(RendererAPI::CullMode::None);
                commands.SetBlendMode(RendererAPI::BlendMode::Opaque); commands.SetScissorEnable(false);
                commands.SetClearColor(0,0,0,1); commands.Clear(); target.ClearAttachment(1,-1);
            };
            auto draw = [&]
            {
                shader->Bind(); shader->BindLayout(vertex.GetLayout()); vertex.Bind(); index.Bind();
                BindMaterialTextures(*shader, fallback, [&](const std::string& name) {
                    const auto found = properties.find(name);
                    return found == properties.end() ? std::shared_ptr<Texture2D>{} : found->second;
                });
                commands.DrawIndexed(6);
            };
            auto read = [&]
            {
                std::vector<uint32_t> result;
                if (!target.ReadColorPixels(result) || result.size() != 4096)
                    throw std::runtime_error("Texture color readback failed");
                if (target.ReadPixel(32,32) != 31 || target.ReadPixel(1,1) != -1)
                    throw std::runtime_error("Texture draw picking mismatch");
                return result;
            };
            auto pixel = [](const std::vector<uint32_t>& pixels, uint32_t x, int r, int g, int b)
            {
                const auto* bytes = reinterpret_cast<const unsigned char*>(&pixels[32*64+x]);
                if (std::abs(int(bytes[0])-r)>1 || std::abs(int(bytes[1])-g)>1 ||
                    std::abs(int(bytes[2])-b)>1 || bytes[3]!=255)
                    throw std::runtime_error("Texture sample mismatch: actual " + std::to_string(bytes[0]) + "," + std::to_string(bytes[1]) + "," + std::to_string(bytes[2]));
            };
            auto check = [&](int r,int g,int b) { begin(); draw(); pixel(read(),32,r,g,b); };
            check(64,191,0);
            mask->SetData(blue,sizeof(blue)); check(64,0,191);
            properties.erase("ZDetail"); check(64,64,255);
            properties.clear(); check(255,255,255);
            properties.emplace("ZDetail",detail); properties.emplace("AMask",mask);
            // readback 전에 텍스처를 교체한다. 먼저 제출한 draw의 리소스 수명이 유지되어야 한다.
            begin(); commands.SetScissorEnable(true); commands.SetScissor(0,0,32,64); draw();
            detail->SetData(blue,sizeof(blue));
            commands.SetScissor(32,0,32,64); draw();
            auto pixels = read(); pixel(pixels,20,64,0,191); pixel(pixels,44,0,0,255);
            check(0,0,255);
        }
    }
#endif
    int RunTextureBindingSmokeCheck(const char* backend, const char* manifest)
    {
        try
        {
            if (!backend || !manifest || !*manifest) throw std::invalid_argument("--rhi-manifest is required");
            struct Window
            {
                HWND Handle = CreateWindowExW(0,L"STATIC",L"RHI texture QA",WS_POPUP,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
                ~Window() { if (Handle) DestroyWindow(Handle); }
            } window;
            if (!window.Handle) throw std::runtime_error("Cannot create hidden QA window");
#if defined(CC_ENABLE_DX12_RHI)
            if (std::string(backend)=="dx12")
                CheckTextures<DX12Context,DX12VertexBuffer,DX12IndexBuffer,DX12Framebuffer,DX12Texture2D>(window.Handle,manifest,LoadDX12ShaderPackage);
            else
#endif
#if defined(CC_ENABLE_OPENGL_RHI)
            if (std::string(backend)=="opengl")
                CheckTextures<OpenGLContext,OpenGLVertexBuffer,OpenGLIndexBuffer,OpenGLFramebuffer,OpenGLTexture2D>(window.Handle,manifest,LoadOpenGLShaderPackage);
            else
#endif
#if defined(CC_ENABLE_VULKAN_RHI)
            if (std::string(backend)=="vulkan")
                CheckTextures<VulkanContext,VulkanVertexBuffer,VulkanIndexBuffer,VulkanFramebuffer,VulkanTexture2D>(window.Handle,manifest,LoadVulkanShaderPackage);
            else
#endif
                throw std::runtime_error("Requested texture QA backend is not compiled");
            std::cout << "PASS " << backend << " GPU material textures: t3/t7 mapping, replacement, missing fallback, in-flight update, color/picking readback\n";
            return 0;
        }
        catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    }
}
