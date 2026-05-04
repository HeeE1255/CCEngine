#pragma once
#include "Core.h"
#include "Core/Layer.h"
#include "Scene/Scene.h"
#include "Renderer/Framebuffer.h"
#include "Editor/EditorCamera.h"

//#include "imgui.h" 
//#include "ImGuizmo.h"
#include <DirectXMath.h>
#include <string>
#include <unordered_set>

// 자체 UI 시스템 헤더
#include "UI/Panel.h"
#include "UI/ImageWidget.h"
#include "UI/Button.h"
#include "UI/WindowPanel.h"
#include "UI/VBoxContainer.h"
#include "UI/HierarchyPanel.h"
#include "UI/InspectorPanel.h"

namespace CCEngine {

    class CC_API EditorLayer : public Layer
    {
    public:
        EditorLayer();
        virtual ~EditorLayer() = default;

        virtual void OnAttach() override;
        virtual void OnDetach() override;
        virtual void OnUpdate(float deltaTime) override;
        virtual void OnEvent(Event& e) override;
        virtual void OnImGuiRender() override;

    private:
        void SaveScene();
        void SaveSceneAs();
        void OpenScene();
        void HandleShortcuts();

    private:
        void DrawEntityNode(CCEngine::Entity entity, float depth);
        void RefreshHierarchy();
        bool m_NeedsHierarchyRefresh = false; // 재조립 트리거 (메모리 뻑 방지용)
        std::unordered_set<entt::entity> m_ExpandedNodes; // 열려있는(Expanded) 엔티티들의 ID를 기억하는 장부

        Scene* m_ActiveScene = nullptr;
        Scene* m_EditorScene = nullptr;
        Framebuffer* m_Framebuffer = nullptr;
        Framebuffer* m_GameFramebuffer = nullptr;

        DirectX::XMFLOAT2 m_ViewportSize = { 1280.f, 720.f };
        DirectX::XMFLOAT2 m_GameViewportSize = { 1280.f, 720.f };
        EditorCamera m_Camera;

        float m_ClearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
        bool m_IsSPressedLastFrame = false;
        std::string m_CurrentScenePath = "";

        int m_GizmoType = 0;

        // ==========================================
        // [자체 UI 시스템 위젯 트리 포인터들]
        // ==========================================
        UI::Panel* m_RootUI = nullptr;

        // 1. 최상단 타이틀 바 (OS 윈도우 조작용)
        UI::Panel* m_TitleBarPanel = nullptr;
        UI::Button* m_BtnCloseMain = nullptr;

        // 2. 메뉴바 & 드롭다운 (기존)
        UI::Panel* m_MenuBarPanel = nullptr;
        UI::Button* m_BtnFileMenu = nullptr;       // 메뉴바의 "File" 버튼
        UI::Panel* m_FileDropdownPanel = nullptr;  // 드롭다운 창(배경)
        UI::Button* m_BtnOpen = nullptr;
        UI::Button* m_BtnSave = nullptr;
        UI::Button* m_BtnSaveAs = nullptr;
        UI::Button* m_BtnExit = nullptr;

        // 3. 좌/우 패널
        UI::HierarchyPanel* m_HierarchyPanel = nullptr;
        UI::InspectorPanel* m_InspectorPanel = nullptr;
        UI::VBoxContainer* m_HierarchyContainer = nullptr;


        // 4. 상단 툴바 (Play 로직)
        UI::Panel* m_ToolbarPanel = nullptr;
        UI::Button* m_BtnPlay = nullptr;
        UI::Button* m_BtnPause = nullptr;
        UI::Button* m_BtnStop = nullptr;

        // 5. 화면 위젯
        UI::WindowPanel* m_ViewportWindow = nullptr;
        UI::WindowPanel* m_GameWindow = nullptr;
        UI::ImageWidget* m_ViewportWidget = nullptr;
        UI::ImageWidget* m_GameViewWidget = nullptr;


    };

}