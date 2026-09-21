#include "Editor/AssetPickerService.h"

#include "Application.h"
#include "Core/AssetDatabase.h"
#include "Core/ConsoleLog.h"
#include "Editor/AssetUndoManager.h"
#include "UI/AssetBrowserPanel.h"
#include "UI/Panel.h"

#include <algorithm>
#include <cctype>

namespace CCEngine
{
    void AssetPickerService::Configure(UI::Panel* rootUI,
        AssetUndoManager* undoManager,
        bool externalWatcherActive,
        const std::filesystem::path& rootDirectory,
        HostCallbacks callbacks)
    {
        m_RootUI = rootUI;
        m_UndoManager = undoManager;
        m_ExternalWatcherActive = externalWatcherActive;
        m_RootDirectory = rootDirectory;
        m_HostCallbacks = std::move(callbacks);

        if (m_Panel)
        {
            m_Panel->SetAssetUndoManager(m_UndoManager);
            m_Panel->SetExternalWatcherActive(m_ExternalWatcherActive);
            if (!m_RootDirectory.empty() && m_Panel->GetRootDirectory() != m_RootDirectory)
                m_Panel->SetRootDirectory(m_RootDirectory);
        }
    }

    void AssetPickerService::Begin(const std::string& label, const std::vector<std::string>& acceptedTypes, PickCallback onPicked)
    {
        m_Request = {};
        m_Request.Label = label;
        m_Request.AcceptedTypes.reserve(acceptedTypes.size());
        for (const std::string& type : acceptedTypes)
            m_Request.AcceptedTypes.push_back(NormalizeType(type));
        m_Request.OnPicked = std::move(onPicked);
        m_Request.Active = true;

        OpenPanel();
        ConsoleLog::Info("Asset Browser picker started: " + label);
    }

    void AssetPickerService::Update()
    {
        if (m_Request.Active && m_Panel && !m_Panel->IsVisible())
        {
            // 창의 X 버튼은 단순한 표시 전환이 아니라 선택 요청의 취소다.
            // 요청을 남겨 두면 이후 일반 브라우저 선택이 이전 슬롯에 들어갈 수 있다.
            Cancel(false);
        }
    }

    void AssetPickerService::Cancel(bool clearFilter)
    {
        m_Request = {};
        if (!m_Panel)
            return;

        if (clearFilter)
            m_Panel->ClearAssetPickerFilter();
        if (m_Panel->GetOwnerWindow())
        {
            Application::Get()->RequestCloseSecondaryWindowByUI(m_Panel);
            m_Panel->SetOwnerWindow(nullptr);
        }
        m_Panel->SetVisible(false);
    }

    void AssetPickerService::Shutdown()
    {
        if (m_Panel)
        {
            m_Panel->SetOnAssetSelected({});
            m_Panel->SetOnModelSelected({});
            m_Panel->SetOnPrefabSelected({});
            m_Panel->SetOnSceneSelected({});
            m_Panel->SetOnCodeAssetOpened({});
            m_Panel->SetOnAnimatorControllerOpened({});
            m_Panel->SetOnAssetDatabaseChanged({});
            m_Panel->SetOnAssetHistoryChanged({});
        }
        m_Request = {};
        m_Panel = nullptr;
        m_RootUI = nullptr;
    }

    void AssetPickerService::BringEmbeddedPanelToFront()
    {
        if (!m_Panel || !m_Panel->IsVisible() || m_Panel->GetOwnerWindow() || !m_RootUI)
            return;

        UI::Widget* branch = m_Panel;
        while (branch && branch != m_RootUI)
        {
            branch->BringToFront();
            branch = branch->GetParent();
        }
    }

