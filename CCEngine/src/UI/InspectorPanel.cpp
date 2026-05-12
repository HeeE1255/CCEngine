#include "InspectorPanel.h"
#include "InspectorRegistry.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/UIRenderer.h"

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

            // ★ 1. 가위질 시작 (타이틀 바 40.0f 아래부터)
            RenderCommand::SetScissorEnable(true);
            RenderCommand::SetScissor((uint32_t)m_CalculatedPos.x, (uint32_t)(m_CalculatedPos.y + 40.0f),
                (uint32_t)m_CalculatedSize.x, (uint32_t)(m_CalculatedSize.y - 40.0f));

            for (auto child : m_Children)
            {
                child->OnRender();
            }

            // ★ 2. 가위질 해제
            RenderCommand::SetScissorEnable(false);

            // ★ 3. 스크롤바 그리기
            if (m_ScrollState.GetMaxScroll() > 0)
            {
                float thumbH = m_ScrollState.GetThumbHeight();
                float thumbY = m_ScrollState.GetThumbY(m_CalculatedPos.y + 40.0f);
                float thumbX = m_CalculatedPos.x + m_CalculatedSize.x - 20.0f;

                UIRenderer::DrawRect({ thumbX, m_CalculatedPos.y + 40.0f }, { 8.0f, m_ScrollState.ViewportHeight }, { 0.1f, 0.1f, 0.1f, 0.5f });
                UIRenderer::DrawRect({ thumbX, thumbY }, { 8.0f, thumbH }, { 0.4f, 0.4f, 0.4f, 1.0f });
            }
        }

        void InspectorPanel::UpdateLayout(const DirectX::XMFLOAT2& parentPos, const DirectX::XMFLOAT2& parentSize)
        {
            // 1. 윈도우 패널 자체의 배경 및 뼈대 업데이트
            WindowPanel::UpdateLayout(parentPos, parentSize);

            if (!m_IsVisible || !m_SelectedEntity) return;

            // 2. 컴포넌트 박스(InspectorItem)들을 세로로 차곡차곡 쌓음
            float startY = 40.0f; // 윈도우 타이틀바 바로 아래부터 시작
            float currentY = startY - m_ScrollState.ScrollY;

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

            m_ScrollState.ContentHeight = (currentY + m_ScrollState.ScrollY) - startY;
            m_ScrollState.ViewportHeight = m_CalculatedSize.y - startY;
        }

    }
}