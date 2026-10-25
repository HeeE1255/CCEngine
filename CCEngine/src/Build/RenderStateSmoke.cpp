#include "Build/RenderStateSmoke.h"
#include "Renderer/BackendShaderPackage.h"
#include <Windows.h>
#include <array>
#include <cmath>
#include <iostream>
#if defined(CC_ENABLE_DX12_RHI)
#include "Platform/DirectX12/DX12Buffer.h"
#include "Platform/DirectX12/DX12Framebuffer.h"
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
        struct SkinVertex { float Position[3], Normal[3]; int Bones[4]; float Weights[4]; };
        struct Probe { float Tint[4]; int Entity; float Depth; float NormalMode = 0, Padding = 0; };
        static_assert(sizeof(SkinVertex) == 56 && sizeof(Probe) == 32);
        template<class Context, class Vertex, class Index, class Uniform, class Target, class Load>
        void Check(HWND window, const char* manifest, Load load)
        {
            Context context(window);
            context.Init(); context.MakeCurrent();
            auto shader = load(context, manifest, "RHIRenderState.hlsl");
            Target target(context, {64,64});
            SkinVertex vertices[] = {
                {{-.2f,-.3f,0},{1,0,0},{0,511,-1,-1},{.25f,.75f,0,0}},
                {{-.2f,.3f,0},{1,0,0},{0,511,-1,-1},{.25f,.75f,0,0}},
                {{.2f,.3f,0},{1,0,0},{0,511,-1,-1},{.25f,.75f,0,0}},
                {{.2f,-.3f,0},{1,0,0},{0,511,-1,-1},{.25f,.75f,0,0}}
            };
            const uint32_t indices[] = {0,1,2,0,2,3};
            Vertex vertex(context,sizeof(vertices),vertices);
            vertex.SetLayout({{ShaderDataType::Float3,"POSITION"},
                {ShaderDataType::Float3,"NORMAL"},
                {ShaderDataType::Int4,"BONEIDS"},{ShaderDataType::Float4,"WEIGHTS"}});
            Index index(context,indices,6);
            Uniform bones(context,512*64), probe(context,sizeof(Probe));
            std::array<float,512*16> matrices{};
            for (size_t i=0;i<512;++i)
                for (size_t d=0;d<4;++d) matrices[i*16+d*5]=1;
            // CPU의 전치된 행렬 업로드 규약을 그대로 사용한다. 511번은 마지막 유효 본이다.
            auto pose = [&](float first, float last) {
                matrices[3]=first; matrices[511*16+3]=last;
                bones.SetData(matrices.data(),sizeof(matrices)); bones.Bind(2);
            };
            auto& commands = *context.GetCommands();
            auto begin = [&] {
                target.Bind(); commands.SetViewport(0,0,64,64);
                commands.SetScissorEnable(false); commands.SetCullMode(RendererAPI::CullMode::None);
                commands.SetDepthTest(true); commands.SetBlendMode(RendererAPI::BlendMode::Opaque);
                commands.SetClearColor(0,0,0,1); commands.Clear(); target.ClearAttachment(1,-1);
                shader->Bind(); shader->BindLayout(vertex.GetLayout()); vertex.Bind(); index.Bind();
            };
            bool normalMode = false;
            auto draw = [&](float r,float g,float b,float alpha,float depth,int id) {
                Probe data{{r,g,b,alpha},id,depth};
                data.NormalMode = normalMode ? 1.f : 0.f;
                probe.SetData(&data,sizeof(data)); probe.Bind(5); commands.DrawIndexed(6);
            };
            auto expect = [&](const char* test,int x,int r,int g,int b,int alpha,int id) {
                std::vector<uint32_t> pixels;
                if (!target.ReadColorPixels(pixels) || pixels.size()!=4096)
                    throw std::runtime_error(std::string(test)+": readback failed");
                const auto* p=reinterpret_cast<const unsigned char*>(&pixels[32*64+x]);
                if (std::abs(int(p[0])-r)>1 || std::abs(int(p[1])-g)>1 ||
                    std::abs(int(p[2])-b)>1 || std::abs(int(p[3])-alpha)>1 ||
                    target.ReadPixel(x,32)!=id)
                    throw std::runtime_error(std::string(test)+": pixel="+std::to_string(p[0])+","+
                        std::to_string(p[1])+","+std::to_string(p[2])+","+std::to_string(p[3])+
                        " id="+std::to_string(target.ReadPixel(x,32)));
            };
            begin(); pose(0,0); draw(1,0,0,1,.2f,11); draw(0,1,0,1,.8f,22);
            expect("near survives later far",32,255,0,0,255,11);
            begin(); pose(0,0); draw(0,1,0,1,.8f,22); draw(1,0,0,1,.2f,11);
            expect("near replaces far",32,255,0,0,255,11);
            commands.SetDepthTest(false); draw(0,0,1,1,.9f,33);
            expect("depth disabled",32,0,0,255,255,33);
            // 깊이를 끈 draw는 깊이 버퍼를 덮지 않아야 한다.
            commands.SetDepthTest(true); draw(0,1,0,1,.5f,22);
            expect("depth restored",32,0,0,255,255,33);
            for (auto mode : {RendererAPI::BlendMode::Transparent,RendererAPI::BlendMode::MRTPicking}) {
                begin(); pose(0,0); draw(0,0,1,1,.8f,33);
                commands.SetBlendMode(mode); draw(1,0,0,.25f,.2f,-17);
                expect("alpha blend with integer MRT",32,64,0,191,64,-17);
                commands.SetBlendMode(RendererAPI::BlendMode::Opaque);
                draw(0,1,0,.5f,.1f,22);
                expect("opaque restored",32,0,255,0,128,22);
            }
            // 첫 draw가 끝나기 전에 같은 상수 버퍼를 갱신해도 이전 포즈는 보존되어야 한다.
            begin(); pose(-.8f,-.4f); draw(1,0,0,1,.5f,11);
            pose(.8f,.4f); draw(0,1,0,1,.5f,22);
            expect("weighted left pose",16,255,0,0,255,11);
            expect("weighted right pose",48,0,255,0,255,22);
            expect("no unskinned center",32,0,0,0,255,-1);
            begin(); pose(0,0); draw(0,0,1,1,.5f,33);
            expect("pose reset",32,0,0,255,255,33);
            expect("old pose cleared",16,0,0,0,255,-1);
            // 비대칭 행렬로 전치/회전 부호 오류를 검출한다. 법선은 색상으로 부호까지 읽는다.
            auto rotate = [&](size_t bone, float sine) {
                matrices[bone*16]=0; matrices[bone*16+1]=-sine;
                matrices[bone*16+4]=sine; matrices[bone*16+5]=0;
            };
            normalMode = true;
            begin(); rotate(0,1); rotate(511,1); pose(0,0); draw(1,1,1,1,.5f,41);
            expect("rotated geometry and normal",40,128,255,128,255,41);
            begin(); rotate(0,-1); rotate(511,-1); pose(0,0); draw(1,1,1,1,.5f,42);
            expect("reverse rotation normal",40,128,0,128,255,42);
            begin();
            matrices[0]=matrices[5]=1; matrices[1]=matrices[4]=0;
            rotate(511,1); pose(0,0); draw(1,1,1,1,.5f,43);
            expect("weighted normalized normal",32,168,248,128,255,43);
        }
    }
