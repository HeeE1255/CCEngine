#include <Application.h>
#include <EntryPoint.h>
#include <Renderer/Renderer.h>
#include <Core/ProjectSettings.h>
#include <Build/GameBuildService.h>
#include <Renderer/RendererAPI.h>
#include <algorithm>
#include <stdexcept>
#include "Editor/EditorLayer.h" // 방금 만든 에디터 레이어!
#include <vector>

class Sandbox : public CCEngine::Application
{
public:
    // Application 생성 시 콘솔 인자 전달 (RHI 셋업용)
    Sandbox(const std::vector<std::string>& commandLineArgs) : Application(commandLineArgs)
    {
        CCEngine::Renderer::Init();
        PushOverlay(new CCEngine::EditorLayer());
    }

    ~Sandbox()
    {
    }
};

CCEngine::Application* CCEngine::CreateApplication(const std::vector<std::string>& commandLineArgs)
{
    auto arguments = CCEngine::ProjectSettings::ResolveEditorGraphicsArguments(commandLineArgs);
    const auto apiFlags = std::count_if(arguments.begin(), arguments.end(), [](const std::string& argument)
        { return argument == "-dx11" || argument == "-dx12" || argument == "-opengl" || argument == "-vulkan"; });
    if (apiFlags > 1)
        throw std::invalid_argument("Specify only one graphics API: -dx11, -dx12, -opengl or -vulkan.");
    auto api = CCEngine::RendererAPI::API::DirectX11;
    for (const auto& argument : arguments)
    {
        if (argument == "-dx12") api = CCEngine::RendererAPI::API::DirectX12;
        else if (argument == "-opengl") api = CCEngine::RendererAPI::API::OpenGL;
        else if (argument == "-vulkan") api = CCEngine::RendererAPI::API::Vulkan;
    }
    const bool explicitPackage = std::any_of(arguments.begin(), arguments.end(), [](const std::string& value)
        { return value.rfind("--shader-package=", 0) == 0; });
    if (api != CCEngine::RendererAPI::API::DirectX11 && !explicitPackage)
    {
        CCEngine::RendererAPI::RequireAvailable(api);
        std::vector<std::filesystem::path> packages;
        std::string error;
        const auto root = std::filesystem::current_path();
        if (!CCEngine::PrepareEditorBackendPackages(root, CCEngine::RendererAPI::GetAPIName(api), packages, error))
            throw std::runtime_error(error);
        arguments.push_back("--shader-source-root=" + root.string());
        for (const auto& path : packages) arguments.push_back("--shader-package=" + path.string());
    }
    return new Sandbox(arguments);
}
