#include "HierarchyItem.h"
#include "Renderer/UIRenderer.h"
#include "Renderer/Font.h"
#include <iostream> // ★ 로그 출력을 위해 추가

namespace CCEngine
{
    namespace UI
    {
        HierarchyItem::HierarchyItem(const std::string& name, const std::string& text)
            : VBoxContainer(name), m_Text(text)
        {
            float HeaderHeight = UIRenderer::GetDefaultFont() ? UIRenderer::GetDefaultFont()->GetFontSize() : 24.0f;
            float verticalPadding = 8.0f;
            HeaderHeight = HeaderHeight + verticalPadding;

            SetSize(0.0f, HeaderHeight);
        }

        void HierarchyItem::UpdateLayout(const DirectX::XMFLOAT2& parentPos, const DirectX::XMFLOAT2& parentSize)
        {
            if (!m_IsVisible) return;

            float anchorLeft = parentPos.x + (parentSize.x * m_AnchorMin.x);
            float anchorTop = parentPos.y + (parentSize.y * m_AnchorMin.y);
            m_CalculatedPos = { anchorLeft + m_OffsetMin.x, anchorTop + m_OffsetMin.y };

            float myWidth = m_OffsetMax.x - m_OffsetMin.x;
            if (myWidth <= 0.0f) myWidth = parentSize.x;
            m_CalculatedSize.x = myWidth;

            float HeaderHeight = UIRenderer::GetDefaultFont() ? UIRenderer::GetDefaultFont()->GetFontSize() : 24.0f;
            float verticalPadding = 8.0f;
            float localChildY = HeaderHeight + verticalPadding;

            if (m_IsExpanded)
            {
                for (auto child : m_Children)
                {
                    if (!child->IsVisible()) continue;

                    child->SetPosition(0.0f, localChildY);
                    child->SetSize(m_CalculatedSize.x, child->GetCalculatedSize().y);
                    child->UpdateLayout(m_CalculatedPos, { m_CalculatedSize.x, parentSize.y });

                    localChildY += child->GetCalculatedSize().y;
                }
            }

            m_CalculatedSize = { m_OffsetMax.x - m_OffsetMin.x, localChildY };
            m_OffsetMax.y = m_OffsetMin.y + localChildY;
        }

        void HierarchyItem::SetRenderClipRange(float top, float bottom)
        {
            m_RenderClipTop = top;
            m_RenderClipBottom = bottom;
        }

