#include "WindowPanel.h"
//#include "imgui.h"
#include "Renderer/UIRenderer.h"
#include "Renderer/Font.h"
#include <iostream>
#include <windows.h>
#include "Application.h"

namespace CCEngine
{
    namespace UI
    {
        WindowPanel::WindowPanel(const std::string& name, const std::string& title)
            : Panel(name, { 0.1f, 0.1f, 0.11f, 1.0f }), m_Title(title)
        {
        }

        void WindowPanel::OnRender()
        {
            if (!m_IsVisible) return;  //임시주석

            Panel::OnRender();

            float HeadeHeight = UIRenderer::GetDefaultFont() ? UIRenderer::GetDefaultFont()->GetFontSize() : 24.0f;

            // 1. 패널(서브 창) 상단 24px 커스텀 타이틀 바
            DirectX::XMFLOAT4 titleColor = { 0.15f, 0.15f, 0.17f, 1.0f };
            UIRenderer::DrawRectFilled(m_CalculatedPos.x, m_CalculatedPos.y, m_CalculatedSize.x, HeadeHeight, titleColor);
            UIRenderer::DrawString(m_Title, m_CalculatedPos.x + 10.0f, m_CalculatedPos.y + HeadeHeight * 0.7f, { 0.8f, 0.8f, 0.8f, 1.0f });

            // 2. 우측 닫기 버튼 렌더링
            float closeBtnX = m_CalculatedPos.x + m_CalculatedSize.x - 30.0f;
            UIRenderer::DrawRectFilled(closeBtnX, m_CalculatedPos.y, 30.0f, HeadeHeight, { 0.8f, 0.2f, 0.2f, 1.0f });
            UIRenderer::DrawString("X", closeBtnX + 10.0f, m_CalculatedPos.y+ HeadeHeight * 0.7f , {1.0f, 1.0f, 1.0f, 1.0f});
            
        }

        bool WindowPanel::OnMouseButtonPressed(MouseButtonPressedEvent& e)
        {
            if (e.GetButton() == 0)  //임시주석
            {
                // 타이틀 바 영역 검사 (상단 24px)
                bool isHoveringTitle = (e.GetX() >= m_CalculatedPos.x && e.GetX() <= m_CalculatedPos.x + m_CalculatedSize.x &&
                    e.GetY() >= m_CalculatedPos.y && e.GetY() <= m_CalculatedPos.y + 24.0f);

                if (isHoveringTitle)
                {
                    // A. 닫기 버튼 영역 검사
                    float closeBtnX = m_CalculatedPos.x + m_CalculatedSize.x - 30.0f;
                    if (e.GetX() >= closeBtnX)
                    {
                        if (m_OwnerWindow) // 독립된 서브 창 상태일 때 창 닫기
                        {
                            m_OwnerWindow->SetShouldClose(true);
                        }
                        else // 도킹 상태일 때 숨김 처리
                        {
                            m_IsVisible = false;
                        }
                        e.Handled = true;
                        return true;
                    }

                    // B. 닫기 버튼이 아닌 빈 타이틀 바 영역 (드래그 시작)
                    m_IsDragging = true;
                    m_LastMouseX = e.GetX();
                    m_LastMouseY = e.GetY();

                    // 패널 로컬 좌표계 기준 드래그 영점
                    m_DragOffsetX = e.GetX() - m_CalculatedPos.x;
                    m_DragOffsetY = e.GetY() - m_CalculatedPos.y;

                    BringToFront();

                    if (!m_IsFloating)
                    {
                        m_IsFloating = true;
                        SetAnchorMin(0.0f, 0.0f); SetAnchorMax(0.0f, 0.0f);
                        SetOffsetMin(m_CalculatedPos.x, m_CalculatedPos.y);
                        SetOffsetMax(m_CalculatedPos.x + m_CalculatedSize.x, m_CalculatedPos.y + m_CalculatedSize.y);
                    }

                    e.Handled = true;
                    return true;
                }
            }
            return false;
        }

