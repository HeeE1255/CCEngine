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
            float ScrollY = 0.0f;           // 현재 스크롤된 양
            float ContentHeight = 0.0f;     // 전체 내용물의 높이
            float ViewportHeight = 0.0f;    // 화면에 보이는 높이
            float ScrollSpeed = 40.0f;

            float GetMaxScroll() const { return (std::max)(0.0f, ContentHeight - ViewportHeight); }

            void ApplyScroll(float delta) 
            {
                ScrollY -= delta * ScrollSpeed;
                ScrollY = (std::clamp)(ScrollY, 0.0f, GetMaxScroll());
            }

            // 스크롤바 손잡이(Thumb)의 높이 계산
            float GetThumbHeight() const 
            {
                if (ContentHeight <= 0) return ViewportHeight;
                float ratio = ViewportHeight / ContentHeight;
                return (std::max)(ViewportHeight * ratio, 30.0f); // 최소 30픽셀 보장
            }

            // 스크롤바 손잡이의 Y 위치 계산
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
            Widget* GetParent() const{return m_Parent;}
            const std::vector<Widget*>& GetChildren() const{return m_Children;}

            virtual void OnUpdate(float deltaTime);
            virtual void OnRender();
            virtual void UpdateLayout(const DirectX::XMFLOAT2& parentPos, const DirectX::XMFLOAT2& parentSize);

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

            DirectX::XMFLOAT2 GetAnchorMin() const { return m_AnchorMin; }
			DirectX::XMFLOAT2 GetAnchorMax() const { return m_AnchorMax; }
			DirectX::XMFLOAT2 GetOffsetMin() const { return m_OffsetMin; }
			DirectX::XMFLOAT2 GetOffsetMax() const { return m_OffsetMax; }

           DirectX::XMFLOAT2 GetCalculatedPosition() const{return m_CalculatedPos;}
           DirectX::XMFLOAT2 GetCalculatedSize() const{return m_CalculatedSize;}
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

            DirectX::XMFLOAT2 m_AnchorMin = { 0.0f, 0.0f };
            DirectX::XMFLOAT2 m_AnchorMax = { 0.0f, 0.0f };
            DirectX::XMFLOAT2 m_OffsetMin = { 0.0f, 0.0f };
            DirectX::XMFLOAT2 m_OffsetMax = { 0.0f, 0.0f };

            DirectX::XMFLOAT2 m_CalculatedPos = { 0.0f, 0.0f };
            DirectX::XMFLOAT2 m_CalculatedSize = { 0.0f, 0.0f };
        };
    }
}