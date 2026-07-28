#include <Application.h>
#include <EntryPoint.h>
#include <Renderer/Renderer.h>
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
        CCEngine::Renderer::Shutdown();
    }
};

CCEngine::Application* CCEngine::CreateApplication(const std::vector<std::string>& commandLineArgs)
{
    return new Sandbox(commandLineArgs);
}