        bool WindowPanel::OnMouseMoved(MouseMovedEvent& e)
        {
            if (m_IsDragging)
            {
                // ==============================================================
                // [상태 B] 내가 독립된 서브 창의 주인(RootUI)일 때
                // ==============================================================
                if (m_Parent == nullptr && m_OwnerWindow != nullptr)
                {
                    // UI를 이동시키는 게 아니라, OS 창 자체를 마우스 따라 이동시킵니다!
                    POINT pt;
                    GetCursorPos(&pt); // 화면 전역 절대 좌표 획득

                    // 저장해둔 영점을 빼서 윈도우가 마우스에 착 달라붙어 따라오게 함
                    m_OwnerWindow->SetPosition(pt.x - (int)m_DragOffsetX, pt.y - (int)m_DragOffsetY);

                    // OS 창이 마우스를 따라 이동하므로 내부 로컬 마우스 좌표(e.GetX)는 변하지 않습니다.
                    e.Handled = true;
                    return true;
                }
                // ==============================================================
                // [상태 A] 내가 메인 창 안의 일반 패널일 때 (도킹 상태)
                // ==============================================================
                else
                {
                    float deltaX = e.GetX() - m_LastMouseX;
                    float deltaY = e.GetY() - m_LastMouseY;

                    m_OffsetMin.x += deltaX; m_OffsetMin.y += deltaY;
                    m_OffsetMax.x += deltaX; m_OffsetMax.y += deltaY;

                    m_LastMouseX = e.GetX();
                    m_LastMouseY = e.GetY();

                    // Tear-off 감지
                    auto& mainWindow = Application::Get()->GetWindow();
                    float displayWidth = (float)mainWindow.GetWidth();
                    float displayHeight = (float)mainWindow.GetHeight();

                    if (e.GetX() < 0 || e.GetX() > displayWidth || e.GetY() < 0 || e.GetY() > displayHeight)
                    {
                        if (m_Parent) m_Parent->RemoveChild(this);

                        // 새 창 생성
                        auto newSecWindow = Application::Get()->CreateSecondaryWindow(m_Name, m_CalculatedSize.x, m_CalculatedSize.y);

                        SetAnchorMin(0.0f, 0.0f); SetAnchorMax(1.0f, 1.0f);
                        SetOffsetMin(0.0f, 0.0f); SetOffsetMax(0.0f, 0.0f);

                        m_CalculatedPos = { 0.0f, 0.0f };

                        newSecWindow->SetRootUI(this);

                        //
                        SetOwnerWindow(newSecWindow);

                        // OS 마우스 캡처 권한 이전 (메인 -> 서브)
                        ReleaseCapture();
                        HWND subHwnd = static_cast<HWND>(newSecWindow->GetNativeWindow());
                        SetCapture(subHwnd);
                    }
                    e.Handled = true;
                    return true;
                }
            }
            return false;
        }

        bool WindowPanel::OnMouseButtonReleased(MouseButtonReleasedEvent& e)
        {
            if (m_IsDragging)
            {
                m_IsDragging = false; // 드래그 종료

                // ★ m_Parent == nullptr (독립 상태) 일 때만 메인 창 위인지 검사하여 Redock 시도
                if (m_IsFloating && m_Parent == nullptr)
                {
                    auto app = Application::Get();
                    auto& mainWindow = app->GetWindow(); // 참조(&)로 메인 윈도우 가져오기

                    // ★ [크로스 플랫폼 설계] Win32 API를 완전히 배제합니다!
                    // 메인 윈도우 기준의 로컬 마우스 좌표를 가져옵니다. (이미 WindowsWindow에 구현해두심!)
                    auto [localX, localY] = mainWindow.GetMousePosition();

                    // 메인 윈도우의 가로/세로 크기 가져오기
                    float mainWidth = (float)mainWindow.GetWidth();
                    float mainHeight = (float)mainWindow.GetHeight();

                    // 마우스 좌표가 메인 윈도우 클라이언트 영역(0,0 ~ Width,Height) 안에 있는지 검사
                    if (localX >= 0.0f && localX <= mainWidth &&
                        localY >= 0.0f && localY <= mainHeight)
                    {
                        // 조건 달성! 메인 창의 RootUI로 도킹(Redock) 실행
                        Redock(mainWindow.GetRootUI());
                    }
                }

                e.Handled = true;
                return true;
            }
            return false;
        }

        void WindowPanel::Redock(Widget* newParent)
        {
            if (!newParent) return;

            auto app = Application::Get();
            auto& mainWindow = app->GetWindow(); // 참조(&) 사용

            // ==========================================================
            // 1. 서브 윈도우 파괴 예약 & 내(UI) 소유권 되찾기
            // ==========================================================
            app->RequestCloseSecondaryWindowByUI(this);
            
            SetOwnerWindow(nullptr);

            // ==========================================================
            // 2. 부모 교체 (Reparenting)
            // ==========================================================
            if (m_Parent) m_Parent->RemoveChild(this);
            m_Parent = nullptr;
            newParent->AddChild(this);

            // ==========================================================
            // 3. 상태 리셋 및 위치 보정 (Win32 API 없이!)
            // ==========================================================
            m_IsFloating = false;

            // 메인 창 기준의 현재 마우스 위치를 가져옵니다.
            auto [localX, localY] = mainWindow.GetMousePosition();

            // 앵커를 0으로 풀어주고(크기 고정)
            SetAnchorMin(0.0f, 0.0f);
            SetAnchorMax(0.0f, 0.0f);

            // 드래그하던 느낌 유지 (마우스 커서가 타이틀 바 중앙쯤 오도록 오프셋 계산)
            float dropX = localX - (m_CalculatedSize.x * 0.5f);
            float dropY = localY - 12.0f; // 타이틀 바 높이(24)의 절반

            SetOffsetMin(dropX, dropY);
            SetOffsetMax(dropX + m_CalculatedSize.x, dropY + m_CalculatedSize.y);

            std::cout << "[Docking] " << m_Name << " 패널이 메인 창으로 복귀되었습니다." << std::endl;
        }
    }
}