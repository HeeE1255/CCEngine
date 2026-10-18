#include "Runtime/GameLayer.h"

#include "Application.h"
#include "Core/AssetDatabase.h"
#include "Core/ConsoleLog.h"
#include "Core/PathContext.h"
#include "Renderer/Renderer.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

#include <algorithm>
#include <exception>
#include <utility>

#ifdef CC_PLATFORM_WINDOWS
#define NOMINMAX
#include <Windows.h>
#endif

namespace CCEngine
{
    GameLayer::GameLayer(RuntimeManifestData manifest, std::filesystem::path runtimeRoot)
        : Layer("GameLayer"), m_Manifest(std::move(manifest)), m_RuntimeRoot(std::move(runtimeRoot))
    {
    }

    void GameLayer::OnAttach()
    {
        if (!RuntimeManifest::ResolvePathInsideRoot(m_RuntimeRoot, m_Manifest.ContentRoot, m_ContentRoot))
        {
            FailStartup("ContentRoot points outside the build directory.");
            return;
        }

        std::filesystem::path assetRoot;
        if (!RuntimeManifest::ResolvePathInsideRoot(m_ContentRoot, m_Manifest.AssetRoot, assetRoot))
        {
            FailStartup("AssetRoot points outside ContentRoot.");
            return;
        }

        std::error_code ec;
        if (!std::filesystem::exists(assetRoot, ec) || ec)
        {
            FailStartup("AssetRoot is missing: " + assetRoot.string());
            return;
        }

        // Player가 프로세스 작업 디렉터리를 바꾸면 외부 플러그인과 비동기 작업의 상대 경로도
        // 함께 바뀐다. 실행 경로는 고정하고 모든 런타임 시스템이 이 컨텍스트를 사용한다.
        PathContext::ConfigureRuntime(m_RuntimeRoot, m_ContentRoot, assetRoot);

        std::filesystem::path assetManifest;
        if (!RuntimeManifest::ResolvePathInsideRoot(m_ContentRoot, m_Manifest.AssetManifest, assetManifest))
        {
            FailStartup("AssetManifest points outside ContentRoot.");
            return;
        }

        std::string assetManifestError;
        if (!AssetDatabase::LoadRuntimeManifest(assetManifest, assetManifestError))
        {
            FailStartup(assetManifestError);
            return;
        }

        std::filesystem::path startScene;
        if (!m_Manifest.StartSceneGuid.empty())
            startScene = AssetDatabase::GetPathFromGuid(m_Manifest.StartSceneGuid);

        if (startScene.empty())
        {
            if (!RuntimeManifest::ResolvePathInsideRoot(m_ContentRoot, m_Manifest.StartScenePath, startScene))
            {
                FailStartup("StartScenePath points outside ContentRoot.");
                return;
            }
        }

        if (!std::filesystem::exists(startScene, ec) || ec)
        {
            FailStartup("Start scene is missing: " + startScene.string());
            return;
        }

        m_Scene = new Scene();
        try
        {
            SceneSerializer serializer(m_Scene);
            if (!serializer.Deserialize(startScene.string()))
            {
                FailStartup("Failed to load start scene: " + startScene.string());
                return;
            }
        }
        catch (const std::exception& exception)
        {
            FailStartup("Start scene parse failed: " + std::string(exception.what()));
            return;
        }

        m_Scene->OnRuntimeStart();
        m_Loaded = true;
        ConsoleLog::Info("Game Player loaded start scene: " + startScene.string());
    }

    void GameLayer::OnDetach()
    {
        if (m_Scene)
        {
            if (m_Loaded)
                m_Scene->OnRuntimeStop();
            delete m_Scene;
            m_Scene = nullptr;
        }
        m_Loaded = false;
    }

    void GameLayer::OnUpdate(float deltaTime)
    {
        if (m_Loaded && m_Scene)
            m_Scene->OnUpdate(deltaTime);
    }

    void GameLayer::OnRender()
    {
        if (!m_Loaded || !m_Scene)
            return;

        Renderer::SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        Renderer::Clear();

        auto view = m_Scene->GetRegistry().view<CameraComponent>();
        for (auto handle : view)
        {
            Entity cameraEntity(handle, m_Scene);
            const CameraComponent& cameraComponent = view.get<CameraComponent>(handle);
            if (!cameraComponent.Primary || !m_Scene->IsEntityActiveInHierarchy(cameraEntity))
                continue;

            const TransformComponent& transform = cameraEntity.GetComponent<TransformComponent>();
            const Window& window = Application::Get()->GetWindow();
            const float height = static_cast<float>((std::max)(window.GetHeight(), 1u));
            const float aspect = static_cast<float>((std::max)(window.GetWidth(), 1u)) / height;

            PerspectiveCamera camera(cameraComponent.FOV, aspect, cameraComponent.NearClip, cameraComponent.FarClip);
            camera.SetPosition(transform.Translation);
            camera.SetRotation(transform.QuaternionRotation);

            m_Scene->OnRender2D(camera);
            m_Scene->OnRender3D(camera);
            return;
        }

        if (!m_ReportedMissingCamera)
        {
            ConsoleLog::Error("Game Player cannot render because the start scene has no active Primary Camera.");
            m_ReportedMissingCamera = true;
        }
    }

    void GameLayer::FailStartup(const std::string& message)
    {
        ConsoleLog::Error("Game Player startup failed: " + message);
#ifdef CC_PLATFORM_WINDOWS
        std::string dialog = message;
        if (!ConsoleLog::GetFilePath().empty())
            dialog += "\n\nLog: " + ConsoleLog::GetFilePath().string();
        MessageBoxA(nullptr, dialog.c_str(), "CCEngine Game Player", MB_OK | MB_ICONERROR);
#endif
        if (m_Scene)
        {
            delete m_Scene;
            m_Scene = nullptr;
        }
        m_Loaded = false;

        if (Application::Get())
        {
            Application::Get()->SetExitCode(1);
            Application::Get()->GetWindow().SetShouldClose(true);
        }
    }
}
