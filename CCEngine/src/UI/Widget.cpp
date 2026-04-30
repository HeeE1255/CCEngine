#include "Widget.h"

namespace CCEngine {
    namespace UI {

        Widget::Widget(const std::string& name) : m_Name(name) {}

        Widget::~Widget()
        {
            // 부모가 소멸될 때 자식들도 함께 메모리에서 해제합니다 (메모리 누수 방지)
            for (auto child : m_Children)
            {
                delete child;
            }
            m_Children.clear();
        }

        void Widget::AddChild(Widget* child)
        {
            if (child->m_Parent) {
                child->m_Parent->RemoveChild(child);
            }
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

        void Widget::UpdateLayout(const UIVec2& parentPos, const UIVec2& parentSize)
        {
            if(!m_IsVisible) return;

            // 1. 앵커(비율)를 기반으로 내 사각형의 가이드라인(기준점)을 잡습니다.
            float anchorLeft = parentPos.x + (parentSize.x * m_AnchorMin.x);
            float anchorTop = parentPos.y + (parentSize.y * m_AnchorMin.y);
            float anchorRight = parentPos.x + (parentSize.x * m_AnchorMax.x);
            float anchorBottom = parentPos.y + (parentSize.y * m_AnchorMax.y);

            // 2. 오프셋(절대 픽셀)을 가이드라인에 더해 최종 Rect를 계산합니다.
            float finalLeft = anchorLeft + m_OffsetMin.x;
            float finalTop = anchorTop + m_OffsetMin.y;
            float finalRight = anchorRight + m_OffsetMax.x;
            float finalBottom = anchorBottom + m_OffsetMax.y;

            // 3. 렌더러가 그릴 수 있도록 최종 위치와 크기를 업데이트 합니다.
            m_CalculatedPos = { finalLeft, finalTop };
            m_CalculatedSize = { finalRight - finalLeft, finalBottom - finalTop };

            // 4. 내 크기가 정해졌으니, 자식들에게 내 위치와 크기를 넘겨주며 연쇄 업데이트를 지시합니다.
            for (auto child : m_Children)
            {
                child->UpdateLayout(m_CalculatedPos, m_CalculatedSize);
            }
        }

        bool Widget::OnEvent(Event& e)
        {
            // Determine if this is a mouse-related event and extract mouse coords early.
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


            if (!m_IsVisible) return false; // 숨겨진 UI는 클릭 불가

            if (e.Handled)
            {
                return true;
            }

            // 1. 역순회 (Reverse Traversal): 나보다 늦게 그려진(맨 위에 있는) 자식부터 검사!
            for (auto it = m_Children.rbegin(); it != m_Children.rend(); ++it)
            {
                // If this is a mouse event and the mouse isn't inside the child's rect,
                // skip calling OnEvent on that child (prune traversal).
                if (isMouseEvent && !(*it)->IsPointInside(mouseX, mouseY))
                    continue;

                if ((*it)->OnEvent(e))
                {
                    // 자식 중 누군가가 이벤트를 먹었다면(true 반환),
                    // 여기서 탐색을 즉시 종료하고 나도 true를 반환해서 상위로 전달!
                    return true;
                }
            }

            // 2. 자식들이 아무도 안 먹었다면, 이제 내가 먹을 차례인지 확인
            if (e.GetEventType() == EventType::MouseMoved)
            {
                MouseMovedEvent& mouseEvent = static_cast<MouseMovedEvent&>(e);
                return OnMouseMoved(mouseEvent);
            }
            else if (e.GetEventType() == EventType::MouseButtonPressed)
            {
                MouseButtonPressedEvent& mouseEvent = static_cast<MouseButtonPressedEvent&>(e);
                return OnMouseButtonPressed(mouseEvent);
            }
            else if (e.GetEventType() == EventType::MouseButtonReleased)
            {
                // 1. 나 자신의 로직 실행 (여기서 WindowPanel의 Redock이 실행됨!)
                if (OnMouseButtonReleased(static_cast<MouseButtonReleasedEvent&>(e)))
                {
                    return true; // 내가 이벤트를 먹었다면(Handled) 종료
                }

                // Note: children were already checked in the reverse traversal above,
                // avoid re-dispatching to children here to prevent duplicate handling
                // or reentrancy issues.
            }

            return false; // 나도 관심 없는 이벤트다
        }

        void Widget::SetSize(float width, float height)
        {
            // 기존의 OffsetMin(Left, Top) 시작점은 유지하고 크기만 변경합니다.
            m_OffsetMax.x = m_OffsetMin.x + width;
            m_OffsetMax.y = m_OffsetMin.y + height;
        }

        void Widget::SetPosition(float x, float y)
        {
            // 1. 위치를 명시적으로 잡으려면 앵커를 부모의 좌상단(0,0)으로 묶는 것이 안전합니다.
            SetAnchorMin(0.0f, 0.0f);
            SetAnchorMax(0.0f, 0.0f);

            // 2. 현재 설정된 Width, Height를 계산해 둡니다.
            float width = m_OffsetMax.x - m_OffsetMin.x;
            float height = m_OffsetMax.y - m_OffsetMin.y;

            // 3. 새로운 위치로 Offset을 통째로 옮깁니다.
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

            for (auto child : m_Children) 
            {
                child->OnRender();
            }
        }

    } // namespace UI
} // namespace CCEngine