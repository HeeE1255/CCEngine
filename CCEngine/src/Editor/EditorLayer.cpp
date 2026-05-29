#include "EditorLayer.h"
#include "Renderer/Renderer.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/Renderer3D.h"
#include "Renderer/UIRenderer.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/MeshFactory.h"
#include "Renderer/Texture.h"
#include "Renderer/Font.h"
#include "Renderer/RendererHandle.h"
#include "Scene/Components.h"
#include "Scene/PrefabSerializer.h"
#include "Scene/SceneSerializer.h"
#include "Utils/PlatformUtils.h"
#include "Utils/MathUtils.h"
#include "Renderer/ModelImporter.h"
#include "Application.h"
#include "UI/HierarchyItem.h"
#include "UI/InspectorPanel.h"
#include "UI/InspectorRegistry.h"
#include "UI/InspectorItem.h"
#include "UI/InspectorUtils.h"
#include <windows.h>
#include <filesystem>
#include <functional>
#include <iostream>

#include "Events/Event.h"
#include "Events/MouseEvent.h"

namespace CCEngine {

    EditorLayer::EditorLayer()
        : Layer("EditorLayer"), m_Camera(45.0f, 1280.0f / 720.0f, 0.1f, 100.0f)
    {
        m_GizmoSystem.Init(); // 기즈모 시스템 초기화
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
        // 씬 기본 오브젝트 세팅 
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

        // --- 에디터 기본 UI 세팅 ---
        BuildEditorUI();

        // --- 인스펙터 패널에 기본 컴포넌트 등록 ---
        UI::InspectorUtils::InitStandardComponents();
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

        // 1. 뷰포트 크기 계산 및 프레임버퍼 리사이즈
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

        // 2. 카메라 및 로직 업데이트
        m_Camera.OnUpdate(deltaTime);
        HandleShortcuts();

        // 선택된 엔티티가 있다면 인스펙터 패널에 전달하여 UI 갱신
        if (m_HierarchyPanel && m_InspectorPanel)
        {
            m_InspectorPanel->SetSelectedEntity(m_HierarchyPanel->GetSelectedEntity());
        }

        if (m_RootUI)
        {
            float winWidth = (float)mainWindow.GetWidth();
            float winHeight = (float)mainWindow.GetHeight();

            if (winWidth >= 50.0f && winHeight >= 50.0f)
            {
                m_RootUI->UpdateLayout({ 0.0f, 0.0f }, { winWidth, winHeight });
            }
        }
        // =========================================================================

        // 최신 프레임버퍼 텍스처를 뷰포트 위젯에 연결
        if (m_ViewportWidget) m_ViewportWidget->SetTexture(m_Framebuffer->GetColorAttachmentRendererID(0));
        if (m_GameViewWidget) m_GameViewWidget->SetTexture(m_GameFramebuffer->GetColorAttachmentRendererID(0));

        // 3. 에디터 프레임버퍼 렌더링
        Renderer::SetClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        Renderer::Clear();

        m_Framebuffer->Bind();
        Renderer::SetClearColor(m_ClearColor[0], m_ClearColor[1], m_ClearColor[2], m_ClearColor[3]);
        Renderer::Clear();
        m_Framebuffer->ClearAttachment(1, -1);

        m_ActiveScene->OnUpdate(deltaTime);
        m_ActiveScene->OnRender2D(m_Camera);
        m_ActiveScene->OnRender3D(m_Camera);

        // 자체 기즈모 시스템 구현
        auto selectedEntity = m_HierarchyPanel->GetSelectedEntity();
        m_GizmoSystem.OnRender(selectedEntity, m_Camera.GetViewMatrix(), m_Camera.GetProjectionMatrix());

        m_Framebuffer->Unbind();

        // 4. 게임 프레임버퍼 렌더링
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

                m_ActiveScene->OnRender2D(gameCamera);
                m_ActiveScene->OnRender3D(gameCamera);
                break;
            }
        }
        m_GameFramebuffer->Unbind();
    }

    void EditorLayer::OnEvent(Event& e)
    {
        // 1. 드롭다운 바깥 클릭 시 닫히는 Focus Out 로직 
        if (e.GetEventType() == EventType::MouseButtonPressed)
        {
            MouseButtonPressedEvent& mouseEvent = static_cast<MouseButtonPressedEvent&>(e);

            if (m_FileDropdownPanel && m_FileDropdownPanel->IsVisible())
            {
                if (!m_FileDropdownPanel->IsPointInside(mouseEvent.GetX(), mouseEvent.GetY()) &&
                    !m_BtnFileMenu->IsPointInside(mouseEvent.GetX(), mouseEvent.GetY()))
                {
                    m_FileDropdownPanel->SetVisible(false);
                }
            }
        }

        // 마우스 피킹 콜백
        m_ViewportWidget->SetOnMouseDown([this](float mouseX, float mouseY) {
            auto vpPos = m_ViewportWidget->GetCalculatedPosition();
            float localX = mouseX - vpPos.x;
            float localY = mouseY - vpPos.y;
            int pixelData = m_Framebuffer->ReadPixel((uint32_t)localX, (uint32_t)localY);

            if (pixelData >= 0 && m_ActiveScene->GetRegistry().valid((entt::entity)pixelData))
            {
                CCEngine::Entity clickedEntity{ (entt::entity)pixelData, m_ActiveScene };
                m_HierarchyPanel->SetSelectedEntity(clickedEntity);
                std::cout << "[Picking] Picked Entity ID: " << pixelData << std::endl;
            }
            else
            {
                std::cout << "[Picking] Ignored Empty Space!" << std::endl;
            }
            });

        std::function<UI::Widget*(UI::Widget*, float, float)> getTopmostWidgetAt =
            [&](UI::Widget* widget, float mouseX, float mouseY) -> UI::Widget*
            {
                if (!widget || !widget->IsVisible() || !widget->IsPointInside(mouseX, mouseY))
                    return nullptr;

                const auto& children = widget->GetChildren();
                for (auto it = children.rbegin(); it != children.rend(); ++it)
                {
                    if (UI::Widget* hit = getTopmostWidgetAt(*it, mouseX, mouseY))
                        return hit;
                }
                return widget;
            };

        bool shouldRouteUIFirst = true;
        if (m_RootUI && m_ViewportWidget)
        {
            float mouseX = 0.0f;
            float mouseY = 0.0f;
            bool isMouseEvent = true;

            if (e.GetEventType() == EventType::MouseButtonPressed) { auto& me = static_cast<MouseButtonPressedEvent&>(e); mouseX = me.GetX(); mouseY = me.GetY(); }
            else if (e.GetEventType() == EventType::MouseMoved) { auto& me = static_cast<MouseMovedEvent&>(e); mouseX = me.GetX(); mouseY = me.GetY(); }
            else if (e.GetEventType() == EventType::MouseButtonReleased) { auto& me = static_cast<MouseButtonReleasedEvent&>(e); mouseX = me.GetX(); mouseY = me.GetY(); }
            else { isMouseEvent = false; }

            if (isMouseEvent)
            {
                UI::Widget* topmost = getTopmostWidgetAt(m_RootUI, mouseX, mouseY);
                shouldRouteUIFirst = (topmost != m_ViewportWidget);
            }
        }

        auto routeViewportGizmo = [&]()
        {
            if (e.Handled || !m_ViewportWidget)
                return;

            float mouseX = 0.0f; float mouseY = 0.0f;
            if (e.GetEventType() == EventType::MouseButtonPressed) { auto& me = static_cast<MouseButtonPressedEvent&>(e); mouseX = me.GetX(); mouseY = me.GetY(); }
            else if (e.GetEventType() == EventType::MouseMoved) { auto& me = static_cast<MouseMovedEvent&>(e); mouseX = me.GetX(); mouseY = me.GetY(); }
            else if (e.GetEventType() == EventType::MouseButtonReleased) { auto& me = static_cast<MouseButtonReleasedEvent&>(e); mouseX = me.GetX(); mouseY = me.GetY(); }

            auto vpPos = m_ViewportWidget->GetCalculatedPosition();
            auto vpSize = m_ViewportWidget->GetCalculatedSize();

            bool isInsideViewport = (mouseX >= vpPos.x && mouseX <= vpPos.x + vpSize.x && mouseY >= vpPos.y && mouseY <= vpPos.y + vpSize.y);

            if (isInsideViewport || m_GizmoSystem.IsDragging())
            {
                auto selectedEntity = m_HierarchyPanel->GetSelectedEntity();
                m_GizmoSystem.OnEvent(e, selectedEntity, m_Camera.GetViewMatrix(), m_Camera.GetProjectionMatrix(), vpSize.x, vpSize.y, vpPos.x, vpPos.y);
            }
        };

        if (shouldRouteUIFirst)
        {
            if (!e.Handled && m_RootUI) m_RootUI->OnEvent(e);
            routeViewportGizmo();
        }
        else
        {
            routeViewportGizmo();
            if (!e.Handled && m_RootUI) m_RootUI->OnEvent(e);
        }

        // 4. UI가 이벤트를 먹지 않았다면, 3D 카메라나 씬에 넘겨줌
        if (!e.Handled)
        {
            // m_Camera.OnEvent(e); 
            // m_ActiveScene->OnEvent(e);
        }
    }

    void EditorLayer::OnImGuiRender()
    {}

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
        if (!filepath.empty()) OpenScene(filepath);
    }

    void EditorLayer::OpenScene(const std::string& filepath)
    {
        if (filepath.empty())
            return;

        CCEngine::SceneSerializer serializer(m_ActiveScene);
        if (serializer.Deserialize(filepath)) {
            m_CurrentScenePath = filepath;
            if (m_HierarchyPanel)
            {
                m_HierarchyPanel->SetSelectedEntity(CCEngine::Entity{});
                m_HierarchyPanel->Refresh();
            }
            printf("Scene Loaded from: %s\n", m_CurrentScenePath.c_str());
        }
        else { printf("Failed to load scene: %s\n", filepath.c_str()); }
    }

    void EditorLayer::LoadSceneAdditive(const std::string& filepath)
    {
        if (filepath.empty())
            return;

        CCEngine::SceneSerializer serializer(m_ActiveScene);
        Entity sceneRoot = serializer.DeserializeAppend(filepath);
        if (sceneRoot)
        {
            if (m_HierarchyPanel)
            {
                m_HierarchyPanel->SetSelectedEntity(sceneRoot);
                m_HierarchyPanel->Refresh();
            }
            printf("Scene Added from: %s\n", filepath.c_str());
        }
        else
        {
            printf("Failed to add scene: %s\n", filepath.c_str());
        }
    }

    void EditorLayer::SaveSelectedPrefab()
    {
        if (!m_HierarchyPanel)
            return;

        Entity selected = m_HierarchyPanel->GetSelectedEntity();
        if (!selected)
        {
            printf("No entity selected. Select an entity before saving a prefab.\n");
            return;
        }

        std::filesystem::path initialDirPath = std::filesystem::current_path() / "assets" / "prefabs";
        std::filesystem::create_directories(initialDirPath);
        std::string initialDirStr = initialDirPath.string();
        std::string filepath = CCEngine::PlatformUtils::SaveFile("CCEngine Prefab (*.ccprefab)\0*.ccprefab\0", initialDirStr.c_str());

        if (!filepath.empty())
        {
            if (PrefabSerializer::Serialize(m_ActiveScene, selected, filepath))
            {
                if (m_AssetBrowserPanel) m_AssetBrowserPanel->Refresh();
                printf("Prefab Saved to: %s\n", filepath.c_str());
            }
            else
                printf("Failed to save prefab: %s\n", filepath.c_str());
        }
    }

    void EditorLayer::InstantiatePrefab()
    {
        std::filesystem::path initialDirPath = std::filesystem::current_path() / "assets" / "prefabs";
        std::filesystem::create_directories(initialDirPath);
        std::string initialDirStr = initialDirPath.string();
        std::string filepath = CCEngine::PlatformUtils::OpenFile("CCEngine Prefab (*.ccprefab)\0*.ccprefab\0", initialDirStr.c_str());

        if (!filepath.empty()) InstantiatePrefab(filepath);
    }

    void EditorLayer::InstantiatePrefab(const std::string& filepath)
    {
        if (!filepath.empty())
        {
            Entity instance = PrefabSerializer::Deserialize(m_ActiveScene, filepath);
            if (instance)
            {
                if (instance.HasComponent<TransformComponent>())
                    instance.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 0.0f };

                m_HierarchyPanel->SetSelectedEntity(instance);
                m_HierarchyPanel->Refresh();
                printf("Prefab Instantiated from: %s\n", filepath.c_str());
            }
            else
            {
                printf("Failed to instantiate prefab: %s\n", filepath.c_str());
            }
        }
    }

    void EditorLayer::ImportModelAsset(const std::string& filepath)
    {
        if (filepath.empty())
            return;

        Entity modelEntity = ModelImporter::ImportModel(m_ActiveScene, filepath);
        if (modelEntity)
        {
            if (m_HierarchyPanel)
            {
                m_HierarchyPanel->SetSelectedEntity(modelEntity);
                m_HierarchyPanel->Refresh();
            }
            printf("Model Imported from: %s\n", filepath.c_str());
        }
    }

    void EditorLayer::HandleAssetDropped(const std::string& filepath, const std::string& assetType)
    {
        if (!m_HierarchyPanel)
            return;

        auto [mouseX, mouseY] = CCEngine::Application::Get()->GetWindow().GetMousePosition();
        if (!m_HierarchyPanel->IsPointInside(mouseX, mouseY))
            return;

        if (assetType == "scene")
        {
            LoadSceneAdditive(filepath);
        }
        else if (assetType == "prefab")
        {
            InstantiatePrefab(filepath);
        }
        else if (assetType == "model")
        {
            ImportModelAsset(filepath);
        }
    }

    void EditorLayer::HandleShortcuts()
    {
        bool isRightMouseDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;

        if (!isRightMouseDown)
        {
            if (GetAsyncKeyState('Q') & 0x8000) m_GizmoSystem.SetMode(GizmoMode::None);
            if (GetAsyncKeyState('W') & 0x8000) m_GizmoSystem.SetMode(GizmoMode::Translate);
            if (GetAsyncKeyState('E') & 0x8000) m_GizmoSystem.SetMode(GizmoMode::Rotate);
            if (GetAsyncKeyState('R') & 0x8000) m_GizmoSystem.SetMode(GizmoMode::Scale);
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
            {
                m_HierarchyPanel->SetSelectedEntity(CCEngine::Entity{});
            }
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
    {}

    void EditorLayer::BuildEditorUI()
    {
        m_RootUI = new UI::Panel("Root", { 0.05f, 0.05f, 0.05f, 1.0f });
        m_RootUI->SetAnchorMin(0.0f, 0.0f);
        m_RootUI->SetAnchorMax(1.0f, 1.0f);
        m_RootUI->SetOffsetMin(0.0f, 0.0f);
        m_RootUI->SetOffsetMax(0.0f, 0.0f);

        m_TitleBarPanel = new UI::Panel("TitleBarUI", { 0.15f, 0.15f, 0.17f, 1.0f });
        m_TitleBarPanel->SetAnchorMin(0.0f, 0.0f); m_TitleBarPanel->SetAnchorMax(1.0f, 0.0f);
        m_TitleBarPanel->SetOffsetMin(0.0f, 0.0f); m_TitleBarPanel->SetOffsetMax(0.0f, 24.0f);
        m_RootUI->AddChild(m_TitleBarPanel);

        m_BtnCloseMain = new UI::Button("BtnCloseMain", "X");
        m_BtnCloseMain->SetAnchorMin(1.0f, 0.0f); m_BtnCloseMain->SetAnchorMax(1.0f, 0.0f);
        m_BtnCloseMain->SetOffsetMin(-30.0f, 0.0f); m_BtnCloseMain->SetOffsetMax(0.0f, 24.0f);
        m_BtnCloseMain->SetOnClick([]() { CCEngine::Application::Get()->GetWindow().SetShouldClose(true); });
        m_TitleBarPanel->AddChild(m_BtnCloseMain);

        m_MenuBarPanel = new UI::Panel("MenuBarUI", { 0.12f, 0.12f, 0.12f, 1.0f });
        m_MenuBarPanel->SetAnchorMin(0.0f, 0.0f); m_MenuBarPanel->SetAnchorMax(1.0f, 0.0f);
        m_MenuBarPanel->SetOffsetMin(0.0f, 24.0f); m_MenuBarPanel->SetOffsetMax(0.0f, 48.0f);
        m_RootUI->AddChild(m_MenuBarPanel);

        m_BtnFileMenu = new UI::Button("BtnFileMenu", "File");
        m_BtnFileMenu->SetAnchorMin(0.0f, 0.0f); m_BtnFileMenu->SetAnchorMax(0.0f, 1.0f);
        m_BtnFileMenu->SetOffsetMin(0.0f, 0.0f); m_BtnFileMenu->SetOffsetMax(60.0f, 0.0f);
        m_MenuBarPanel->AddChild(m_BtnFileMenu);

        m_HierarchyPanel = new UI::HierarchyPanel("Hierarchy");
        m_HierarchyPanel->SetAnchorMin(0.0f, 0.0f);
        m_HierarchyPanel->SetAnchorMax(0.2f, 1.0f);
        m_HierarchyPanel->SetOffsetMin(0.0f, 48.0f);
        m_HierarchyPanel->SetOffsetMax(0.0f, 0.0f);
        m_RootUI->AddChild(m_HierarchyPanel);
        m_HierarchyPanel->SetContext(m_ActiveScene);
        m_HierarchyPanel->Refresh();

        m_InspectorPanel = new UI::InspectorPanel("InspectorUI", "Inspector");
        m_InspectorPanel->SetAnchorMin(0.8f, 0.0f);
        m_InspectorPanel->SetAnchorMax(1.0f, 1.0f);
        m_InspectorPanel->SetOffsetMin(0.0f, 48.0f);
        m_InspectorPanel->SetOffsetMax(0.0f, 0.0f);
        m_RootUI->AddChild(m_InspectorPanel);

        m_ToolbarPanel = new UI::Panel("ToolbarUI", { 0.15f, 0.15f, 0.15f, 1.0f });
        m_ToolbarPanel->SetAnchorMin(0.0f, 0.0f); m_ToolbarPanel->SetAnchorMax(1.0f, 0.0f);
        m_ToolbarPanel->SetOffsetMin(250.0f, 48.0f); m_ToolbarPanel->SetOffsetMax(-300.0f, 88.0f);
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
        m_ViewportWindow->SetAnchorMin(0.2f, 0.0f);
        m_ViewportWindow->SetAnchorMax(0.8f, 0.55f);
        m_ViewportWindow->SetOffsetMin(0.0f, 48.0f);
        m_ViewportWindow->SetOffsetMax(0.0f, 0.0f);
        m_RootUI->AddChild(m_ViewportWindow);

        RendererHandle editorTex = m_Framebuffer->GetColorAttachmentRendererID(0);
        m_ViewportWidget = new UI::ImageWidget("ViewportWidget", editorTex);
        m_ViewportWidget->SetAnchorMin(0.0f, 0.0f); m_ViewportWidget->SetAnchorMax(1.0f, 1.0f);
        m_ViewportWidget->SetOffsetMin(0.0f, 24.0f); m_ViewportWidget->SetOffsetMax(0.0f, 0.0f);
        m_ViewportWindow->AddChild(m_ViewportWidget);

        m_GameWindow = new UI::WindowPanel("GameWindowUI", "Game View");
        m_GameWindow->SetAnchorMin(0.2f, 0.55f); m_GameWindow->SetAnchorMax(0.8f, 0.75f);
        m_GameWindow->SetOffsetMin(0.0f, 0.0f); m_GameWindow->SetOffsetMax(0.0f, 0.0f);
        m_RootUI->AddChild(m_GameWindow);

        RendererHandle gameTex = m_GameFramebuffer->GetColorAttachmentRendererID(0);
        m_GameViewWidget = new UI::ImageWidget("GameViewWidget", gameTex);
        m_GameViewWidget->SetAnchorMin(0.0f, 0.0f); m_GameViewWidget->SetAnchorMax(1.0f, 1.0f);
        m_GameViewWidget->SetOffsetMin(0.0f, 24.0f); m_GameViewWidget->SetOffsetMax(0.0f, 0.0f);
        m_GameWindow->AddChild(m_GameViewWidget);

        m_AssetBrowserPanel = new UI::AssetBrowserPanel("AssetBrowserUI");
        m_AssetBrowserPanel->SetAnchorMin(0.2f, 0.75f);
        m_AssetBrowserPanel->SetAnchorMax(0.8f, 1.0f);
        m_AssetBrowserPanel->SetOffsetMin(0.0f, 0.0f);
        m_AssetBrowserPanel->SetOffsetMax(0.0f, 0.0f);
        m_AssetBrowserPanel->SetOnPrefabSelected([this](const std::string& path) { InstantiatePrefab(path); });
        m_AssetBrowserPanel->SetOnModelSelected([this](const std::string& path) { ImportModelAsset(path); });
        m_AssetBrowserPanel->SetOnSceneSelected([this](const std::string& path) { OpenScene(path); });
        m_AssetBrowserPanel->SetOnAssetDropped([this](const std::string& path, const std::string& type, float, float) { HandleAssetDropped(path, type); });
        m_RootUI->AddChild(m_AssetBrowserPanel);

        m_FileDropdownPanel = new UI::Panel("FileDropdownUI", { 0.18f, 0.18f, 0.18f, 1.0f });
        m_FileDropdownPanel->SetVisible(false);
        m_FileDropdownPanel->SetAnchorMin(0.0f, 0.0f); m_FileDropdownPanel->SetAnchorMax(0.0f, 0.0f);
        m_FileDropdownPanel->SetOffsetMin(0.0f, 48.0f); m_FileDropdownPanel->SetOffsetMax(150.0f, 48.0f + 150.0f);
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

        m_BtnSavePrefab = new UI::Button("BtnSavePrefab", "Save Prefab");
        m_BtnSavePrefab->SetAnchorMin(0.0f, 0.0f); m_BtnSavePrefab->SetAnchorMax(1.0f, 0.0f);
        m_BtnSavePrefab->SetOffsetMin(0.0f, 75.0f); m_BtnSavePrefab->SetOffsetMax(0.0f, 100.0f);
        m_FileDropdownPanel->AddChild(m_BtnSavePrefab);

        m_BtnInstantiatePrefab = new UI::Button("BtnInstantiatePrefab", "Load Prefab");
        m_BtnInstantiatePrefab->SetAnchorMin(0.0f, 0.0f); m_BtnInstantiatePrefab->SetAnchorMax(1.0f, 0.0f);
        m_BtnInstantiatePrefab->SetOffsetMin(0.0f, 100.0f); m_BtnInstantiatePrefab->SetOffsetMax(0.0f, 125.0f);
        m_FileDropdownPanel->AddChild(m_BtnInstantiatePrefab);

        m_BtnExit = new UI::Button("BtnExit", "Exit");
        m_BtnExit->SetAnchorMin(0.0f, 0.0f); m_BtnExit->SetAnchorMax(1.0f, 0.0f);
        m_BtnExit->SetOffsetMin(0.0f, 125.0f); m_BtnExit->SetOffsetMax(0.0f, 150.0f);
        m_FileDropdownPanel->AddChild(m_BtnExit);

        // --- 버튼 & 툴바 콜백 등록 ---
        m_BtnFileMenu->SetOnClick([this]() { m_FileDropdownPanel->SetVisible(!m_FileDropdownPanel->IsVisible()); });
        m_BtnOpen->SetOnClick([this]() { m_FileDropdownPanel->SetVisible(false); OpenScene(); });
        m_BtnSave->SetOnClick([this]() { m_FileDropdownPanel->SetVisible(false); SaveScene(); });
        m_BtnSaveAs->SetOnClick([this]() { m_FileDropdownPanel->SetVisible(false); SaveSceneAs(); });
        m_BtnSavePrefab->SetOnClick([this]() { m_FileDropdownPanel->SetVisible(false); SaveSelectedPrefab(); });
        m_BtnInstantiatePrefab->SetOnClick([this]() { m_FileDropdownPanel->SetVisible(false); InstantiatePrefab(); });
        m_BtnExit->SetOnClick([this]() { CCEngine::Application::Get()->GetWindow().SetShouldClose(true); });

        m_BtnPlay->SetOnClick([this]() {
            CCEngine::SceneState state = m_ActiveScene->GetState();
            if (state == CCEngine::SceneState::Edit) {
                m_EditorScene = m_ActiveScene;
                m_ActiveScene = CCEngine::Scene::Copy(m_EditorScene);
                m_ActiveScene->OnRuntimeStart();
                m_ActiveScene->SetSceneState(CCEngine::SceneState::Play);
                m_HierarchyPanel->SetContext(m_ActiveScene);
                m_BtnPlay->SetActive(true);
                m_BtnPause->SetActive(false);
            }
            else if (state == CCEngine::SceneState::Play) {
                m_ActiveScene->OnRuntimeStop();
                delete m_ActiveScene;
                m_ActiveScene = m_EditorScene;
                m_EditorScene = nullptr;
                m_ActiveScene->SetSceneState(CCEngine::SceneState::Edit);
                m_HierarchyPanel->SetContext(m_ActiveScene);
                m_BtnPlay->SetActive(false);
                m_BtnPause->SetActive(false);
            }
            else if (state == CCEngine::SceneState::Pause) {
                m_ActiveScene->SetSceneState(CCEngine::SceneState::Play);
                m_BtnPlay->SetActive(true);
                m_BtnPause->SetActive(false);
            }
            });

        m_BtnPause->SetOnClick([this]() {
            CCEngine::SceneState state = m_ActiveScene->GetState();
            if (state == CCEngine::SceneState::Play) {
                m_ActiveScene->SetSceneState(CCEngine::SceneState::Pause);
                m_BtnPause->SetActive(true);
            }
            else if (state == CCEngine::SceneState::Pause) {
                m_ActiveScene->SetSceneState(CCEngine::SceneState::Play);
                m_BtnPause->SetActive(false);
            }
            });

        m_BtnStop->SetOnClick([this]() {
            CCEngine::SceneState state = m_ActiveScene->GetState();
            if (state != CCEngine::SceneState::Edit) {
                m_ActiveScene->OnRuntimeStop();
                delete m_ActiveScene;
                m_ActiveScene = m_EditorScene;
                m_EditorScene = nullptr;
                m_ActiveScene->SetSceneState(CCEngine::SceneState::Edit);
                m_HierarchyPanel->SetContext(m_ActiveScene);
                m_BtnPlay->SetActive(false);
                m_BtnPause->SetActive(false);
            }
            });

        CCEngine::Application::Get()->GetWindow().SetRootUI(m_RootUI);
    }
}
