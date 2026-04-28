#pragma once
#include "Panel.h"
//#include "imgui.h" 
#include "Renderer/UIRenderer.h"

namespace CCEngine {
    namespace UI {

        Panel::Panel(const std::string& name, UIColor color)
            : Widget(name), m_Color(color)
        {
        }

        void Panel::OnRender()
        {

            if (!m_IsVisible) return;

            // 마우스 호버 상태에 따른 색상 결정
            UIColor currentColor = m_IsHovered ? m_HoverColor : m_Color;
            DirectX::XMFLOAT4 dxColor = { currentColor.r, currentColor.g, currentColor.b, currentColor.a };

            // ★ ImGui를 버리고 100% 자체 UI 엔진으로 렌더링!
            // 절대 픽셀 좌표계로 사각형을 쏴줍니다.
            UIRenderer::DrawRectFilled(
                m_CalculatedPos.x,
                m_CalculatedPos.y,
                m_CalculatedSize.x,
                m_CalculatedSize.y,
                dxColor
            );

            Widget::OnRender();
        }

    }
}