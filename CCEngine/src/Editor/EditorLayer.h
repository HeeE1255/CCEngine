#pragma once
#include "Core.h"
#include "Core/Layer.h"
#include "Scene/Scene.h"
#include "Renderer/Framebuffer.h"
#include "Editor/EditorCamera.h"
#include "Editor/AssetFileWatcher.h"
#include "Editor/AssetUndoManager.h"
#include "Editor/EditorUndoManager.h"
#include "Core/ConsoleLog.h"
#include "Core/ProjectSettings.h"

//#include "imgui.h" 
//#include "ImGuizmo.h"
#include <DirectXMath.h>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

// 자체 UI 시스템 헤더
#include "UI/Panel.h"
#include "UI/ImageWidget.h"
#include "UI/Button.h"
#include "UI/AssetBrowserPanel.h"
#include "UI/AssetReferenceValidatorPanel.h"
#include "UI/ConsolePanel.h"
#include "UI/ProjectSettingsPanel.h"
#include "UI/WindowPanel.h"
#include "UI/VBoxContainer.h"
#include "UI/HierarchyPanel.h"
#include "UI/InspectorPanel.h"
#include "UI/KeyBindingInput.h"
#include "GizmoSystem.h"

#include <chrono>

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
        void SaveSelectedPrefabToCurrentAssetFolder();
        void SavePrefabToDirectory(Entity entity, const std::filesystem::path& directory);
        void InstantiatePrefab();
        void InstantiatePrefab(const std::string& filepath);
        void ImportModelAsset(const std::string& filepath);
        void HandleAssetDropped(const std::string& filepath, const std::string& assetType, float mouseX, float mouseY);
        bool ApplyTextureAssetToEntity(Entity entity, const std::string& filepath);
        Entity PickSceneEntityAt(float mouseX, float mouseY) const;
        void ShowObjectContextMenu(float x, float y, bool allowDelete);
        void ShowMeshObjectSubmenu();
        void HideObjectContextMenu();
        Entity CreateEmptyObject();
        Entity CreatePrimitiveObject(const std::string& name, int meshType);
        Entity CreateLightObject();
        Entity CreateCameraObject();
        void DeleteSelectedObject();
        void DuplicateSelectedObject();
        void HandleShortcuts();
        void ConfigureUndoManager();
        UI::AssetBrowserPanel* FindAssetBrowserAt(float mouseX, float mouseY) const;
        void RememberActiveAssetBrowserFromMouse(float mouseX, float mouseY);
        bool TryUndoAssetOperation();
        bool TryRedoAssetOperation();
        void RebuildHistoryPanel();
        void MarkHistoryPanelDirty();
        void RefreshEditorSelection(Entity selected = {});
        void SetCurrentSceneAsProjectStartScene();
        void OpenProjectStartScene();
        void SaveProjectSettings();
        void ApplyProjectGameResolution();
        void ValidateAssetReferences(bool fullScan);
        void QueueAssetReferenceValidation();
        void ProcessAssetFileWatcher();
        void OpenEditorWindow(int windowKind);
        void OpenProjectSettingsWindow();
        void OpenAssetReferenceValidatorWindow();
        void OpenKeyBindingPickerWindow(UI::KeyBindingInput* targetInput);
        void BringEditorOverlaysToFront();

    private:
        void BuildEditorUI();
        void RefreshHierarchy();

        bool m_NeedsHierarchyRefresh = false; // 재조립 트리거 (메모리 뻑 방지용)
        std::unordered_set<entt::entity> m_ExpandedNodes; // 열려있는(Expanded) 엔티티들의 ID를 기억하는 장부

        EditorUndoManager m_UndoManager;
        AssetUndoManager m_AssetUndoManager;
        AssetFileWatcher m_AssetFileWatcher;
        bool m_HistoryPanelDirty = false;
        std::string m_LastHistoryPanelSignature;
        bool m_PendingAssetReferenceValidation = false;
        std::chrono::steady_clock::time_point m_AssetReferenceValidationRequestedAt = {};
        bool m_PendingAssetFileRefresh = false;
        std::chrono::steady_clock::time_point m_AssetFileRefreshRequestedAt = {};
        std::vector<std::filesystem::path> m_PendingAssetFileWatcherPaths;

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
        ProjectSettings m_ProjectSettings;

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
        UI::Button* m_BtnEditMenu = nullptr;
        UI::Button* m_BtnWindowMenu = nullptr;
        UI::Panel* m_FileDropdownPanel = nullptr;  // 드롭다운 창(배경)
        UI::Panel* m_EditDropdownPanel = nullptr;
        UI::Panel* m_WindowDropdownPanel = nullptr;
        UI::Button* m_BtnOpen = nullptr;
        UI::Button* m_BtnSave = nullptr;
        UI::Button* m_BtnSaveAs = nullptr;
        UI::Button* m_BtnSavePrefab = nullptr;
        UI::Button* m_BtnInstantiatePrefab = nullptr;
        UI::Button* m_BtnExit = nullptr;
        UI::Button* m_BtnEditUndo = nullptr;
        UI::Button* m_BtnEditRedo = nullptr;
        UI::Button* m_BtnEditDuplicate = nullptr;
        UI::Button* m_BtnProjectSettings = nullptr;
        std::vector<UI::Button*> m_WindowMenuButtons;

        // 3. 좌/우 패널
        UI::HierarchyPanel* m_HierarchyPanel = nullptr;
        UI::InspectorPanel* m_InspectorPanel = nullptr;
        UI::AssetBrowserPanel* m_AssetBrowserPanel = nullptr;
        UI::AssetBrowserPanel* m_ActiveAssetBrowserPanel = nullptr;
        UI::WindowPanel* m_HistoryPanel = nullptr;
        UI::ConsolePanel* m_ConsolePanel = nullptr;
        UI::ProjectSettingsPanel* m_ProjectSettingsPanel = nullptr;
        UI::AssetReferenceValidatorPanel* m_AssetReferenceValidatorPanel = nullptr;
        UI::Panel* m_HistoryContentPanel = nullptr;
        UI::VBoxContainer* m_HierarchyContainer = nullptr;


        // 4. 상단 툴바 (Play 로직)
        UI::Panel* m_ToolbarPanel = nullptr;
        UI::Button* m_BtnPlay = nullptr;
        UI::Button* m_BtnPause = nullptr;
        UI::Button* m_BtnStop = nullptr;
        UI::Panel* m_ObjectContextMenuPanel = nullptr;
        UI::Panel* m_MeshObjectSubmenuPanel = nullptr;
        UI::Button* m_BtnCreateEmpty = nullptr;
        UI::Button* m_BtnCreateMeshObject = nullptr;
        UI::Button* m_BtnCreateLight = nullptr;
        UI::Button* m_BtnCreateCamera = nullptr;
        UI::Button* m_BtnCreatePrefab = nullptr;
        UI::Button* m_BtnCreateCube = nullptr;
        UI::Button* m_BtnCreateSphere = nullptr;
        UI::Button* m_BtnCreateCapsule = nullptr;
        UI::Button* m_BtnCreateCylinder = nullptr;
        UI::Button* m_BtnCreatePlane = nullptr;
        UI::Button* m_BtnCreateQuad = nullptr;
        UI::Button* m_BtnCreateTorus = nullptr;
        UI::Button* m_BtnDeleteObject = nullptr;

        // 5. 화면 위젯
        UI::WindowPanel* m_ViewportWindow = nullptr;
        UI::WindowPanel* m_GameWindow = nullptr;
        UI::ImageWidget* m_ViewportWidget = nullptr;
        UI::ImageWidget* m_GameViewWidget = nullptr;
        std::vector<UI::ImageWidget*> m_ViewportWidgets;
        std::vector<UI::ImageWidget*> m_GameViewWidgets;
        std::vector<UI::WindowPanel*> m_ViewportWindows;
        std::vector<UI::WindowPanel*> m_GameWindows;
        std::vector<UI::WindowPanel*> m_HistoryPanels;
        std::vector<UI::HierarchyPanel*> m_HierarchyPanels;
        std::vector<UI::InspectorPanel*> m_InspectorPanels;
        std::vector<UI::AssetBrowserPanel*> m_AssetBrowserPanels;
        std::vector<UI::ConsolePanel*> m_ConsolePanels;
        std::vector<UI::Panel*> m_HistoryContentPanels;

        GizmoSystem m_GizmoSystem;

        Entity m_PrefabDragEntity = {};
        bool m_IsDraggingPrefabToAssetBrowser = false;
        float m_PrefabDragStartX = 0.0f;
        float m_PrefabDragStartY = 0.0f;

    };

}
