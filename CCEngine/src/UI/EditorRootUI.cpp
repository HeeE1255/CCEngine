#include "EditorRootUI.h"
#include "Renderer/UIRenderer.h"

namespace CCEngine {
    namespace UI {
        EditorRootUI::EditorRootUI(const std::string& name)
            : Panel(name, { 0.1f, 0.1f, 0.1f, 1.0f })
        {
            // Root UI는 화면 전체를 덮도록 앵커 설정
            SetAnchorMin(0.0f, 0.0f);
            SetAnchorMax(1.0f, 1.0f);
        }

        void EditorRootUI::OnRender()
        {
            Panel::OnRender(); // 자식 패널들 먼저 렌더링

            auto& mainWindow = Application::Get()->GetWindow();
            float windowWidth = (float)mainWindow.GetWidth();

            // 1. 메인 윈도우 상단 24px 타이틀 바 렌더링
            DirectX::XMFLOAT4 titleColor = { 0.15f, 0.15f, 0.17f, 1.0f };
            UIRenderer::DrawRectFilled(0.0f, 0.0f, windowWidth, 24.0f, titleColor);
            UIRenderer::DrawString("CCEngine Main", 10.0f, 4.0f, { 0.8f, 0.8f, 0.8f, 1.0f });

            // 2. 우측 닫기 버튼 영역 렌더링 (30px x 24px)
            float closeBtnX = windowWidth - 30.0f;
            UIRenderer::DrawRectFilled(closeBtnX, 0.0f, 30.0f, 24.0f, { 0.8f, 0.2f, 0.2f, 1.0f });
            UIRenderer::DrawString("X", closeBtnX + 10.0f, 4.0f, { 1.0f, 1.0f, 1.0f, 1.0f });
        }

        bool EditorRootUI::OnMouseButtonPressed(MouseButtonPressedEvent& e)
        {
            if (e.GetButton() == 0) // 좌클릭
            {
                auto& mainWindow = Application::Get()->GetWindow();
                float windowWidth = (float)mainWindow.GetWidth();

                // 닫기 버튼 충돌 판정
                if (e.GetY() >= 0.0f && e.GetY() <= 24.0f && e.GetX() >= (windowWidth - 30.0f))
                {
                    mainWindow.SetShouldClose(true); // 애플리케이션 종료
                    e.Handled = true;
                    return true;
                }
            }
            return false;
        }
    }
}