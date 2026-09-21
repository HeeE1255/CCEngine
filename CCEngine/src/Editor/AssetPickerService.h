#pragma once

#include "Core.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace CCEngine
{
    class AssetUndoManager;

    namespace UI
    {
        class AssetBrowserPanel;
        class Panel;
    }

    class CC_API AssetPickerService
    {
    public:
        using PickCallback = std::function<bool(const std::filesystem::path&, const std::string&)>;

        struct HostCallbacks
        {
            std::function<void()> OnAssetDatabaseChanged;
            std::function<void()> OnAssetHistoryChanged;
        };

        void Configure(UI::Panel* rootUI,
            AssetUndoManager* undoManager,
            bool externalWatcherActive,
            const std::filesystem::path& rootDirectory,
            HostCallbacks callbacks);
        void Begin(const std::string& label, const std::vector<std::string>& acceptedTypes, PickCallback onPicked);
        void Update();
        void Cancel(bool clearFilter);
        void Shutdown();

        UI::AssetBrowserPanel* GetPanel() const { return m_Panel; }
        bool IsActive() const { return m_Request.Active; }
        void BringEmbeddedPanelToFront();

    private:
        struct Request
        {
            std::string Label;
            std::vector<std::string> AcceptedTypes;
            PickCallback OnPicked;
            bool Active = false;
        };

        void EnsurePanel();
        void OpenPanel();
        void HandleSelection(const std::filesystem::path& assetPath, const std::string& assetType);
        static std::string NormalizeType(std::string type);

        UI::Panel* m_RootUI = nullptr;
        UI::AssetBrowserPanel* m_Panel = nullptr;
        AssetUndoManager* m_UndoManager = nullptr;
        bool m_ExternalWatcherActive = false;
        std::filesystem::path m_RootDirectory;
        HostCallbacks m_HostCallbacks;
        Request m_Request;
    };
}
