#pragma once

#include "Core.h"
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
#include <vector>

namespace CCEngine
{
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

            void SetOnPrefabSelected(std::function<void(const std::string&)> callback) { m_OnPrefabSelected = callback; }
            void SetOnModelSelected(std::function<void(const std::string&)> callback) { m_OnModelSelected = callback; }
            void SetOnSceneSelected(std::function<void(const std::string&)> callback) { m_OnSceneSelected = callback; }
            void SetOnAssetDropped(std::function<void(const std::string&, const std::string&, float, float)> callback) { m_OnAssetDropped = callback; }

            virtual void UpdateLayout(const DirectX::XMFLOAT2& parentPos, const DirectX::XMFLOAT2& parentSize) override;
            virtual void OnRender() override;
            virtual bool OnEvent(Event& e) override;
            virtual bool WantsMouseCapture() const override { return WindowPanel::WantsMouseCapture() || m_IsDraggingScrollbar || m_IsDraggingTreeScrollbar || m_IsDraggingSplitter || m_IsDraggingAsset; }

        private:
            enum class AssetType
            {
                Unknown = 0,
                Folder,
                Scene,
                Prefab,
                Model,
                Texture
            };

            struct AssetEntry
            {
                std::filesystem::path Path;
                std::string DisplayName;
                AssetType Type = AssetType::Unknown;
            };

            AssetType GetAssetType(const std::filesystem::path& path) const;
            std::string GetTypeLabel(AssetType type) const;
            std::string GetTypeKey(AssetType type) const;
            void ActivateEntry(const AssetEntry& entry);
            bool DeleteSelectedAsset();
            bool RequestDeleteSelectedAsset();
            bool RecycleSelectedAsset();
            bool CreateFolderInCurrentDirectory();
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
            int GetTreeIndexAt(float mouseX, float mouseY) const;
            bool IsTreeScrollbarPoint(float mouseX, float mouseY) const;
            bool IsContextMenuPoint(float mouseX, float mouseY) const;
            int GetContextMenuItemAt(float mouseX, float mouseY) const;
            bool IsNameModalPoint(float mouseX, float mouseY) const;
            bool IsDeleteModalPoint(float mouseX, float mouseY) const;
            bool IsPathInsideRoot(const std::filesystem::path& path, bool allowRoot) const;
            std::filesystem::path MakeUniquePath(const std::filesystem::path& directory, const std::string& baseName, const std::string& extension) const;
            bool IsSearchBoxPoint(float mouseX, float mouseY) const;
            void SetSearchQuery(const std::string& query);
            void ApplyFilter();
            void BuildTreeEntries();
            void BuildTreeEntriesRecursive(const std::filesystem::path& directory, int depth);
            std::string GetTreeKey(const std::filesystem::path& path) const;
            bool TreeEntryHasChildren(const std::filesystem::path& path) const;
            void ToggleTreeFolder(const std::filesystem::path& path);
            bool MoveEntryToDirectory(const AssetEntry& entry, const std::filesystem::path& targetDirectory);
            void StepIconSize(int direction);
            Texture2D* GetTexturePreview(const AssetEntry& entry);
            void DrawAssetPreview(const AssetEntry& entry, float x, float y, float size);
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

        private:
            std::filesystem::path m_RootDirectory;
            std::filesystem::path m_CurrentDirectory;
            std::vector<AssetEntry> m_Entries;
            std::vector<AssetEntry> m_ViewEntries;
            std::vector<TreeEntry> m_TreeEntries;
            std::unordered_set<std::string> m_ExpandedTreeFolders;
            std::unordered_map<std::string, uint64_t> m_DirectoryWatchSignatures;
            mutable std::unordered_map<std::string, bool> m_TreeChildCache;
            std::unordered_map<std::string, std::shared_ptr<TexturePreviewJob>> m_TexturePreviewCache;
            std::vector<std::string> m_TexturePreviewOrder;
            int m_TexturePreviewUploadsThisFrame = 0;
            ScrollState m_ScrollState;
            ScrollState m_TreeScrollState;

            int m_HoveredIndex = -1;
            int m_SelectedIndex = -1;
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
            int m_DragEntryIndex = -1;
            float m_DragStartX = 0.0f;
            float m_DragStartY = 0.0f;
            bool m_ContextMenuVisible = false;
            float m_ContextMenuX = 0.0f;
            float m_ContextMenuY = 0.0f;
            float m_ContextMenuWidth = 150.0f;
            float m_ContextMenuHeight = 28.0f;
            std::vector<std::string> m_ContextMenuItems;
            std::string m_SearchQuery;
            bool m_SearchFocused = false;
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
            std::function<void(const std::string&, const std::string&, float, float)> m_OnAssetDropped = nullptr;
        };
    }
}
