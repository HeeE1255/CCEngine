#pragma once
#include "Panel.h"
//#include "imgui.h" 
#include "Renderer/UIRenderer.h"
#include "DirectXMath.h"

namespace CCEngine {
    namespace UI {

        Panel::Panel(const std::string& name, DirectX::XMFLOAT4  color)
            : Widget(name), m_Color(color)
        {
        }

        void Panel::OnRender()
        {

            if (!m_IsVisible) return;

            // 마우스 호버 상태에 따른 색상 결정
            DirectX::XMFLOAT4 currentColor = m_IsHovered ? m_HoverColor : m_Color;
            DirectX::XMFLOAT4 dxColor = { currentColor.x, currentColor.y, currentColor.z, currentColor.w };

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