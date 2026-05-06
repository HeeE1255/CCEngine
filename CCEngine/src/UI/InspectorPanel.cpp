#include "InspectorPanel.h"
#include "InspectorRegistry.h"

namespace CCEngine 
{
    namespace UI 
    {

        InspectorPanel::InspectorPanel(const std::string& name, const std::string& title)
            : WindowPanel(name, title)
        {
        }

        void InspectorPanel::SetSelectedEntity(Entity entity)
        {
            // 같은 엔티티를 다시 클릭했으면 무시
            if (m_SelectedEntity == entity) return;

            m_SelectedEntity = entity;

            // ★ 중요: 다른 엔티티가 선택되었으므로, 기존에 그려져 있던 UI 위젯(DragFloat 등)을 모두 날립니다.
            m_Children.clear();

            if (!m_SelectedEntity) return;

            // 타이틀 바 아래쪽 여백부터 UI 생성 시작
            float currentY = 40.0f;

            InspectorRegistry::DrawAllComponents(this, m_SelectedEntity);
        }

        void InspectorPanel::OnRender()
        {
            // 1. 배경 및 윈도우 타이틀 렌더링
            WindowPanel::OnRender();

            if (!m_IsVisible || !m_SelectedEntity) return;

            // 2. 동적으로 생성된 자식 위젯들(DragFloat, Button 등) 렌더링
            for (auto child : m_Children)
            {
                child->OnRender();
            }
        }

        void InspectorPanel::UpdateLayout(const DirectX::XMFLOAT2& parentPos, const DirectX::XMFLOAT2& parentSize)
        {
            // 1. 윈도우 패널 자체의 배경 및 뼈대 업데이트
            WindowPanel::UpdateLayout(parentPos, parentSize);

            if (!m_IsVisible || !m_SelectedEntity) return;

            // 2. 컴포넌트 박스(InspectorItem)들을 세로로 차곡차곡 쌓음
            float currentY = 40.0f; // 윈도우 타이틀바 바로 아래부터 시작
            for (auto child : m_Children)
            {
                if (!child->IsVisible()) continue;

                // 자식의 Y좌표를 현재 currentY로 강제 할당
                child->SetOffsetMin(0.0f, currentY);
                child->SetOffsetMax(0.0f, currentY); // 높이는 InspectorItem이 스스로 계산해서 늘림!

                // 자식 레이아웃 갱신
                child->UpdateLayout(m_CalculatedPos, m_CalculatedSize);

                // 방금 갱신된 자식의 진짜 높이만큼 Y축을 전진시킴
                currentY += child->GetCalculatedSize().y + 4.0f;
            }
        }

    }
}