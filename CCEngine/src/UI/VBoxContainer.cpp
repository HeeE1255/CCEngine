#include "VBoxContainer.h"

namespace CCEngine 
{
    namespace UI 
    {
        VBoxContainer::VBoxContainer(const std::string& name) : Widget(name) {}

        void VBoxContainer::UpdateLayout(const UIVec2& parentPos, const UIVec2& parentSize)
        {
            if (!m_IsVisible) return;

            // 1. 나 자신의 기본 앵커/오프셋 계산
            Widget::UpdateLayout(parentPos, parentSize);

            float currentY = m_CalculatedPos.y;
            float currentX = m_CalculatedPos.x;

            // 2. 자식들을 순회하며 위치를 강제로 잡아줌
            for (Widget* child : m_Children)
            {
                if (!child->IsVisible()) continue;

                // 자식의 시작 위치를 현재 누적된 Y로 설정
                child->SetPosition(currentX, currentY);

                // 자식에게 자신의 레이아웃을 다시 계산하라고 명령 (재귀적 갱신)
                child->UpdateLayout(m_CalculatedPos, m_CalculatedSize);

                // 다음 자식을 위해 Y축 전진
                currentY += child->GetCalculatedSize().y + m_Spacing;
            }

            // 3. 내 최종 높이는 자식들을 모두 품은 전체 높이가 됨!
            m_CalculatedSize.y = currentY - m_CalculatedPos.y;
        }
    }
}