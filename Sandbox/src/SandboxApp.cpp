#include <Application.h>
#include <EntryPoint.h>
#include <Renderer/Renderer.h>
#include <Renderer/Buffer.h>
#include <Renderer/Shader.h>
#include <DirectXMath.h> 
#include <chrono>
#include <windows.h>
#include <filesystem>

#include "imgui.h" 
#include "Renderer/PerspectiveCamera.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/Texture.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ScriptableEntity.h"
#include "Scene/SceneSerializer.h"  
#include "Utils/PlatformUtils.h"    
#include "Editor/SceneHierarchyPanel.h" 
#include "Core/MemoryMacro.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class Sandbox : public CCEngine::Application
{
private:
    CCEngine::Scene* m_ActiveScene = nullptr;
    CCEngine::Scene* m_EditorScene = nullptr; // 플레이(Play) 모드 진입 시 원본 씬을 보관할 포인터

    CCEngine::SceneHierarchyPanel m_HierarchyPanel;
    CCEngine::PerspectiveCamera m_Camera;

    DirectX::XMFLOAT3 m_CameraPosition = { 0.0f, 0.0f, -5.0f };
    DirectX::XMFLOAT4 m_CameraQuat = { 0.0f, 0.0f, 0.0f, 1.0f };
    float m_ClearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
    std::chrono::time_point<std::chrono::high_resolution_clock> m_LastFrameTime;

    /////////////////////////////
    bool m_IsSPressedLastFrame = false;
    std::string m_CurrentScenePath = "";

    void SaveScene()
    {
        if (m_CurrentScenePath.empty())
        {
            SaveSceneAs();
            return;
        }

        CCEngine::SceneSerializer serializer(m_ActiveScene);
        serializer.Serialize(m_CurrentScenePath);
        printf("Scene Saved to: %s\n", m_CurrentScenePath.c_str());
    }

    void SaveSceneAs()
    {
        std::filesystem::path initialDirPath = std::filesystem::current_path() / "assets" / "scenes";
        std::string initialDirStr = initialDirPath.string();

        std::string filepath = CCEngine::PlatformUtils::SaveFile("CCEngine Scene (*.ccscene)\0*.ccscene\0", initialDirStr.c_str());

        if (!filepath.empty())
        {
            m_CurrentScenePath = filepath;

            CCEngine::SceneSerializer serializer(m_ActiveScene);
            serializer.Serialize(m_CurrentScenePath);
            printf("Scene Saved As: %s\n", m_CurrentScenePath.c_str());
        }
    }

    void OpenScene()
    {
        std::filesystem::path initialDirPath = std::filesystem::current_path() / "assets" / "scenes";
        std::string initialDirStr = initialDirPath.string();

        std::string filepath = CCEngine::PlatformUtils::OpenFile("CCEngine Scene (*.ccscene)\0*.ccscene\0", initialDirStr.c_str());

        if (!filepath.empty())
        {
            CCEngine::SceneSerializer serializer(m_ActiveScene);
            if (serializer.Deserialize(filepath))
            {
                m_CurrentScenePath = filepath;
                printf("Scene Loaded from: %s\n", m_CurrentScenePath.c_str());
            }
            else
            {
                printf("Failed to load scene!\n");
            }
        }
    }
    //////////////////////////////

    // ==============================================================================
    // 에디터 재생/정지 버튼을 포함한 툴바 UI
    // ==============================================================================
    void UI_Toolbar()
    {
        // 1. 창을 상단 메뉴바 바로 아래에 꽉 차게 고정
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + ImGui::GetFrameHeight()));
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, 40.0f));

        // 2. 타이틀바 숨김 및 이동/크기조절 불가 플래그
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;

        ImGui::Begin("FixedToolbar", nullptr, window_flags);

        CCEngine::SceneState state = m_ActiveScene->GetState();
        bool isEdit = (state == CCEngine::SceneState::Edit);
        bool isPlay = (state == CCEngine::SceneState::Play);
        bool isPause = (state == CCEngine::SceneState::Pause);

        // 3. 버튼 3개(가로 60픽셀 * 3)가 정중앙에 오도록 커서 위치 계산
        float totalButtonWidth = (60.0f * 3.0f) + (ImGui::GetStyle().ItemSpacing.x * 2.0f);
        ImGui::SetCursorPosX((viewport->Size.x * 0.5f) - (totalButtonWidth * 0.5f));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f); // 위쪽 여백

        // [버튼 1: Play / Resume]
        if (ImGui::Button(isPause ? "Resume" : "Play", ImVec2(60, 24)))
        {
            if (isEdit)
            {
                // 에디트 모드 -> 플레이 시작 (씬 복사)
                m_EditorScene = m_ActiveScene;
                m_ActiveScene = CCEngine::Scene::Copy(m_EditorScene);
                m_ActiveScene->OnRuntimeStart();
                m_HierarchyPanel.SetContext(m_ActiveScene);
            }
            else if (isPause)
            {
                // 일시정지 -> 다시 플레이
                m_ActiveScene->SetSceneState(CCEngine::SceneState::Play);
            }
        }

        ImGui::SameLine();

        // [버튼 2: Pause]
        if (ImGui::Button("Pause", ImVec2(60, 24)))
        {
            if (isPlay)
            {
                // 플레이 중 -> 일시정지
                m_ActiveScene->SetSceneState(CCEngine::SceneState::Pause);
            }
        }

        ImGui::SameLine();

        // [버튼 3: Stop]
        if (ImGui::Button("Stop", ImVec2(60, 24)))
        {
            if (!isEdit)
            {
                // 플레이/일시정지 중 -> 원본 씬 복원
                m_ActiveScene->OnRuntimeStop();
                delete m_ActiveScene;

                m_ActiveScene = m_EditorScene;
                m_EditorScene = nullptr;
                m_HierarchyPanel.SetContext(m_ActiveScene);
            }
        }

        ImGui::End();
    }
    // ==============================================================================

