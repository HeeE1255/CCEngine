#include "EditorLayer.h"
#include "Renderer/Renderer.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/Renderer3D.h"
#include "Renderer/UIRenderer.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/MeshFactory.h"
#include "Renderer/Texture.h"
#include "Renderer/Font.h"
#include "Scene/Components.h"
#include "Scene/SceneSerializer.h"
#include "Utils/PlatformUtils.h"
#include "Utils/MathUtils.h"
#include "Renderer/ModelImporter.h"
#include "Application.h"
#include "UI/HierarchyItem.h"
#include "UI/InspectorPanel.h"
#include <windows.h>
#include <filesystem>
#include <iostream>

// [ImGui 완전 제거됨]
// #include "imgui.h" 
// #include "ImGuizmo.h" // [TODO] 자체 기즈모(Gizmo) 렌더링 시스템 구현 필요

// [추가] 이벤트 시스템 헤더 (경로는 프로젝트에 맞게 수정하세요)
#include "Events/Event.h"
#include "Events/MouseEvent.h"

namespace CCEngine {

    EditorLayer::EditorLayer()
        : Layer("EditorLayer"), m_Camera(45.0f, 1280.0f / 720.0f, 0.1f, 100.0f)
    {
    }

    void EditorLayer::OnAttach()
    {
        FramebufferSpecification fbSpec;
        fbSpec.Width = 1280;
        fbSpec.Height = 720;
        m_Framebuffer = Framebuffer::Create(fbSpec);

        FramebufferSpecification gameFbSpec;
        gameFbSpec.Width = 1280;
        gameFbSpec.Height = 720;
        m_GameFramebuffer = Framebuffer::Create(gameFbSpec);

        m_ActiveScene = new Scene();

        // ==========================================
        // 씬 기본 오브젝트 세팅 (기존과 동일)
        // ==========================================
        auto cameraEntity = m_ActiveScene->CreateEntity("Main Camera");
        auto& cameraComp = cameraEntity.AddComponent<CameraComponent>();
        cameraComp.Primary = true;
        auto& camTransform = cameraEntity.GetComponent<TransformComponent>();
        camTransform.Translation = { 0.0f, 3.0f, -6.0f };
        camTransform.Rotation = { DirectX::XMConvertToRadians(20.0f), 0.0f, 0.0f };
        DirectX::XMVECTOR quat = DirectX::XMQuaternionRotationRollPitchYaw(camTransform.Rotation.x, camTransform.Rotation.y, camTransform.Rotation.z);
        DirectX::XMStoreFloat4(&camTransform.QuaternionRotation, quat);

        auto mainLight = m_ActiveScene->CreateEntity("Main Light (Warm)");
        auto& tcMain = mainLight.GetComponent<TransformComponent>();
        tcMain.Rotation = { DirectX::XMConvertToRadians(45.0f), DirectX::XMConvertToRadians(-45.0f), 0.0f };
        DirectX::XMVECTOR qMain = DirectX::XMQuaternionRotationRollPitchYaw(tcMain.Rotation.x, tcMain.Rotation.y, tcMain.Rotation.z);
        DirectX::XMStoreFloat4(&tcMain.QuaternionRotation, qMain);
        auto& lcMain = mainLight.AddComponent<LightComponent>();
        lcMain.LightColor = { 1.0f, 0.9f, 0.8f };
        lcMain.Intensity = 1.0f;

        auto fillLight = m_ActiveScene->CreateEntity("Fill Light (Cool)");
        auto& tcFill = fillLight.GetComponent<TransformComponent>();
        tcFill.Rotation = { DirectX::XMConvertToRadians(15.0f), DirectX::XMConvertToRadians(135.0f), 0.0f };
        DirectX::XMVECTOR qFill = DirectX::XMQuaternionRotationRollPitchYaw(tcFill.Rotation.x, tcFill.Rotation.y, tcFill.Rotation.z);
        DirectX::XMStoreFloat4(&tcFill.QuaternionRotation, qFill);
        auto& lcFill = fillLight.AddComponent<LightComponent>();
        lcFill.LightColor = { 0.4f, 0.5f, 1.0f };
        lcFill.Intensity = 0.5f;

        auto cube1 = m_ActiveScene->CreateEntity("Cube 1 (No Tex)");
        auto& tc1 = cube1.GetComponent<TransformComponent>();
        tc1.Translation = { -2.5f, 0.0f, 2.0f };
        auto& mesh1 = cube1.AddComponent<MeshComponent>(MeshComponent::MeshType::Cube);
        mesh1.MeshData = MeshFactory::CreateCube();
        mesh1.BaseColor = { 0.2f, 0.3f, 0.8f, 1.0f };

        Entity mayoModel = ModelImporter::ImportModel(m_ActiveScene, "assets/Chocolate rice/0.MAYO/FBX/FBX_MAYO.fbx");

        // =========================================================
        // [완전 자체 UI 구조 조립] (기존과 동일)
        // =========================================================
       // =========================================================
        // [완전 자체 UI 구조 조립]
        // =========================================================
        m_RootUI = new UI::Panel("Root", { 0.05f, 0.05f, 0.05f, 1.0f });
        m_RootUI->SetAnchorMin(0.0f, 0.0f);
        m_RootUI->SetAnchorMax(1.0f, 1.0f);

        // =========================================================
        // 1단: 메인 창 전용 타이틀 바 (Y: 0 ~ 24px)
        // =========================================================
        m_TitleBarPanel = new UI::Panel("TitleBarUI", { 0.15f, 0.15f, 0.17f, 1.0f });
        m_TitleBarPanel->SetAnchorMin(0.0f, 0.0f); m_TitleBarPanel->SetAnchorMax(1.0f, 0.0f);
        m_TitleBarPanel->SetOffsetMin(0.0f, 0.0f); m_TitleBarPanel->SetOffsetMax(0.0f, 24.0f);
        m_RootUI->AddChild(m_TitleBarPanel);

        m_BtnCloseMain = new UI::Button("BtnCloseMain", "X");
        m_BtnCloseMain->SetAnchorMin(1.0f, 0.0f); m_BtnCloseMain->SetAnchorMax(1.0f, 0.0f);
        m_BtnCloseMain->SetOffsetMin(-30.0f, 0.0f); m_BtnCloseMain->SetOffsetMax(0.0f, 24.0f);
        m_BtnCloseMain->SetOnClick([]() { CCEngine::Application::Get()->GetWindow().SetShouldClose(true); });
        m_TitleBarPanel->AddChild(m_BtnCloseMain);

        // =========================================================
        // 2단: 메뉴 바 (Y: 24 ~ 48px) - 24px 밀림
        // =========================================================
        m_MenuBarPanel = new UI::Panel("MenuBarUI", { 0.12f, 0.12f, 0.12f, 1.0f });
        m_MenuBarPanel->SetAnchorMin(0.0f, 0.0f); m_MenuBarPanel->SetAnchorMax(1.0f, 0.0f);
        m_MenuBarPanel->SetOffsetMin(0.0f, 24.0f); m_MenuBarPanel->SetOffsetMax(0.0f, 48.0f);
        m_RootUI->AddChild(m_MenuBarPanel);

        m_BtnFileMenu = new UI::Button("BtnFileMenu", "File");
        m_BtnFileMenu->SetAnchorMin(0.0f, 0.0f); m_BtnFileMenu->SetAnchorMax(0.0f, 1.0f);
        m_BtnFileMenu->SetOffsetMin(0.0f, 0.0f); m_BtnFileMenu->SetOffsetMax(60.0f, 0.0f);
        m_MenuBarPanel->AddChild(m_BtnFileMenu);

        // =========================================================
        // 3단: 메인 작업 영역들 (Y 오프셋 +24px씩 적용)
        // =========================================================
        m_HierarchyPanel = new UI::HierarchyPanel("Hierarchy");
        m_HierarchyPanel->SetAnchorMin(0.0f, 0.0f); m_HierarchyPanel->SetAnchorMax(0.2f, 1.0f);
        m_HierarchyPanel->SetOffsetMin(0.0f, 48.0f); m_HierarchyPanel->SetOffsetMax(0.0f, 0.0f); // 24 -> 48
        m_RootUI->AddChild(m_HierarchyPanel);

        m_HierarchyPanel->SetContext(m_ActiveScene);
        m_HierarchyPanel->Refresh();

        m_InspectorPanel = new UI::InspectorPanel("InspectorUI", "Inspector");
        m_InspectorPanel->SetAnchorMin(0.8f, 0.0f); m_InspectorPanel->SetAnchorMax(1.0f, 1.0f);
        m_InspectorPanel->SetOffsetMin(0.0f, 48.0f); m_InspectorPanel->SetOffsetMax(0.0f, 0.0f); // 24 -> 48
        m_RootUI->AddChild(m_InspectorPanel);

        m_ToolbarPanel = new UI::Panel("ToolbarUI", { 0.15f, 0.15f, 0.15f, 1.0f });
        m_ToolbarPanel->SetAnchorMin(0.0f, 0.0f); m_ToolbarPanel->SetAnchorMax(1.0f, 0.0f);
        m_ToolbarPanel->SetOffsetMin(250.0f, 48.0f); m_ToolbarPanel->SetOffsetMax(-300.0f, 88.0f); // 24,64 -> 48,88
        m_RootUI->AddChild(m_ToolbarPanel);

        m_BtnPlay = new UI::Button("BtnPlay", "Play");
        m_BtnPlay->SetAnchorMin(0.5f, 0.5f); m_BtnPlay->SetAnchorMax(0.5f, 0.5f);
        m_BtnPlay->SetOffsetMin(-100.0f, -12.0f); m_BtnPlay->SetOffsetMax(-40.0f, 12.0f);
        m_ToolbarPanel->AddChild(m_BtnPlay);

        m_BtnPause = new UI::Button("BtnPause", "Pause");
        m_BtnPause->SetAnchorMin(0.5f, 0.5f); m_BtnPause->SetAnchorMax(0.5f, 0.5f);
        m_BtnPause->SetOffsetMin(-30.0f, -12.0f); m_BtnPause->SetOffsetMax(30.0f, 12.0f);
        m_ToolbarPanel->AddChild(m_BtnPause);

        m_BtnStop = new UI::Button("BtnStop", "Stop");
        m_BtnStop->SetAnchorMin(0.5f, 0.5f); m_BtnStop->SetAnchorMax(0.5f, 0.5f);
        m_BtnStop->SetOffsetMin(40.0f, -12.0f); m_BtnStop->SetOffsetMax(100.0f, 12.0f);
        m_ToolbarPanel->AddChild(m_BtnStop);

        m_ViewportWindow = new UI::WindowPanel("ViewportWindowUI", "Scene View");
        m_ViewportWindow->SetAnchorMin(0.2f, 0.0f); m_ViewportWindow->SetAnchorMax(0.8f, 0.6f);
        m_ViewportWindow->SetOffsetMin(0.0f, 88.0f); m_ViewportWindow->SetOffsetMax(0.0f, 0.0f); // 64 -> 88
        m_RootUI->AddChild(m_ViewportWindow);

        void* editorTex = m_Framebuffer->GetColorAttachmentRendererID(0);
        m_ViewportWidget = new UI::ImageWidget("ViewportWidget", editorTex);
        m_ViewportWidget->SetAnchorMin(0.0f, 0.0f); m_ViewportWidget->SetAnchorMax(1.0f, 1.0f);
        m_ViewportWidget->SetOffsetMin(0.0f, 24.0f); m_ViewportWidget->SetOffsetMax(0.0f, 0.0f);
        m_ViewportWindow->AddChild(m_ViewportWidget);

        m_GameWindow = new UI::WindowPanel("GameWindowUI", "Game View");
        m_GameWindow->SetAnchorMin(0.2f, 0.6f); m_GameWindow->SetAnchorMax(0.8f, 1.0f);
        m_GameWindow->SetOffsetMin(0.0f, 0.0f); m_GameWindow->SetOffsetMax(0.0f, 0.0f);
        m_RootUI->AddChild(m_GameWindow);

        void* gameTex = m_GameFramebuffer->GetColorAttachmentRendererID(0);
        m_GameViewWidget = new UI::ImageWidget("GameViewWidget", gameTex);
        m_GameViewWidget->SetAnchorMin(0.0f, 0.0f); m_GameViewWidget->SetAnchorMax(1.0f, 1.0f);
        m_GameViewWidget->SetOffsetMin(0.0f, 24.0f); m_GameViewWidget->SetOffsetMax(0.0f, 0.0f);
        m_GameWindow->AddChild(m_GameViewWidget);

        m_FileDropdownPanel = new UI::Panel("FileDropdownUI", { 0.18f, 0.18f, 0.18f, 1.0f });
        m_FileDropdownPanel->SetVisible(false);
        m_FileDropdownPanel->SetAnchorMin(0.0f, 0.0f); m_FileDropdownPanel->SetAnchorMax(0.0f, 0.0f);
        m_FileDropdownPanel->SetOffsetMin(0.0f, 48.0f); m_FileDropdownPanel->SetOffsetMax(120.0f, 48.0f + 100.0f); // 24 -> 48
        m_RootUI->AddChild(m_FileDropdownPanel);

        m_BtnOpen = new UI::Button("BtnOpen", "Open Scene");
        m_BtnOpen->SetAnchorMin(0.0f, 0.0f); m_BtnOpen->SetAnchorMax(1.0f, 0.0f);
        m_BtnOpen->SetOffsetMin(0.0f, 0.0f); m_BtnOpen->SetOffsetMax(0.0f, 25.0f);
        m_FileDropdownPanel->AddChild(m_BtnOpen);

        m_BtnSave = new UI::Button("BtnSave", "Save");
        m_BtnSave->SetAnchorMin(0.0f, 0.0f); m_BtnSave->SetAnchorMax(1.0f, 0.0f);
        m_BtnSave->SetOffsetMin(0.0f, 25.0f); m_BtnSave->SetOffsetMax(0.0f, 50.0f);
        m_FileDropdownPanel->AddChild(m_BtnSave);

        m_BtnSaveAs = new UI::Button("BtnSaveAs", "Save As...");
        m_BtnSaveAs->SetAnchorMin(0.0f, 0.0f); m_BtnSaveAs->SetAnchorMax(1.0f, 0.0f);
        m_BtnSaveAs->SetOffsetMin(0.0f, 50.0f); m_BtnSaveAs->SetOffsetMax(0.0f, 75.0f);
        m_FileDropdownPanel->AddChild(m_BtnSaveAs);

        m_BtnExit = new UI::Button("BtnExit", "Exit");
        m_BtnExit->SetAnchorMin(0.0f, 0.0f); m_BtnExit->SetAnchorMax(1.0f, 0.0f);
        m_BtnExit->SetOffsetMin(0.0f, 75.0f); m_BtnExit->SetOffsetMax(0.0f, 100.0f);
        m_FileDropdownPanel->AddChild(m_BtnExit);

        // 버튼 콜백
        m_BtnFileMenu->SetOnClick([this]() {
            m_FileDropdownPanel->SetVisible(!m_FileDropdownPanel->IsVisible());
            });
        m_BtnOpen->SetOnClick([this]() { m_FileDropdownPanel->SetVisible(false); OpenScene(); });
        m_BtnSave->SetOnClick([this]() { m_FileDropdownPanel->SetVisible(false); SaveScene(); });
        m_BtnSaveAs->SetOnClick([this]() { m_FileDropdownPanel->SetVisible(false); SaveSceneAs(); });
        //m_BtnExit->SetOnClick([this]() { PostQuitMessage(0); });
        m_BtnExit->SetOnClick([this]() { CCEngine::Application::Get()->GetWindow().SetShouldClose(true); });

        // 툴바 콜백
        m_BtnPlay->SetOnClick([this]() {
            CCEngine::SceneState state = m_ActiveScene->GetState();

            if (state == CCEngine::SceneState::Edit) {
                // 1. [에디터 -> 시작] 씬을 복사하고 런타임 시작
                m_EditorScene = m_ActiveScene;
                m_ActiveScene = CCEngine::Scene::Copy(m_EditorScene);
                m_ActiveScene->OnRuntimeStart();
                m_ActiveScene->SetSceneState(CCEngine::SceneState::Play); // 상태 명확히 세팅
                m_HierarchyPanel->SetContext(m_ActiveScene);

                // UI 갱신
                m_BtnPlay->SetActive(true);
                m_BtnPause->SetActive(false);
            }
            else if (state == CCEngine::SceneState::Play) {
                // 2. [플레이 중 -> 한 번 더 클릭] 정지(Stop)와 동일하게 동작하여 에디터로 복귀
                m_ActiveScene->OnRuntimeStop();
                delete m_ActiveScene;
                m_ActiveScene = m_EditorScene;
                m_EditorScene = nullptr;
                m_ActiveScene->SetSceneState(CCEngine::SceneState::Edit);
                m_HierarchyPanel->SetContext(m_ActiveScene);

                // UI 갱신 (모두 끔)
                m_BtnPlay->SetActive(false);
                m_BtnPause->SetActive(false);
            }
            else if (state == CCEngine::SceneState::Pause) {
                // 3. [일시정지 중 -> 플레이 클릭] 퍼즈를 풀고 다시 런타임 재개
                m_ActiveScene->SetSceneState(CCEngine::SceneState::Play);

                // UI 갱신
                m_BtnPlay->SetActive(true);
                m_BtnPause->SetActive(false);
            }
            });

        m_BtnPause->SetOnClick([this]() {
            CCEngine::SceneState state = m_ActiveScene->GetState();

            if (state == CCEngine::SceneState::Play) {
                // 1. [플레이 중 -> 일시정지]
                m_ActiveScene->SetSceneState(CCEngine::SceneState::Pause);
                m_BtnPause->SetActive(true);
            }
            else if (state == CCEngine::SceneState::Pause) {
                // 2. [일시정지 중 -> 한 번 더 클릭] 퍼즈 해제 후 런타임 재개
                m_ActiveScene->SetSceneState(CCEngine::SceneState::Play);
                m_BtnPause->SetActive(false); // 퍼즈 불 끄기 (Play 불은 켜진 상태 유지)
            }
            });

        m_BtnStop->SetOnClick([this]() {
            CCEngine::SceneState state = m_ActiveScene->GetState();

            if (state != CCEngine::SceneState::Edit) {
                // 1. [강제 종료] 현재 상태와 무관하게 런타임을 종료하고 에디터로 복귀
                m_ActiveScene->OnRuntimeStop();
                delete m_ActiveScene;
                m_ActiveScene = m_EditorScene;
                m_EditorScene = nullptr;
                m_ActiveScene->SetSceneState(CCEngine::SceneState::Edit);
                m_HierarchyPanel->SetContext(m_ActiveScene);

                // UI 갱신 (모두 끔)
                m_BtnPlay->SetActive(false);
                m_BtnPause->SetActive(false);
            }
            });

        CCEngine::Application::Get()->GetWindow().SetRootUI(m_RootUI);
    }

