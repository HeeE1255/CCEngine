#include "UI/AssetReferenceValidatorPanel.h"

#include "Application.h"
#include "Events/MouseEvent.h"
#include "UI/Button.h"
#include "Renderer/UIRenderer.h"

#include <algorithm>

namespace CCEngine::UI
{
    AssetReferenceValidatorPanel::AssetReferenceValidatorPanel(const std::string& name)
        : WindowPanel(name, "Asset Reference Validator")
    {
        SetClipToBounds(true);
        SetDockingEnabled(false);

        m_ScanButton = new Button(name + "_ScanButton", "Scan");
        m_ScanButton->SetAnchorMin(0.0f, 0.0f);
        m_ScanButton->SetAnchorMax(0.0f, 0.0f);
        m_ScanButton->SetOffsetMin(10.0f, m_ContentTop + 10.0f);
        m_ScanButton->SetOffsetMax(126.0f, m_ContentTop + 36.0f);
        m_ScanButton->SetNormalColor({ 0.23f, 0.23f, 0.24f, 1.0f });
        m_ScanButton->SetHoverColor({ 0.32f, 0.32f, 0.34f, 1.0f });
        m_ScanButton->SetClickColor({ 0.14f, 0.14f, 0.15f, 1.0f });
        m_ScanButton->SetOnClick([this]() { Validate(false); });
        AddChild(m_ScanButton);

        m_RepairButton = new Button(name + "_RepairButton", "Repair");
        m_RepairButton->SetAnchorMin(0.0f, 0.0f);
        m_RepairButton->SetAnchorMax(0.0f, 0.0f);
        m_RepairButton->SetOffsetMin(134.0f, m_ContentTop + 10.0f);
        m_RepairButton->SetOffsetMax(250.0f, m_ContentTop + 36.0f);
        m_RepairButton->SetNormalColor({ 0.21f, 0.28f, 0.22f, 1.0f });
        m_RepairButton->SetHoverColor({ 0.29f, 0.38f, 0.30f, 1.0f });
        m_RepairButton->SetClickColor({ 0.12f, 0.18f, 0.13f, 1.0f });
        m_RepairButton->SetOnClick([this]() { Validate(true); });
        AddChild(m_RepairButton);

        m_ClearButton = new Button(name + "_ClearButton", "Clear");
        m_ClearButton->SetAnchorMin(0.0f, 0.0f);
        m_ClearButton->SetAnchorMax(0.0f, 0.0f);
        m_ClearButton->SetOffsetMin(258.0f, m_ContentTop + 10.0f);
        m_ClearButton->SetOffsetMax(374.0f, m_ContentTop + 36.0f);
        m_ClearButton->SetNormalColor({ 0.23f, 0.23f, 0.24f, 1.0f });
        m_ClearButton->SetHoverColor({ 0.32f, 0.32f, 0.34f, 1.0f });
        m_ClearButton->SetClickColor({ 0.14f, 0.14f, 0.15f, 1.0f });
        m_ClearButton->SetOnClick([this]() { ClearReport(); });
        AddChild(m_ClearButton);
    }

    void AssetReferenceValidatorPanel::Validate(bool repairFiles)
    {
        // 리포트는 검사 시점의 스냅샷이다.
        // UI는 이 값을 보관하고, 사용자가 다시 검사할 때만 새 상태로 교체한다.
        // repairFiles가 false면 문제를 찾기만 하고, true면 GUID 기준으로 고칠 수 있는 경로를 저장 파일에 반영한다.
        m_Report = AssetDatabase::ValidateProjectReferences(m_RootDirectory, repairFiles);
        m_SelectedIssue = m_Report.Issues.empty() ? -1 : 0;
        m_ListScroll.ScrollY = 0.0f;
    }

    void AssetReferenceValidatorPanel::ClearReport()
    {
        m_Report = {};
        m_SelectedIssue = -1;
        m_ListScroll.ScrollY = 0.0f;
    }