public:
    Sandbox()
        : m_Camera(45.0f, 1280.0f / 720.0f, 0.1f, 100.0f)
    {
        CCEngine::Renderer2D::Init();

        m_ActiveScene = new CCEngine::Scene();
        m_HierarchyPanel.SetContext(m_ActiveScene);

        // 1. 바닥(Floor) 생성 - 떨어지지 않는 Static 속성
        auto floor = m_ActiveScene->CreateEntity("Physics Floor");
        floor.GetComponent<CCEngine::TransformComponent>().Translation.y = -2.0f;
        floor.GetComponent<CCEngine::TransformComponent>().Scale = { 10.0f, 0.5f, 1.0f };
        floor.AddComponent<CCEngine::SpriteRendererComponent>(DirectX::XMFLOAT4(0.2f, 0.8f, 0.2f, 1.0f));
        floor.AddComponent<CCEngine::Rigidbody2DComponent>();
        floor.AddComponent<CCEngine::BoxCollider2DComponent>();

        // 2. 상자(Box) 생성 - 중력을 받는 Dynamic 속성
        auto box = m_ActiveScene->CreateEntity("Physics Box");
        box.GetComponent<CCEngine::TransformComponent>().Translation.y = 5.0f;
        box.AddComponent<CCEngine::SpriteRendererComponent>(DirectX::XMFLOAT4(0.8f, 0.2f, 0.2f, 1.0f));
        auto& boxRb = box.AddComponent<CCEngine::Rigidbody2DComponent>();
        boxRb.Type = CCEngine::Rigidbody2DComponent::BodyType::Dynamic;
        box.AddComponent<CCEngine::BoxCollider2DComponent>();

        m_LastFrameTime = std::chrono::high_resolution_clock::now();
    }

    ~Sandbox()
    {
        if (m_EditorScene)
        {
            m_ActiveScene->OnRuntimeStop();
            delete m_ActiveScene;
            m_ActiveScene = m_EditorScene;
        }

        if (m_ActiveScene != nullptr)
        {
            delete m_ActiveScene;
        }

        CCEngine::Renderer2D::Shutdown();
    }

    virtual void OnUpdate() override
    {
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - m_LastFrameTime).count();
        m_LastFrameTime = currentTime;

        if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
            DirectX::XMVECTOR quat = DirectX::XMLoadFloat4(&m_CameraQuat);

            DirectX::XMMATRIX camRotationMatrix = DirectX::XMMatrixRotationQuaternion(quat);
            DirectX::XMVECTOR forward = DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), camRotationMatrix);
            DirectX::XMVECTOR right = DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), camRotationMatrix);
            DirectX::XMVECTOR up = DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), camRotationMatrix);

            ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
            float pitchAngle = mouseDelta.y * 0.003f;
            float yawAngle = mouseDelta.x * 0.003f;

            float rollAngle = 0.0f;
            float rollSpeed = 2.0f * deltaTime;
            if (GetAsyncKeyState('Z') & 0x8000) rollAngle += rollSpeed;
            if (GetAsyncKeyState('C') & 0x8000) rollAngle -= rollSpeed;

            DirectX::XMVECTOR qPitch = DirectX::XMQuaternionRotationAxis(right, pitchAngle);
            DirectX::XMVECTOR qYaw = DirectX::XMQuaternionRotationAxis(up, yawAngle);
            DirectX::XMVECTOR qRoll = DirectX::XMQuaternionRotationAxis(forward, rollAngle);

            quat = DirectX::XMQuaternionMultiply(quat, qPitch);
            quat = DirectX::XMQuaternionMultiply(quat, qYaw);
            quat = DirectX::XMQuaternionMultiply(quat, qRoll);

            quat = DirectX::XMQuaternionNormalize(quat);

            DirectX::XMStoreFloat4(&m_CameraQuat, quat);

            float moveSpeed = 5.0f * deltaTime;
            DirectX::XMVECTOR pos = DirectX::XMLoadFloat3(&m_CameraPosition);

            if (GetAsyncKeyState('W') & 0x8000) pos = DirectX::XMVectorAdd(pos, DirectX::XMVectorScale(forward, moveSpeed));
            if (GetAsyncKeyState('S') & 0x8000) pos = DirectX::XMVectorSubtract(pos, DirectX::XMVectorScale(forward, moveSpeed));
            if (GetAsyncKeyState('D') & 0x8000) pos = DirectX::XMVectorAdd(pos, DirectX::XMVectorScale(right, moveSpeed));
            if (GetAsyncKeyState('A') & 0x8000) pos = DirectX::XMVectorSubtract(pos, DirectX::XMVectorScale(right, moveSpeed));
            if (GetAsyncKeyState('E') & 0x8000) pos = DirectX::XMVectorAdd(pos, DirectX::XMVectorScale(up, moveSpeed));
            if (GetAsyncKeyState('Q') & 0x8000) pos = DirectX::XMVectorSubtract(pos, DirectX::XMVectorScale(up, moveSpeed));

            DirectX::XMStoreFloat3(&m_CameraPosition, pos);
        }

        m_Camera.SetPosition(m_CameraPosition);
        m_Camera.SetRotation(m_CameraQuat);

        bool isCtrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool isShiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        bool isSPressedNow = (GetAsyncKeyState('S') & 0x8000) != 0;

        static bool s_IsOPressedLastFrame = false;
        bool isOPressedNow = (GetAsyncKeyState('O') & 0x8000) != 0;

        if (isSPressedNow && !m_IsSPressedLastFrame)
        {
            if (isCtrlPressed && isShiftPressed)
            {
                SaveSceneAs();
            }
            else if (isCtrlPressed && !isShiftPressed)
            {
                SaveScene();
            }
        }

        if (isCtrlPressed && isOPressedNow && !s_IsOPressedLastFrame)
        {
            OpenScene();
        }
        s_IsOPressedLastFrame = isOPressedNow;

        m_IsSPressedLastFrame = isSPressedNow;

        // 렌더링 시작 전 준비
        CCEngine::Renderer::SetClearColor(m_ClearColor[0], m_ClearColor[1], m_ClearColor[2], m_ClearColor[3]);
        CCEngine::Renderer::Clear();

        CCEngine::Renderer2D::BeginScene(m_Camera);

        // 씬 로직 & 렌더링 업데이트
        m_ActiveScene->OnUpdate(deltaTime);

        CCEngine::Renderer2D::EndScene();

        // ImGui Toolbar 패널 그리기
        UI_Toolbar();

        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) OpenScene();
                ImGui::Separator();
                if (ImGui::MenuItem("Save Scene", "Ctrl+S")) SaveScene();
                if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) SaveSceneAs();
                ImGui::Separator();
                if (ImGui::MenuItem("Exit", "Alt+F4")) PostQuitMessage(0);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Window"))
            {
                if (ImGui::MenuItem("Reset Layout")) m_HierarchyPanel.ResetLayout();
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

        m_HierarchyPanel.OnImGuiRender();

        ImGui::Begin("CCEngine Editor");
        ImGui::Text("--- Camera ---");
        ImGui::Text("Position: %.2f, %.2f, %.2f", m_CameraPosition.x, m_CameraPosition.y, m_CameraPosition.z);
        if (ImGui::Button("Reset Camera"))
        {
            m_CameraPosition = { 0.0f, 0.0f, -5.0f };
            m_CameraQuat = { 0.0f, 0.0f, 0.0f, 1.0f };
        }
        ImGui::Separator();
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        ImGui::End();
    }

};

CCEngine::Application* CCEngine::CreateApplication()
{
    return new Sandbox();
}