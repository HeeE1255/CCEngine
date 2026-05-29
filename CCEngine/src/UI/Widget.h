#pragma once
#include "Core.h"
#include "Application.h"
#include "Events/Event.h"
#include "Events/MouseEvent.h"
#include <DirectXMath.h>
#include <vector>
#include <string>
#include <algorithm>

namespace CCEngine
{
    namespace UI
    {
        struct ScrollState
        {
            float ScrollY = 0.0f;
            float ContentHeight = 0.0f;
            float ViewportHeight = 0.0f;
            float ScrollSpeed = 40.0f;

            float GetMaxScroll() const { return (std::max)(0.0f, ContentHeight - ViewportHeight); }

            void ApplyScroll(float delta)
            {
                ScrollY -= delta * ScrollSpeed;
                ScrollY = (std::clamp)(ScrollY, 0.0f, GetMaxScroll());
            }

            float GetThumbHeight() const
            {
                if (ContentHeight <= 0) return ViewportHeight;
                float ratio = ViewportHeight / ContentHeight;
                return (std::max)(ViewportHeight * ratio, 30.0f);
            }

            float GetThumbY(float trackStartY) const
            {
                if (GetMaxScroll() <= 0) return trackStartY;
                float scrollRatio = ScrollY / GetMaxScroll();
                float trackSpace = ViewportHeight - GetThumbHeight();
                return trackStartY + (scrollRatio * trackSpace);
            }
        };

        class CC_API Widget
        {
        public:
            Widget(const std::string& name = "Widget");
            virtual ~Widget();

            void AddChild(Widget* child);
            void RemoveChild(Widget* child);
            Widget* GetParent() const { return m_Parent; }
            const std::vector<Widget*>& GetChildren() const { return m_Children; }

            virtual void OnUpdate(float deltaTime);
            virtual void OnRender();
            virtual void UpdateLayout(const DirectX::XMFLOAT2& parentPos, const DirectX::XMFLOAT2& parentSize);

            virtual bool OnEvent(Event& e);
            virtual bool WantsMouseCapture() const { return false; }

            bool IsPointInside(float mouseX, float mouseY) const
            {
                return mouseX >= m_CalculatedPos.x &&
                    mouseX <= m_CalculatedPos.x + m_CalculatedSize.x &&
                    mouseY >= m_CalculatedPos.y &&
                    mouseY <= m_CalculatedPos.y + m_CalculatedSize.y;
            }

            void BringToFront()
            {
                if (m_Parent)
                {
                    auto it = std::find(m_Parent->m_Children.begin(), m_Parent->m_Children.end(), this);
                    if (it != m_Parent->m_Children.end())
                    {
                        m_Parent->m_Children.erase(it);
                        m_Parent->m_Children.push_back(this);
                    }
                }
            }

            void ClearChildren()
            {
                for (Widget* child : m_Children)
                {
                    delete child;
                }
                m_Children.clear();
            }

            void SetVisible(bool visible) { m_IsVisible = visible; }
            bool IsVisible() const { return m_IsVisible; }

            void SetAnchorMin(float x, float y) { m_AnchorMin = { x, y }; }
            void SetAnchorMax(float x, float y) { m_AnchorMax = { x, y }; }

            void SetOffsetMin(float left, float top) { m_OffsetMin = { left, top }; }
            void SetOffsetMax(float right, float bottom) { m_OffsetMax = { right, bottom }; }

            DirectX::XMFLOAT2 GetAnchorMin() const { return m_AnchorMin; }
            DirectX::XMFLOAT2 GetAnchorMax() const { return m_AnchorMax; }
            DirectX::XMFLOAT2 GetOffsetMin() const { return m_OffsetMin; }
            DirectX::XMFLOAT2 GetOffsetMax() const { return m_OffsetMax; }

            DirectX::XMFLOAT2 GetCalculatedPosition() const { return m_CalculatedPos; }
            DirectX::XMFLOAT2 GetCalculatedSize() const { return m_CalculatedSize; }
            void SetIndentLevel(float level) { m_IndentLevel = level; }
            float GetIndentLevel() const { return m_IndentLevel; }

            std::string GetName() const { return m_Name; }

            void SetSize(float width, float height);
            void SetPosition(float x, float y);
            DirectX::XMFLOAT2 GetSize() const { return { m_OffsetMax.x - m_OffsetMin.x, m_OffsetMax.y - m_OffsetMin.y }; }

            // ==============================================================
            // 자식 렌더링을 위젯 영역으로 제한할 때 사용합니다.
            // ==============================================================
            void SetClipToBounds(bool clip) { m_ClipToBounds = clip; }
            void SetClipPadding(float left, float top, float right, float bottom) { m_ClipPadding = { left, top, right, bottom }; }

        protected:
            virtual bool OnMouseButtonPressed(MouseButtonPressedEvent& e) { return false; }
            virtual bool OnMouseMoved(MouseMovedEvent& e) { return false; }
            virtual bool OnMouseButtonReleased(MouseButtonReleasedEvent& e) { return false; }

            std::string m_Name;
            bool m_IsVisible = true;

            Widget* m_Parent = nullptr;
            std::vector<Widget*> m_Children;
            float m_IndentLevel = 0.0f;

            DirectX::XMFLOAT2 m_AnchorMin = { 0.0f, 0.0f };
            DirectX::XMFLOAT2 m_AnchorMax = { 0.0f, 0.0f };
            DirectX::XMFLOAT2 m_OffsetMin = { 0.0f, 0.0f };
            DirectX::XMFLOAT2 m_OffsetMax = { 0.0f, 0.0f };

            DirectX::XMFLOAT2 m_CalculatedPos = { 0.0f, 0.0f };
            DirectX::XMFLOAT2 m_CalculatedSize = { 0.0f, 0.0f };

            bool m_ClipToBounds = false;
            DirectX::XMFLOAT4 m_ClipPadding = { 0.0f, 0.0f, 0.0f, 0.0f }; // 좌, 상, 우, 하
        };
    }
}