#endif
    int RunRenderStateSmokeCheck(const char* backend,const char* manifest)
    {
        try {
            if (!backend || !manifest || !*manifest) throw std::invalid_argument("--rhi-manifest is required");
            struct Window {
                HWND Handle=CreateWindowExW(0,L"STATIC",L"RHI render state QA",WS_POPUP,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
                ~Window() { if(Handle) DestroyWindow(Handle); }
            } window;
            if (!window.Handle) throw std::runtime_error("Cannot create hidden QA window");
#if defined(CC_ENABLE_DX12_RHI)
            if (std::string(backend)=="dx12")
                Check<DX12Context,DX12VertexBuffer,DX12IndexBuffer,DX12ConstantBuffer,DX12Framebuffer>(window.Handle,manifest,LoadDX12ShaderPackage);
            else
#endif
#if defined(CC_ENABLE_OPENGL_RHI)
            if (std::string(backend)=="opengl")
                Check<OpenGLContext,OpenGLVertexBuffer,OpenGLIndexBuffer,OpenGLConstantBuffer,OpenGLFramebuffer>(window.Handle,manifest,LoadOpenGLShaderPackage);
            else
#endif
#if defined(CC_ENABLE_VULKAN_RHI)
            if (std::string(backend)=="vulkan")
                Check<VulkanContext,VulkanVertexBuffer,VulkanIndexBuffer,VulkanConstantBuffer,VulkanFramebuffer>(window.Handle,manifest,LoadVulkanShaderPackage);
            else
#endif
                throw std::runtime_error("Requested render state backend is not compiled");
            std::cout<<"PASS "<<backend<<" GPU depth, blend, signed MRT, weighted skinning, bone 511, in-flight uniform updates, rotation and normals\n";
            return 0;
        } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
    }
}