    void EditorLayer::OnDetach()
    {
        if (m_EditorScene) { m_ActiveScene->OnRuntimeStop(); delete m_ActiveScene; }
        else { delete m_ActiveScene; }

        delete m_Framebuffer;
        delete m_GameFramebuffer;
        delete m_RootUI;
    }

    void EditorLayer::OnUpdate(float deltaTime)
    {
        auto& mainWindow = CCEngine::Application::Get()->GetWindow();

        // 1. RHI나 Win32를 몰라도 창 기준 마우스 좌표를 완벽히 가져옴
        auto [mouseX, mouseY] = mainWindow.GetMousePosition();

        // 2. 마우스 이동(Hover) 라우팅
        static float s_LastMouseX = 0.0f;
        static float s_LastMouseY = 0.0f;
        if (mouseX != s_LastMouseX || mouseY != s_LastMouseY)
        {
            MouseMovedEvent moveEvent(mouseX, mouseY);
            OnEvent(moveEvent);

            s_LastMouseX = mouseX;
            s_LastMouseY = mouseY;
        }

        // 3. 마우스 클릭(Press) 라우팅 (0번: 좌클릭)
        static bool s_WasLeftMouseDown = false;
        bool isLeftMouseDown = mainWindow.IsMouseButtonPressed(0);

        //if (isLeftMouseDown && !s_WasLeftMouseDown)
        //{
        //    // 눌렀을 때 (Tear-off 시작점 잡기)
        //    MouseButtonPressedEvent pressEvent(0, mouseX, mouseY);
        //    OnEvent(pressEvent);
        //}
        //else if (!isLeftMouseDown && s_WasLeftMouseDown)
        //{
        //    // 놓았을 때 (Redock! 뜯어낸 창을 다시 꽂아넣기)
        //    MouseButtonReleasedEvent releaseEvent(0, mouseX, mouseY);
        //    OnEvent(releaseEvent);
        //}

        s_WasLeftMouseDown = isLeftMouseDown;

        //// 1. 하이어라키 갱신 요청 처리
        //if (m_NeedsHierarchyRefresh)
        //{
        //    RefreshHierarchy();
        //    m_NeedsHierarchyRefresh = false;
        //}

        // 2. 뷰포트 크기 계산 및 프레임버퍼 리사이즈
        if (m_ViewportWidget)
        {
            auto vpSize = m_ViewportWidget->GetCalculatedSize();
            if (vpSize.x > 0 && vpSize.y > 0) m_ViewportSize = { vpSize.x, vpSize.y };
        }
        if (m_GameViewWidget)
        {
            auto gvSize = m_GameViewWidget->GetCalculatedSize();
            if (gvSize.x > 0 && gvSize.y > 0) m_GameViewportSize = { gvSize.x, gvSize.y };
        }

        if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f &&
            (m_Framebuffer->GetSpecification().Width != (uint32_t)m_ViewportSize.x ||
                m_Framebuffer->GetSpecification().Height != (uint32_t)m_ViewportSize.y))
        {
            m_Framebuffer->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            m_Camera.SetProjectionMatrix(m_Camera.GetFOV(), m_ViewportSize.x / m_ViewportSize.y, 0.1f, 100.0f);
        }

