#include "InspectorPanel.h"
#include "InspectorRegistry.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/UIRenderer.h"
#include "Renderer/Renderer2D.h"
#include "Application.h"
#include <vector>

namespace CCEngine
{
    namespace UI
    {

        InspectorPanel::InspectorPanel(const std::string& name, const std::string& title)
            : WindowPanel(name, title)
        {
            SetClipToBounds(true);
        }


        void InspectorPanel::SetSelectedEntity(Entity entity)
        {
            if (m_SelectedEntity == entity) return;

            m_SelectedEntity = entity;
            m_Children.clear();

            if (!m_SelectedEntity) return;

            // UI 컴포넌트 생성
            InspectorRegistry::DrawAllComponents(this, m_SelectedEntity);

            // ★ 자식이 생성된 직후 바로 자신의 레이아웃 갱신
            auto& window = CCEngine::Application::Get()->GetWindow();
            UpdateLayout({ 0.0f, 0.0f }, { (float)window.GetWidth(), (float)window.GetHeight() });
        }

        void InspectorPanel::OnRender()
        {
            if (!m_IsVisible) return;

            float rightPadding = (m_ScrollState.GetMaxScroll() > 0) ? 22.0f : 0.0f;
            SetClipPadding(0.0f, 40.0f, rightPadding, 0.0f);

            UI::WindowPanel::OnRender();

            // 스크롤바 그리기
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
            WindowPanel::UpdateLayout(parentPos, parentSize);

            if (!m_IsVisible || !m_SelectedEntity) return;

            float startY = 40.0f;
            float currentY = startY - m_ScrollState.ScrollY;

            for (auto child : m_Children)
            {
                if (!child->IsVisible()) continue;

                child->SetOffsetMin(0.0f, currentY);
                child->SetOffsetMax(0.0f, currentY);
                child->UpdateLayout(m_CalculatedPos, m_CalculatedSize);

                currentY += child->GetCalculatedSize().y + 4.0f;
            }

            m_ScrollState.ContentHeight = (currentY + m_ScrollState.ScrollY) - startY;
            m_ScrollState.ViewportHeight = m_CalculatedSize.y - startY;
        }

    }
}