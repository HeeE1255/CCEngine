#include "WindowPanel.h"
// #include "imgui.h"
#include "Renderer/UIRenderer.h"
#include "Renderer/Font.h"
#include <iostream>

#define NOMINMAX
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
            if (!m_IsVisible) return;
            if (m_ResizeMode != ResizeMode::None || m_IsDragging)
            {
                auto& mainWindow = Application::Get()->GetWindow();
                auto [mouseX, mouseY] = mainWindow.GetMousePosition();

                if (m_ResizeMode != ResizeMode::None)
                {
                    float deltaX = mouseX - m_ResizeLastMouseX;
                    float deltaY = mouseY - m_ResizeLastMouseY;
                    float minWidth = 150.0f; float minHeight = 100.0f;

                    if (m_ResizeMode == ResizeMode::Right || m_ResizeMode == ResizeMode::TopRight || m_ResizeMode == ResizeMode::BottomRight) {
                        m_OffsetMax.x += deltaX;
                        if (m_OffsetMax.x - m_OffsetMin.x < minWidth) m_OffsetMax.x = m_OffsetMin.x + minWidth;
                    }
                    if (m_ResizeMode == ResizeMode::Left || m_ResizeMode == ResizeMode::TopLeft || m_ResizeMode == ResizeMode::BottomLeft) {
                        m_OffsetMin.x += deltaX;
                        if (m_OffsetMax.x - m_OffsetMin.x < minWidth) m_OffsetMin.x = m_OffsetMax.x - minWidth;
                    }
                    if (m_ResizeMode == ResizeMode::Bottom || m_ResizeMode == ResizeMode::BottomLeft || m_ResizeMode == ResizeMode::BottomRight) {
                        m_OffsetMax.y += deltaY;
                        if (m_OffsetMax.y - m_OffsetMin.y < minHeight) m_OffsetMax.y = m_OffsetMin.y + minHeight;
                    }
                    if (m_ResizeMode == ResizeMode::Top || m_ResizeMode == ResizeMode::TopLeft || m_ResizeMode == ResizeMode::TopRight) {
                        m_OffsetMin.y += deltaY;
                        if (m_OffsetMax.y - m_OffsetMin.y < minHeight) m_OffsetMin.y = m_OffsetMax.y - minHeight;
                    }

                    m_CalculatedSize.x = m_OffsetMax.x - m_OffsetMin.x;
                    m_CalculatedSize.y = m_OffsetMax.y - m_OffsetMin.y;
                    m_ResizeLastMouseX = mouseX;
                    m_ResizeLastMouseY = mouseY;
                }

                if (m_IsDragging)
                {
                    // If floating we shouldn't perform internal drag logic here
                    if (m_OwnerWindow == nullptr)
                    {
                        float deltaX = mouseX - m_LastMouseX;
                        float deltaY = mouseY - m_LastMouseY;
                        m_OffsetMin.x += deltaX; m_OffsetMin.y += deltaY;
                        m_OffsetMax.x += deltaX; m_OffsetMax.y += deltaY;
                        m_LastMouseX = mouseX; m_LastMouseY = mouseY;
                    }
                }
            }
            Panel::OnRender();

            float HeadeHeight = UIRenderer::GetDefaultFont() ? UIRenderer::GetDefaultFont()->GetFontSize() : 24.0f;

            // 1. 패널 상단 커스텀 타이틀 바
            DirectX::XMFLOAT4 titleColor = { 0.15f, 0.15f, 0.17f, 1.0f };
            UIRenderer::DrawRectFilled(m_CalculatedPos.x, m_CalculatedPos.y, m_CalculatedSize.x, HeadeHeight, titleColor);
            UIRenderer::DrawString(m_Title, m_CalculatedPos.x + 10.0f, m_CalculatedPos.y + HeadeHeight * 0.7f, { 0.8f, 0.8f, 0.8f, 1.0f });

            // 2. 우측 닫기 버튼
            float closeBtnX = m_CalculatedPos.x + m_CalculatedSize.x - 30.0f;
            UIRenderer::DrawRectFilled(closeBtnX, m_CalculatedPos.y, 30.0f, HeadeHeight, { 0.8f, 0.2f, 0.2f, 1.0f });
            UIRenderer::DrawString("X", closeBtnX + 10.0f, m_CalculatedPos.y + HeadeHeight * 0.7f, { 1.0f, 1.0f, 1.0f, 1.0f });

            // (이전에 있던 우측 하단 점 4개 그리는 코드는 이제 필요 없으므로 삭제합니다!)
        }

        bool WindowPanel::OnMouseButtonPressed(MouseButtonPressedEvent& e)
        {
            bool isTornOff = (m_OwnerWindow != nullptr);
            float edge = 8.0f; // 테두리 판정 픽셀

            if (e.GetButton() == 0)
            {
                // ==============================================================
                // 1. 상하좌우 8방향 모서리 리사이즈 히트박스 판별
                // ==============================================================
                bool isLeft = e.GetX() >= m_CalculatedPos.x && e.GetX() <= m_CalculatedPos.x + edge;
                bool isRight = e.GetX() >= m_CalculatedPos.x + m_CalculatedSize.x - edge && e.GetX() <= m_CalculatedPos.x + m_CalculatedSize.x;
                bool isTop = e.GetY() >= m_CalculatedPos.y && e.GetY() <= m_CalculatedPos.y + edge;
                bool isBottom = e.GetY() >= m_CalculatedPos.y + m_CalculatedSize.y - edge && e.GetY() <= m_CalculatedPos.y + m_CalculatedSize.y;

                if (isLeft || isRight || isTop || isBottom)
                {
                    if (isTornOff)
                    {
                        // ★ [핵심] 독립된 OS 윈도우일 경우: Win32 API를 호출해 OS에게 리사이징을 일임합니다!
                        int htCode = 0;
                        if (isTop && isLeft) htCode = HTTOPLEFT;
                        else if (isTop && isRight) htCode = HTTOPRIGHT;
                        else if (isBottom && isLeft) htCode = HTBOTTOMLEFT;
                        else if (isBottom && isRight) htCode = HTBOTTOMRIGHT;
                        else if (isLeft) htCode = HTLEFT;
                        else if (isRight) htCode = HTRIGHT;
                        else if (isTop) htCode = HTTOP;
                        else if (isBottom) htCode = HTBOTTOM;

                        HWND hwnd = static_cast<HWND>(m_OwnerWindow->GetNativeWindow());
                        ReleaseCapture(); // 엔진 UI의 마우스 캡처 권한 해제
                        SendMessage(hwnd, WM_NCLBUTTONDOWN, htCode, 0); // OS야, 이 창 좀 리사이즈 해줘!

                        e.Handled = true;
                        return true;
                    }
                    else
                    {
                        // 메인 창 내부에 도킹된 상태: 엔진 내부 UI 리사이징 수행
                        if (isTop && isLeft) m_ResizeMode = ResizeMode::TopLeft;
                        else if (isTop && isRight) m_ResizeMode = ResizeMode::TopRight;
                        else if (isBottom && isLeft) m_ResizeMode = ResizeMode::BottomLeft;
                        else if (isBottom && isRight) m_ResizeMode = ResizeMode::BottomRight;
                        else if (isLeft) m_ResizeMode = ResizeMode::Left;
                        else if (isRight) m_ResizeMode = ResizeMode::Right;
                        else if (isTop) m_ResizeMode = ResizeMode::Top;
                        else if (isBottom) m_ResizeMode = ResizeMode::Bottom;

                        m_ResizeLastMouseX = e.GetX();
                        m_ResizeLastMouseY = e.GetY();
                        e.Handled = true;
                        return true;
                    }
                }

                // ==============================================================
                // 2. 타이틀 바 판별 (창 닫기 & 드래그)
                // ==============================================================
                bool isHoveringTitle = (e.GetX() >= m_CalculatedPos.x && e.GetX() <= m_CalculatedPos.x + m_CalculatedSize.x &&
                    e.GetY() >= m_CalculatedPos.y && e.GetY() <= m_CalculatedPos.y + 24.0f);

                if (isHoveringTitle)
                {
                    float closeBtnX = m_CalculatedPos.x + m_CalculatedSize.x - 30.0f;
                    if (e.GetX() >= closeBtnX)
                    {
                        if (isTornOff) m_OwnerWindow->SetShouldClose(true);
                        else m_IsVisible = false;
                        e.Handled = true;
                        return true;
                    }

                    if (isTornOff)
                    {
                        // ★ [핵심] 독립된 OS 윈도우일 경우: Win32 API를 호출해 OS 창 이동 권한을 넘깁니다!
                        HWND hwnd = static_cast<HWND>(m_OwnerWindow->GetNativeWindow());
                        ReleaseCapture();

                        // SendMessage는 마우스를 뗄 때까지 프로그램의 흐름을 멈춥니다 (OS가 드래그를 끝낼 때까지 대기)
                        SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);

                        // --- 마우스를 떼고 창 이동이 끝난 직후 여기서부터 다시 실행됨 ---

                        // 드래그가 끝났으니, 메인 창 영역 안으로 들어왔는지 (Redock 조건) 검사합니다.
                        auto app = Application::Get();
                        auto& mainWindow = app->GetWindow();
                        auto [localX, localY] = mainWindow.GetMousePosition(); // 메인 창 기준 상대 좌표

                        if (localX >= 0.0f && localX <= (float)mainWindow.GetWidth() &&
                            localY >= 0.0f && localY <= (float)mainWindow.GetHeight())
                        {
                            Redock(mainWindow.GetRootUI()); // 메인 창에 다시 흡수!
                        }

                        e.Handled = true;
                        return true;
                    }
                    else
                    {
                        // 메인 창 내부에 도킹된 상태: 엔진 내부 가상 창 이동
                        m_IsDragging = true;
                        m_LastMouseX = e.GetX();
                        m_LastMouseY = e.GetY();
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
            }
            return false;
        }

        bool WindowPanel::OnMouseMoved(MouseMovedEvent& e)
        {
            float edge = 8.0f;

            // ==============================================================
            // 1. 테두리 마우스 오버 시 윈도우 화살표 커서 변경
            // ==============================================================
            if (m_ResizeMode == ResizeMode::None && !m_IsDragging)
            {
                bool isLeft = e.GetX() >= m_CalculatedPos.x && e.GetX() <= m_CalculatedPos.x + edge;
                bool isRight = e.GetX() >= m_CalculatedPos.x + m_CalculatedSize.x - edge && e.GetX() <= m_CalculatedPos.x + m_CalculatedSize.x;
                bool isTop = e.GetY() >= m_CalculatedPos.y && e.GetY() <= m_CalculatedPos.y + edge;
                bool isBottom = e.GetY() >= m_CalculatedPos.y + m_CalculatedSize.y - edge && e.GetY() <= m_CalculatedPos.y + m_CalculatedSize.y;

                if (e.GetY() >= m_CalculatedPos.y && e.GetY() <= m_CalculatedPos.y + m_CalculatedSize.y &&
                    e.GetX() >= m_CalculatedPos.x && e.GetX() <= m_CalculatedPos.x + m_CalculatedSize.x)
                {
                    if ((isTop && isLeft) || (isBottom && isRight)) SetCursor(LoadCursor(nullptr, IDC_SIZENWSE));
                    else if ((isTop && isRight) || (isBottom && isLeft)) SetCursor(LoadCursor(nullptr, IDC_SIZENESW));
                    else if (isLeft || isRight) SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
                    else if (isTop || isBottom) SetCursor(LoadCursor(nullptr, IDC_SIZENS));
                }
            }

            // ==============================================================
            // 2. 엔진 내부 도킹(Floating) 패널 리사이징 수학 계산
            // ==============================================================
            if (m_ResizeMode != ResizeMode::None)
            {
                float deltaX = e.GetX() - m_ResizeLastMouseX;
                float deltaY = e.GetY() - m_ResizeLastMouseY;
                float minWidth = 150.0f; float minHeight = 100.0f;

                if (m_ResizeMode == ResizeMode::Right || m_ResizeMode == ResizeMode::TopRight || m_ResizeMode == ResizeMode::BottomRight) {
                    m_OffsetMax.x += deltaX;
                    if (m_OffsetMax.x - m_OffsetMin.x < minWidth) m_OffsetMax.x = m_OffsetMin.x + minWidth;
                }
                if (m_ResizeMode == ResizeMode::Left || m_ResizeMode == ResizeMode::TopLeft || m_ResizeMode == ResizeMode::BottomLeft) {
                    m_OffsetMin.x += deltaX;
                    if (m_OffsetMax.x - m_OffsetMin.x < minWidth) m_OffsetMin.x = m_OffsetMax.x - minWidth;
                }
                if (m_ResizeMode == ResizeMode::Bottom || m_ResizeMode == ResizeMode::BottomLeft || m_ResizeMode == ResizeMode::BottomRight) {
                    m_OffsetMax.y += deltaY;
                    if (m_OffsetMax.y - m_OffsetMin.y < minHeight) m_OffsetMax.y = m_OffsetMin.y + minHeight;
                }
                if (m_ResizeMode == ResizeMode::Top || m_ResizeMode == ResizeMode::TopLeft || m_ResizeMode == ResizeMode::TopRight) {
                    m_OffsetMin.y += deltaY;
                    if (m_OffsetMax.y - m_OffsetMin.y < minHeight) m_OffsetMin.y = m_OffsetMax.y - minHeight;
                }

                m_CalculatedSize.x = m_OffsetMax.x - m_OffsetMin.x;
                m_CalculatedSize.y = m_OffsetMax.y - m_OffsetMin.y;
                m_ResizeLastMouseX = e.GetX();
                m_ResizeLastMouseY = e.GetY();

                e.Handled = true;
                return true;
            }

            // ==============================================================
            // 3. 엔진 내부 도킹(Floating) 패널 이동 & 분리(Tear-off) 판정
            // ==============================================================
            if (m_IsDragging)
            {
                // [안전 장치] 만약 이미 분리된 창이라면 내부 드래그 오작동 방지
                if (m_OwnerWindow != nullptr) {
                    m_IsDragging = false;
                    return false;
                }

                float deltaX = e.GetX() - m_LastMouseX;
                float deltaY = e.GetY() - m_LastMouseY;
                m_OffsetMin.x += deltaX; m_OffsetMin.y += deltaY;
                m_OffsetMax.x += deltaX; m_OffsetMax.y += deltaY;
                m_LastMouseX = e.GetX(); m_LastMouseY = e.GetY();

                auto& mainWindow = Application::Get()->GetWindow();
                float displayWidth = (float)mainWindow.GetWidth();
                float displayHeight = (float)mainWindow.GetHeight();

                // 메인 창 밖으로 드래그 시 분리(Tear-off) 시작!
                if (e.GetX() < 0 || e.GetX() > displayWidth || e.GetY() < 0 || e.GetY() > displayHeight)
                {
                    // ★ 핵심 1: UI가 우주로 날아가는 것을 방지하기 위해 내부 드래그 즉시 차단!
                    m_IsDragging = false;

                    if (m_Parent) m_Parent->RemoveChild(this);

                    auto newSecWindow = Application::Get()->CreateSecondaryWindow(m_Name, (uint32_t)m_CalculatedSize.x, (uint32_t)m_CalculatedSize.y);

                    // 서브 윈도우 안에 꽉 차도록 앵커와 여백 초기화
                    SetAnchorMin(0.0f, 0.0f); SetAnchorMax(1.0f, 1.0f);
                    SetOffsetMin(0.0f, 0.0f); SetOffsetMax(0.0f, 0.0f);
                    m_CalculatedPos = { 0.0f, 0.0f };

                    newSecWindow->SetRootUI(this);
                    SetOwnerWindow(newSecWindow);

                    // ★ 핵심 2: OS 창이 생성되자마자 마우스 위치로 강제 이동 (부드러운 전환)
                    POINT pt;
                    GetCursorPos(&pt);
                    newSecWindow->SetPosition(pt.x - (int)m_DragOffsetX, pt.y - (int)m_DragOffsetY);

                    ReleaseCapture(); // 엔진 UI 마우스 캡처 해제
                    HWND subHwnd = static_cast<HWND>(newSecWindow->GetNativeWindow());

                    // ★ 핵심 3: 윈도우 OS에게 "이 창 제목표시줄 잡고 드래그 중인걸로 쳐줘!" 라고 명령
                    // (이 함수는 사용자가 마우스를 뗄 때까지 프로그램 흐름을 멈춥니다)
                    SendMessage(subHwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);

                    // --- 마우스를 떼고 창 이동이 끝난 직후 여기서부터 다시 실행됨 ---

                    // 메인 창 영역 안으로 다시 돌아왔다면 Redock(도킹) 실행
                    auto [localX, localY] = mainWindow.GetMousePosition();
                    if (localX >= 0.0f && localX <= displayWidth && localY >= 0.0f && localY <= displayHeight)
                    {
                        Redock(mainWindow.GetRootUI());
                    }
                }
                e.Handled = true;
                return true;
            }
            return false;
        }

        bool WindowPanel::OnMouseButtonReleased(MouseButtonReleasedEvent& e)
        {
            if (m_ResizeMode != ResizeMode::None)
            {
                m_ResizeMode = ResizeMode::None; // 리사이징 종료
                e.Handled = true; return true;
            }

            if (m_IsDragging)
            {
                m_IsDragging = false;
                if (m_IsFloating && m_Parent == nullptr)
                {
                    auto app = Application::Get();
                    auto& mainWindow = app->GetWindow();
                    auto [localX, localY] = mainWindow.GetMousePosition();
                    if (localX >= 0.0f && localX <= (float)mainWindow.GetWidth() && localY >= 0.0f && localY <= (float)mainWindow.GetHeight()) {
                        Redock(mainWindow.GetRootUI());
                    }
                }
                e.Handled = true; return true;
            }
            return false;
        }

        void WindowPanel::Redock(Widget* newParent)
        {
            if (!newParent) return;

            auto app = Application::Get();
            auto& mainWindow = app->GetWindow();

            // 서브 윈도우 닫기
            app->RequestCloseSecondaryWindowByUI(this);
            SetOwnerWindow(nullptr);

            if (m_Parent) m_Parent->RemoveChild(this);
            m_Parent = nullptr;
            newParent->AddChild(this);

            m_IsFloating = false;

            auto [localX, localY] = mainWindow.GetMousePosition();

            SetAnchorMin(0.0f, 0.0f);
            SetAnchorMax(0.0f, 0.0f);

            // ★ 복귀 시 크기가 미친듯이 폭발하는 현상 방지! (안전 사이즈 보정)
            if (m_CalculatedSize.x > 1200.0f || m_CalculatedSize.y > 1000.0f || m_CalculatedSize.x <= 0.0f) {
                m_CalculatedSize = { 400.0f, 600.0f }; // 상식적인 기본 도킹 사이즈로 덮어쓰기
            }

            float dropX = localX - (m_CalculatedSize.x * 0.5f);
            float dropY = localY - 12.0f;

            SetOffsetMin(dropX, dropY);
            SetOffsetMax(dropX + m_CalculatedSize.x, dropY + m_CalculatedSize.y);

            std::cout << "[Docking] " << m_Name << " 패널이 메인 창으로 복귀되었습니다." << std::endl;
        }
    }
}