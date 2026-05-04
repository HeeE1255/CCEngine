#pragma once
#include "Core.h"
#include "Application.h"
#include "Events/Event.h"
#include "Events/MouseEvent.h"
#include <vector>
#include <string>
#include <algorithm>

namespace CCEngine
{
    namespace UI
    {
        struct UIVec2
        {
            float x, y;
        };

        struct UIColor
        {
            float r, g, b, a;
        };

        class CC_API Widget
        {
        public:
            Widget(const std::string& name = "Widget");
            virtual ~Widget();

            void AddChild(Widget* child);
            void RemoveChild(Widget* child);
            Widget* GetParent() const{return m_Parent;}
            const std::vector<Widget*>& GetChildren() const{return m_Children;}

            virtual void OnUpdate(float deltaTime);
            virtual void OnRender();
            virtual void UpdateLayout(const UIVec2& parentPos, const UIVec2& parentSize);

            // 이벤트 처리 함수: 마우스 클릭, 이동, 키보드 입력 등
            virtual bool OnEvent(Event& e);

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
                        m_Parent->m_Children.push_back(this); // 맨 뒤로 보내면 렌더링 시 맨 위에 그려짐
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

            void SetVisible(bool visible){m_IsVisible = visible;}
            bool IsVisible() const{return m_IsVisible;}

            void SetAnchorMin(float x, float y){m_AnchorMin = { x, y };}
            void SetAnchorMax(float x, float y){m_AnchorMax = { x, y };}

            void SetOffsetMin(float left, float top){m_OffsetMin = { left, top };}
            void SetOffsetMax(float right, float bottom){m_OffsetMax = { right, bottom };}

           UIVec2 GetCalculatedPosition() const{return m_CalculatedPos;}
           UIVec2 GetCalculatedSize() const{return m_CalculatedSize;}
           void SetIndentLevel(float level) { m_IndentLevel = level; } 
           float GetIndentLevel() const { return m_IndentLevel; }

		   std::string GetName() const { return m_Name; }

           void SetSize(float width, float height);
           void SetPosition(float x, float y);

        protected:
            virtual bool OnMouseButtonPressed(MouseButtonPressedEvent& e) {  return false; }
            virtual bool OnMouseMoved(MouseMovedEvent& e) { return false; }
            virtual bool OnMouseButtonReleased(MouseButtonReleasedEvent& e) { return false; }


            std::string m_Name;
            bool m_IsVisible = true;

            Widget* m_Parent = nullptr;
            std::vector<Widget*> m_Children;
            float m_IndentLevel = 0.0f; 

            UIVec2 m_AnchorMin = { 0.0f, 0.0f };
            UIVec2 m_AnchorMax = { 0.0f, 0.0f };
            UIVec2 m_OffsetMin = { 0.0f, 0.0f };
            UIVec2 m_OffsetMax = { 0.0f, 0.0f };

            UIVec2 m_CalculatedPos = { 0.0f, 0.0f };
            UIVec2 m_CalculatedSize = { 0.0f, 0.0f };
        };
    }
}