        if (m_GameViewportSize.x > 0.0f && m_GameViewportSize.y > 0.0f &&
            (m_GameFramebuffer->GetSpecification().Width != (uint32_t)m_GameViewportSize.x ||
                m_GameFramebuffer->GetSpecification().Height != (uint32_t)m_GameViewportSize.y))
        {
            m_GameFramebuffer->Resize((uint32_t)m_GameViewportSize.x, (uint32_t)m_GameViewportSize.y);
        }

        // 3. 카메라 및 로직 업데이트
        m_Camera.OnUpdate(deltaTime);
        HandleShortcuts();

		// 선택된 엔티티가 있다면 인스펙터 패널에 전달하여 UI 갱신
        if (m_HierarchyPanel && m_InspectorPanel)
        {
            m_InspectorPanel->SetSelectedEntity(m_HierarchyPanel->GetSelectedEntity());
        }

        // 렌더링 직전에 현재 창 크기를 기반으로 UI 크기를 재계산합니다.
        if (m_RootUI)
        {
            auto& mainWindow = CCEngine::Application::Get()->GetWindow();
            float winWidth = (float)mainWindow.GetWidth();
            float winHeight = (float)mainWindow.GetHeight();
            m_RootUI->UpdateLayout({ 0.0f, 0.0f }, { winWidth, winHeight });
        }