    void AssetPickerService::EnsurePanel()
    {
        if (!m_RootUI)
            return;

        if (!m_Panel)
        {
            m_Panel = new UI::AssetBrowserPanel("AssetPickerPanel", "Select Asset");
            m_Panel->SetDockingEnabled(true);
            m_Panel->SetOnAssetSelected([this](const std::string& path, const std::string& type) { HandleSelection(path, type); });
            m_Panel->SetOnModelSelected([this](const std::string& path) { HandleSelection(path, "model"); });
            m_Panel->SetOnPrefabSelected([this](const std::string& path) { HandleSelection(path, "prefab"); });
            m_Panel->SetOnSceneSelected([this](const std::string& path) { HandleSelection(path, "scene"); });
            m_Panel->SetOnCodeAssetOpened([this](const std::string& path)
                {
                    const std::filesystem::path assetPath = path;
                    HandleSelection(assetPath, AssetDatabase::AssetKindToString(AssetDatabase::GetAssetKind(assetPath)));
                });
            m_Panel->SetOnAnimatorControllerOpened([this](const std::string& path) { HandleSelection(path, "animatorcontroller"); });
            m_Panel->SetOnAssetDatabaseChanged([this]()
                {
                    if (m_HostCallbacks.OnAssetDatabaseChanged)
                        m_HostCallbacks.OnAssetDatabaseChanged();
                });
            m_Panel->SetOnAssetHistoryChanged([this]()
                {
                    if (m_HostCallbacks.OnAssetHistoryChanged)
                        m_HostCallbacks.OnAssetHistoryChanged();
                });
            m_RootUI->AddChild(m_Panel);
        }
        else if (!m_Panel->GetOwnerWindow() && m_Panel->GetParent() != m_RootUI)
        {
            m_RootUI->AddChild(m_Panel);
        }

        m_Panel->SetAssetUndoManager(m_UndoManager);
        m_Panel->SetExternalWatcherActive(m_ExternalWatcherActive);
        if (!m_RootDirectory.empty() && m_Panel->GetRootDirectory() != m_RootDirectory)
            m_Panel->SetRootDirectory(m_RootDirectory);
    }

    void AssetPickerService::OpenPanel()
    {
        EnsurePanel();
        if (!m_Panel)
            return;

        auto& mainWindow = Application::Get()->GetWindow();
        const float width = (std::min)(760.0f, (std::max)(420.0f, static_cast<float>(mainWindow.GetWidth()) - 120.0f));
        const float height = (std::min)(520.0f, (std::max)(300.0f, static_cast<float>(mainWindow.GetHeight()) - 120.0f));
        const float x = (static_cast<float>(mainWindow.GetWidth()) - width) * 0.5f;
        const float y = (static_cast<float>(mainWindow.GetHeight()) - height) * 0.5f;

        // 분리된 선택창은 사용자가 배치한 OS 창 위치를 유지한다. 메인 창에 있을 때만 중앙 배치를 갱신한다.
        if (!m_Panel->GetOwnerWindow())
        {
            m_Panel->SetAnchorMin(0.0f, 0.0f);
            m_Panel->SetAnchorMax(0.0f, 0.0f);
            m_Panel->SetOffsetMin(x, y);
            m_Panel->SetOffsetMax(x + width, y + height);
        }

        m_Panel->SetDockingEnabled(true);
        m_Panel->SetVisible(true);
        m_Panel->BeginAssetPickerFilter(m_Request.Label, m_Request.AcceptedTypes);
        m_Panel->Refresh(false);
        m_Panel->BringToFront();
    }

    void AssetPickerService::HandleSelection(const std::filesystem::path& assetPath, const std::string& assetType)
    {
        if (!m_Request.Active)
            return;

        const std::string normalizedType = NormalizeType(assetType);
        if (std::find(m_Request.AcceptedTypes.begin(), m_Request.AcceptedTypes.end(), normalizedType) == m_Request.AcceptedTypes.end())
        {
            ConsoleLog::Warning("Asset pick expects type for " + m_Request.Label + ", ignored: " + assetType);
            return;
        }

        if (m_Request.OnPicked && m_Request.OnPicked(assetPath, normalizedType))
            Cancel(true);
    }

    std::string AssetPickerService::NormalizeType(std::string type)
    {
        std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return type;
    }
}
