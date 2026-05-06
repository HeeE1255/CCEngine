#include "InspectorItem.h"
#include "Renderer/UIRenderer.h"

namespace CCEngine {
    namespace UI {

        InspectorItem::InspectorItem(const std::string& name, const std::string& title)
            : Panel(name, { 0.18f, 0.18f, 0.18f, 1.0f }), m_Title(title)
        {
        }

        void InspectorItem::OnRender()
        {
            if (!m_IsVisible) return;

            // 1. 전체 배경색
            UIRenderer::DrawRectFilled(m_CalculatedPos.x, m_CalculatedPos.y, m_CalculatedSize.x, m_CalculatedSize.y, m_Color);

            // 2. 상단 타이틀 바 (더 어두운 색상)
            float headerHeight = 24.0f;
            UIRenderer::DrawRectFilled(m_CalculatedPos.x, m_CalculatedPos.y, m_CalculatedSize.x, headerHeight, { 0.12f, 0.12f, 0.12f, 1.0f });

            // 3. 타이틀 텍스트
            UIRenderer::DrawString(m_Title, m_CalculatedPos.x + 10.0f, m_CalculatedPos.y + 18.0f, { 0.9f, 0.9f, 0.9f, 1.0f });

            // 4. 자식 요소들 (DragFloat 등) 렌더링
            Widget::OnRender();
        }

        void InspectorItem::UpdateLayout(const DirectX::XMFLOAT2& parentPos, const DirectX::XMFLOAT2& parentSize)
        {
            // 1. 부모(Panel)의 기본 위치/크기 계산 먼저 실행
            Panel::UpdateLayout(parentPos, parentSize);

            // 2. 타이틀 바(24px) + 여백(4px) 아래부터 첫 번째 자식 배치 시작
            float currentY = 28.0f;

            for (auto* child : m_Children)
            {
                float childHeight = 24.0f; // 입력 칸이나 버튼의 고정 높이

                // 자식의 앵커를 상단 기준으로 고정
                child->SetAnchorMin(0.0f, 0.0f);
                child->SetAnchorMax(1.0f, 0.0f);

                // ★ 핵심: currentY를 기반으로 세로 위치(Offset)를 강제 지정!
                child->SetOffsetMin(15.0f, currentY); // 15px 들여쓰기
                child->SetOffsetMax(-10.0f, currentY + childHeight); // 우측 10px 띄우고 높이 설정

                // 위치를 지정했으니 자식의 레이아웃 갱신
                child->UpdateLayout(m_CalculatedPos, m_CalculatedSize);

                // 다음 자식을 위해 Y좌표를 밑으로 내림 (높이 24 + 간격 4)
                currentY += childHeight + 4.0f;
            }

            // 3. 자식들을 다 배치한 후, 내 자신의 높이(CalculatedSize.y)를 내용물에 맞게 쫙 늘려줍니다!
            m_CalculatedSize.y = currentY + 4.0f;
            m_OffsetMax.y = m_OffsetMin.y + m_CalculatedSize.y;
        }

    }
}