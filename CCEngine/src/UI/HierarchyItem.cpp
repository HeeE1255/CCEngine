#include "HierarchyItem.h"
#include "Renderer/UIRenderer.h"
#include "Renderer/Font.h"
#include <iostream>

namespace CCEngine 
{
    namespace UI 
    {
        HierarchyItem::HierarchyItem(const std::string& name, const std::string& text)
            : VBoxContainer(name), m_Text(text)
        {
            float HeaderHeight = UIRenderer::GetDefaultFont() ? UIRenderer::GetDefaultFont()->GetFontSize() : 24.0f; // 폰트 높이 가져오기 (예시로 24.0f 사용)
            float verticalPadding = 8.0f;
            HeaderHeight = HeaderHeight + verticalPadding;

            // 4. 초기 크기 세팅
            //SetSize(m_CalculatedSize.x, HeaderHeight);

            SetSize(0.0f, HeaderHeight); // 초기 크기
        }

        void HierarchyItem::UpdateLayout(const UIVec2& parentPos, const UIVec2& parentSize)
        {
            if (!m_IsVisible) return;

            float anchorLeft = parentPos.x + (parentSize.x * m_AnchorMin.x);
            float anchorTop = parentPos.y + (parentSize.y * m_AnchorMin.y);
            m_CalculatedPos = { anchorLeft + m_OffsetMin.x, anchorTop + m_OffsetMin.y };

			// 내 넓이는 앵커와 오프셋을 기반으로 계산하되, 최소한 부모의 넓이만큼은 확보하도록 합니다.
            float myWidth = m_OffsetMax.x - m_OffsetMin.x;
			if (myWidth <= 0.0f) myWidth = parentSize.x; // 부모의 넓이로 확장
            m_CalculatedSize.x = myWidth;

            float HeaderHeight = UIRenderer::GetDefaultFont() ? UIRenderer::GetDefaultFont()->GetFontSize() : 24.0f; // 폰트 높이 가져오기 (예시로 24.0f 사용)
            float verticalPadding = 8.0f;
            float localChildY = HeaderHeight + verticalPadding;

            if (m_IsExpanded)
            {
                for (auto child : m_Children)
                {
                    if (!child->IsVisible()) continue;

                    // 내 좌표계를 기준으로 자식 배치
                    child->SetPosition(0.0f, localChildY);
                    child->SetSize(m_CalculatedSize.x, child->GetCalculatedSize().y);

                    // 자식의 레이아웃 갱신 (단 1번만!) - 전달 가능한 실제 높이를 사용합니다.
                    // 이전에는 큰 상수(9999)를 넘겨 자식들이 부모 영역 전체로 확장되어
                    // 마우스 히트 테스트가 모든 자식에 걸리는 문제가 있었습니다.
                    child->UpdateLayout(m_CalculatedPos, { m_CalculatedSize.x, parentSize.y });

                    // 자식이 계산한 자기 높이만큼 Y축 전진
                    localChildY += child->GetCalculatedSize().y;
                }
            }

            m_CalculatedSize = { m_OffsetMax.x - m_OffsetMin.x, localChildY };
            m_OffsetMax.y = m_OffsetMin.y + localChildY;

        }

        void HierarchyItem::OnRender()
        {
            if (!m_IsVisible) return;

            float mouseX = 0.0f; // 임시: Input::GetMouseX(); 
            float mouseY = 0.0f; // 임시: Input::GetMouseY();

            float headerHeight = UIRenderer::GetDefaultFont() ? UIRenderer::GetDefaultFont()->GetFontSize() : 24.0f; // 폰트 높이 가져오기 (예시로 24.0f 사용)
            float verticalPadding = 8.0f;
            headerHeight = headerHeight + verticalPadding;

            // 배경은 내 전체 사이즈가 아니라 '헤더(m_HeaderHeight)' 만큼만 칠해야 함!
            bool isHovered = (mouseX >= m_CalculatedPos.x && mouseX <= m_CalculatedPos.x + m_CalculatedSize.x &&
                mouseY >= m_CalculatedPos.y && mouseY <= m_CalculatedPos.y + headerHeight);

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
            float centerY = m_CalculatedPos.y + (headerHeight * 0.5f);
            float arrowAreaWidth = 14.0f;
            bool hoveringArrow = (mouseX >= indentX && mouseX <= indentX + arrowAreaWidth &&
                mouseY >= m_CalculatedPos.y && mouseY <= m_CalculatedPos.y + headerHeight);

            if (m_HasChildren)
            {
                DirectX::XMFLOAT4 arrowColor = hoveringArrow ?
                    DirectX::XMFLOAT4{ 220.0f / 255.0f, 220.0f / 255.0f, 220.0f / 255.0f, 1.0f } :
                    DirectX::XMFLOAT4{ 150.0f / 255.0f, 150.0f / 255.0f, 150.0f / 255.0f, 1.0f };

                if (m_IsExpanded) UIRenderer::DrawString("v", indentX + 2.0f, centerY + 5.0f, arrowColor);
                else UIRenderer::DrawString(">", indentX + 4.0f, centerY + 6.0f, arrowColor);
            }

            float textX = indentX + 18.0f;
			float textY = m_CalculatedPos.y + headerHeight * 0.7f; // 텍스트가 중앙에 오도록 약간 조정
            DirectX::XMFLOAT4 textColor = { 210.0f / 255.0f, 210.0f / 255.0f, 210.0f / 255.0f, 1.0f };
            UIRenderer::DrawString(m_Text, textX, textY, textColor);

            // 내가 펴져있다면 자식들 렌더링
            if (m_IsExpanded)
            {
                for (auto child : m_Children)
                    child->OnRender();
            }
        }

        bool HierarchyItem::OnMouseButtonPressed(MouseButtonPressedEvent& e)
        {
            if (!IsPointInside(e.GetX(), e.GetY()))
            {
                return false;
            }

            float headerHeight = UIRenderer::GetDefaultFont() ? UIRenderer::GetDefaultFont()->GetFontSize() : 24.0f;
            float verticalPadding = 8.0f;
            headerHeight = headerHeight + verticalPadding;

            // If click is in the area below the header, forward the event to children
            if (e.GetY() > m_CalculatedPos.y + headerHeight && e.GetY() <= m_CalculatedPos.y + m_CalculatedSize.y)
            {
                for (auto child : m_Children)
                {
                    if (child->OnEvent(e)) return true; // if a child handled it, we're done
                }
                return false; // clicked empty child area
            }

            // 헤더를 좌클릭 했을 때
            if (e.GetButton() == 0)
            {
                float indentX = m_CalculatedPos.x + (m_IndentLevel * 14.0f) + 8.0f;
                bool isArrowClicked = (e.GetX() >= indentX && e.GetX() <= indentX + 20.0f);

                if (isArrowClicked && m_HasChildren)
                {
                    if (m_OnToggleExpand) m_OnToggleExpand();
                }
                else
                {
                    if (m_OnSelect) m_OnSelect();
                }

                e.Handled = true;
                return true;
            }
            return false;
        }
    }
}