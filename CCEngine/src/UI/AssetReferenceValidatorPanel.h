#pragma once

#include "Core/AssetDatabase.h"
#include "UI/WindowPanel.h"
#include <filesystem>
#include <string>

namespace CCEngine::UI
{
    class Button;

    class CC_API AssetReferenceValidatorPanel : public WindowPanel
    {
    public:
        AssetReferenceValidatorPanel(const std::string& name = "AssetReferenceValidatorPanel");

        void Validate(bool repairFiles);
        void ClearReport();

        virtual void OnRender() override;
        virtual bool OnEvent(Event& e) override;

    private:
        int GetIssueIndexAt(float mouseX, float mouseY) const;
        std::string BuildIssueTitle(const AssetReferenceIssue& issue) const;
        std::string ShortenMiddle(const std::string& text, size_t maxLength) const;

    private:
        // 마지막 검사 결과를 보관한다.
        // 검사 결과가 곧 UI 모델이므로, 화면 갱신마다 파일을 다시 훑지 않는다.
        AssetReferenceValidationReport m_Report;
        std::filesystem::path m_RootDirectory = std::filesystem::current_path() / "assets";
        int m_SelectedIssue = -1;
        ScrollState m_ListScroll;
        float m_ContentTop = 28.0f;
        float m_ToolbarHeight = 42.0f;
        float m_RowHeight = 28.0f;

        Button* m_ScanButton = nullptr;
        Button* m_RepairButton = nullptr;
        Button* m_ClearButton = nullptr;
    };
}
