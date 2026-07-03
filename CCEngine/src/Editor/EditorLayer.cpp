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
#include "UI/KeyBindingPickerPanel.h"
#include "Core/AssetDatabase.h"
#include <windows.h>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <unordered_map>

#include "Events/Event.h"
#include "Events/MouseEvent.h"

namespace CCEngine {
    namespace
    {
        std::shared_ptr<Mesh> CreateDefaultMeshForType(MeshComponent::MeshType type)
        {
            switch (type)
            {
                case MeshComponent::MeshType::Cube: return MeshFactory::CreateCube();
                case MeshComponent::MeshType::Sphere: return MeshFactory::CreateSphere();
                case MeshComponent::MeshType::Capsule: return MeshFactory::CreateCapsule();
                case MeshComponent::MeshType::Cylinder: return MeshFactory::CreateCylinder();
                case MeshComponent::MeshType::Plane: return MeshFactory::CreatePlane();
                case MeshComponent::MeshType::Quad: return MeshFactory::CreateQuad();
                case MeshComponent::MeshType::Torus: return MeshFactory::CreateTorus();
                default: return nullptr;
            }
        }

        std::string MakeSafeAssetFileName(const std::string& name)
        {
            std::string result;
            result.reserve(name.size());

            for (char c : name)
            {
                unsigned char uc = static_cast<unsigned char>(c);
                if (std::isalnum(uc) || c == '_' || c == '-' || c == ' ')
                    result.push_back(c);
                else
                    result.push_back('_');
            }

            while (!result.empty() && result.back() == ' ')
                result.pop_back();

            return result.empty() ? "Prefab" : result;
        }

        std::filesystem::path MakeUniquePrefabPath(const std::filesystem::path& directory, const std::string& baseName)
        {
            std::filesystem::path safeBase = MakeSafeAssetFileName(baseName);
            std::filesystem::path candidate = directory / (safeBase.string() + ".ccprefab");

            int index = 1;
            while (std::filesystem::exists(candidate))
            {
                candidate = directory / (safeBase.string() + " " + std::to_string(index) + ".ccprefab");
                ++index;
            }

            return candidate;
        }
    }

    EditorLayer::EditorLayer()
        : Layer("EditorLayer"), m_Camera(45.0f, 1280.0f / 720.0f, 0.1f, 100.0f)
    {
        m_GizmoSystem.Init(); // 기즈모 시스템 초기화
    }

    void EditorLayer::OnAttach()
    {
        m_ProjectSettings.Load();
        m_ProjectSettings.Normalize();

        FramebufferSpecification fbSpec;
        fbSpec.Width = 1280;
        fbSpec.Height = 720;
        m_Framebuffer = Framebuffer::Create(fbSpec);

        FramebufferSpecification gameFbSpec;
        gameFbSpec.Width = m_ProjectSettings.Data().GameWidth;
        gameFbSpec.Height = m_ProjectSettings.Data().GameHeight;
        m_GameFramebuffer = Framebuffer::Create(gameFbSpec);
        m_GameViewportSize = { (float)gameFbSpec.Width, (float)gameFbSpec.Height };

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
        ConsoleLog::Info("Editor scene initialized.");

        // --- 에디터 기본 UI 세팅 ---
        BuildEditorUI();
        ConfigureUndoManager();

        // --- 인스펙터 패널에 기본 컴포넌트 등록 ---
        UI::InspectorUtils::InitStandardComponents();

        std::string startScenePath = m_ProjectSettings.Data().StartScenePath;
        if (!m_ProjectSettings.Data().StartSceneGuid.empty())
        {
            std::filesystem::path resolved = AssetDatabase::GetPathFromGuid(m_ProjectSettings.Data().StartSceneGuid);
            if (!resolved.empty())
                startScenePath = resolved.string();
        }
        if (!startScenePath.empty())
            OpenScene(startScenePath);
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
        auto getVisibleImageSize = [](const std::vector<UI::ImageWidget*>& widgets, DirectX::XMFLOAT2 fallback)
        {
            for (UI::ImageWidget* widget : widgets)
            {
                if (!widget || !widget->IsVisible())
                    continue;
                UI::Widget* parent = widget->GetParent();
                if (parent && !parent->IsVisible())
                    continue;

                auto size = widget->GetCalculatedSize();
                if (size.x > 0.0f && size.y > 0.0f)
                    return size;
            }
            return fallback;
        };

        if (!m_ViewportWidgets.empty())
        {
            auto vpSize = getVisibleImageSize(m_ViewportWidgets, m_ViewportSize);
            if (vpSize.x > 0 && vpSize.y > 0) m_ViewportSize = { vpSize.x, vpSize.y };
        }
        if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f &&
            (m_Framebuffer->GetSpecification().Width != (uint32_t)m_ViewportSize.x ||
                m_Framebuffer->GetSpecification().Height != (uint32_t)m_ViewportSize.y))
        {
            m_Framebuffer->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            m_Camera.SetProjectionMatrix(m_Camera.GetFOV(), m_ViewportSize.x / m_ViewportSize.y, 0.1f, 100.0f);
        }

        if (!m_GameViewWidgets.empty())
        {
            auto gameSize = getVisibleImageSize(m_GameViewWidgets, m_GameViewportSize);
            // 에디터의 Game View는 창 크기를 따라간다.
            // 프로젝트 해상도는 빌드/플레이 기본값이고, 에디터 패널 크기를 고정하지 않는다.
            if (gameSize.x > 0.0f && gameSize.y > 0.0f)
                m_GameViewportSize = { gameSize.x, gameSize.y };
        }

        if (m_GameViewportSize.x > 0.0f && m_GameViewportSize.y > 0.0f &&
            (m_GameFramebuffer->GetSpecification().Width != (uint32_t)m_GameViewportSize.x ||
                m_GameFramebuffer->GetSpecification().Height != (uint32_t)m_GameViewportSize.y))
        {
            m_GameFramebuffer->Resize((uint32_t)m_GameViewportSize.x, (uint32_t)m_GameViewportSize.y);
        }

        // 2. 카메라 및 로직 업데이트
        m_Camera.OnUpdate(deltaTime, m_ProjectSettings.Data());
        HandleShortcuts();

        // 선택된 엔티티가 있다면 인스펙터 패널에 전달하여 UI 갱신
        if (m_HierarchyPanel && m_InspectorPanel)
        {
            Entity selected = m_HierarchyPanel->GetSelectedEntity();
            for (UI::InspectorPanel* inspector : m_InspectorPanels)
            {
                if (inspector)
                    inspector->SetSelectedEntity(selected);
            }
        }

        if (m_RootUI)
        {
            BringEditorOverlaysToFront();

            float winWidth = (float)mainWindow.GetWidth();
            float winHeight = (float)mainWindow.GetHeight();

            if (winWidth >= 50.0f && winHeight >= 50.0f)
            {
                m_RootUI->UpdateLayout({ 0.0f, 0.0f }, { winWidth, winHeight });
            }
        }

        if (m_HistoryPanelDirty)
            RebuildHistoryPanel();
        // =========================================================================

        // 최신 프레임버퍼 텍스처를 뷰포트 위젯에 연결
        RendererHandle editorTexture = m_Framebuffer->GetColorAttachmentRendererID(0);
        for (UI::ImageWidget* viewportWidget : m_ViewportWidgets)
        {
            if (viewportWidget)
                viewportWidget->SetTexture(editorTexture);
        }
        RendererHandle gameTexture = m_GameFramebuffer->GetColorAttachmentRendererID(0);
        for (UI::ImageWidget* gameViewWidget : m_GameViewWidgets)
        {
            if (gameViewWidget)
                gameViewWidget->SetTexture(gameTexture);
        }

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
        m_GizmoSystem.OnRenderSkeleton(selectedEntity);
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