    void AssetReferenceValidatorPanel::OnRender()
    {
        if (!m_IsVisible)
            return;

        WindowPanel::OnRender();

        const float x = m_CalculatedPos.x;
        const float y = m_CalculatedPos.y + m_ContentTop;
        const float w = m_CalculatedSize.x;
        const float h = (std::max)(0.0f, m_CalculatedSize.y - m_ContentTop);

        const float padding = 10.0f;
        const float toolbarY = y + padding;
        std::string summary =
            std::to_string(m_Report.FilesScanned) + " files, " +
            std::to_string(m_Report.ReferencesChecked) + " refs, " +
            std::to_string(m_Report.RepairedReferences) + " repaired, " +
            std::to_string(m_Report.MissingReferences) + " missing";
        UIRenderer::DrawString(summary, x + padding, toolbarY + 48.0f, { 0.76f, 0.76f, 0.76f, 1.0f });

        // 왼쪽은 문제 목록, 오른쪽은 선택된 문제의 상세 정보다.
        // 상용 엔진의 진단 창처럼 한눈에 상태를 보고, 필요한 경우 원본 파일/JSON 위치까지 추적할 수 있게 나눈다.
        const float listX = x + padding;
        const float listY = y + m_ToolbarHeight + 46.0f;
        const float listW = (std::max)(180.0f, w * 0.55f - padding * 1.5f);
        const float listH = (std::max)(80.0f, h - m_ToolbarHeight - 62.0f);
        const float detailX = listX + listW + padding;
        const float detailY = listY;
        const float detailW = (std::max)(120.0f, w - detailX + x - padding);
        const float detailH = listH;

        UIRenderer::DrawRectFilled(listX, listY, listW, listH, { 0.10f, 0.10f, 0.11f, 1.0f });
        UIRenderer::DrawRect(listX, listY, listW, listH, { 0.25f, 0.25f, 0.26f, 1.0f });
        UIRenderer::DrawString("Issues", listX + 8.0f, listY + 20.0f, { 0.82f, 0.82f, 0.82f, 1.0f });

        const float rowsY = listY + 28.0f;
        const float rowsH = (std::max)(0.0f, listH - 30.0f);
        m_ListScroll.ContentHeight = (float)m_Report.Issues.size() * m_RowHeight;
        m_ListScroll.ViewportHeight = rowsH;
        m_ListScroll.ScrollY = (std::clamp)(m_ListScroll.ScrollY, 0.0f, m_ListScroll.GetMaxScroll());

        UIRenderer::SetClipRect(listX, rowsY, listW, rowsH);
        for (size_t i = 0; i < m_Report.Issues.size(); ++i)
        {
            const AssetReferenceIssue& issue = m_Report.Issues[i];
            const float rowY = rowsY + (float)i * m_RowHeight - m_ListScroll.ScrollY;
            if (rowY + m_RowHeight < rowsY || rowY > rowsY + rowsH)
                continue;

            DirectX::XMFLOAT4 rowColor = (int)i == m_SelectedIssue
                ? DirectX::XMFLOAT4{ 0.18f, 0.33f, 0.48f, 1.0f }
                : DirectX::XMFLOAT4{ 0.13f, 0.13f, 0.14f, 1.0f };
            UIRenderer::DrawRectFilled(listX + 1.0f, rowY, listW - 2.0f, m_RowHeight - 1.0f, rowColor);

            DirectX::XMFLOAT4 textColor = issue.Repaired
                ? DirectX::XMFLOAT4{ 0.72f, 0.92f, 0.74f, 1.0f }
                : DirectX::XMFLOAT4{ 0.95f, 0.58f, 0.54f, 1.0f };
            // FIX는 자동 복구된 항목, MISS는 자동 복구가 불가능해서 사람이 확인해야 하는 항목이다.
            UIRenderer::DrawString(issue.Repaired ? "FIX" : "MISS", listX + 8.0f, rowY + 19.0f, textColor);
            UIRenderer::DrawString(ShortenMiddle(BuildIssueTitle(issue), 68), listX + 58.0f, rowY + 19.0f, { 0.84f, 0.84f, 0.84f, 1.0f });
        }
        UIRenderer::ClearClipRect();

        if (m_Report.Issues.empty())
            UIRenderer::DrawString("No reference issues in the last report.", listX + 8.0f, rowsY + 24.0f, { 0.58f, 0.58f, 0.58f, 1.0f });

        UIRenderer::DrawRectFilled(detailX, detailY, detailW, detailH, { 0.12f, 0.12f, 0.13f, 1.0f });
        UIRenderer::DrawRect(detailX, detailY, detailW, detailH, { 0.25f, 0.25f, 0.26f, 1.0f });
        UIRenderer::DrawString("Details", detailX + 8.0f, detailY + 20.0f, { 0.82f, 0.82f, 0.82f, 1.0f });

        if (m_SelectedIssue >= 0 && m_SelectedIssue < (int)m_Report.Issues.size())
        {
            const AssetReferenceIssue& issue = m_Report.Issues[(size_t)m_SelectedIssue];
            float textY = detailY + 52.0f;
            // 상세 정보는 나중에 "문제 위치로 이동", "다른 에셋으로 교체" 같은 수동 복구 기능의 입력값이 된다.
            auto drawLine = [&](const std::string& label, const std::string& value)
            {
                UIRenderer::DrawString(label, detailX + 10.0f, textY, { 0.62f, 0.62f, 0.62f, 1.0f });
                UIRenderer::DrawString(ShortenMiddle(value, 64), detailX + 96.0f, textY, { 0.84f, 0.84f, 0.84f, 1.0f });
                textY += 24.0f;
            };

            drawLine("Status", issue.Repaired ? "Repaired" : "Missing");
            drawLine("Kind", issue.ReferenceKind);
            drawLine("File", issue.SourceFile.string());
            drawLine("Location", issue.JsonLocation);
            drawLine("GUID", issue.Guid);
            drawLine("Path", issue.StoredPath);
            drawLine("Resolved", issue.ResolvedPath);
            drawLine("Message", issue.Message);
        }
        else
        {
            UIRenderer::DrawString("Select an issue to inspect its source file, GUID, path, and repair result.",
                detailX + 10.0f, detailY + 54.0f, { 0.62f, 0.62f, 0.62f, 1.0f });
        }
    }

