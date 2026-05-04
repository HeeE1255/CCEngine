#include "InspectorPanel.h"
#include "Renderer/UIRenderer.h"
#include "Scene/Components.h"

namespace CCEngine 
{
    namespace UI 
    {

        InspectorPanel::InspectorPanel(const std::string& name, const std::string& title)
            : WindowPanel(name, title)
        {
        }

        void InspectorPanel::OnRender()
        {
            // 1. 기본 윈도우 패널 배경 및 타이틀 렌더링
            WindowPanel::OnRender();

            if (!m_IsVisible) return;

            // 2. 선택된 엔티티가 없으면 안내 문구만 띄우고 종료
            if (!m_SelectedEntity)
            {
                UIRenderer::DrawString("No Entity Selected",
                    m_CalculatedPos.x + 10.0f, m_CalculatedPos.y + 50.0f,
                    { 0.5f, 0.5f, 0.5f, 1.0f });
                return;
            }

            // ==========================================================
            // 3. 선택된 엔티티의 컴포넌트 정보 읽어오기
            // ==========================================================
            float currentY = m_CalculatedPos.y + 40.0f;
            float leftPadding = m_CalculatedPos.x + 10.0f;
            float lineHeight = 30.0f;
            DirectX::XMFLOAT4 textColor = { 1.0f, 1.0f, 1.0f, 1.0f };

            // [Tag Component] (이름)
            if (m_SelectedEntity.HasComponent<TagComponent>())
            {
                auto& tag = m_SelectedEntity.GetComponent<TagComponent>().Tag;
                UIRenderer::DrawString("Name: " + tag, leftPadding, currentY, textColor);
                currentY += lineHeight;
            }

            // 구분선 하나 그어주기
            UIRenderer::DrawRectFilled(m_CalculatedPos.x + 5.0f, currentY, m_CalculatedSize.x - 10.0f, 2.0f, { 0.3f, 0.3f, 0.3f, 1.0f });
            currentY += 15.0f;

            // [Transform Component] (트랜스폼)
            if (m_SelectedEntity.HasComponent<TransformComponent>())
            {
                auto& transform = m_SelectedEntity.GetComponent<TransformComponent>();

                UIRenderer::DrawString("[ Transform ]", leftPadding, currentY, { 0.7f, 0.9f, 0.7f, 1.0f });
                currentY += lineHeight;

                std::string posX = "Position X: " + std::to_string(transform.Translation.x);
                std::string posY = "Position Y: " + std::to_string(transform.Translation.y);
                std::string posZ = "Position Z: " + std::to_string(transform.Translation.z);

                UIRenderer::DrawString(posX, leftPadding, currentY, textColor); currentY += lineHeight;
                UIRenderer::DrawString(posY, leftPadding, currentY, textColor); currentY += lineHeight;
                UIRenderer::DrawString(posZ, leftPadding, currentY, textColor); currentY += lineHeight;
            }
        }

    }
}