        void HierarchyItem::OnRender()
        {
            if (!m_IsVisible) return;

            float headerHeight = UIRenderer::GetDefaultFont() ? UIRenderer::GetDefaultFont()->GetFontSize() : 24.0f;
            float verticalPadding = 8.0f;
            headerHeight += verticalPadding;

            const bool hasClipRange = m_RenderClipBottom > m_RenderClipTop;
            if (hasClipRange)
            {
                if (m_CalculatedPos.y + m_CalculatedSize.y < m_RenderClipTop)
                    return;
                if (m_CalculatedPos.y > m_RenderClipBottom)
                    return;
            }

            // GPU scissor는 픽셀만 잘라낸다. 항목이 수백 개면 화면 밖 줄도
            // hover 검사와 문자열 렌더 준비를 하므로, 보이는 줄만 CPU에서 처리한다.
            const bool headerVisible = !hasClipRange ||
                (m_CalculatedPos.y + headerHeight >= m_RenderClipTop &&
                    m_CalculatedPos.y <= m_RenderClipBottom);

            if (headerVisible)
            {
                Window* renderWindow = Widget::GetCurrentRenderWindow();
                auto [mouseX, mouseY] = renderWindow
                    ? renderWindow->GetMousePosition()
                    : CCEngine::Application::Get()->GetWindow().GetMousePosition();

                bool isHovered = Widget::IsCurrentRenderWindowMouseActive() &&
                    !Widget::IsMouseInteractionActive() &&
                    (mouseX >= m_CalculatedPos.x && mouseX <= m_CalculatedPos.x + m_CalculatedSize.x &&
                    mouseY >= m_CalculatedPos.y && mouseY <= m_CalculatedPos.y + headerHeight);
                isHovered = isHovered && !IsMouseBlockedByWidgetAbove(mouseX, mouseY);

                if (m_IsSelected)
                {
                    DirectX::XMFLOAT4 selectedColor = { 44.0f / 255.0f, 93.0f / 255.0f, 135.0f / 255.0f, 1.0f };
                    UIRenderer::DrawRectFilled(m_CalculatedPos.x, m_CalculatedPos.y, m_CalculatedSize.x, headerHeight, selectedColor);
                }
                else if (isHovered)
                {
                    DirectX::XMFLOAT4 hoverColor = { 60.0f / 255.0f, 60.0f / 255.0f, 60.0f / 255.0f, 1.0f };
                    UIRenderer::DrawRectFilled(m_CalculatedPos.x, m_CalculatedPos.y, m_CalculatedSize.x, headerHeight, hoverColor);
                }

                float indentX = m_CalculatedPos.x + (m_IndentLevel * 14.0f) + 8.0f;
                float hitBoxLeft = indentX;
                float hitBoxRight = indentX + 24.0f;

                bool hoveringArrow = Widget::IsCurrentRenderWindowMouseActive() &&
                    !Widget::IsMouseInteractionActive() &&
                    (mouseX >= hitBoxLeft && mouseX <= hitBoxRight &&
                    mouseY >= m_CalculatedPos.y && mouseY <= m_CalculatedPos.y + headerHeight);
                hoveringArrow = hoveringArrow && !IsMouseBlockedByWidgetAbove(mouseX, mouseY);

                float centerY = m_CalculatedPos.y + (headerHeight * 0.5f);

                if (m_HasChildren)
                {
                    DirectX::XMFLOAT4 arrowColor = hoveringArrow ?
                        DirectX::XMFLOAT4{ 220.0f / 255.0f, 220.0f / 255.0f, 220.0f / 255.0f, 1.0f } :
                        DirectX::XMFLOAT4{ 150.0f / 255.0f, 150.0f / 255.0f, 150.0f / 255.0f, 1.0f };

                    if (m_IsExpanded) UIRenderer::DrawString("v", indentX + 2.0f, centerY + 5.0f, arrowColor);
                    else UIRenderer::DrawString(">", indentX + 4.0f, centerY + 6.0f, arrowColor);
                }

                float textX = indentX + 18.0f;
                float textY = m_CalculatedPos.y + headerHeight * 0.7f;
                DirectX::XMFLOAT4 textColor = { 210.0f / 255.0f, 210.0f / 255.0f, 210.0f / 255.0f, 1.0f };
                UIRenderer::DrawString(m_Text, textX, textY, textColor);
            }

            if (m_IsExpanded)
            {
                for (auto child : m_Children)
                {
                    if (auto item = dynamic_cast<HierarchyItem*>(child))
                        item->SetRenderClipRange(m_RenderClipTop, m_RenderClipBottom);
                    child->OnRender();
                }
            }
        }

        bool HierarchyItem::OnMouseButtonPressed(MouseButtonPressedEvent& e)
        {
            float headerHeight = UIRenderer::GetDefaultFont() ? UIRenderer::GetDefaultFont()->GetFontSize() : 24.0f;
            headerHeight += 8.0f;

            bool isInsideHeader = (e.GetX() >= m_CalculatedPos.x && e.GetX() <= m_CalculatedPos.x + m_CalculatedSize.x &&
                e.GetY() >= m_CalculatedPos.y && e.GetY() <= m_CalculatedPos.y + headerHeight);

            if (isInsideHeader && e.GetButton() == 0)
            {
                float indentX = m_CalculatedPos.x + (m_IndentLevel * 14.0f) + 8.0f;
                float hitBoxLeft = indentX;
                float hitBoxRight = indentX + 24.0f;

                bool isArrowClicked = (e.GetX() >= hitBoxLeft && e.GetX() <= hitBoxRight);

                if (isArrowClicked && m_HasChildren)
                {
                    m_IsExpanded = !m_IsExpanded;

                    for (auto child : m_Children)
                    {
                        child->SetVisible(m_IsExpanded);
                    }

                    DirectX::XMFLOAT2 dummyParentPos = { m_CalculatedPos.x - m_OffsetMin.x, m_CalculatedPos.y - m_OffsetMin.y };
                    DirectX::XMFLOAT2 dummyParentSize = { 9999.0f, 9999.0f };
                    this->UpdateLayout(dummyParentPos, dummyParentSize);

                    if (m_OnToggleExpand) m_OnToggleExpand();
                }
                else
                {
                    if (m_OnSelect) m_OnSelect();
                }

                e.Handled = true;
                return true;
            }

            if (m_IsExpanded && e.GetY() > m_CalculatedPos.y + headerHeight)
            {
                for (auto child : m_Children)
                {
                    if (child->OnEvent(e)) return true;
                }
            }

            return false;
        }
    }
}