        m_UndoManager.TrackTransformUndo();
    }

    void EditorLayer::BringEditorOverlaysToFront()
    {
        if (!m_RootUI)
            return;

        // 메뉴와 팝업은 일반 창보다 높은 레이어로 취급한다.
        // 움직인 패널이 BringToFront 되어도 드롭다운이 그 아래에 깔리면 안 된다.
        if (m_TitleBarPanel) m_TitleBarPanel->BringToFront();
        if (m_MenuBarPanel) m_MenuBarPanel->BringToFront();
        if (m_ProjectSettingsPanel) m_ProjectSettingsPanel->BringToFront();
        if (m_ObjectContextMenuPanel) m_ObjectContextMenuPanel->BringToFront();
        if (m_MeshObjectSubmenuPanel) m_MeshObjectSubmenuPanel->BringToFront();
        if (m_FileDropdownPanel) m_FileDropdownPanel->BringToFront();
        if (m_EditDropdownPanel) m_EditDropdownPanel->BringToFront();
        if (m_WindowDropdownPanel) m_WindowDropdownPanel->BringToFront();
    }

    void EditorLayer::OnEvent(Event& e)
    {
        auto getMousePoint = [](Event& event, float& mouseX, float& mouseY) -> bool
            {
                if (event.GetEventType() == EventType::MouseButtonPressed)
                {
                    auto& mouseEvent = static_cast<MouseButtonPressedEvent&>(event);
                    mouseX = mouseEvent.GetX();
                    mouseY = mouseEvent.GetY();
                    return true;
                }
                if (event.GetEventType() == EventType::MouseMoved)
                {
                    auto& mouseEvent = static_cast<MouseMovedEvent&>(event);
                    mouseX = mouseEvent.GetX();
                    mouseY = mouseEvent.GetY();
                    return true;
                }
                if (event.GetEventType() == EventType::MouseButtonReleased)
                {
                    auto& mouseEvent = static_cast<MouseButtonReleasedEvent&>(event);
                    mouseX = mouseEvent.GetX();
                    mouseY = mouseEvent.GetY();
                    return true;
                }
                return false;
            };

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

        auto isWidgetOrChildOf = [](UI::Widget* widget, UI::Widget* parent) -> bool
            {
                // 겹친 창에서는 마우스 아래 최상위 위젯만 입력을 가져야 한다.
                // 부모 체인을 타고 올라가며 현재 패널 안쪽 위젯인지 확인한다.
                while (widget)
                {
                    if (widget == parent)
                        return true;
                    widget = widget->GetParent();
                }
                return false;
            };

        float popupMouseX = 0.0f;
        float popupMouseY = 0.0f;
        bool isPopupMouseEvent = getMousePoint(e, popupMouseX, popupMouseY);
        bool hadBlockingPopup =
            (m_FileDropdownPanel && m_FileDropdownPanel->IsVisible()) ||
            (m_EditDropdownPanel && m_EditDropdownPanel->IsVisible()) ||
            (m_WindowDropdownPanel && m_WindowDropdownPanel->IsVisible()) ||
            (m_ObjectContextMenuPanel && m_ObjectContextMenuPanel->IsVisible()) ||
            (m_MeshObjectSubmenuPanel && m_MeshObjectSubmenuPanel->IsVisible()) ||
            (m_ProjectSettingsPanel && m_ProjectSettingsPanel->IsVisible());

        // 1. 드롭다운 바깥 클릭 시 닫히는 Focus Out 로직 
        if (e.GetEventType() == EventType::MouseButtonPressed)
        {
            MouseButtonPressedEvent& mouseEvent = static_cast<MouseButtonPressedEvent&>(e);

            bool insideObjectMenu = m_ObjectContextMenuPanel && m_ObjectContextMenuPanel->IsVisible() &&
                m_ObjectContextMenuPanel->IsPointInside(mouseEvent.GetX(), mouseEvent.GetY());
            bool insideMeshSubmenu = m_MeshObjectSubmenuPanel && m_MeshObjectSubmenuPanel->IsVisible() &&
                m_MeshObjectSubmenuPanel->IsPointInside(mouseEvent.GetX(), mouseEvent.GetY());
            if (m_ObjectContextMenuPanel && m_ObjectContextMenuPanel->IsVisible() &&
                !insideObjectMenu && !insideMeshSubmenu)
            {
                HideObjectContextMenu();
            }

            if (m_FileDropdownPanel && m_FileDropdownPanel->IsVisible())
            {
                if (!m_FileDropdownPanel->IsPointInside(mouseEvent.GetX(), mouseEvent.GetY()) &&
                    !m_BtnFileMenu->IsPointInside(mouseEvent.GetX(), mouseEvent.GetY()))
                {
                    m_FileDropdownPanel->SetVisible(false);
                }
            }

            if (m_EditDropdownPanel && m_EditDropdownPanel->IsVisible())
            {
                if (!m_EditDropdownPanel->IsPointInside(mouseEvent.GetX(), mouseEvent.GetY()) &&
                    !m_BtnEditMenu->IsPointInside(mouseEvent.GetX(), mouseEvent.GetY()))
                {
                    m_EditDropdownPanel->SetVisible(false);
                }
            }

            if (m_WindowDropdownPanel && m_WindowDropdownPanel->IsVisible())
            {
                if (!m_WindowDropdownPanel->IsPointInside(mouseEvent.GetX(), mouseEvent.GetY()) &&
                    !m_BtnWindowMenu->IsPointInside(mouseEvent.GetX(), mouseEvent.GetY()))
                {
                    m_WindowDropdownPanel->SetVisible(false);
                }
            }

            // 팝업/메뉴가 떠 있던 프레임의 마우스 입력은 아래 하이어라키/씬뷰로 보내지 않는다.
            if (hadBlockingPopup && isPopupMouseEvent)
            {
                if (m_RootUI)
                    m_RootUI->OnEvent(e);

                e.Handled = true;
                return;
            }

            if (mouseEvent.GetButton() == 1)
            {
                float mouseX = mouseEvent.GetX();
                float mouseY = mouseEvent.GetY();
                UI::Widget* topmostWidget = m_RootUI ? getTopmostWidgetAt(m_RootUI, mouseX, mouseY) : nullptr;

                if (m_HierarchyPanel && m_HierarchyPanel->IsPointInside(mouseX, mouseY) &&
                    isWidgetOrChildOf(topmostWidget, m_HierarchyPanel))
                {
                    Entity hoveredEntity = m_HierarchyPanel->GetEntityAt(mouseX, mouseY);
                    if (hoveredEntity)
                        m_HierarchyPanel->SetSelectedEntity(hoveredEntity);

                    ShowObjectContextMenu(mouseX, mouseY, hoveredEntity || m_HierarchyPanel->GetSelectedEntity());
                    mouseEvent.Handled = true;
                    return;
                }

                if (m_ViewportWidget && m_ViewportWidget->IsPointInside(mouseX, mouseY) &&
                    isWidgetOrChildOf(topmostWidget, m_ViewportWidget))
                {
                    ShowObjectContextMenu(mouseX, mouseY, m_HierarchyPanel && m_HierarchyPanel->GetSelectedEntity());
                    mouseEvent.Handled = true;
                    return;
                }
            }

            if (mouseEvent.GetButton() == 0 && m_HierarchyPanel && m_HierarchyPanel->IsPointInside(mouseEvent.GetX(), mouseEvent.GetY()))
            {
                Entity hoveredEntity = m_HierarchyPanel->GetEntityAt(mouseEvent.GetX(), mouseEvent.GetY());
                if (hoveredEntity)
                {
                    m_PrefabDragEntity = hoveredEntity;
                    m_PrefabDragStartX = mouseEvent.GetX();
                    m_PrefabDragStartY = mouseEvent.GetY();
                    m_IsDraggingPrefabToAssetBrowser = false;
                }
            }
        }

        if (hadBlockingPopup && isPopupMouseEvent)
        {
            if (m_RootUI)
                m_RootUI->OnEvent(e);

            e.Handled = true;
            return;
        }

        if (e.GetEventType() == EventType::MouseMoved && m_PrefabDragEntity)
        {
            MouseMovedEvent& mouseEvent = static_cast<MouseMovedEvent&>(e);
            float dx = mouseEvent.GetX() - m_PrefabDragStartX;
            float dy = mouseEvent.GetY() - m_PrefabDragStartY;

            if ((dx * dx + dy * dy) > 64.0f)
                m_IsDraggingPrefabToAssetBrowser = true;

            if (m_IsDraggingPrefabToAssetBrowser)
            {
                e.Handled = true;
                return;
            }
        }

        if (e.GetEventType() == EventType::MouseButtonReleased)
        {
            MouseButtonReleasedEvent& mouseEvent = static_cast<MouseButtonReleasedEvent&>(e);
            if (mouseEvent.GetButton() == 0 && m_PrefabDragEntity)
            {
                if (m_IsDraggingPrefabToAssetBrowser && m_AssetBrowserPanel &&
                    m_AssetBrowserPanel->IsDropTargetPoint(mouseEvent.GetX(), mouseEvent.GetY()))
                {
                    // 하이어라키 오브젝트를 에셋 브라우저에 놓으면 현재 폴더에 프리팹을 만든다.
                    SavePrefabToDirectory(m_PrefabDragEntity, m_AssetBrowserPanel->GetCurrentAssetDirectory());
                    e.Handled = true;
                }

                m_PrefabDragEntity = {};
                m_IsDraggingPrefabToAssetBrowser = false;
                if (e.Handled)
                    return;
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

    void EditorLayer::ShowObjectContextMenu(float x, float y, bool allowDelete)
    {
        if (!m_ObjectContextMenuPanel)
            return;

        float width = 160.0f;
        float height = allowDelete ? 156.0f : 104.0f;
        auto& mainWindow = Application::Get()->GetWindow();
        x = (std::min)(x, (float)mainWindow.GetWidth() - width);
        y = (std::min)(y, (float)mainWindow.GetHeight() - height);
        x = (std::max)(x, 0.0f);
        y = (std::max)(y, 0.0f);

        m_ObjectContextMenuPanel->SetAnchorMin(0.0f, 0.0f);
        m_ObjectContextMenuPanel->SetAnchorMax(0.0f, 0.0f);
        m_ObjectContextMenuPanel->SetOffsetMin(x, y);
        m_ObjectContextMenuPanel->SetOffsetMax(x + width, y + height);
        if (m_BtnDeleteObject)
            m_BtnDeleteObject->SetVisible(allowDelete);
        if (m_BtnCreatePrefab)
            m_BtnCreatePrefab->SetVisible(allowDelete);
        if (m_MeshObjectSubmenuPanel)
            m_MeshObjectSubmenuPanel->SetVisible(false);

        m_ObjectContextMenuPanel->SetVisible(true);
        m_ObjectContextMenuPanel->BringToFront();
    }

    void EditorLayer::ShowMeshObjectSubmenu()
    {
        if (!m_ObjectContextMenuPanel || !m_MeshObjectSubmenuPanel)
            return;

        const auto menuPos = m_ObjectContextMenuPanel->GetCalculatedPosition();
        const auto menuSize = m_ObjectContextMenuPanel->GetCalculatedSize();
        constexpr float submenuWidth = 160.0f;
        constexpr float submenuHeight = 182.0f;
        auto& mainWindow = Application::Get()->GetWindow();

        float x = menuPos.x + menuSize.x;
        float y = menuPos.y + 26.0f;
        if (x + submenuWidth > mainWindow.GetWidth())
            x = menuPos.x - submenuWidth;
        y = (std::min)(y, (float)mainWindow.GetHeight() - submenuHeight);
        y = (std::max)(y, 0.0f);

        m_MeshObjectSubmenuPanel->SetOffsetMin(x, y);
        m_MeshObjectSubmenuPanel->SetOffsetMax(x + submenuWidth, y + submenuHeight);
        m_MeshObjectSubmenuPanel->SetVisible(true);
    }

    void EditorLayer::HideObjectContextMenu()
    {
        if (m_ObjectContextMenuPanel)
            m_ObjectContextMenuPanel->SetVisible(false);
        if (m_MeshObjectSubmenuPanel)
            m_MeshObjectSubmenuPanel->SetVisible(false);
    }

    Entity EditorLayer::CreateEmptyObject()
    {
        if (!m_ActiveScene)
            return {};

        m_UndoManager.BeginSceneStructureChange("Create GameObject");
        Entity entity = m_ActiveScene->CreateEntity("GameObject");
        RefreshEditorSelection(entity);
        m_UndoManager.CommitSceneStructureChange();
        return entity;
    }

    Entity EditorLayer::CreatePrimitiveObject(const std::string& name, int meshTypeValue)
    {
        if (!m_ActiveScene)
            return {};

        MeshComponent::MeshType meshType = static_cast<MeshComponent::MeshType>(meshTypeValue);
        m_UndoManager.BeginSceneStructureChange("Create " + name);
        Entity entity = m_ActiveScene->CreateEntity(name);
        auto& mesh = entity.AddComponent<MeshComponent>(meshType);
        mesh.MeshData = CreateDefaultMeshForType(meshType);
        mesh.BaseColor = { 0.2f, 0.3f, 0.9f, 1.0f };

        RefreshEditorSelection(entity);
        m_UndoManager.CommitSceneStructureChange();
        return entity;
    }

    Entity EditorLayer::CreateLightObject()
    {
        if (!m_ActiveScene)
            return {};

        m_UndoManager.BeginSceneStructureChange("Create Light");
        Entity entity = m_ActiveScene->CreateEntity("Light");
        auto& transform = entity.GetComponent<TransformComponent>();
        auto& light = entity.AddComponent<LightComponent>();

        // 초기 씬의 Fill Light (Cool)과 같은 값을 새 Light의 고정 기본값으로 사용한다.
        transform.Rotation = {
            DirectX::XMConvertToRadians(15.0f),
            DirectX::XMConvertToRadians(135.0f),
            0.0f
        };
        DirectX::XMStoreFloat4(
            &transform.QuaternionRotation,
            DirectX::XMQuaternionRotationRollPitchYaw(
                transform.Rotation.x,
                transform.Rotation.y,
                transform.Rotation.z));
        light.LightColor = { 0.4f, 0.5f, 1.0f };
        light.Intensity = 0.5f;

        RefreshEditorSelection(entity);
        m_UndoManager.CommitSceneStructureChange();
        return entity;
    }

    Entity EditorLayer::CreateCameraObject()
    {
        if (!m_ActiveScene)
            return {};

        m_UndoManager.BeginSceneStructureChange("Create Camera");
        Entity entity = m_ActiveScene->CreateEntity("Camera");
        auto& transform = entity.GetComponent<TransformComponent>();
        auto& camera = entity.AddComponent<CameraComponent>();

        // 초기 Main Camera와 같은 고정 기본 위치/회전을 사용한다.
        transform.Translation = { 0.0f, 3.0f, -6.0f };
        transform.Rotation = { DirectX::XMConvertToRadians(20.0f), 0.0f, 0.0f };
        DirectX::XMStoreFloat4(
            &transform.QuaternionRotation,
            DirectX::XMQuaternionRotationRollPitchYaw(
                transform.Rotation.x,
                transform.Rotation.y,
                transform.Rotation.z));

        // 이미 게임 뷰 카메라가 있으면 보조 카메라, 없으면 즉시 게임 뷰 카메라가 된다.
        bool hasPrimaryCamera = false;
        auto cameraView = m_ActiveScene->GetRegistry().view<TransformComponent, CameraComponent>();
        for (auto source : cameraView)
        {
            if (source == (entt::entity)entity)
                continue;
            if (cameraView.get<CameraComponent>(source).Primary)
            {
                hasPrimaryCamera = true;
                break;
            }
        }
        camera.Primary = !hasPrimaryCamera;

        RefreshEditorSelection(entity);
        m_UndoManager.CommitSceneStructureChange();
        return entity;
    }

    void EditorLayer::DeleteSelectedObject()
    {
        if (!m_ActiveScene || !m_HierarchyPanel)
            return;

        Entity selected = m_HierarchyPanel->GetSelectedEntity();
        if (!selected || !m_ActiveScene->GetRegistry().valid((entt::entity)selected))
            return;

        std::string label = "Delete Object";
        if (selected.HasComponent<TagComponent>())
            label = "Delete " + selected.GetComponent<TagComponent>().Tag;
        m_UndoManager.BeginSceneStructureChange(label);

        bool deletedPrimaryCamera = selected.HasComponent<CameraComponent>() &&
            selected.GetComponent<CameraComponent>().Primary;
        m_ActiveScene->DestroyEntity(selected);

        // 게임 뷰 카메라가 삭제되면 남은 첫 카메라가 자동 승계한다.
        if (deletedPrimaryCamera)
        {
            auto cameraView = m_ActiveScene->GetRegistry().view<CameraComponent>();
            for (auto entity : cameraView)
            {
                cameraView.get<CameraComponent>(entity).Primary = true;
                break;
            }
        }
        RefreshEditorSelection({});
        m_UndoManager.CommitSceneStructureChange();
    }

    void EditorLayer::DuplicateSelectedObject()
    {
        if (!m_ActiveScene || !m_HierarchyPanel)
            return;

        Entity selected = m_HierarchyPanel->GetSelectedEntity();
        if (!selected || !m_ActiveScene->GetRegistry().valid((entt::entity)selected))
            return;

        std::string label = "Duplicate Object";
        if (selected.HasComponent<TagComponent>())
            label = "Duplicate " + selected.GetComponent<TagComponent>().Tag;

        m_UndoManager.BeginSceneStructureChange(label);
        Entity duplicated = m_ActiveScene->DuplicateEntity(selected);
        if (duplicated)
            RefreshEditorSelection(duplicated);
        m_UndoManager.CommitSceneStructureChange();
    }

    void EditorLayer::RefreshEditorSelection(Entity selected)
    {
        for (UI::HierarchyPanel* hierarchy : m_HierarchyPanels)
        {
            if (!hierarchy)
                continue;
            hierarchy->SetSelectedEntity(selected);
            hierarchy->Refresh();
        }

        for (UI::InspectorPanel* inspector : m_InspectorPanels)
        {
            if (inspector)
                inspector->SetSelectedEntity(selected);
        }
    }

    void EditorLayer::ConfigureUndoManager()
    {
        EditorUndoManager::Callbacks callbacks;
        callbacks.GetActiveScene = [this]() { return m_ActiveScene; };
        callbacks.ReplaceActiveScene = [this](Scene* scene)
        {
            delete m_ActiveScene;
            m_ActiveScene = scene;
            for (UI::HierarchyPanel* hierarchy : m_HierarchyPanels)
            {
                if (!hierarchy)
                    continue;
                hierarchy->SetContext(m_ActiveScene);
                hierarchy->Refresh();
            }
        };
        callbacks.GetSelectedEntity = [this]()
        {
            return m_HierarchyPanel ? m_HierarchyPanel->GetSelectedEntity() : Entity{};
        };
        callbacks.SetSelectedEntity = [this](Entity entity)
        {
            RefreshEditorSelection(entity);
        };
        callbacks.IsLeftMouseDown = []()
        {
            return CCEngine::Application::Get()->GetWindow().IsMouseButtonPressed(0);
        };
        callbacks.IsGizmoDragging = [this]()
        {
            return m_GizmoSystem.IsDragging();
        };
        callbacks.OnHistoryChanged = [this]()
        {
            MarkHistoryPanelDirty();
        };

        // UndoManager는 스택과 명령만 관리하고, 씬/선택/UI 접근은 이 콜백을 통해 처리한다.
        m_UndoManager.SetCallbacks(std::move(callbacks));
    }

    // =========================================================================
    // 파일 세이브/로드 및 단축키 로직
    // =========================================================================
    void EditorLayer::SaveScene()
    {
        if (m_CurrentScenePath.empty()) { SaveSceneAs(); return; }
        CCEngine::SceneSerializer serializer(m_ActiveScene);
        serializer.Serialize(m_CurrentScenePath);
        ConsoleLog::Info("Scene saved: " + m_CurrentScenePath);
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
            ConsoleLog::Info("Scene saved as: " + m_CurrentScenePath);
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
            m_UndoManager.ClearTransformHistory();
            m_UndoManager.ClearSceneStructureHistory();
            RefreshEditorSelection(CCEngine::Entity{});
            ConsoleLog::Info("Scene loaded: " + m_CurrentScenePath);
            printf("Scene Loaded from: %s\n", m_CurrentScenePath.c_str());
        }
        else
        {
            ConsoleLog::Error("Failed to load scene: " + filepath);
            printf("Failed to load scene: %s\n", filepath.c_str());
        }
    }

    void EditorLayer::LoadSceneAdditive(const std::string& filepath)
    {
        if (filepath.empty())
            return;

        CCEngine::SceneSerializer serializer(m_ActiveScene);
        Entity sceneRoot = serializer.DeserializeAppend(filepath);
        if (sceneRoot)
        {
            RefreshEditorSelection(sceneRoot);
            ConsoleLog::Info("Scene added: " + filepath);
            printf("Scene Added from: %s\n", filepath.c_str());
        }
        else
        {
            ConsoleLog::Error("Failed to add scene: " + filepath);
            printf("Failed to add scene: %s\n", filepath.c_str());
        }
    }

    void EditorLayer::SetCurrentSceneAsProjectStartScene()
    {
        if (m_CurrentScenePath.empty())
        {
            ConsoleLog::Warning("Save the scene before setting it as start scene.");
            return;
        }

        std::string pathToStore = m_CurrentScenePath;
        try
        {
            pathToStore = std::filesystem::relative(std::filesystem::path(m_CurrentScenePath), std::filesystem::current_path()).string();
        }
        catch (...)
        {
            pathToStore = m_CurrentScenePath;
        }

        m_ProjectSettings.Data().StartScenePath = pathToStore;
        // 시작 씬도 경로만 저장하면 파일명을 바꾸는 순간 끊어진다.
        // GUID를 같이 저장해 두고, 경로는 사람이 읽기 쉬운 보조값으로 남긴다.
        m_ProjectSettings.Data().StartSceneGuid = AssetDatabase::GetGuidFromPath(m_CurrentScenePath);
        SaveProjectSettings();
    }

    void EditorLayer::OpenProjectStartScene()
    {
        std::string startScene = m_ProjectSettings.Data().StartScenePath;
        if (!m_ProjectSettings.Data().StartSceneGuid.empty())
        {
            std::filesystem::path resolved = AssetDatabase::GetPathFromGuid(m_ProjectSettings.Data().StartSceneGuid);
            if (!resolved.empty())
                startScene = resolved.string();
        }
        if (startScene.empty())
        {
            ConsoleLog::Warning("Project start scene is not set.");
            return;
        }

        OpenScene(startScene);
    }

    void EditorLayer::SaveProjectSettings()
    {
        m_ProjectSettings.Normalize();
        m_ProjectSettings.Save();
        if (m_ProjectSettingsPanel)
            m_ProjectSettingsPanel->OnOpened();
    }

    void EditorLayer::ApplyProjectGameResolution()
    {
        m_ProjectSettings.Normalize();
        uint32_t width = m_ProjectSettings.Data().GameWidth;
        uint32_t height = m_ProjectSettings.Data().GameHeight;

        // Game View 패널은 창 크기를 따라가고, 이 값은 패널이 없을 때와 플레이어 빌드의 기본값으로 쓴다.
        m_GameViewportSize = { (float)width, (float)height };
        SaveProjectSettings();
        ConsoleLog::Info("Default game resolution saved: " + std::to_string(width) + " x " + std::to_string(height));
    }

    void EditorLayer::SaveSelectedPrefab()
    {
        if (!m_HierarchyPanel)
            return;

        Entity selected = m_HierarchyPanel->GetSelectedEntity();
        if (!selected)
        {
            ConsoleLog::Warning("No entity selected. Select an entity before saving a prefab.");
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
                AssetDatabase::MarkDirty(std::filesystem::current_path() / "assets");
                for (UI::AssetBrowserPanel* browser : m_AssetBrowserPanels)
                {
                    if (browser)
                        browser->Refresh(true);
                }
                ConsoleLog::Info("Prefab saved: " + filepath);
                printf("Prefab Saved to: %s\n", filepath.c_str());
            }
            else
            {
                ConsoleLog::Error("Failed to save prefab: " + filepath);
                printf("Failed to save prefab: %s\n", filepath.c_str());
            }
        }
    }

    void EditorLayer::SaveSelectedPrefabToCurrentAssetFolder()
    {
        if (!m_HierarchyPanel)
            return;

        Entity selected = m_HierarchyPanel->GetSelectedEntity();
        if (!selected)
        {
            ConsoleLog::Warning("No entity selected. Select an entity before creating a prefab.");
            printf("No entity selected. Select an entity before creating a prefab.\n");
            return;
        }

        std::filesystem::path targetDirectory = std::filesystem::current_path() / "assets" / "prefabs";
        if (m_AssetBrowserPanel)
            targetDirectory = m_AssetBrowserPanel->GetCurrentAssetDirectory();

        SavePrefabToDirectory(selected, targetDirectory);
    }

    void EditorLayer::SavePrefabToDirectory(Entity entity, const std::filesystem::path& directory)
    {
        if (!m_ActiveScene || !entity)
            return;

        std::filesystem::create_directories(directory);

        std::string baseName = "Prefab";
        if (entity.HasComponent<TagComponent>())
            baseName = entity.GetComponent<TagComponent>().Tag;

        // 에셋 브라우저로 만드는 프리팹은 현재 폴더에 바로 생성한다. 같은 이름은 번호를 붙여 보존한다.
        std::filesystem::path filepath = MakeUniquePrefabPath(directory, baseName);
        if (PrefabSerializer::Serialize(m_ActiveScene, entity, filepath.string()))
        {
            AssetDatabase::MarkDirty(std::filesystem::current_path() / "assets");
            if (m_AssetBrowserPanel)
                m_AssetBrowserPanel->Refresh(true);
            ConsoleLog::Info("Prefab created: " + filepath.string());
            printf("Prefab Created: %s\n", filepath.string().c_str());
        }
        else
        {
            ConsoleLog::Error("Failed to create prefab: " + filepath.string());
            printf("Failed to create prefab: %s\n", filepath.string().c_str());
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

                RefreshEditorSelection(instance);
                ConsoleLog::Info("Prefab instantiated: " + filepath);
                printf("Prefab Instantiated from: %s\n", filepath.c_str());
            }
            else
            {
                ConsoleLog::Error("Failed to instantiate prefab: " + filepath);
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
            RefreshEditorSelection(modelEntity);
            ConsoleLog::Info("Model imported: " + filepath);
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
        static bool s_IsZPressedLastFrame = false;
        static bool s_IsYPressedLastFrame = false;
        static bool s_IsDPressedLastFrame = false;
        bool isOPressedNow = (GetAsyncKeyState('O') & 0x8000) != 0;
        bool isZPressedNow = (GetAsyncKeyState('Z') & 0x8000) != 0;
        bool isYPressedNow = (GetAsyncKeyState('Y') & 0x8000) != 0;
        bool isDPressedNow = (GetAsyncKeyState('D') & 0x8000) != 0;

        if (isSPressedNow && !m_IsSPressedLastFrame)
        {
            if (isCtrlPressed && isShiftPressed) SaveSceneAs();
            else if (isCtrlPressed && !isShiftPressed) SaveScene();
        }
        if (isCtrlPressed && isZPressedNow && !s_IsZPressedLastFrame)
        {
            if (isShiftPressed) m_UndoManager.Redo();
            else m_UndoManager.Undo();
        }
        if (isCtrlPressed && isYPressedNow && !s_IsYPressedLastFrame)
        {
            m_UndoManager.Redo();
        }
        if (isCtrlPressed && isDPressedNow && !s_IsDPressedLastFrame)
        {
            DuplicateSelectedObject();
        }
        if (isCtrlPressed && isOPressedNow && !s_IsOPressedLastFrame) OpenScene();
        s_IsOPressedLastFrame = isOPressedNow;
        s_IsZPressedLastFrame = isZPressedNow;
        s_IsYPressedLastFrame = isYPressedNow;
        s_IsDPressedLastFrame = isDPressedNow;
        m_IsSPressedLastFrame = isSPressedNow;
    }


    void EditorLayer::RebuildHistoryPanel()
    {
        // UndoStack + RedoStack을 합쳐 포토샵/유니티식 History 목록으로 다시 그린다.
        // UndoStack 크기가 현재 적용된 작업 위치이며, 해당 항목을 활성 표시한다.
        m_HistoryPanelDirty = false;
        if (m_HistoryContentPanels.empty())
            return;

        const auto& transformUndoStack = m_UndoManager.GetTransformUndoStack();
        const auto& transformRedoStack = m_UndoManager.GetTransformRedoStack();
        const auto& sceneUndoStack = m_UndoManager.GetSceneUndoStack();
        const auto& sceneRedoStack = m_UndoManager.GetSceneRedoStack();

        for (UI::Panel* historyContent : m_HistoryContentPanels)
        {
            if (!historyContent)
                continue;

            historyContent->ClearChildren();

        if (transformUndoStack.empty() && transformRedoStack.empty() &&
            (!sceneUndoStack.empty() || !sceneRedoStack.empty()))
        {
            std::vector<EditorUndoManager::SceneStructureCommand> history = sceneUndoStack;
            for (auto it = sceneRedoStack.rbegin(); it != sceneRedoStack.rend(); ++it)
                history.push_back(*it);

            size_t appliedCount = sceneUndoStack.size();
            float itemHeight = 24.0f;

            auto createSceneHistoryButton = [&](size_t stateIndex, const std::string& text)
            {
                UI::Button* button = new UI::Button("HistoryItem", text);
                button->SetAnchorMin(0.0f, 0.0f);
                button->SetAnchorMax(1.0f, 0.0f);
                button->SetOffsetMin(0.0f, (float)stateIndex * itemHeight);
                button->SetOffsetMax(0.0f, ((float)stateIndex + 1.0f) * itemHeight);
                button->SetNormalColor({ 0.14f, 0.14f, 0.15f, 1.0f });
                button->SetHoverColor({ 0.22f, 0.22f, 0.24f, 1.0f });
                button->SetClickColor({ 0.10f, 0.10f, 0.11f, 1.0f });
                button->SetActive(stateIndex == appliedCount);
                button->SetOnClick([this, stateIndex]() { m_UndoManager.SeekSceneHistory(stateIndex); });
                historyContent->AddChild(button);
            };

            createSceneHistoryButton(0, "Scene Start");
            for (size_t i = 0; i < history.size(); ++i)
            {
                std::string label = std::to_string(i + 1) + ". " + history[i].Label;
                createSceneHistoryButton(i + 1, label);
            }
            continue;
        }

        std::vector<EditorUndoManager::TransformUndoCommand> history = transformUndoStack;
        for (auto it = transformRedoStack.rbegin(); it != transformRedoStack.rend(); ++it)
            history.push_back(*it);

        size_t appliedCount = transformUndoStack.size();
        float itemHeight = 24.0f;

        auto createHistoryButton = [&](size_t stateIndex, const std::string& text)
        {
            // stateIndex는 "이 버튼을 눌렀을 때 적용되어 있어야 하는 작업 개수"다.
            // 0은 Scene Start, 1은 첫 작업 적용 후, 2는 두 번째 작업 적용 후를 뜻한다.
            UI::Button* button = new UI::Button("HistoryItem", text);
            button->SetAnchorMin(0.0f, 0.0f);
            button->SetAnchorMax(1.0f, 0.0f);
            button->SetOffsetMin(0.0f, (float)stateIndex * itemHeight);
            button->SetOffsetMax(0.0f, ((float)stateIndex + 1.0f) * itemHeight);
            button->SetNormalColor({ 0.14f, 0.14f, 0.15f, 1.0f });
            button->SetHoverColor({ 0.22f, 0.22f, 0.24f, 1.0f });
            button->SetClickColor({ 0.10f, 0.10f, 0.11f, 1.0f });
            button->SetActive(stateIndex == appliedCount);
            button->SetOnClick([this, stateIndex]() { m_UndoManager.SeekTransformHistory(stateIndex); });
            historyContent->AddChild(button);
        };

        createHistoryButton(0, "Scene Start");
        for (size_t i = 0; i < history.size(); ++i)
        {
            const EditorUndoManager::TransformUndoCommand& command = history[i];
            std::string actionName = "Transform";
            if (command.Before.Translation.x != command.After.Translation.x ||
                command.Before.Translation.y != command.After.Translation.y ||
                command.Before.Translation.z != command.After.Translation.z)
                actionName = "Move";
            else if (command.Before.Rotation.x != command.After.Rotation.x ||
                command.Before.Rotation.y != command.After.Rotation.y ||
                command.Before.Rotation.z != command.After.Rotation.z)
                actionName = "Rotate";
            else if (command.Before.Scale.x != command.After.Scale.x ||
                command.Before.Scale.y != command.After.Scale.y ||
                command.Before.Scale.z != command.After.Scale.z)
                actionName = "Scale";

            std::string entityName = "Entity";
            if (!command.EntityPath.empty())
                entityName = command.EntityPath.back();

            std::string label = std::to_string(i + 1) + ". " + actionName + " " + entityName;
            createHistoryButton(i + 1, label);
        }
        }
    }

    void EditorLayer::MarkHistoryPanelDirty()
    {
        m_HistoryPanelDirty = true;
    }

    void EditorLayer::OpenEditorWindow(int windowKind)
    {
        if (!m_RootUI)
            return;

        auto showHiddenWindow = [](auto& windows) -> bool
        {
            for (auto* window : windows)
            {
                if (window && !window->IsVisible())
                {
                    window->SetVisible(true);
                    window->BringToFront();
                    return true;
                }
            }
            return false;
        };

        auto configureFloatingWindow = [this](UI::WindowPanel* window, float x, float y, float width, float height)
        {
            window->SetAnchorMin(0.0f, 0.0f);
            window->SetAnchorMax(0.0f, 0.0f);
            window->SetOffsetMin(x, y);
            window->SetOffsetMax(x + width, y + height);
            m_RootUI->AddChild(window);
            window->BringToFront();
        };

        auto configureInspector = [this](UI::InspectorPanel* inspector)
        {
            inspector->SetSceneStructureChangeCallbacks(
                [this](const std::string& label) { m_UndoManager.BeginSceneStructureChange(label); },
                [this]() { m_UndoManager.CommitSceneStructureChange(); });
            if (m_HierarchyPanel)
                inspector->SetSelectedEntity(m_HierarchyPanel->GetSelectedEntity());
            m_InspectorPanels.push_back(inspector);
        };

        auto configureAssetBrowser = [this](UI::AssetBrowserPanel* browser)
        {
            browser->SetOnPrefabSelected([this](const std::string& path) { InstantiatePrefab(path); });
            browser->SetOnModelSelected([this](const std::string& path) { ImportModelAsset(path); });
            browser->SetOnSceneSelected([this](const std::string& path) { OpenScene(path); });
            browser->SetOnAssetDropped([this](const std::string& path, const std::string& type, float, float) { HandleAssetDropped(path, type); });
            m_AssetBrowserPanels.push_back(browser);
        };

        switch (windowKind)
        {
            case 0:
            {
                if (showHiddenWindow(m_HierarchyPanels)) return;
                auto* hierarchy = new UI::HierarchyPanel("HierarchyExtra");
                hierarchy->SetContext(m_ActiveScene);
                hierarchy->Refresh();
                m_HierarchyPanels.push_back(hierarchy);
                configureFloatingWindow(hierarchy, 80.0f, 100.0f, 300.0f, 520.0f);
                break;
            }
            case 1:
            {
                if (showHiddenWindow(m_InspectorPanels)) return;
                auto* inspector = new UI::InspectorPanel("InspectorExtra", "Inspector");
                configureInspector(inspector);
                configureFloatingWindow(inspector, 920.0f, 100.0f, 320.0f, 520.0f);
                break;
            }
            case 2:
            {
                if (showHiddenWindow(m_ViewportWindows)) return;
                auto* sceneWindow = new UI::WindowPanel("SceneViewExtra", "Scene View");
                auto* sceneImage = new UI::ImageWidget("SceneViewImageExtra", m_Framebuffer->GetColorAttachmentRendererID(0));
                sceneImage->SetAnchorMin(0.0f, 0.0f);
                sceneImage->SetAnchorMax(1.0f, 1.0f);
                sceneImage->SetOffsetMin(0.0f, 24.0f);
                sceneImage->SetOffsetMax(0.0f, 0.0f);
                sceneWindow->AddChild(sceneImage);
                m_ViewportWindows.push_back(sceneWindow);
                m_ViewportWidgets.push_back(sceneImage);
                configureFloatingWindow(sceneWindow, 300.0f, 120.0f, 620.0f, 380.0f);
                break;
            }
            case 3:
            {
                if (showHiddenWindow(m_GameWindows)) return;
                auto* gameWindow = new UI::WindowPanel("GameViewExtra", "Game View");
                auto* gameImage = new UI::ImageWidget("GameViewImageExtra", m_GameFramebuffer->GetColorAttachmentRendererID(0));
                gameImage->SetAnchorMin(0.0f, 0.0f);
                gameImage->SetAnchorMax(1.0f, 1.0f);
                gameImage->SetOffsetMin(0.0f, 24.0f);
                gameImage->SetOffsetMax(0.0f, 0.0f);
                gameWindow->AddChild(gameImage);
                m_GameWindows.push_back(gameWindow);
                m_GameViewWidgets.push_back(gameImage);
                configureFloatingWindow(gameWindow, 320.0f, 520.0f, 620.0f, 260.0f);
                break;
            }
            case 4:
            {
                if (showHiddenWindow(m_AssetBrowserPanels)) return;
                auto* browser = new UI::AssetBrowserPanel("AssetBrowserExtra");
                configureAssetBrowser(browser);
                configureFloatingWindow(browser, 320.0f, 560.0f, 720.0f, 260.0f);
                break;
            }
            case 5:
            {
                if (showHiddenWindow(m_HistoryPanels)) return;
                auto* history = new UI::WindowPanel("HistoryPanelExtra", "History");
                auto* content = new UI::Panel("HistoryContentExtra", { 0.10f, 0.10f, 0.11f, 1.0f });
                content->SetAnchorMin(0.0f, 0.0f);
                content->SetAnchorMax(1.0f, 1.0f);
                content->SetOffsetMin(0.0f, 24.0f);
                content->SetOffsetMax(0.0f, 0.0f);
                content->SetClipToBounds(true);
                history->AddChild(content);
                m_HistoryPanels.push_back(history);
                m_HistoryContentPanels.push_back(content);
                configureFloatingWindow(history, 960.0f, 420.0f, 300.0f, 260.0f);
                MarkHistoryPanelDirty();
                break;
            }
            case 6:
            {
                if (showHiddenWindow(m_ConsolePanels)) return;
                auto* console = new UI::ConsolePanel("ConsolePanelExtra");
                m_ConsolePanels.push_back(console);
                configureFloatingWindow(console, 960.0f, 600.0f, 360.0f, 260.0f);
                break;
            }
            case 7:
            {
                OpenProjectSettingsWindow();
                break;
            }
            default:
                break;
        }
    }

    void EditorLayer::OpenProjectSettingsWindow()
    {
        if (!m_ProjectSettingsPanel)
            return;

        for (Window* window : Application::Get()->GetSecondaryWindows())
        {
            if (window && !window->ShouldClose() && window->GetRootUI() == m_ProjectSettingsPanel)
            {
                auto [mouseX, mouseY] = Application::Get()->GetWindow().GetScreenMousePosition();
                window->SetPosition(mouseX - 410, mouseY - 24);
                m_ProjectSettingsPanel->SetVisible(true);
                m_ProjectSettingsPanel->OnOpened();
                Application::Get()->SetModalInputWindow(window);
                return;
            }
        }

        if (m_ProjectSettingsPanel->GetParent())
            m_ProjectSettingsPanel->GetParent()->RemoveChild(m_ProjectSettingsPanel);

        Window* settingsWindow = Application::Get()->CreateSecondaryWindow("Project Settings", 820, 560);
        m_ProjectSettingsPanel->SetDockingEnabled(false);
        m_ProjectSettingsPanel->SetOwnerWindow(settingsWindow);
        m_ProjectSettingsPanel->SetVisible(true);
        m_ProjectSettingsPanel->SetAnchorMin(0.0f, 0.0f);
        m_ProjectSettingsPanel->SetAnchorMax(1.0f, 1.0f);
        m_ProjectSettingsPanel->SetOffsetMin(0.0f, 0.0f);
        m_ProjectSettingsPanel->SetOffsetMax(0.0f, 0.0f);
        settingsWindow->SetRootUI(m_ProjectSettingsPanel);
        m_ProjectSettingsPanel->OnOpened();
        Application::Get()->SetModalInputWindow(settingsWindow);
    }

    void EditorLayer::OpenKeyBindingPickerWindow(UI::KeyBindingInput* targetInput)
    {
        if (!targetInput)
            return;

        Window* pickerWindow = Application::Get()->CreateSecondaryWindow("Key Binding", 560, 360);
        auto* pickerPanel = new UI::KeyBindingPickerPanel("KeyBindingPickerPanelUI");
        pickerPanel->SetOwnerWindow(pickerWindow);
        pickerPanel->SetDockingEnabled(false);
        pickerPanel->SetAnchorMin(0.0f, 0.0f);
        pickerPanel->SetAnchorMax(1.0f, 1.0f);
        pickerPanel->SetOffsetMin(0.0f, 0.0f);
        pickerPanel->SetOffsetMax(0.0f, 0.0f);
        pickerPanel->SetInitialBinding(targetInput->GetBinding());
        pickerPanel->SetOnBindingSelected([targetInput](const std::string& binding)
            {
                // 선택 창은 키 문자열만 만들고, 실제 설정 반영은 입력칸의 변경 콜백을 탄다.
                targetInput->SetBinding(binding);
            });

        pickerWindow->SetRootUI(pickerPanel);
        Application::Get()->SetModalInputWindow(pickerWindow);
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

        m_BtnEditMenu = new UI::Button("BtnEditMenu", "Edit");
        m_BtnEditMenu->SetAnchorMin(0.0f, 0.0f); m_BtnEditMenu->SetAnchorMax(0.0f, 1.0f);
        m_BtnEditMenu->SetOffsetMin(60.0f, 0.0f); m_BtnEditMenu->SetOffsetMax(120.0f, 0.0f);
        m_MenuBarPanel->AddChild(m_BtnEditMenu);

        m_BtnWindowMenu = new UI::Button("BtnWindowMenu", "Window");
        m_BtnWindowMenu->SetAnchorMin(0.0f, 0.0f); m_BtnWindowMenu->SetAnchorMax(0.0f, 1.0f);
        m_BtnWindowMenu->SetOffsetMin(120.0f, 0.0f); m_BtnWindowMenu->SetOffsetMax(205.0f, 0.0f);
        m_MenuBarPanel->AddChild(m_BtnWindowMenu);

        m_HierarchyPanel = new UI::HierarchyPanel("Hierarchy");
        m_HierarchyPanel->SetAnchorMin(0.0f, 0.0f);
        m_HierarchyPanel->SetAnchorMax(0.2f, 1.0f);
        m_HierarchyPanel->SetOffsetMin(0.0f, 48.0f);
        m_HierarchyPanel->SetOffsetMax(0.0f, 0.0f);
        m_RootUI->AddChild(m_HierarchyPanel);
        m_HierarchyPanel->SetContext(m_ActiveScene);
        m_HierarchyPanel->Refresh();
        m_HierarchyPanels.push_back(m_HierarchyPanel);

        m_InspectorPanel = new UI::InspectorPanel("InspectorUI", "Inspector");
        m_InspectorPanel->SetAnchorMin(0.8f, 0.0f);
        m_InspectorPanel->SetAnchorMax(1.0f, 0.50f);
        m_InspectorPanel->SetOffsetMin(0.0f, 48.0f);
        m_InspectorPanel->SetOffsetMax(0.0f, 0.0f);
        m_InspectorPanel->SetSceneStructureChangeCallbacks(
            [this](const std::string& label) { m_UndoManager.BeginSceneStructureChange(label); },
            [this]() { m_UndoManager.CommitSceneStructureChange(); });
        m_RootUI->AddChild(m_InspectorPanel);
        m_InspectorPanels.push_back(m_InspectorPanel);

        m_HistoryPanel = new UI::WindowPanel("HistoryPanelUI", "History");
        m_HistoryPanel->SetAnchorMin(0.8f, 0.50f);
        m_HistoryPanel->SetAnchorMax(1.0f, 0.68f);
        m_HistoryPanel->SetOffsetMin(0.0f, 0.0f);
        m_HistoryPanel->SetOffsetMax(0.0f, 0.0f);
        m_RootUI->AddChild(m_HistoryPanel);

        m_HistoryContentPanel = new UI::Panel("HistoryContentUI", { 0.10f, 0.10f, 0.11f, 1.0f });
        m_HistoryContentPanel->SetAnchorMin(0.0f, 0.0f);
        m_HistoryContentPanel->SetAnchorMax(1.0f, 1.0f);
        m_HistoryContentPanel->SetOffsetMin(0.0f, 24.0f);
        m_HistoryContentPanel->SetOffsetMax(0.0f, 0.0f);
        m_HistoryContentPanel->SetClipToBounds(true);
        m_HistoryPanel->AddChild(m_HistoryContentPanel);
        m_HistoryPanels.push_back(m_HistoryPanel);
        m_HistoryContentPanels.push_back(m_HistoryContentPanel);
        MarkHistoryPanelDirty();

        m_ConsolePanel = new UI::ConsolePanel("ConsolePanelUI");
        m_ConsolePanel->SetAnchorMin(0.8f, 0.68f);
        m_ConsolePanel->SetAnchorMax(1.0f, 1.0f);
        m_ConsolePanel->SetOffsetMin(0.0f, 0.0f);
        m_ConsolePanel->SetOffsetMax(0.0f, 0.0f);
        m_RootUI->AddChild(m_ConsolePanel);
        m_ConsolePanels.push_back(m_ConsolePanel);

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
        m_ViewportWindows.push_back(m_ViewportWindow);
        m_ViewportWidgets.push_back(m_ViewportWidget);

        m_GameWindow = new UI::WindowPanel("GameWindowUI", "Game View");
        m_GameWindow->SetAnchorMin(0.2f, 0.55f); m_GameWindow->SetAnchorMax(0.8f, 0.75f);
        m_GameWindow->SetOffsetMin(0.0f, 0.0f); m_GameWindow->SetOffsetMax(0.0f, 0.0f);
        m_RootUI->AddChild(m_GameWindow);

        RendererHandle gameTex = m_GameFramebuffer->GetColorAttachmentRendererID(0);
        m_GameViewWidget = new UI::ImageWidget("GameViewWidget", gameTex);
        m_GameViewWidget->SetAnchorMin(0.0f, 0.0f); m_GameViewWidget->SetAnchorMax(1.0f, 1.0f);
        m_GameViewWidget->SetOffsetMin(0.0f, 24.0f); m_GameViewWidget->SetOffsetMax(0.0f, 0.0f);
        m_GameWindow->AddChild(m_GameViewWidget);
        m_GameWindows.push_back(m_GameWindow);
        m_GameViewWidgets.push_back(m_GameViewWidget);

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
        m_AssetBrowserPanels.push_back(m_AssetBrowserPanel);

        m_FileDropdownPanel = new UI::Panel("FileDropdownUI", { 0.18f, 0.18f, 0.18f, 1.0f });
        m_FileDropdownPanel->SetVisible(false);
        m_FileDropdownPanel->SetBlockMouseEvents(true);
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

        m_EditDropdownPanel = new UI::Panel("EditDropdownUI", { 0.18f, 0.18f, 0.18f, 1.0f });
        m_EditDropdownPanel->SetVisible(false);
        m_EditDropdownPanel->SetBlockMouseEvents(true);
        m_EditDropdownPanel->SetAnchorMin(0.0f, 0.0f); m_EditDropdownPanel->SetAnchorMax(0.0f, 0.0f);
        m_EditDropdownPanel->SetOffsetMin(60.0f, 48.0f); m_EditDropdownPanel->SetOffsetMax(230.0f, 48.0f + 100.0f);
        m_RootUI->AddChild(m_EditDropdownPanel);

        m_BtnEditUndo = new UI::Button("BtnEditUndo", "Undo    Ctrl+Z");
        m_BtnEditUndo->SetAnchorMin(0.0f, 0.0f); m_BtnEditUndo->SetAnchorMax(1.0f, 0.0f);
        m_BtnEditUndo->SetOffsetMin(0.0f, 0.0f); m_BtnEditUndo->SetOffsetMax(0.0f, 25.0f);
        m_EditDropdownPanel->AddChild(m_BtnEditUndo);

        m_BtnEditRedo = new UI::Button("BtnEditRedo", "Redo    Ctrl+Y");
        m_BtnEditRedo->SetAnchorMin(0.0f, 0.0f); m_BtnEditRedo->SetAnchorMax(1.0f, 0.0f);
        m_BtnEditRedo->SetOffsetMin(0.0f, 25.0f); m_BtnEditRedo->SetOffsetMax(0.0f, 50.0f);
        m_EditDropdownPanel->AddChild(m_BtnEditRedo);

        m_BtnEditDuplicate = new UI::Button("BtnEditDuplicate", "Duplicate Ctrl+D");
        m_BtnEditDuplicate->SetAnchorMin(0.0f, 0.0f); m_BtnEditDuplicate->SetAnchorMax(1.0f, 0.0f);
        m_BtnEditDuplicate->SetOffsetMin(0.0f, 50.0f); m_BtnEditDuplicate->SetOffsetMax(0.0f, 75.0f);
        m_EditDropdownPanel->AddChild(m_BtnEditDuplicate);

        m_BtnProjectSettings = new UI::Button("BtnProjectSettings", "Project Settings...");
        m_BtnProjectSettings->SetAnchorMin(0.0f, 0.0f); m_BtnProjectSettings->SetAnchorMax(1.0f, 0.0f);
        m_BtnProjectSettings->SetOffsetMin(0.0f, 75.0f); m_BtnProjectSettings->SetOffsetMax(0.0f, 100.0f);
        m_EditDropdownPanel->AddChild(m_BtnProjectSettings);

        m_WindowDropdownPanel = new UI::Panel("WindowDropdownUI", { 0.18f, 0.18f, 0.18f, 1.0f });
        m_WindowDropdownPanel->SetVisible(false);
        m_WindowDropdownPanel->SetBlockMouseEvents(true);
        m_WindowDropdownPanel->SetAnchorMin(0.0f, 0.0f);
        m_WindowDropdownPanel->SetAnchorMax(0.0f, 0.0f);
        m_WindowDropdownPanel->SetOffsetMin(120.0f, 48.0f);
        m_WindowDropdownPanel->SetOffsetMax(320.0f, 48.0f + 200.0f);
        m_RootUI->AddChild(m_WindowDropdownPanel);

        const char* windowNames[] =
        {
            "Hierarchy",
            "Inspector",
            "Scene View",
            "Game View",
            "Asset Browser",
            "History",
            "Console",
            "Project Settings..."
        };

        for (int i = 0; i < 8; ++i)
        {
            auto* button = new UI::Button(std::string("BtnWindowMenu") + std::to_string(i), windowNames[i]);
            button->SetAnchorMin(0.0f, 0.0f);
            button->SetAnchorMax(1.0f, 0.0f);
            button->SetOffsetMin(0.0f, (float)i * 25.0f);
            button->SetOffsetMax(0.0f, ((float)i + 1.0f) * 25.0f);
            m_WindowDropdownPanel->AddChild(button);
            m_WindowMenuButtons.push_back(button);
        }

        m_ProjectSettingsPanel = new UI::ProjectSettingsPanel("ProjectSettingsPanelUI");
        m_ProjectSettingsPanel->SetSettings(&m_ProjectSettings);
        m_ProjectSettingsPanel->SetDockingEnabled(false);
        m_ProjectSettingsPanel->SetVisible(false);
        m_ProjectSettingsPanel->SetBlockMouseEvents(true);
        m_ProjectSettingsPanel->SetAnchorMin(0.0f, 0.0f);
        m_ProjectSettingsPanel->SetAnchorMax(1.0f, 1.0f);
        m_ProjectSettingsPanel->SetOffsetMin(0.0f, 0.0f);
        m_ProjectSettingsPanel->SetOffsetMax(0.0f, 0.0f);
        m_ProjectSettingsPanel->SetCallbacks(
            [this]() { SetCurrentSceneAsProjectStartScene(); },
            [this]() { OpenProjectStartScene(); },
            [this]() { SaveProjectSettings(); },
            [this]() { ApplyProjectGameResolution(); });
        m_ProjectSettingsPanel->SetKeyBindingPickerCallback(
            [this](UI::KeyBindingInput* targetInput) { OpenKeyBindingPickerWindow(targetInput); });

        m_ObjectContextMenuPanel = new UI::Panel("ObjectContextMenu", { 0.14f, 0.14f, 0.15f, 1.0f });
        m_ObjectContextMenuPanel->SetVisible(false);
        m_ObjectContextMenuPanel->SetBlockMouseEvents(true);
        m_ObjectContextMenuPanel->SetAnchorMin(0.0f, 0.0f);
        m_ObjectContextMenuPanel->SetAnchorMax(0.0f, 0.0f);
        m_ObjectContextMenuPanel->SetOffsetMin(0.0f, 0.0f);
        m_ObjectContextMenuPanel->SetOffsetMax(160.0f, 130.0f);
        m_RootUI->AddChild(m_ObjectContextMenuPanel);

        m_BtnCreateEmpty = new UI::Button("BtnCreateEmpty", "Create Empty");
        m_BtnCreateEmpty->SetAnchorMin(0.0f, 0.0f); m_BtnCreateEmpty->SetAnchorMax(1.0f, 0.0f);
        m_BtnCreateEmpty->SetOffsetMin(0.0f, 0.0f); m_BtnCreateEmpty->SetOffsetMax(0.0f, 26.0f);
        m_ObjectContextMenuPanel->AddChild(m_BtnCreateEmpty);

        m_BtnCreateMeshObject = new UI::Button("BtnCreateMeshObject", "Create Mesh Object >");
        m_BtnCreateMeshObject->SetAnchorMin(0.0f, 0.0f); m_BtnCreateMeshObject->SetAnchorMax(1.0f, 0.0f);
        m_BtnCreateMeshObject->SetOffsetMin(0.0f, 26.0f); m_BtnCreateMeshObject->SetOffsetMax(0.0f, 52.0f);
        m_ObjectContextMenuPanel->AddChild(m_BtnCreateMeshObject);

        m_BtnCreateLight = new UI::Button("BtnCreateLight", "Create Light");
        m_BtnCreateLight->SetAnchorMin(0.0f, 0.0f); m_BtnCreateLight->SetAnchorMax(1.0f, 0.0f);
        m_BtnCreateLight->SetOffsetMin(0.0f, 52.0f); m_BtnCreateLight->SetOffsetMax(0.0f, 78.0f);
        m_ObjectContextMenuPanel->AddChild(m_BtnCreateLight);

        m_BtnCreateCamera = new UI::Button("BtnCreateCamera", "Create Camera");
        m_BtnCreateCamera->SetAnchorMin(0.0f, 0.0f); m_BtnCreateCamera->SetAnchorMax(1.0f, 0.0f);
        m_BtnCreateCamera->SetOffsetMin(0.0f, 78.0f); m_BtnCreateCamera->SetOffsetMax(0.0f, 104.0f);
        m_ObjectContextMenuPanel->AddChild(m_BtnCreateCamera);

        m_BtnCreatePrefab = new UI::Button("BtnCreatePrefab", "Create Prefab");
        m_BtnCreatePrefab->SetAnchorMin(0.0f, 0.0f); m_BtnCreatePrefab->SetAnchorMax(1.0f, 0.0f);
        m_BtnCreatePrefab->SetOffsetMin(0.0f, 104.0f); m_BtnCreatePrefab->SetOffsetMax(0.0f, 130.0f);
        m_ObjectContextMenuPanel->AddChild(m_BtnCreatePrefab);

        m_BtnDeleteObject = new UI::Button("BtnDeleteObject", "Delete Selected");
        m_BtnDeleteObject->SetAnchorMin(0.0f, 0.0f); m_BtnDeleteObject->SetAnchorMax(1.0f, 0.0f);
        m_BtnDeleteObject->SetOffsetMin(0.0f, 130.0f); m_BtnDeleteObject->SetOffsetMax(0.0f, 156.0f);
        m_ObjectContextMenuPanel->AddChild(m_BtnDeleteObject);

        // 메시 프리미티브는 별도 하위 메뉴로 묶는다.
        m_MeshObjectSubmenuPanel = new UI::Panel("MeshObjectSubmenu", { 0.14f, 0.14f, 0.15f, 1.0f });
        m_MeshObjectSubmenuPanel->SetVisible(false);
        m_MeshObjectSubmenuPanel->SetBlockMouseEvents(true);
        m_MeshObjectSubmenuPanel->SetAnchorMin(0.0f, 0.0f);
        m_MeshObjectSubmenuPanel->SetAnchorMax(0.0f, 0.0f);
        m_MeshObjectSubmenuPanel->SetOffsetMin(0.0f, 0.0f);
        m_MeshObjectSubmenuPanel->SetOffsetMax(160.0f, 182.0f);
        m_RootUI->AddChild(m_MeshObjectSubmenuPanel);

        m_BtnCreateCube = new UI::Button("BtnCreateCube", "Create Cube");
        m_BtnCreateCube->SetAnchorMin(0.0f, 0.0f); m_BtnCreateCube->SetAnchorMax(1.0f, 0.0f);
        m_BtnCreateCube->SetOffsetMin(0.0f, 0.0f); m_BtnCreateCube->SetOffsetMax(0.0f, 26.0f);
        m_MeshObjectSubmenuPanel->AddChild(m_BtnCreateCube);

        m_BtnCreateSphere = new UI::Button("BtnCreateSphere", "Create Sphere");
        m_BtnCreateSphere->SetAnchorMin(0.0f, 0.0f); m_BtnCreateSphere->SetAnchorMax(1.0f, 0.0f);
        m_BtnCreateSphere->SetOffsetMin(0.0f, 26.0f); m_BtnCreateSphere->SetOffsetMax(0.0f, 52.0f);
        m_MeshObjectSubmenuPanel->AddChild(m_BtnCreateSphere);

        m_BtnCreateCapsule = new UI::Button("BtnCreateCapsule", "Create Capsule");
        m_BtnCreateCapsule->SetAnchorMin(0.0f, 0.0f); m_BtnCreateCapsule->SetAnchorMax(1.0f, 0.0f);
        m_BtnCreateCapsule->SetOffsetMin(0.0f, 52.0f); m_BtnCreateCapsule->SetOffsetMax(0.0f, 78.0f);
        m_MeshObjectSubmenuPanel->AddChild(m_BtnCreateCapsule);

        m_BtnCreateCylinder = new UI::Button("BtnCreateCylinder", "Create Cylinder");
        m_BtnCreateCylinder->SetAnchorMin(0.0f, 0.0f); m_BtnCreateCylinder->SetAnchorMax(1.0f, 0.0f);
        m_BtnCreateCylinder->SetOffsetMin(0.0f, 78.0f); m_BtnCreateCylinder->SetOffsetMax(0.0f, 104.0f);
        m_MeshObjectSubmenuPanel->AddChild(m_BtnCreateCylinder);

        m_BtnCreatePlane = new UI::Button("BtnCreatePlane", "Create Plane");
        m_BtnCreatePlane->SetAnchorMin(0.0f, 0.0f); m_BtnCreatePlane->SetAnchorMax(1.0f, 0.0f);
        m_BtnCreatePlane->SetOffsetMin(0.0f, 104.0f); m_BtnCreatePlane->SetOffsetMax(0.0f, 130.0f);
        m_MeshObjectSubmenuPanel->AddChild(m_BtnCreatePlane);

        m_BtnCreateQuad = new UI::Button("BtnCreateQuad", "Create Quad");
        m_BtnCreateQuad->SetAnchorMin(0.0f, 0.0f); m_BtnCreateQuad->SetAnchorMax(1.0f, 0.0f);
        m_BtnCreateQuad->SetOffsetMin(0.0f, 130.0f); m_BtnCreateQuad->SetOffsetMax(0.0f, 156.0f);
        m_MeshObjectSubmenuPanel->AddChild(m_BtnCreateQuad);

        m_BtnCreateTorus = new UI::Button("BtnCreateTorus", "Create Torus");
        m_BtnCreateTorus->SetAnchorMin(0.0f, 0.0f); m_BtnCreateTorus->SetAnchorMax(1.0f, 0.0f);
        m_BtnCreateTorus->SetOffsetMin(0.0f, 156.0f); m_BtnCreateTorus->SetOffsetMax(0.0f, 182.0f);
        m_MeshObjectSubmenuPanel->AddChild(m_BtnCreateTorus);

        // --- 버튼 & 툴바 콜백 등록 ---
        m_BtnFileMenu->SetOnClick([this]()
            {
                m_FileDropdownPanel->SetVisible(!m_FileDropdownPanel->IsVisible());
                m_EditDropdownPanel->SetVisible(false);
                m_WindowDropdownPanel->SetVisible(false);
                BringEditorOverlaysToFront();
            });
        m_BtnEditMenu->SetOnClick([this]()
            {
                m_EditDropdownPanel->SetVisible(!m_EditDropdownPanel->IsVisible());
                m_FileDropdownPanel->SetVisible(false);
                m_WindowDropdownPanel->SetVisible(false);
                BringEditorOverlaysToFront();
            });
        m_BtnWindowMenu->SetOnClick([this]()
            {
                m_WindowDropdownPanel->SetVisible(!m_WindowDropdownPanel->IsVisible());
                m_FileDropdownPanel->SetVisible(false);
                m_EditDropdownPanel->SetVisible(false);
                BringEditorOverlaysToFront();
            });

        for (size_t i = 0; i < m_WindowMenuButtons.size(); ++i)
        {
            m_WindowMenuButtons[i]->SetOnClick([this, i]()
                {
                    m_WindowDropdownPanel->SetVisible(false);
                    OpenEditorWindow((int)i);
                });
        }
        m_BtnOpen->SetOnClick([this]() { m_FileDropdownPanel->SetVisible(false); OpenScene(); });
        m_BtnSave->SetOnClick([this]() { m_FileDropdownPanel->SetVisible(false); SaveScene(); });
        m_BtnSaveAs->SetOnClick([this]() { m_FileDropdownPanel->SetVisible(false); SaveSceneAs(); });
        m_BtnSavePrefab->SetOnClick([this]() { m_FileDropdownPanel->SetVisible(false); SaveSelectedPrefab(); });
        m_BtnInstantiatePrefab->SetOnClick([this]() { m_FileDropdownPanel->SetVisible(false); InstantiatePrefab(); });
        m_BtnExit->SetOnClick([this]() { CCEngine::Application::Get()->GetWindow().SetShouldClose(true); });
        m_BtnEditUndo->SetOnClick([this]() { m_EditDropdownPanel->SetVisible(false); m_UndoManager.Undo(); });
        m_BtnEditRedo->SetOnClick([this]() { m_EditDropdownPanel->SetVisible(false); m_UndoManager.Redo(); });
        m_BtnEditDuplicate->SetOnClick([this]() { m_EditDropdownPanel->SetVisible(false); DuplicateSelectedObject(); });
        m_BtnProjectSettings->SetOnClick([this]()
            {
                m_EditDropdownPanel->SetVisible(false);
                OpenProjectSettingsWindow();
                BringEditorOverlaysToFront();
            });
        m_BtnCreateEmpty->SetOnClick([this]() { CreateEmptyObject(); HideObjectContextMenu(); });
        m_BtnCreateMeshObject->SetOnClick([this]() { ShowMeshObjectSubmenu(); });
        m_BtnCreateLight->SetOnClick([this]() { CreateLightObject(); HideObjectContextMenu(); });
        m_BtnCreateCamera->SetOnClick([this]() { CreateCameraObject(); HideObjectContextMenu(); });
        m_BtnCreatePrefab->SetOnClick([this]() { SaveSelectedPrefab(); HideObjectContextMenu(); });
        m_BtnCreateCube->SetOnClick([this]() { CreatePrimitiveObject("Cube", (int)MeshComponent::MeshType::Cube); HideObjectContextMenu(); });
        m_BtnCreateSphere->SetOnClick([this]() { CreatePrimitiveObject("Sphere", (int)MeshComponent::MeshType::Sphere); HideObjectContextMenu(); });
        m_BtnCreateCapsule->SetOnClick([this]() { CreatePrimitiveObject("Capsule", (int)MeshComponent::MeshType::Capsule); HideObjectContextMenu(); });
        m_BtnCreateCylinder->SetOnClick([this]() { CreatePrimitiveObject("Cylinder", (int)MeshComponent::MeshType::Cylinder); HideObjectContextMenu(); });
        m_BtnCreatePlane->SetOnClick([this]() { CreatePrimitiveObject("Plane", (int)MeshComponent::MeshType::Plane); HideObjectContextMenu(); });
        m_BtnCreateQuad->SetOnClick([this]() { CreatePrimitiveObject("Quad", (int)MeshComponent::MeshType::Quad); HideObjectContextMenu(); });
        m_BtnCreateTorus->SetOnClick([this]() { CreatePrimitiveObject("Torus", (int)MeshComponent::MeshType::Torus); HideObjectContextMenu(); });
        m_BtnDeleteObject->SetOnClick([this]() { DeleteSelectedObject(); HideObjectContextMenu(); });

        m_BtnPlay->SetOnClick([this]() {
            CCEngine::SceneState state = m_ActiveScene->GetState();
            if (state == CCEngine::SceneState::Edit) {
                m_UndoManager.ClearTransformHistory();
                m_UndoManager.ClearSceneStructureHistory();
                m_EditorScene = m_ActiveScene;
                m_ActiveScene = CCEngine::Scene::Copy(m_EditorScene);
                m_ActiveScene->OnRuntimeStart();
                m_ActiveScene->SetSceneState(CCEngine::SceneState::Play);
                for (UI::HierarchyPanel* hierarchy : m_HierarchyPanels)
                {
                    if (hierarchy)
                    {
                        hierarchy->SetContext(m_ActiveScene);
                        hierarchy->Refresh();
                    }
                }
                m_BtnPlay->SetActive(true);
                m_BtnPause->SetActive(false);
            }
            else if (state == CCEngine::SceneState::Play) {
                m_ActiveScene->OnRuntimeStop();
                delete m_ActiveScene;
                m_ActiveScene = m_EditorScene;
                m_EditorScene = nullptr;
                m_UndoManager.ClearTransformHistory();
                m_UndoManager.ClearSceneStructureHistory();
                m_ActiveScene->SetSceneState(CCEngine::SceneState::Edit);
                for (UI::HierarchyPanel* hierarchy : m_HierarchyPanels)
                {
                    if (hierarchy)
                    {
                        hierarchy->SetContext(m_ActiveScene);
                        hierarchy->Refresh();
                    }
                }
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
                m_UndoManager.ClearTransformHistory();
                m_UndoManager.ClearSceneStructureHistory();
                m_ActiveScene->SetSceneState(CCEngine::SceneState::Edit);
                for (UI::HierarchyPanel* hierarchy : m_HierarchyPanels)
                {
                    if (hierarchy)
                    {
                        hierarchy->SetContext(m_ActiveScene);
                        hierarchy->Refresh();
                    }
                }
                m_BtnPlay->SetActive(false);
                m_BtnPause->SetActive(false);
            }
            });

        CCEngine::Application::Get()->GetWindow().SetRootUI(m_RootUI);
    }
}
