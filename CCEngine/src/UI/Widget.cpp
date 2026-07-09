#include "Widget.h"
#include "Events/ApplicationEvent.h"
#include "Renderer/Renderer2D.h"    
#include "Renderer/RenderCommand.h" 

namespace CCEngine {
    namespace UI {

        Widget* Widget::s_MouseInteractionOwner = nullptr;
        Widget* Widget::s_KeyboardFocusOwner = nullptr;
        Window* Widget::s_CurrentRenderWindow = nullptr;
        bool Widget::s_CurrentRenderWindowMouseActive = true;

        Widget::Widget(const std::string& name) : m_Name(name) {}

        Widget::~Widget()
        {
            if (s_MouseInteractionOwner == this)
                s_MouseInteractionOwner = nullptr;
            if (s_KeyboardFocusOwner == this)
                s_KeyboardFocusOwner = nullptr;

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
                e.GetEventType() == EventType::MouseButtonReleased ||
                e.GetEventType() == EventType::FileDrop);
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
                else if (e.GetEventType() == EventType::FileDrop)
                {
                    FileDropEvent& fde = static_cast<FileDropEvent&>(e);
                    mouseX = fde.GetX(); mouseY = fde.GetY();
                }
            }

            if (!m_IsVisible) return false;

            if (e.Handled) return true;

            if (e.GetEventType() == EventType::MouseButtonPressed &&
                s_KeyboardFocusOwner &&
                !s_KeyboardFocusOwner->IsPointInside(mouseX, mouseY))
            {
                // 입력칸 밖을 누르면 키보드 포커스를 비운다.
                // 이렇게 해야 값 입력 후 다른 UI를 눌렀을 때 편집 색이 남지 않는다.
                s_KeyboardFocusOwner = nullptr;
            }

            if (isMouseEvent && s_MouseInteractionOwner && IsMouseInteractionBlockedFor(this))
            {
                e.Handled = true;
                return true;
            }

            auto hasMouseCapture = [](Widget* widget, const auto& self) -> bool
                {
                    if (!widget || !widget->IsVisible())
                        return false;

                    if (widget->WantsMouseCapture())
                        return true;

                    for (Widget* child : widget->GetChildren())
                    {
                        if (self(child, self))
                            return true;
                    }
                    return false;
                };

            // 콜백 안에서 BringToFront처럼 자식 순서를 바꾸면 원본 vector iterator가 무효화된다.
            // 이벤트 디스패치 중에는 스냅샷을 순회해서 메뉴/창 정렬 변경으로 인한 Debug assertion을 막는다.
            auto childrenSnapshot = m_Children;

            if (isMouseEvent)
            {
                for (auto it = childrenSnapshot.rbegin(); it != childrenSnapshot.rend(); ++it)
                {
                    Widget* child = *it;
                    if (!child || !child->IsVisible())
                        continue;

                    if (!hasMouseCapture(child, hasMouseCapture))
                        continue;

                    // 드래그/리사이즈 중인 위젯은 마우스를 캡처한 상태다.
                    // 이때는 뒤쪽 패널에 hover/click 이벤트가 새지 않도록 캡처 위젯에만 전달한다.
                    bool handled = child->OnEvent(e);
                    e.Handled = true;
                    return handled || true;
                }
            }

            for (auto it = childrenSnapshot.rbegin(); it != childrenSnapshot.rend(); ++it)
            {
                if (!(*it)->IsVisible())
                    continue;

                if (isMouseEvent && !(*it)->IsPointInside(mouseX, mouseY) && !(*it)->WantsMouseCapture())
                    continue;

                if ((*it)->OnEvent(e)) return true;

                if (isMouseEvent && (*it)->BlocksMouseEvents() && (*it)->IsPointInside(mouseX, mouseY))
                {
                    e.Handled = true;
                    return true;
                }
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
            else if (e.GetEventType() == EventType::KeyPressed)
            {
                if (OnKeyPressed(static_cast<KeyPressedEvent&>(e))) return true;
            }
            else if (e.GetEventType() == EventType::TextInput)
            {
                if (OnTextInput(static_cast<TextInputEvent&>(e))) return true;
            }

            return false;
        }

