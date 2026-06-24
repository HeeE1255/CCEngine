#pragma once

#include "Core.h"
#include "UI/WindowPanel.h"
#include <filesystem>
#include <functional>
#include <chrono>
#include <string>
#include <vector>

namespace CCEngine
{
    namespace UI
    {
        class CC_API AssetBrowserPanel : public WindowPanel
        {
        public:
            AssetBrowserPanel(const std::string& name = "AssetBrowser");

            void SetRootDirectory(const std::filesystem::path& rootDirectory);
            void Refresh();
            const std::filesystem::path& GetCurrentAssetDirectory() const { return m_CurrentDirectory; }
            bool IsDropTargetPoint(float mouseX, float mouseY) const { return IsContentPoint(mouseX, mouseY); }

            void SetOnPrefabSelected(std::function<void(const std::string&)> callback) { m_OnPrefabSelected = callback; }
            void SetOnModelSelected(std::function<void(const std::string&)> callback) { m_OnModelSelected = callback; }
            void SetOnSceneSelected(std::function<void(const std::string&)> callback) { m_OnSceneSelected = callback; }
            void SetOnAssetDropped(std::function<void(const std::string&, const std::string&, float, float)> callback) { m_OnAssetDropped = callback; }

            virtual void UpdateLayout(const DirectX::XMFLOAT2& parentPos, const DirectX::XMFLOAT2& parentSize) override;
            virtual void OnRender() override;
            virtual bool OnEvent(Event& e) override;
            virtual bool WantsMouseCapture() const override { return WindowPanel::WantsMouseCapture() || m_IsDraggingScrollbar || m_IsDraggingAsset; }

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
            void NavigateTo(const std::filesystem::path& directory);
            bool IsContentPoint(float mouseX, float mouseY) const;
            bool IsScrollbarPoint(float mouseX, float mouseY) const;
            int GetEntryIndexAt(float mouseX, float mouseY) const;
            bool IsContextMenuPoint(float mouseX, float mouseY) const;

        private:
            std::filesystem::path m_RootDirectory;
            std::filesystem::path m_CurrentDirectory;
            std::vector<AssetEntry> m_Entries;
            ScrollState m_ScrollState;

            int m_HoveredIndex = -1;
            int m_SelectedIndex = -1;
            bool m_IsDraggingScrollbar = false;
            float m_DragMouseStartY = 0.0f;
            float m_DragScrollStartY = 0.0f;
            int m_LastClickedIndex = -1;
            std::chrono::steady_clock::time_point m_LastClickTime = {};
            bool m_IsMouseDownOnEntry = false;
            bool m_IsDraggingAsset = false;
            int m_DragEntryIndex = -1;
            float m_DragStartX = 0.0f;
            float m_DragStartY = 0.0f;
            bool m_ContextMenuVisible = false;
            float m_ContextMenuX = 0.0f;
            float m_ContextMenuY = 0.0f;
            float m_ContextMenuWidth = 120.0f;
            float m_ContextMenuHeight = 28.0f;

            float m_ContentTop = 50.0f;
            float m_RowHeight = 26.0f;
            float m_RowGap = 2.0f;

            std::function<void(const std::string&)> m_OnPrefabSelected = nullptr;
            std::function<void(const std::string&)> m_OnModelSelected = nullptr;
            std::function<void(const std::string&)> m_OnSceneSelected = nullptr;
            std::function<void(const std::string&, const std::string&, float, float)> m_OnAssetDropped = nullptr;
        };
    }
}
