#include "UI/ConsolePanel.h"
#include "Application.h"
#include "Core/ConsoleLog.h"
#include "Renderer/UIRenderer.h"
#include "Events/MouseEvent.h"
#include <algorithm>

namespace CCEngine::UI
{
    ConsolePanel::ConsolePanel(const std::string& name)
        : WindowPanel(name, "Console")
    {
        SetClipToBounds(true);
    }

    void ConsolePanel::UpdateLayout(const DirectX::XMFLOAT2& parentPos, const DirectX::XMFLOAT2& parentSize)
    {
        WindowPanel::UpdateLayout(parentPos, parentSize);

        m_ScrollState.ContentHeight = (float)ConsoleLog::GetEntries().size() * m_RowHeight;
        m_ScrollState.ViewportHeight = (std::max)(0.0f, m_CalculatedSize.y - m_ContentTop);
    }

    void ConsolePanel::OnRender()
    {
        if (!m_IsVisible)
            return;

        SetClipPadding(0.0f, m_ContentTop, 0.0f, 0.0f);
        WindowPanel::OnRender();

        const auto& entries = ConsoleLog::GetEntries();
        float x = m_CalculatedPos.x + 8.0f;
        float y = m_CalculatedPos.y + m_ContentTop + 8.0f - m_ScrollState.ScrollY;
        float contentX = m_CalculatedPos.x;
        float contentY = m_CalculatedPos.y + m_ContentTop;
        float contentW = (std::max)(0.0f, m_CalculatedSize.x - 16.0f);
        float contentH = (std::max)(0.0f, m_CalculatedSize.y - m_ContentTop);

        // ConsolePanel은 로그 문자열을 자식 위젯이 아니라 직접 그린다.
        // 그래서 Widget의 자식용 scissor와 별도로, 로그 영역에 맞춰 문자열 클립을 건다.
        UIRenderer::SetClipRect(contentX, contentY, contentW, contentH);

        for (size_t i = 0; i < entries.size(); ++i)
        {
            float rowY = y + (float)i * m_RowHeight;
            if (rowY + m_RowHeight < m_CalculatedPos.y + m_ContentTop || rowY > m_CalculatedPos.y + m_CalculatedSize.y)
                continue;

            DirectX::XMFLOAT4 color = { 0.78f, 0.78f, 0.78f, 1.0f };
            if (entries[i].Level == ConsoleLogLevel::Warning)
                color = { 0.95f, 0.78f, 0.34f, 1.0f };
            else if (entries[i].Level == ConsoleLogLevel::Error)
                color = { 0.95f, 0.42f, 0.38f, 1.0f };

            UIRenderer::DrawString(entries[i].Message, x, rowY + 17.0f, color);
        }

        if (entries.empty())
            UIRenderer::DrawString("No logs.", x, m_CalculatedPos.y + m_ContentTop + 24.0f, { 0.55f, 0.55f, 0.55f, 1.0f });

        UIRenderer::ClearClipRect();

        if (m_ScrollState.GetMaxScroll() > 0.0f)
        {
            float thumbH = m_ScrollState.GetThumbHeight();
            float thumbY = m_ScrollState.GetThumbY(m_CalculatedPos.y + m_ContentTop);
            float thumbX = m_CalculatedPos.x + m_CalculatedSize.x - 14.0f;
            UIRenderer::DrawRect({ thumbX, m_CalculatedPos.y + m_ContentTop }, { 8.0f, m_ScrollState.ViewportHeight }, { 0.08f, 0.08f, 0.08f, 0.5f });
            UIRenderer::DrawRect({ thumbX, thumbY }, { 8.0f, thumbH }, { 0.42f, 0.42f, 0.42f, 1.0f });
        }
    }

    bool ConsolePanel::OnEvent(Event& e)
    {
        if (!m_IsVisible)
            return false;

        if (e.GetEventType() == EventType::MouseScrolled)
        {
            auto& se = static_cast<MouseScrolledEvent&>(e);
            auto [mouseX, mouseY] = CCEngine::Application::Get()->GetWindow().GetMousePosition();
            if (IsPointInside(mouseX, mouseY))
            {
                m_ScrollState.ApplyScroll(se.GetYOffset() * -1.0f);
                e.Handled = true;
                return true;
            }
        }

        return WindowPanel::OnEvent(e);
    }
}