        // 최신 프레임버퍼 텍스처를 뷰포트 위젯에 연결
        if (m_ViewportWidget) m_ViewportWidget->SetTexture(m_Framebuffer->GetColorAttachmentRendererID(0));
        if (m_GameViewWidget) m_GameViewWidget->SetTexture(m_GameFramebuffer->GetColorAttachmentRendererID(0));

        // 4. 에디터 프레임버퍼 렌더링
        Renderer::SetClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        Renderer::Clear();

        m_Framebuffer->Bind();
        Renderer::SetClearColor(m_ClearColor[0], m_ClearColor[1], m_ClearColor[2], m_ClearColor[3]);
        Renderer::Clear();
        m_Framebuffer->ClearAttachment(1, -1);

        m_ActiveScene->OnUpdate(deltaTime);

        Renderer2D::BeginScene(m_Camera);
        Renderer2D::EndScene();
        m_ActiveScene->OnRender3D(m_Camera);
        m_Framebuffer->Unbind();

        // 5. 게임 프레임버퍼 렌더링
        m_GameFramebuffer->Bind();
        Renderer::SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        Renderer::Clear();

        auto view = m_ActiveScene->GetRegistry().view<CameraComponent>();
        for (auto entity : view)
        {
            auto& cameraComp = view.get<CameraComponent>(entity);
            if (cameraComp.Primary)
            {
                CCEngine::Entity cameraEntity(entity, m_ActiveScene);
                auto& transformComp = cameraEntity.GetComponent<TransformComponent>();

                float aspect = 16.0f / 9.0f;
                if (m_GameViewportSize.y > 0.001f) {
                    aspect = m_GameViewportSize.x / m_GameViewportSize.y;
                }

                PerspectiveCamera gameCamera(cameraComp.FOV, aspect, cameraComp.NearClip, cameraComp.FarClip);
                gameCamera.SetPosition(transformComp.Translation);
                gameCamera.SetRotation(transformComp.QuaternionRotation);

                Renderer2D::BeginScene(gameCamera);
                Renderer2D::EndScene();
                m_ActiveScene->OnRender3D(gameCamera);
                break;
            }
        }
        m_GameFramebuffer->Unbind();

