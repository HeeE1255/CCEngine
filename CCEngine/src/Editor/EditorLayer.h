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
#include <vector>

// 자체 UI 시스템 헤더
#include "UI/Panel.h"
#include "UI/ImageWidget.h"
#include "UI/Button.h"
#include "UI/AssetBrowserPanel.h"
#include "UI/WindowPanel.h"
#include "UI/VBoxContainer.h"
#include "UI/HierarchyPanel.h"
#include "UI/InspectorPanel.h"
#include "GizmoSystem.h"

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
        void OpenScene(const std::string& filepath);
        void LoadSceneAdditive(const std::string& filepath);
        void SaveSelectedPrefab();
        void InstantiatePrefab();
        void InstantiatePrefab(const std::string& filepath);
        void ImportModelAsset(const std::string& filepath);
        void HandleAssetDropped(const std::string& filepath, const std::string& assetType);
        void HandleShortcuts();
        void TrackTransformUndo();
        void CommitPendingTransformUndo();
        void UndoTransform();
        void RedoTransform();
        void SeekTransformHistory(size_t targetAppliedCount);
        void ClearTransformUndoHistory();
        void RebuildHistoryPanel();
        void MarkHistoryPanelDirty();

    private:
        void BuildEditorUI();
        void RefreshHierarchy();

        // Transform Undo/Redo에서 한 순간의 Transform 상태를 저장하는 스냅샷.
        // Before/After 두 개의 스냅샷을 비교하거나 적용해서 되돌리기/다시하기를 수행한다.
        struct TransformSnapshot
        {
            DirectX::XMFLOAT3 Translation = { 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 Rotation = { 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 Scale = { 1.0f, 1.0f, 1.0f };
            DirectX::XMFLOAT3 EulerRotation = { 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT4 QuaternionRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
        };

        // 하나의 Undo 작업 단위.
        // 특정 엔티티가 Before 상태에서 After 상태로 바뀐 기록이다.
        struct TransformUndoCommand
        {
            entt::entity Entity = entt::null;
            TransformSnapshot Before;
            TransformSnapshot After;
        };

        TransformSnapshot CaptureTransform(Entity entity) const;
        void ApplyTransform(entt::entity entityHandle, const TransformSnapshot& snapshot);
        bool IsValidTransformEntity(entt::entity entityHandle) const;
        static bool SameTransformSnapshot(const TransformSnapshot& a, const TransformSnapshot& b);

        bool m_NeedsHierarchyRefresh = false; // 재조립 트리거 (메모리 뻑 방지용)
        std::unordered_set<entt::entity> m_ExpandedNodes; // 열려있는(Expanded) 엔티티들의 ID를 기억하는 장부

        // 적용된 작업은 UndoStack, 되돌린 작업은 RedoStack에 저장한다.
        // History 패널은 이 두 스택을 합쳐서 전체 작업 타임라인처럼 보여준다.
        std::vector<TransformUndoCommand> m_TransformUndoStack;
        std::vector<TransformUndoCommand> m_TransformRedoStack;

        // 드래그 중 계속 변하는 Transform을 매 프레임 작업으로 쌓지 않고,
        // 마우스를 놓을 때 하나의 작업으로 확정하기 위한 임시 기록이다.
        TransformUndoCommand m_PendingTransformUndo;
        TransformSnapshot m_LastObservedTransform;
        entt::entity m_ObservedTransformEntity = entt::null;
        bool m_HasLastObservedTransform = false;
        bool m_HasPendingTransformUndo = false;
        bool m_IsApplyingTransformUndoRedo = false;
        bool m_HistoryPanelDirty = false;

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
        UI::Button* m_BtnSavePrefab = nullptr;
        UI::Button* m_BtnInstantiatePrefab = nullptr;
        UI::Button* m_BtnExit = nullptr;

        // 3. 좌/우 패널
        UI::HierarchyPanel* m_HierarchyPanel = nullptr;
        UI::InspectorPanel* m_InspectorPanel = nullptr;
        UI::AssetBrowserPanel* m_AssetBrowserPanel = nullptr;
        UI::WindowPanel* m_HistoryPanel = nullptr;
        UI::Panel* m_HistoryContentPanel = nullptr;
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

        GizmoSystem m_GizmoSystem;

    };

}
