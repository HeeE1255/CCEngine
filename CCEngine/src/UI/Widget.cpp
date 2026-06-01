#include "Widget.h"
#include "Renderer/Renderer2D.h"    
#include "Renderer/RenderCommand.h" 

namespace CCEngine {
    namespace UI {

        Widget::Widget(const std::string& name) : m_Name(name) {}

        Widget::~Widget()
        {
            for (auto child : m_Children)
            {
                delete child;
            }
            m_Children.clear();
        }

        void Widget::AddChild(Widget* child)
        {
            if (!child || child == this)
                return;

            if (child->m_Parent) {
                child->m_Parent->RemoveChild(child);
            }

            RemoveDescendant(child);

            child->m_Parent = this;
            m_Children.push_back(child);
        }

        void Widget::RemoveChild(Widget* child)
        {
            auto it = std::find(m_Children.begin(), m_Children.end(), child);
            if (it != m_Children.end())
            {
                (*it)->m_Parent = nullptr;
                m_Children.erase(it);
            }
        }

        bool Widget::RemoveDescendant(Widget* descendant)
        {
            if (!descendant)
                return false;

            bool removed = false;
            for (auto it = m_Children.begin(); it != m_Children.end(); )
            {
                Widget* child = *it;
                if (child == descendant)
                {
                    child->m_Parent = nullptr;
                    it = m_Children.erase(it);
                    removed = true;
                    continue;
                }

                if (child && child->RemoveDescendant(descendant))
                    removed = true;

                ++it;
            }

            return removed;
        }

        void Widget::UpdateLayout(const DirectX::XMFLOAT2& parentPos, const DirectX::XMFLOAT2& parentSize)
        {
            if (!m_IsVisible) return;

            float anchorLeft = parentPos.x + (parentSize.x * m_AnchorMin.x);
            float anchorTop = parentPos.y + (parentSize.y * m_AnchorMin.y);
            float anchorRight = parentPos.x + (parentSize.x * m_AnchorMax.x);
            float anchorBottom = parentPos.y + (parentSize.y * m_AnchorMax.y);

            float finalLeft = anchorLeft + m_OffsetMin.x;
            float finalTop = anchorTop + m_OffsetMin.y;
            float finalRight = anchorRight + m_OffsetMax.x;
            float finalBottom = anchorBottom + m_OffsetMax.y;

            m_CalculatedPos = { finalLeft, finalTop };
            m_CalculatedSize = { finalRight - finalLeft, finalBottom - finalTop };

            for (auto child : m_Children)
            {
                child->UpdateLayout(m_CalculatedPos, m_CalculatedSize);
            }
        }

        bool Widget::OnEvent(Event& e)
        {
            bool isMouseEvent = (e.GetEventType() == EventType::MouseMoved ||
                e.GetEventType() == EventType::MouseButtonPressed ||
                e.GetEventType() == EventType::MouseButtonReleased);
            float mouseX = 0.0f, mouseY = 0.0f;
            if (isMouseEvent)
            {
                if (e.GetEventType() == EventType::MouseMoved)
                {
                    MouseMovedEvent& me = static_cast<MouseMovedEvent&>(e);
                    mouseX = me.GetX(); mouseY = me.GetY();
                }
                else if (e.GetEventType() == EventType::MouseButtonPressed)
                {
                    MouseButtonPressedEvent& mbe = static_cast<MouseButtonPressedEvent&>(e);
                    mouseX = mbe.GetX(); mouseY = mbe.GetY();
                }
                else if (e.GetEventType() == EventType::MouseButtonReleased)
                {
                    MouseButtonReleasedEvent& mre = static_cast<MouseButtonReleasedEvent&>(e);
                    mouseX = mre.GetX(); mouseY = mre.GetY();
                }
            }

            if (!m_IsVisible) return false;

            if (e.Handled) return true;

            for (auto it = m_Children.rbegin(); it != m_Children.rend(); ++it)
            {
                if (isMouseEvent && !(*it)->IsPointInside(mouseX, mouseY) && !(*it)->WantsMouseCapture())
                    continue;

                if ((*it)->OnEvent(e)) return true;
            }

            if (e.GetEventType() == EventType::MouseMoved)
            {
                return OnMouseMoved(static_cast<MouseMovedEvent&>(e));
            }
            else if (e.GetEventType() == EventType::MouseButtonPressed)
            {
                return OnMouseButtonPressed(static_cast<MouseButtonPressedEvent&>(e));
            }
            else if (e.GetEventType() == EventType::MouseButtonReleased)
            {
                if (OnMouseButtonReleased(static_cast<MouseButtonReleasedEvent&>(e))) return true;
            }

            return false;
        }

        void Widget::SetSize(float width, float height)
        {
            m_OffsetMax.x = m_OffsetMin.x + width;
            m_OffsetMax.y = m_OffsetMin.y + height;
        }

        void Widget::SetPosition(float x, float y)
        {
            SetAnchorMin(0.0f, 0.0f);
            SetAnchorMax(0.0f, 0.0f);

            float width = m_OffsetMax.x - m_OffsetMin.x;
            float height = m_OffsetMax.y - m_OffsetMin.y;

            m_OffsetMin = { x, y };
            m_OffsetMax = { x + width, y + height };
        }

        void Widget::OnUpdate(float deltaTime)
        {
            for (auto child : m_Children)
            {
                child->OnUpdate(deltaTime);
            }
        }

        void Widget::OnRender()
        {
            if (!m_IsVisible) return;

            bool applyScissor = m_ClipToBounds && m_CalculatedSize.x > 0.0f && m_CalculatedSize.y > 0.0f;

            if (applyScissor)
            {
                Renderer2D::Flush();
                Renderer2D::StartBatch();

                // 패딩을 적용한 실제 가위질 영역 계산
                float sX = m_CalculatedPos.x + m_ClipPadding.x;
                float sY = m_CalculatedPos.y + m_ClipPadding.y;
                float sW = m_CalculatedSize.x - m_ClipPadding.x - m_ClipPadding.z;
                float sH = m_CalculatedSize.y - m_ClipPadding.y - m_ClipPadding.w;

                if (sW < 0.0f) sW = 0.0f;
                if (sH < 0.0f) sH = 0.0f;

                RenderCommand::SetScissorEnable(true);
                RenderCommand::SetScissor((uint32_t)sX, (uint32_t)sY, (uint32_t)sW, (uint32_t)sH);
            }

            // 자식들 순회하며 그리기 (가위질 영역 밖은 GPU가 자동으로 잘라버림)
            for (auto child : m_Children)
            {
                child->OnRender();
            }

            if (applyScissor)
            {
                Renderer2D::Flush();
                Renderer2D::StartBatch();
                RenderCommand::SetScissorEnable(false);
            }
        }

    }
}