  //      auto& mainWindow = CCEngine::Application::Get()->GetWindow();
  //      auto [mouseX, mouseY] = mainWindow.GetMousePosition();

  //      // ==========================================================
  //      // 1. 뷰포트(Scene View) 호버/포커스 상태 판별
  //      // ==========================================================
  //      bool isViewportHovered = false;
  //      if (m_ViewportWidget && m_ViewportWindow && m_ViewportWindow->IsVisible())
  //      {
  //          // 뷰포트 '내부 이미지 영역(ImageWidget)'의 좌표와 크기를 기준으로 검사
  //          auto pos = m_ViewportWidget->GetCalculatedPosition();
  //          auto size = m_ViewportWidget->GetCalculatedSize();

  //          if (mouseX >= pos.x && mouseX <= pos.x + size.x &&
  //              mouseY >= pos.y && mouseY <= pos.y + size.y)
  //          {
  //              isViewportHovered = true;
  //          }
  //      }

  //      // ==========================================================
  //      // 2. 마우스 입력 및 카메라 조작 처리 (포커스가 있을 때만!)
  //      // ==========================================================
  //      // 마우스 이동 이벤트 라우팅
  //      static float s_LastMouseX = 0.0f;
  //      static float s_LastMouseY = 0.0f;
  //      if (mouseX != s_LastMouseX || mouseY != s_LastMouseY)
  //      {
  //          MouseMovedEvent moveEvent(mouseX, mouseY);
  //          OnEvent(moveEvent);
  //          s_LastMouseX = mouseX;
  //          s_LastMouseY = mouseY;
  //      }