        namespace
        {
            bool ContainsMouseCaptureWidget(const Widget* widget)
            {
                if (!widget || !widget->IsVisible())
                    return false;

                if (widget->WantsMouseCapture())
                    return true;

                const auto& children = widget->GetChildren();
                for (auto it = children.rbegin(); it != children.rend(); ++it)
                {
                    if (ContainsMouseCaptureWidget(*it))
                        return true;
                }

                return false;
            }

            bool ContainsBlockingWidgetAt(const Widget* widget, float mouseX, float mouseY)
            {
                if (!widget || !widget->IsVisible() || !widget->IsPointInside(mouseX, mouseY))
                    return false;

                if (widget->BlocksMouseEvents())
                    return true;

                const auto& children = widget->GetChildren();
                for (auto it = children.rbegin(); it != children.rend(); ++it)
                {
                    if (ContainsBlockingWidgetAt(*it, mouseX, mouseY))
                        return true;
                }

                return false;
            }
        }

        bool Widget::IsMouseBlockedByWidgetAbove(float mouseX, float mouseY) const
        {
            if (IsMouseInteractionBlockedFor(this))
                return true;

            const Widget* childOnPath = this;
            const Widget* parent = m_Parent;

            while (parent)
            {
                const auto& siblings = parent->GetChildren();
                auto it = std::find(siblings.begin(), siblings.end(), childOnPath);
                if (it != siblings.end())
                {
                    ++it;
                    for (; it != siblings.end(); ++it)
                    {
                        if (ContainsMouseCaptureWidget(*it))
                            return true;

                        if (ContainsBlockingWidgetAt(*it, mouseX, mouseY))
                            return true;
                    }
                }

                childOnPath = parent;
                parent = parent->GetParent();
            }

            return false;
        }

        void Widget::BeginMouseInteraction(Widget* owner)
        {
            if (owner)
                s_MouseInteractionOwner = owner;
        }

        void Widget::EndMouseInteraction(Widget* owner)
        {
            if (!owner || s_MouseInteractionOwner == owner)
                s_MouseInteractionOwner = nullptr;
        }

        bool Widget::IsMouseInteractionActive()
        {
            // 창 이동이나 리사이즈 중에는 마우스 아래에 있는 다른 위젯이 hover/click을 받으면 안 된다.
            // 현재 조작을 시작한 위젯 하나를 캡처 소유자로 두고, 렌더/이벤트 쪽에서 이 값을 공통으로 본다.
            return s_MouseInteractionOwner != nullptr;
        }

        bool Widget::IsMouseInteractionBlockedFor(const Widget* widget)
        {
            if (!s_MouseInteractionOwner)
                return false;

            const Widget* ownerPath = s_MouseInteractionOwner;
            while (ownerPath)
            {
                if (ownerPath == widget)
                    return false;
                ownerPath = ownerPath->GetParent();
            }

            const Widget* current = widget;
            while (current)
            {
                if (current == s_MouseInteractionOwner)
                    return false;
                current = current->GetParent();
            }

            return true;
        }

        void Widget::SetKeyboardFocus(Widget* owner)
        {
            s_KeyboardFocusOwner = owner;
        }

        void Widget::ClearKeyboardFocus(Widget* owner)
        {
            if (!owner || s_KeyboardFocusOwner == owner)
                s_KeyboardFocusOwner = nullptr;
        }

        bool Widget::IsKeyboardFocusOwner(const Widget* widget)
        {
            return widget && s_KeyboardFocusOwner == widget;
        }

        void Widget::SetCurrentRenderWindow(Window* window)
        {
            s_CurrentRenderWindow = window;
        }

        Window* Widget::GetCurrentRenderWindow()
        {
            return s_CurrentRenderWindow;
        }

        void Widget::SetCurrentRenderWindowMouseActive(bool active)
        {
            s_CurrentRenderWindowMouseActive = active;
        }

        bool Widget::IsCurrentRenderWindowMouseActive()
        {
            return s_CurrentRenderWindowMouseActive;
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
