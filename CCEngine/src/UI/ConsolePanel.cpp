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

        const size_t entryCount = ConsoleLog::GetEntries().size();
        const bool hasNewEntries = entryCount != m_LastEntryCount;

        m_ScrollState.ContentHeight = (float)entryCount * m_RowHeight;
        m_ScrollState.ViewportHeight = (std::max)(0.0f, m_CalculatedSize.y - m_ContentTop);
        if (hasNewEntries)
        {
            // 새 로그가 들어오면 최근 항목이 보이도록 아래쪽에 붙인다.
            // 검증/컴파일 로그처럼 즉시 확인해야 하는 메시지를 놓치지 않기 위한 처리다.
            m_ScrollState.ScrollY = m_ScrollState.GetMaxScroll();
            m_LastEntryCount = entryCount;
        }
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

        if (e.GetEventType() == EventType::MouseButtonPressed)
        {
            auto& me = static_cast<MouseButtonPressedEvent&>(e);
            if (me.GetButton() == 0 && m_ScrollState.GetMaxScroll() > 0.0f)
            {
                float thumbH = m_ScrollState.GetThumbHeight();
                float thumbY = m_ScrollState.GetThumbY(m_CalculatedPos.y + m_ContentTop);
                float thumbX = m_CalculatedPos.x + m_CalculatedSize.x - 14.0f;
                bool onThumb = me.GetX() >= thumbX && me.GetX() <= thumbX + 8.0f &&
                    me.GetY() >= thumbY && me.GetY() <= thumbY + thumbH;
                if (onThumb)
                {
                    m_IsDraggingScrollbar = true;
                    m_DragMouseStartY = me.GetY();
                    m_DragScrollStartY = m_ScrollState.ScrollY;
                    Widget::BeginMouseInteraction(this);
                    e.Handled = true;
                    return true;
                }
            }
        }

        if (e.GetEventType() == EventType::MouseMoved && m_IsDraggingScrollbar)
        {
            auto& me = static_cast<MouseMovedEvent&>(e);
            m_ScrollState.SetFromThumbDrag(me.GetY(), m_DragMouseStartY, m_DragScrollStartY);
            e.Handled = true;
            return true;
        }

        if (e.GetEventType() == EventType::MouseButtonReleased)
        {
            auto& me = static_cast<MouseButtonReleasedEvent&>(e);
            if (me.GetButton() == 0 && m_IsDraggingScrollbar)
            {
                m_IsDraggingScrollbar = false;
                Widget::EndMouseInteraction(this);
                e.Handled = true;
                return true;
            }
        }

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