  //      // 마우스 클릭 이벤트 라우팅
  //      static bool s_WasLeftMouseDown = false;
  //      bool isLeftMouseDown = mainWindow.IsMouseButtonPressed(0);
  //      if (isLeftMouseDown && !s_WasLeftMouseDown)
  //      {
  //          MouseButtonPressedEvent pressEvent(0, mouseX, mouseY);
  //          OnEvent(pressEvent);
  //      }
  //      s_WasLeftMouseDown = isLeftMouseDown;

  //      // ★ 뷰포트 위에 마우스가 있을 때만 에디터 카메라 업데이트!
  //      // (이렇게 해야 UI 패널을 클릭할 때 씬 카메라가 휙휙 돌아가지 않습니다)
  //      if (isViewportHovered)
  //      {
  //          m_Camera.OnUpdate(deltaTime);
  //      }

  //      HandleShortcuts();

  //      // ==========================================================
  //      // 3. 뷰포트 리사이즈 처리 (화면 찌그러짐 방지)
  //      // ==========================================================
  //      if (m_ViewportWidget)
  //      {
  //          auto vpSize = m_ViewportWidget->GetCalculatedSize();
  //          // 크기가 0보다 크고, 프레임버퍼 크기와 다를 때만 리사이즈 수행
  //          if (vpSize.x > 0.0f && vpSize.y > 0.0f &&
  //              (m_Framebuffer->GetSpecification().Width != (uint32_t)vpSize.x ||
  //                  m_Framebuffer->GetSpecification().Height != (uint32_t)vpSize.y))
  //          {
  //              m_ViewportSize = { vpSize.x, vpSize.y };
  //              m_Framebuffer->Resize((uint32_t)vpSize.x, (uint32_t)vpSize.y);

  //              // ★ 카메라 투영 행렬 비율 업데이트 (가장 중요!)
  //              m_Camera.SetProjectionMatrix(m_Camera.GetFOV(), vpSize.x / vpSize.y, 0.1f, 100.0f);
  //          }
  //      }

  //      // Game View 리사이즈 처리도 동일하게 적용
  //      if (m_GameViewWidget && m_GameWindow && m_GameWindow->IsVisible())
  //      {
  //          auto gvSize = m_GameViewWidget->GetCalculatedSize();
  //          if (gvSize.x > 0.0f && gvSize.y > 0.0f &&
  //              (m_GameFramebuffer->GetSpecification().Width != (uint32_t)gvSize.x ||
  //                  m_GameFramebuffer->GetSpecification().Height != (uint32_t)gvSize.y))
  //          {
  //              m_GameViewportSize = { gvSize.x, gvSize.y };
  //              m_GameFramebuffer->Resize((uint32_t)gvSize.x, (uint32_t)gvSize.y);
  //              // 게임 카메라는 아래 렌더링 루프에서 aspect ratio를 갱신하므로 프레임버퍼만 조절해도 무방합니다.
  //          }
  //      }

  //      // ==========================================================
  //      // 4. UI 레이아웃 갱신 및 렌더링 준비
  //      // ==========================================================
  //      if (m_RootUI)
  //      {
  //          float winWidth = (float)mainWindow.GetWidth();
  //          float winHeight = (float)mainWindow.GetHeight();
  //          m_RootUI->UpdateLayout({ 0.0f, 0.0f }, { winWidth, winHeight });
  //      }

  //      if (m_ViewportWidget) m_ViewportWidget->SetTexture(m_Framebuffer->GetColorAttachmentRendererID(0));
  //      if (m_GameViewWidget) m_GameViewWidget->SetTexture(m_GameFramebuffer->GetColorAttachmentRendererID(0));


		//// ==========================================================
		//// 5. 에디터 프레임버퍼 렌더링
		//// ==========================================================
  //      Renderer::SetClearColor(0.1f, 0.1f, 0.1f, 1.0f);
  //      Renderer::Clear();

