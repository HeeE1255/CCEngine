#include "UI/AssetBrowserPanel.h"

#include <filesystem>

namespace CCEngine::UI
{
    namespace
    {
        bool IsPickerMetaFile(const std::filesystem::path& path)
        {
            return path.extension() == ".meta";
        }
    }
    void AssetBrowserPanel::BeginAssetPickerFilter(const std::string& label, const std::vector<std::string>& acceptedTypeKeys)
        {
            if (!m_PickerState.IsActive())
                m_PrePickerTypeFilter = m_TypeFilter;
            m_TypeFilter = TypeFilter::All;
            m_PickerState.Begin(label, acceptedTypeKeys);
            m_TypeFilterDropdownVisible = false;
            // Object picker를 다시 열었을 때 일반 Asset Browser의 이전 선택이
            // 현재 슬롯의 선택처럼 보이면 사용자가 아직 고르지 않은 에셋이 강조된다.
            ClearSelection();
            m_LastClickedIndex = -1;
            ApplyFilter();
        }


    void AssetBrowserPanel::ClearAssetPickerFilter()
        {
            if (!m_PickerState.IsActive())
                return;

            m_TypeFilter = m_PrePickerTypeFilter;
            m_PickerState.Clear();
            ApplyFilter();
        }


    void AssetBrowserPanel::BuildProjectWidePickerEntries(const std::string& query, const std::string& extensionFilter, TypeFilter queryTypeFilter)
        {
            if (!std::filesystem::exists(m_RootDirectory))
                return;

            std::error_code ec;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(
                m_RootDirectory,
                std::filesystem::directory_options::skip_permission_denied,
                ec))
            {
                if (ec)
                    break;

                if (!entry.is_regular_file(ec) || ec)
                {
                    ec.clear();
                    continue;
                }

                if (IsPickerMetaFile(entry.path()))
                    continue;

                AssetType type = GetAssetType(entry.path());
                if (type == AssetType::Unknown)
                    continue;

                AssetEntry assetEntry;
                assetEntry.Path = entry.path();
                assetEntry.Type = type;
                std::error_code relEc;
                std::filesystem::path relativePath = std::filesystem::relative(entry.path(), m_RootDirectory, relEc);
                assetEntry.DisplayName = relEc ? entry.path().filename().string() : relativePath.generic_string();

                if (EntryMatchesAdvancedFilter(assetEntry, query, extensionFilter, queryTypeFilter))
                    m_ViewEntries.push_back(assetEntry);
            }
        }

}
