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

            // ★ 가짜 0.0f 좌표를 지우고, OS에서 진짜 마우스 좌표를 가져옵니다!
            auto [mouseX, mouseY] = CCEngine::Application::Get()->GetWindow().GetMousePosition();

            float headerHeight = UIRenderer::GetDefaultFont() ? UIRenderer::GetDefaultFont()->GetFontSize() : 24.0f;
            float verticalPadding = 8.0f;
            headerHeight += verticalPadding;

            // 전체 호버 검사
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
            float hitBoxLeft = indentX;
            float hitBoxRight = indentX + 24.0f;

            // 화살표 호버 검사 (마우스가 올라가면 밝게 빛남)
            bool hoveringArrow = (mouseX >= hitBoxLeft && mouseX <= hitBoxRight &&
                mouseY >= m_CalculatedPos.y && mouseY <= m_CalculatedPos.y + headerHeight);

            float centerY = m_CalculatedPos.y + (headerHeight * 0.5f);

            if (m_HasChildren)
            {
                DirectX::XMFLOAT4 arrowColor = hoveringArrow ?
                    DirectX::XMFLOAT4{ 220.0f / 255.0f, 220.0f / 255.0f, 220.0f / 255.0f, 1.0f } : // 밝은 흰색
                    DirectX::XMFLOAT4{ 150.0f / 255.0f, 150.0f / 255.0f, 150.0f / 255.0f, 1.0f }; // 어두운 회색

                if (m_IsExpanded) UIRenderer::DrawString("v", indentX + 2.0f, centerY + 5.0f, arrowColor);
                else UIRenderer::DrawString(">", indentX + 4.0f, centerY + 6.0f, arrowColor);
            }

            float textX = indentX + 18.0f;
            float textY = m_CalculatedPos.y + headerHeight * 0.7f;
            DirectX::XMFLOAT4 textColor = { 210.0f / 255.0f, 210.0f / 255.0f, 210.0f / 255.0f, 1.0f };
            UIRenderer::DrawString(m_Text, textX, textY, textColor);

            if (m_IsExpanded)
            {
                for (auto child : m_Children)
                    child->OnRender();
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
                    // 1. 상태 즉시 토글
                    m_IsExpanded = !m_IsExpanded;

                    //std::cout << "[Hierarchy] Arrow Clicked! Name: " << m_Text << " | Child Count: " << m_Children.size() << std::endl;

                    // 자식들의 표시 여부를 강제로 세팅
                    for (auto child : m_Children)
                    {
                        child->SetVisible(m_IsExpanded);
                    }

                    // (중요) 내 레이아웃을 억지로 다시 계산해서 크기를 부풀립니다.
                    // 원래는 부모의 Pos/Size를 받아야 하지만, 임시로 현재 값을 그대로 넣어서 크기(높이)만 다시 계산하게 만듭니다.
                    UIVec2 dummyParentPos = { m_CalculatedPos.x - m_OffsetMin.x, m_CalculatedPos.y - m_OffsetMin.y };
                    UIVec2 dummyParentSize = { 9999.0f, 9999.0f }; // 일단 가로세로 제한 무시
                    this->UpdateLayout(dummyParentPos, dummyParentSize);
                    // ==========================================================

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