    bool AssetReferenceValidatorPanel::OnEvent(Event& e)
    {
        if (!m_IsVisible)
            return false;

        // 제목줄, 닫기 버튼, 테두리 리사이즈, 그리고 자식 Button 입력은 WindowPanel/Widget 기본 경로가 맡는다.
        // 이 패널이 먼저 전체 클릭을 먹어버리면 창 이동과 X 닫기가 막히므로, 공통 창 동작을 항상 먼저 처리한다.
        if (WindowPanel::OnEvent(e))
            return true;

        if (e.GetEventType() == EventType::MouseButtonPressed)
        {
            auto& mouseEvent = static_cast<MouseButtonPressedEvent&>(e);
            if (mouseEvent.GetButton() == 0 && IsPointInside(mouseEvent.GetX(), mouseEvent.GetY()))
            {
                int issueIndex = GetIssueIndexAt(mouseEvent.GetX(), mouseEvent.GetY());
                if (issueIndex >= 0)
                    m_SelectedIssue = issueIndex;

                e.Handled = true;
                return true;
            }
        }
        else if (e.GetEventType() == EventType::MouseScrolled)
        {
            auto& scrollEvent = static_cast<MouseScrolledEvent&>(e);
            Window* ownerWindow = GetOwnerWindow();
            auto [mouseX, mouseY] = ownerWindow
                ? ownerWindow->GetMousePosition()
                : Application::Get()->GetWindow().GetMousePosition();
            if (IsPointInside(mouseX, mouseY))
            {
                m_ListScroll.ApplyScroll(scrollEvent.GetYOffset() * -1.0f);
                e.Handled = true;
                return true;
            }
        }

        return false;
    }

    int AssetReferenceValidatorPanel::GetIssueIndexAt(float mouseX, float mouseY) const
    {
        // 화면에 보이는 행 위치에 현재 스크롤 값을 더해서 실제 리포트 인덱스로 되돌린다.
        // 이 계산이 맞아야 긴 목록에서도 클릭한 항목과 상세 패널이 정확히 일치한다.
        const float x = m_CalculatedPos.x;
        const float y = m_CalculatedPos.y + m_ContentTop;
        const float w = m_CalculatedSize.x;
        const float h = (std::max)(0.0f, m_CalculatedSize.y - m_ContentTop);
        const float listX = x + 10.0f;
        const float listY = y + m_ToolbarHeight + 46.0f + 28.0f;
        const float listW = (std::max)(180.0f, w * 0.55f - 15.0f);
        const float listH = (std::max)(0.0f, h - m_ToolbarHeight - 92.0f);

        if (mouseX < listX || mouseX > listX + listW || mouseY < listY || mouseY > listY + listH)
            return -1;

        int index = (int)((mouseY - listY + m_ListScroll.ScrollY) / m_RowHeight);
        if (index < 0 || index >= (int)m_Report.Issues.size())
            return -1;
        return index;
    }

    std::string AssetReferenceValidatorPanel::BuildIssueTitle(const AssetReferenceIssue& issue) const
    {
        std::string title = issue.ReferenceKind;
        if (!issue.SourceFile.empty())
            title += " - " + issue.SourceFile.filename().string();
        if (!issue.JsonLocation.empty())
            title += " " + issue.JsonLocation;
        return title;
    }

    std::string AssetReferenceValidatorPanel::ShortenMiddle(const std::string& text, size_t maxLength) const
    {
        if (text.size() <= maxLength || maxLength < 8)
            return text;

        size_t left = (maxLength - 3) / 2;
        size_t right = maxLength - 3 - left;
        return text.substr(0, left) + "..." + text.substr(text.size() - right);
    }
}