  //      m_Framebuffer->Bind();
  //      //Renderer::SetViewport(0, 0, (uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);

  //      Renderer::SetClearColor(m_ClearColor[0], m_ClearColor[1], m_ClearColor[2], m_ClearColor[3]);
  //      Renderer::Clear();
  //      m_Framebuffer->ClearAttachment(1, -1);

  //      m_ActiveScene->OnUpdate(deltaTime);

  //      Renderer2D::BeginScene(m_Camera);
  //      Renderer2D::EndScene();
  //      m_ActiveScene->OnRender3D(m_Camera);
  //      m_Framebuffer->Unbind();

  //      m_GameFramebuffer->Bind();
  //      Renderer::SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  //      Renderer::Clear();

  //      auto view = m_ActiveScene->GetRegistry().view<CameraComponent>();
  //      for (auto entity : view)
  //      {
  //          auto& cameraComp = view.get<CameraComponent>(entity);
  //          if (cameraComp.Primary)
  //          {
  //              CCEngine::Entity cameraEntity(entity, m_ActiveScene);
  //              auto& transformComp = cameraEntity.GetComponent<TransformComponent>();

  //              float aspect = 16.0f / 9.0f;
  //              if (m_GameViewportSize.y > 0.001f) {
  //                  aspect = m_GameViewportSize.x / m_GameViewportSize.y;
  //              }

  //              PerspectiveCamera gameCamera(cameraComp.FOV, aspect, cameraComp.NearClip, cameraComp.FarClip);
  //              gameCamera.SetPosition(transformComp.Translation);
  //              gameCamera.SetRotation(transformComp.QuaternionRotation);

