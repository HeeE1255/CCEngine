#pragma once
#include "ImageWidget.h"
#include "Renderer/UIRenderer.h"
//#include "imgui.h"
#include <iostream>

namespace CCEngine 
{
    namespace UI 
    {

        ImageWidget::ImageWidget(const std::string& name, void* textureID)
            : Widget(name), m_TextureID(textureID)
        {
        }

        void ImageWidget::OnRender()
        {
            //if (!m_IsVisible || !m_TextureID) return;

            //ImDrawList* drawList = ImGui::GetForegroundDrawList();

            //ImVec2 p_min = ImVec2(m_CalculatedPos.x, m_CalculatedPos.y);
            //ImVec2 p_max = ImVec2(m_CalculatedPos.x + m_CalculatedSize.x, m_CalculatedPos.y + m_CalculatedSize.y);

            //// 1. 텍스처 그리기
            //drawList->AddImage((ImTextureID)m_TextureID, p_min, p_max);

            //// 2. 텍스처가 덮어버리지 못하게 5px 두께의 '테두리'만 강제로 그립니다.
            ////drawList->AddRect(p_min, p_max, IM_COL32(255, 0, 0, 255), 0.0f, 0, 5.0f);

            //std::string debugText = m_Name + " | Pos: " + std::to_string(int(p_min.x)) + ", " + std::to_string(int(p_min.y)) + " | Size: " + std::to_string(int(m_CalculatedSize.x)) + ", " + std::to_string(int(m_CalculatedSize.y));
            //drawList->AddText(ImVec2(p_min.x + 5, p_min.y + 5), IM_COL32(0, 255, 0, 255), debugText.c_str());

            //Widget::OnRender();

            if (!m_IsVisible || !m_TextureID) return;

            // ★ ImGui를 버리고 UIRenderer를 사용!
            // m_TextureID는 DX11Texture2D 객체이거나 SRV 포인터일 것입니다.
            UIRenderer::DrawImage(
                m_CalculatedPos.x,
                m_CalculatedPos.y,
                m_CalculatedSize.x,
                m_CalculatedSize.y,
                m_TextureID
            );

            Widget::OnRender();
        }

        bool ImageWidget::OnMouseButtonPressed(MouseButtonPressedEvent& e)
        {
            float edgeSize = 8.0f;
            if (e.GetX() >= (m_CalculatedPos.x + m_CalculatedSize.x - edgeSize) ||
                e.GetY() >= (m_CalculatedPos.y + m_CalculatedSize.y - edgeSize))
            {
                return false; // 내가 안 먹고 부모(WindowPanel)에게 넘김!
            }

            if (e.GetX() >= m_CalculatedPos.x && e.GetX() <= m_CalculatedPos.x + m_CalculatedSize.x &&
                e.GetY() >= m_CalculatedPos.y && e.GetY() <= m_CalculatedPos.y + m_CalculatedSize.y)
            {
                // 0번(좌클릭)일 때만 콜백 실행
                if (e.GetButton() == 0 && m_OnMouseDown)
                {
                    m_OnMouseDown(e.GetX(), e.GetY());

                    // 이벤트를 여기서 먹어치움! (뒤에 있는 다른 패널로 넘어가지 않게 함)
                    e.Handled = true;
                    return true;
                }
            }

            return false;
        }

    }
}