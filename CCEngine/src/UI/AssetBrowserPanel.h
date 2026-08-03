#pragma once

#include "Core.h"
#include "Editor/AssetUndoManager.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/MaterialAsset.h"
#include "UI/WindowPanel.h"
#include <filesystem>
#include <functional>
#include <chrono>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace CCEngine
{
    class Mesh;
    class Texture2D;

    namespace UI
    {
        class CC_API AssetBrowserPanel : public WindowPanel
        {
        public:
            AssetBrowserPanel(const std::string& name = "AssetBrowser");
            AssetBrowserPanel(const AssetBrowserPanel&) = delete;
            AssetBrowserPanel& operator=(const AssetBrowserPanel&) = delete;

            void SetRootDirectory(const std::filesystem::path& rootDirectory);
            void Refresh(bool forceAssetScan = false);
            const std::filesystem::path& GetCurrentAssetDirectory() const { return m_CurrentDirectory; }
            bool IsDropTargetPoint(float mouseX, float mouseY) const { return IsContentPoint(mouseX, mouseY); }
            bool ImportExternalPaths(const std::vector<std::filesystem::path>& sourcePaths, float mouseX, float mouseY);

            void SetOnPrefabSelected(std::function<void(const std::string&)> callback) { m_OnPrefabSelected = callback; }
            void SetOnModelSelected(std::function<void(const std::string&)> callback) { m_OnModelSelected = callback; }
            void SetOnSceneSelected(std::function<void(const std::string&)> callback) { m_OnSceneSelected = callback; }
            void SetOnAssetSelected(std::function<void(const std::string&, const std::string&)> callback) { m_OnAssetSelected = std::move(callback); }
            void SetOnAssetDropped(std::function<void(const std::string&, const std::string&, float, float)> callback) { m_OnAssetDropped = callback; }
            void SetOnAssetDatabaseChanged(std::function<void()> callback) { m_OnAssetDatabaseChanged = std::move(callback); }
            void SetOnAssetHistoryChanged(std::function<void()> callback) { m_OnAssetHistoryChanged = std::move(callback); }
            void SetAssetUndoManager(AssetUndoManager* manager) { m_AssetUndoManager = manager; }
            void SetExternalWatcherActive(bool active) { m_ExternalWatcherActive = active; }
            void OnExternalAssetFilesChanged();
            void ApplyMaterialPreviewOverride(const std::filesystem::path& materialPath, const MaterialAsset& material);
            void ApplyMaterialPreviewCapture(const std::filesystem::path& materialPath, uint32_t width, uint32_t height, const std::vector<uint32_t>& pixels);
            void ApplyMaterialPreviewTexture(const std::filesystem::path& materialPath, RendererHandle previewTexture);
            std::vector<std::string> GetAssetHistoryLabels() const;
            size_t GetAppliedAssetHistoryCount() const;
            bool SeekAssetHistory(size_t targetAppliedCount);
            bool CanUndoAssetOperation() const;
            bool CanRedoAssetOperation() const;
            bool RequestAssetUndo();
            bool RequestAssetRedo();
            bool RunQualityRegressionChecks();

            virtual void UpdateLayout(const DirectX::XMFLOAT2& parentPos, const DirectX::XMFLOAT2& parentSize) override;
            virtual void OnUpdate(float deltaTime) override;
            virtual void OnRender() override;
            virtual bool OnEvent(Event& e) override;
            virtual bool WantsMouseCapture() const override { return WindowPanel::WantsMouseCapture() || m_IsDraggingScrollbar || m_IsDraggingTreeScrollbar || m_IsDraggingSplitter || m_IsDraggingAsset || m_IsDraggingSelectionBox || m_IsMouseDownOnEmptyContent; }

        private:
            enum class AssetType
            {
                Unknown = 0,
                Folder,
                Scene,
                Prefab,
                Material,
                Model,
                FbxMesh,
                Texture,
                Script
            };

            struct AssetEntry
            {
                std::filesystem::path Path;
                std::string DisplayName;
                AssetType Type = AssetType::Unknown;
                std::filesystem::path SourceAssetPath;
                int SubAssetIndex = -1;
                bool IsSubAsset = false;
                std::string SubAssetParentKey;
                int SubAssetOrder = -1;
                int SubAssetCount = 0;
            };

            struct FbxMeshInfo;
            struct MaterialPreviewCacheEntry;

            enum class TypeFilter
            {
                All = 0,
                Texture,
                Model,
                Material,
                Prefab,
                Scene,
                Script
            };

            enum class SortMode
            {
                Name = 0,
                Type,
                ModifiedTime
            };

            struct ToolbarMetrics
            {
                float ButtonY = 0.0f;
                float SearchX = 0.0f;
                float SearchW = 0.0f;
                float TypeX = 0.0f;
                float SortX = 0.0f;
                float MinusX = 0.0f;
                float PlusX = 0.0f;
            };

            AssetType GetAssetType(const std::filesystem::path& path) const;
            std::string GetTypeLabel(AssetType type) const;
            std::string GetTypeKey(AssetType type) const;
            std::string GetTypeFilterLabel(TypeFilter filter) const;
            std::string GetSortModeLabel(SortMode mode) const;
            std::string GetTypeFilterLabel() const;
            std::string GetSortModeLabel() const;
            void ActivateEntry(const AssetEntry& entry);
            bool DeleteSelectedAsset();
            bool RequestDeleteSelectedAsset();
            bool RecycleSelectedAsset();
            bool ReimportSelectedAssets();
            bool RefreshCurrentFolder(bool forceReimport);
            bool CreateFolderInCurrentDirectory();
            bool CreateMaterialInCurrentDirectory();
            bool RenameSelectedAsset(const std::string& newName);
            void BeginCreateFolder();
            void BeginRenameSelected();
            void BeginDeleteSelected();
            void CancelModal();
            bool ConfirmNameModal();
            bool ConfirmDeleteModal();
            void NavigateTo(const std::filesystem::path& directory);
            bool IsContentPoint(float mouseX, float mouseY) const;
            bool IsTreePoint(float mouseX, float mouseY) const;
            bool IsSplitterPoint(float mouseX, float mouseY) const;
            bool IsScrollbarPoint(float mouseX, float mouseY) const;
            int GetEntryIndexAt(float mouseX, float mouseY) const;
            bool GetEntryBounds(int index, float& x, float& y, float& w, float& h) const;
            int GetTreeIndexAt(float mouseX, float mouseY) const;
            bool IsTreeScrollbarPoint(float mouseX, float mouseY) const;
            bool IsContextMenuPoint(float mouseX, float mouseY) const;
            int GetContextMenuItemAt(float mouseX, float mouseY) const;
            bool IsNameModalPoint(float mouseX, float mouseY) const;
            bool IsDeleteModalPoint(float mouseX, float mouseY) const;
            bool IsPathInsideRoot(const std::filesystem::path& path, bool allowRoot) const;
            std::filesystem::path MakeUniquePath(const std::filesystem::path& directory, const std::string& baseName, const std::string& extension) const;
            bool IsSearchBoxPoint(float mouseX, float mouseY) const;
            bool IsTypeFilterButtonPoint(float mouseX, float mouseY) const;
            bool IsSortButtonPoint(float mouseX, float mouseY) const;
            bool IsTypeFilterDropdownPoint(float mouseX, float mouseY) const;
            bool IsSortDropdownPoint(float mouseX, float mouseY) const;
            int GetTypeFilterDropdownItemAt(float mouseX, float mouseY) const;
            int GetSortDropdownItemAt(float mouseX, float mouseY) const;
            ToolbarMetrics GetToolbarMetrics() const;
            void SetTypeFilter(TypeFilter filter);
            void SetSortMode(SortMode mode);
            void SetSearchQuery(const std::string& query);
            void ApplyFilter();
            bool EntryMatchesAdvancedFilter(const AssetEntry& entry, const std::string& textQuery, const std::string& extensionFilter, TypeFilter queryTypeFilter) const;
            void SortViewEntries();
            void AppendFbxSubAssetEntries(const AssetEntry& fbxEntry, const std::string& query);
            const std::vector<FbxMeshInfo>& GetFbxMeshInfos(const std::filesystem::path& path);
            bool IsFbxContainer(const AssetEntry& entry) const;
            bool IsVirtualSubAsset(const AssetEntry& entry) const;
            bool IsFbxExpandButtonPoint(int index, float mouseX, float mouseY) const;
            void ToggleFbxExpanded(const std::filesystem::path& path);
            bool IsEntrySelected(int index) const;
            void ClearSelection();
            void SelectSingle(int index);
            void ToggleSelection(int index);
            void SelectRange(int index);
            void UpdateSelectionBox(float mouseX, float mouseY);
            std::vector<AssetEntry> GetSelectedEntries() const;
            void BuildTreeEntries();
            void BuildTreeEntriesRecursive(const std::filesystem::path& directory, int depth);
            std::string GetTreeKey(const std::filesystem::path& path) const;
            bool TreeEntryHasChildren(const std::filesystem::path& path) const;
            void ToggleTreeFolder(const std::filesystem::path& path);
            bool MoveEntryToDirectory(const AssetEntry& entry, const std::filesystem::path& targetDirectory, AssetUndoManager::Command* undoCommand);
            bool MoveSelectedEntriesToDirectory(const std::filesystem::path& targetDirectory);
            void StepIconSize(int direction);
            void NotifyAssetDatabaseChanged();
            bool ShowSelectedEntryInFolder();
            bool RevealSelectedEntryInExplorer();
            Texture2D* GetAssetPreviewTexture(const AssetEntry& entry);
            const DirectX::XMFLOAT4* GetMaterialPreviewColor(const AssetEntry& entry);
            void PrepareMaterialPreviewRequests();
            void UpdateMaterialPreviewThumbnails();
            RendererHandle GetMaterialPreviewTexture(const AssetEntry& entry);
            void RenderMaterialPreviewThumbnail(MaterialPreviewCacheEntry& preview);
            void InvalidateMaterialPreviewCache(bool discardCapturedPixels);
            void DrawAssetPreview(const AssetEntry& entry, float x, float y, float size);
            void DrawMaterialPreview(const AssetEntry& entry, float x, float y, float size);
            void DrawFallbackAssetIcon(const AssetEntry& entry, float x, float y, float size);
            uint64_t ComputeDirectorySignature(const std::filesystem::path& directory) const;
            void UpdateDirectoryWatchState();
            void CheckExternalFileChanges();

            struct TreeEntry
            {
                std::filesystem::path Path;
                std::string DisplayName;
                int Depth = 0;
                bool HasChildren = false;
            };

            struct FbxMeshInfo
            {
                std::string Name;
                int MeshIndex = -1;
            };

            struct TexturePreviewJob
            {
                std::atomic<bool> IsLoading = false;
                std::atomic<bool> IsReady = false;
                std::atomic<bool> Failed = false;
                std::mutex Mutex;
                int Width = 0;
                int Height = 0;
                std::vector<uint32_t> Pixels;
                std::unique_ptr<Texture2D> Texture;
            };

            struct MaterialPreviewCacheEntry
            {
                DirectX::XMFLOAT4 AlbedoColor = { 0.62f, 0.62f, 0.66f, 1.0f };
                std::filesystem::path SourcePath;
                std::filesystem::file_time_type LastWriteTime = {};
                MaterialAsset Material;
                std::unique_ptr<Framebuffer> PreviewFramebuffer;
                std::unique_ptr<Texture2D> CapturedTexture;
                std::vector<uint32_t> CapturedPixels;
                int CapturedPixelWidth = 0;
                int CapturedPixelHeight = 0;
                bool CapturedFromInspector = false;
                bool Valid = false;
                bool Dirty = true;
                bool Rendered = false;
                bool CaptureFailed = false;
                bool CapturePending = false;
                uint8_t CaptureAttempts = 0;
            };

            struct BorrowedMaterialPreview
            {
                std::filesystem::path SourcePath;
                RendererHandle Texture = nullptr;
            };

            struct PrefabPreviewRenderItem
            {
                DirectX::XMMATRIX Transform = DirectX::XMMatrixIdentity();
                DirectX::XMFLOAT4 Color = { 1.0f, 1.0f, 1.0f, 1.0f };
                std::shared_ptr<Mesh> MeshData;
                std::shared_ptr<Texture2D> Texture;
            };

            struct PrefabPreviewCacheEntry
            {
                std::filesystem::file_time_type LastWriteTime = {};
                std::vector<PrefabPreviewRenderItem> Items;
                std::unique_ptr<Framebuffer> PreviewFramebuffer;
                std::unique_ptr<Texture2D> CapturedTexture;
                bool Valid = false;
                bool Dirty = true;
                bool Rendered = false;
                bool CaptureFailed = false;
            };

            void PushAssetUndoCommand(const AssetUndoManager::Command& command);
            void RefreshAfterAssetUndo(const std::filesystem::path& preferredDirectory);
            void UpdatePrefabPreviewThumbnails();
            RendererHandle GetPrefabPreviewTexture(const AssetEntry& entry);
            void RenderPrefabPreviewThumbnail(PrefabPreviewCacheEntry& preview);

        private:
            std::filesystem::path m_RootDirectory;
            std::filesystem::path m_CurrentDirectory;
            std::vector<AssetEntry> m_Entries;
            std::vector<AssetEntry> m_ViewEntries;
            std::vector<TreeEntry> m_TreeEntries;
            std::unordered_set<std::string> m_ExpandedTreeFolders;
            std::unordered_map<std::string, uint64_t> m_DirectoryWatchSignatures;
            std::vector<std::string> m_DirectoryWatchOrder;
            size_t m_DirectoryWatchCursor = 0;
            mutable std::unordered_map<std::string, bool> m_TreeChildCache;
            std::unordered_set<std::string> m_ExpandedFbxAssets;
            std::unordered_map<std::string, std::vector<FbxMeshInfo>> m_FbxMeshCache;
            std::unordered_map<std::string, std::shared_ptr<TexturePreviewJob>> m_TexturePreviewCache;
            std::vector<std::string> m_TexturePreviewOrder;
            std::unordered_map<std::string, MaterialPreviewCacheEntry> m_MaterialPreviewCache;
            std::unordered_map<std::string, BorrowedMaterialPreview> m_BorrowedMaterialPreviews;
            std::unordered_map<std::string, PrefabPreviewCacheEntry> m_PrefabPreviewCache;
            std::shared_ptr<Mesh> m_MaterialPreviewMesh;
            int m_TexturePreviewUploadsThisFrame = 0;
            int m_TexturePreviewLoadsStartedThisFrame = 0;
            ScrollState m_ScrollState;
            ScrollState m_TreeScrollState;

            int m_HoveredIndex = -1;
            int m_SelectedIndex = -1;
            int m_AnchorSelectedIndex = -1;
            std::unordered_set<int> m_SelectedIndices;
            bool m_IsDraggingScrollbar = false;
            bool m_IsDraggingTreeScrollbar = false;
            bool m_IsDraggingSplitter = false;
            float m_DragMouseStartY = 0.0f;
            float m_DragScrollStartY = 0.0f;
            float m_DragMouseStartX = 0.0f;
            float m_DragTreeScrollStartY = 0.0f;
            float m_DragSplitterStartWidth = 0.0f;
            int m_LastClickedIndex = -1;
            int m_LastTreeClickedIndex = -1;
            std::chrono::steady_clock::time_point m_LastClickTime = {};
            std::chrono::steady_clock::time_point m_LastTreeClickTime = {};
            bool m_IsMouseDownOnEntry = false;
            bool m_IsDraggingAsset = false;
            bool m_IsMouseDownOnEmptyContent = false;
            bool m_IsDraggingSelectionBox = false;
            bool m_SelectionBoxAdditive = false;
            int m_DragEntryIndex = -1;
            float m_DragStartX = 0.0f;
            float m_DragStartY = 0.0f;
            float m_SelectionBoxStartX = 0.0f;
            float m_SelectionBoxStartY = 0.0f;
            float m_SelectionBoxCurrentX = 0.0f;
            float m_SelectionBoxCurrentY = 0.0f;
            std::unordered_set<int> m_SelectionBeforeBox;
            bool m_ContextMenuVisible = false;
            float m_ContextMenuX = 0.0f;
            float m_ContextMenuY = 0.0f;
            float m_ContextMenuWidth = 150.0f;
            float m_ContextMenuHeight = 28.0f;
            std::vector<std::string> m_ContextMenuItems;
            std::string m_SearchQuery;
            bool m_SearchFocused = false;
            TypeFilter m_TypeFilter = TypeFilter::All;
            SortMode m_SortMode = SortMode::Name;
            bool m_TypeFilterDropdownVisible = false;
            bool m_SortDropdownVisible = false;
            bool m_ExternalWatcherActive = false;
            std::chrono::steady_clock::time_point m_LastExternalFileCheck = {};

            enum class ModalMode
            {
                None = 0,
                CreateFolder,
                Rename,
                ConfirmDelete
            };

            ModalMode m_ModalMode = ModalMode::None;
            std::string m_ModalText;
            std::filesystem::path m_ModalTargetPath;
            std::vector<std::filesystem::path> m_ModalTargetPaths;

            float m_ContentTop = 50.0f;
            float m_RowHeight = 26.0f;
            float m_RowGap = 2.0f;
            float m_TreeWidth = 190.0f;
            float m_MinTreeWidth = 110.0f;
            float m_MaxTreeWidth = 420.0f;
            int m_IconSizeStep = 1;

            std::function<void(const std::string&)> m_OnPrefabSelected = nullptr;
            std::function<void(const std::string&)> m_OnModelSelected = nullptr;
            std::function<void(const std::string&)> m_OnSceneSelected = nullptr;
            std::function<void(const std::string&, const std::string&)> m_OnAssetSelected = nullptr;
            std::function<void(const std::string&, const std::string&, float, float)> m_OnAssetDropped = nullptr;
            std::function<void()> m_OnAssetDatabaseChanged = nullptr;
            std::function<void()> m_OnAssetHistoryChanged = nullptr;

            AssetUndoManager* m_AssetUndoManager = nullptr;
        };
    }
}