  //              Renderer2D::BeginScene(gameCamera);
  //              Renderer2D::EndScene();
  //              m_ActiveScene->OnRender3D(gameCamera);
  //              break;
  //          }
  //      }
  //      m_GameFramebuffer->Unbind();
        
    }

    // =========================================================================
    // [새로운 이벤트 처리 라우팅] (EditorLayer.h에 선언 필요)
    // =========================================================================
    void EditorLayer::OnEvent(Event& e)
    {
        // 1. UI에게 이벤트를 최우선으로 던집니다 (역순회로 인해 맨 위 클릭 가로채기 가능)
        if (m_RootUI)
        {
            m_RootUI->OnEvent(e);
        }

        // 2. 드롭다운 바깥 클릭 시 닫히는 Focus Out 로직 
        if (e.GetEventType() == EventType::MouseButtonPressed)
        {
            MouseButtonPressedEvent& mouseEvent = static_cast<MouseButtonPressedEvent&>(e);

            if (m_FileDropdownPanel && m_FileDropdownPanel->IsVisible())
            {
                // 마우스가 드롭다운 안에도 없고, 메뉴 버튼 위에도 없다면 닫기
                if (!m_FileDropdownPanel->IsPointInside(mouseEvent.GetX(), mouseEvent.GetY()) &&
                    !m_BtnFileMenu->IsPointInside(mouseEvent.GetX(), mouseEvent.GetY()))
                {
                    m_FileDropdownPanel->SetVisible(false);
                }
            }
        }

        // 3. UI가 이벤트를 먹지 않았다면, 3D 카메라나 씬에 넘겨줌
        if (!e.Handled)
        {
            // m_Camera.OnEvent(e); 
            // m_ActiveScene->OnEvent(e);
        }
    }

    void EditorLayer::OnImGuiRender()
    {
        // [완전 제거됨] 
        // 1. Layout Update 로직은 OnUpdate로 이동
        // 2. UI Rendering 로직은 Application::Run() 루프에서 UIRenderer를 통해 직접 수행됨
        // 3. ImGui 이벤트 로직은 OnEvent로 분리
    }

    // =========================================================================
    // 파일 세이브/로드 및 단축키 로직
    // =========================================================================
    void EditorLayer::SaveScene()
    {
        if (m_CurrentScenePath.empty()) { SaveSceneAs(); return; }
        CCEngine::SceneSerializer serializer(m_ActiveScene);
        serializer.Serialize(m_CurrentScenePath);
        printf("Scene Saved to: %s\n", m_CurrentScenePath.c_str());
    }

    void EditorLayer::SaveSceneAs()
    {
        std::filesystem::path initialDirPath = std::filesystem::current_path() / "assets" / "scenes";
        std::string initialDirStr = initialDirPath.string();
        std::string filepath = CCEngine::PlatformUtils::SaveFile("CCEngine Scene (*.ccscene)\0*.ccscene\0", initialDirStr.c_str());
        if (!filepath.empty()) {
            m_CurrentScenePath = filepath;
            CCEngine::SceneSerializer serializer(m_ActiveScene);
            serializer.Serialize(m_CurrentScenePath);
            printf("Scene Saved As: %s\n", m_CurrentScenePath.c_str());
        }
    }

    void EditorLayer::OpenScene()
    {
        std::filesystem::path initialDirPath = std::filesystem::current_path() / "assets" / "scenes";
        std::string initialDirStr = initialDirPath.string();
        std::string filepath = CCEngine::PlatformUtils::OpenFile("CCEngine Scene (*.ccscene)\0*.ccscene\0", initialDirStr.c_str());
        if (!filepath.empty()) {
            CCEngine::SceneSerializer serializer(m_ActiveScene);
            if (serializer.Deserialize(filepath)) {
                m_CurrentScenePath = filepath;
                printf("Scene Loaded from: %s\n", m_CurrentScenePath.c_str());
            }
            else { printf("Failed to load scene!\n"); }
        }
    }

    void EditorLayer::HandleShortcuts()
    {
        // [수정] ImGui::IsMouseDown 대신 Win32 네이티브 입력 감지
        bool isRightMouseDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;

        if (!isRightMouseDown)
        {
            // [임시 주석] ImGuizmo는 ImGui 기반이므로, 자체 기즈모 구현 전까지 비활성화
            // if (GetAsyncKeyState('Q') & 0x8000) m_GizmoType = -1;
            // if (GetAsyncKeyState('W') & 0x8000) m_GizmoType = ImGuizmo::TRANSLATE;
            // if (GetAsyncKeyState('E') & 0x8000) m_GizmoType = ImGuizmo::ROTATE;
            // if (GetAsyncKeyState('R') & 0x8000) m_GizmoType = ImGuizmo::SCALE;
        }

        bool isCtrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool isShiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        bool isSPressedNow = (GetAsyncKeyState('S') & 0x8000) != 0;
        static bool s_IsOPressedLastFrame = false;
        bool isOPressedNow = (GetAsyncKeyState('O') & 0x8000) != 0;

        if (isSPressedNow && !m_IsSPressedLastFrame)
        {
            if (isCtrlPressed && isShiftPressed) SaveSceneAs();
            else if (isCtrlPressed && !isShiftPressed) SaveScene();
        }
        if (isCtrlPressed && isOPressedNow && !s_IsOPressedLastFrame) OpenScene();
        s_IsOPressedLastFrame = isOPressedNow;
        m_IsSPressedLastFrame = isSPressedNow;
    }

    void EditorLayer::RefreshHierarchy() 
    { 
        //if(!m_ActiveScene) return;

        //m_HierarchyContainer->ClearChildren(); 

        //auto view = m_ActiveScene->GetRegistry().view<CCEngine::TagComponent>();
        //for (auto entityID : view)
        //{
        //    CCEngine::Entity entity{ entityID, m_ActiveScene };
        //    bool isRoot = true;

        //    if (entity.HasComponent<CCEngine::RelationshipComponent>())
        //    {
        //        if (entity.GetComponent<CCEngine::RelationshipComponent>().Parent != entt::null)
        //            isRoot = false;
        //    }

        //    if (isRoot)
        //        DrawEntityNode(entity, 0.0f);
        //}
    }

    void EditorLayer::DrawEntityNode(CCEngine::Entity entity, float depth) 
    { 
    //    std::string entityName = entity.GetComponent<CCEngine::TagComponent>().Tag;

    //    // 자식이 있는지 검사
    //    bool hasChildren = false;
    //    auto relView = m_ActiveScene->GetRegistry().view<CCEngine::RelationshipComponent>();
    //    for (auto childID : relView)
    //    {
    //        if (relView.get<CCEngine::RelationshipComponent>(childID).Parent == (entt::entity)entity)
    //        {
    //            hasChildren = true;
    //            break;
    //        }
    //    }

    //    bool isExpanded = m_ExpandedNodes.find((entt::entity)entity) != m_ExpandedNodes.end();

    //    // ★ 현재 선택된 엔티티인지 확인 (인스펙터 패널 등과 연동하기 위함)
    //    bool isSelected = (m_HierarchyPanel->GetSelectedEntity() == entity);

    //    UI::HierarchyItem* itemNode = new UI::HierarchyItem(entityName, entityName);
    //    itemNode->SetIndentLevel(depth);
    //    itemNode->SetHasChildren(hasChildren);
    //    itemNode->SetExpanded(isExpanded);
    //    itemNode->SetSelected(isSelected);

    //    // 콜백 1: 항목 자체를 클릭했을 때 (선택)
    //    itemNode->SetOnSelect([this, entity]() mutable
    //        {
    //            m_HierarchyPanel->SetSelectedEntity(entity);
    //            std::cout << "[Hierarchy] Selected: " << entity.GetComponent<CCEngine::TagComponent>().Tag << std::endl;
    //            m_NeedsHierarchyRefresh = true; // 선택 상태(파란 배경)를 갱신하기 위해 새로고침
    //        });

    //    // 콜백 2: 화살표를 클릭했을 때 (접고 펴기)
    //    itemNode->SetOnToggleExpand([this, entity, isExpanded]() mutable
    //        {
    //            if (isExpanded)
    //                m_ExpandedNodes.erase((entt::entity)entity);
    //            else
    //                m_ExpandedNodes.insert((entt::entity)entity);

    //            m_NeedsHierarchyRefresh = true;
    //        });

    //    m_HierarchyContainer->AddChild(itemNode);

    //    // 자식들 재귀 호출 (열려 있을 때만)
    //    if (isExpanded)
    //    {
    //        for (auto childID : relView)
    //        {
    //            if (relView.get<CCEngine::RelationshipComponent>(childID).Parent == (entt::entity)entity)
    //            {
    //                CCEngine::Entity child{ childID, m_ActiveScene };
    //                DrawEntityNode(child, depth + 1.0f);
    //            }
    //        }
    //    }
    }
}