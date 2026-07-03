#include "WindowPanel.h"
// #include "imgui.h"
#include "Renderer/UIRenderer.h"
#include "Renderer/Font.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

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
            // 창 패널은 사각형 전체가 마우스 차단 영역이다.
            // 위에 떠 있는 창 아래의 버튼/하이어라키가 hover나 클릭을 받지 않게 한다.
            SetBlockMouseEvents(true);
        }

        void WindowPanel::OnUpdate(float deltaTime)
        {
            Panel::OnUpdate(deltaTime);

            if (!m_IsVisible)
            {
                return;
            }

            if (m_OwnerWindow != nullptr && m_OwnerWindow->GetRootUI() == this && !m_IsDragging)
            {
                m_PreferredFloatingSize = { (float)m_OwnerWindow->GetWidth(), (float)m_OwnerWindow->GetHeight() };
            }

            if (m_ResizeMode == ResizeMode::None && !m_IsDragging)
            {
                return;
            }

            if (m_OwnerWindow != nullptr)
            {
                bool isLMBDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                if (!isLMBDown)
                {
                    m_ResizeMode = ResizeMode::None;
                    m_IsDragging = false;
                    Widget::EndMouseInteraction(this);

                    if (!m_IsMovingOwnerWindow && m_DockingEnabled)
                    {
                        DockManager::UpdatePreview(this, 0.0f, 0.0f);
                        DockManager::ApplyPreview(this);
                    }
                    DockManager::ClearPreview();
                    m_IsMovingOwnerWindow = false;
                    m_DockIgnoreWindow = nullptr;
                    return;
                }

                auto [screenX, screenY] = m_OwnerWindow->GetScreenMousePosition();

                if (m_IsDragging)
                {
                    m_OwnerWindow->SetPosition((int)(screenX - m_DragOffsetX), (int)(screenY - m_DragOffsetY));

                    if (!m_IsMovingOwnerWindow && m_DockingEnabled)
                        DockManager::UpdatePreview(this, 0.0f, 0.0f);
                    return;
                }

                HWND hwnd = static_cast<HWND>(m_OwnerWindow->GetNativeWindow());
                RECT rect;
                GetWindowRect(hwnd, &rect);

                float deltaX = (float)screenX - m_ResizeLastMouseX;
                float deltaY = (float)screenY - m_ResizeLastMouseY;
                float minWidth = 150.0f;
                float minHeight = 100.0f;

                float newLeft = (float)rect.left;
                float newTop = (float)rect.top;
                float newRight = (float)rect.right;
                float newBottom = (float)rect.bottom;

                if (m_ResizeMode == ResizeMode::Left || m_ResizeMode == ResizeMode::TopLeft || m_ResizeMode == ResizeMode::BottomLeft) newLeft += deltaX;
                if (m_ResizeMode == ResizeMode::Right || m_ResizeMode == ResizeMode::TopRight || m_ResizeMode == ResizeMode::BottomRight) newRight += deltaX;
                if (m_ResizeMode == ResizeMode::Top || m_ResizeMode == ResizeMode::TopLeft || m_ResizeMode == ResizeMode::TopRight) newTop += deltaY;
                if (m_ResizeMode == ResizeMode::Bottom || m_ResizeMode == ResizeMode::BottomLeft || m_ResizeMode == ResizeMode::BottomRight) newBottom += deltaY;

                if (newRight - newLeft < minWidth)
                {
                    if (m_ResizeMode == ResizeMode::Left || m_ResizeMode == ResizeMode::TopLeft || m_ResizeMode == ResizeMode::BottomLeft) newLeft = newRight - minWidth;
                    if (m_ResizeMode == ResizeMode::Right || m_ResizeMode == ResizeMode::TopRight || m_ResizeMode == ResizeMode::BottomRight) newRight = newLeft + minWidth;
                }
                if (newBottom - newTop < minHeight)
                {
                    if (m_ResizeMode == ResizeMode::Top || m_ResizeMode == ResizeMode::TopLeft || m_ResizeMode == ResizeMode::TopRight) newTop = newBottom - minHeight;
                    if (m_ResizeMode == ResizeMode::Bottom || m_ResizeMode == ResizeMode::BottomLeft || m_ResizeMode == ResizeMode::BottomRight) newBottom = newTop + minHeight;
                }

                SetWindowPos(
                    hwnd,
                    nullptr,
                    (int)newLeft,
                    (int)newTop,
                    (int)(newRight - newLeft),
                    (int)(newBottom - newTop),
                    SWP_NOZORDER
                );

                m_ResizeLastMouseX = (float)screenX;
                m_ResizeLastMouseY = (float)screenY;
                return;
            }

            auto& mainWindow = Application::Get()->GetWindow();
            auto [mouseX, mouseY] = mainWindow.GetMousePosition();

            MouseMovedEvent moveEvent(mouseX, mouseY);
            OnMouseMoved(moveEvent);
        }

        void WindowPanel::OnRender()
        {
            if (!m_IsVisible) return;

            Panel::OnRender();


            float HeadeHeight = UIRenderer::GetDefaultFont() ? UIRenderer::GetDefaultFont()->GetFontSize() : 24.0f;
            DirectX::XMFLOAT4 titleColor = { 0.15f, 0.15f, 0.17f, 1.0f };
            UIRenderer::DrawRectFilled(m_CalculatedPos.x, m_CalculatedPos.y, m_CalculatedSize.x, HeadeHeight, titleColor);
            UIRenderer::DrawString(m_Title, m_CalculatedPos.x + 10.0f, m_CalculatedPos.y + HeadeHeight * 0.7f, { 0.8f, 0.8f, 0.8f, 1.0f });

            float closeBtnX = m_CalculatedPos.x + m_CalculatedSize.x - 30.0f;
            UIRenderer::DrawRectFilled(closeBtnX, m_CalculatedPos.y, 30.0f, HeadeHeight, { 0.8f, 0.2f, 0.2f, 1.0f });
            UIRenderer::DrawString("X", closeBtnX + 10.0f, m_CalculatedPos.y + HeadeHeight * 0.7f, { 1.0f, 1.0f, 1.0f, 1.0f });

            constexpr float borderThickness = 1.0f;
            DirectX::XMFLOAT4 outerBorderColor = { 0.26f, 0.26f, 0.29f, 0.95f };
            DirectX::XMFLOAT4 bottomBorderColor = { 0.11f, 0.11f, 0.13f, 1.0f };

            // 창들이 같은 배경색으로 겹치면 경계가 사라진다.
            // 얇은 고채도 선을 공통 WindowPanel에서 그려 모든 패널의 외곽을 한눈에 구분하게 한다.
            UIRenderer::DrawRectFilled(m_CalculatedPos.x, m_CalculatedPos.y, m_CalculatedSize.x, borderThickness, outerBorderColor);
            UIRenderer::DrawRectFilled(m_CalculatedPos.x, m_CalculatedPos.y, borderThickness, m_CalculatedSize.y, outerBorderColor);
            UIRenderer::DrawRectFilled(m_CalculatedPos.x + m_CalculatedSize.x - borderThickness, m_CalculatedPos.y, borderThickness, m_CalculatedSize.y, outerBorderColor);
            UIRenderer::DrawRectFilled(m_CalculatedPos.x, m_CalculatedPos.y + (std::max)(0.0f, m_CalculatedSize.y - 2.0f), m_CalculatedSize.x, borderThickness, outerBorderColor);
            UIRenderer::DrawRectFilled(m_CalculatedPos.x, m_CalculatedPos.y + (std::max)(0.0f, m_CalculatedSize.y - 1.0f), m_CalculatedSize.x, borderThickness, bottomBorderColor);

        }

        bool WindowPanel::IsPointInside(float mouseX, float mouseY) const
        {
            constexpr float resizeHitPadding = 6.0f;

            // 창 테두리는 화면에는 1픽셀로 보이지만 실제 조작은 조금 넓게 잡아야 한다.
            // 특히 왼쪽/위쪽 테두리는 마우스가 창 밖으로 살짝 나가면 부모가 이벤트를 넘기지 않는다.
            return mouseX >= m_CalculatedPos.x - resizeHitPadding &&
                mouseX <= m_CalculatedPos.x + m_CalculatedSize.x + resizeHitPadding &&
                mouseY >= m_CalculatedPos.y - resizeHitPadding &&
                mouseY <= m_CalculatedPos.y + m_CalculatedSize.y + resizeHitPadding;
        }

        bool WindowPanel::OnMouseButtonPressed(MouseButtonPressedEvent& e)
        {
            bool isTornOff = (m_OwnerWindow != nullptr);
            float edge = 8.0f; // 테두리 판정 픽셀
            float outerEdge = 6.0f; // 창 밖으로 살짝 나간 마우스도 리사이즈로 인정하는 여유 폭

            if (e.GetButton() == 0)
            {
                // ==============================================================
                // 1. 상하좌우 8방향 모서리 리사이즈 히트박스 판별
                // ==============================================================
                bool isLeft = e.GetX() >= m_CalculatedPos.x - outerEdge && e.GetX() <= m_CalculatedPos.x + edge;
                bool isRight = e.GetX() >= m_CalculatedPos.x + m_CalculatedSize.x - edge && e.GetX() <= m_CalculatedPos.x + m_CalculatedSize.x + outerEdge;
                bool isTop = e.GetY() >= m_CalculatedPos.y - outerEdge && e.GetY() <= m_CalculatedPos.y + edge;
                bool isBottom = e.GetY() >= m_CalculatedPos.y + m_CalculatedSize.y - edge && e.GetY() <= m_CalculatedPos.y + m_CalculatedSize.y + outerEdge;

                if (isLeft || isRight || isTop || isBottom)
                {
                    if (isTornOff)
                    {
                        if (isTop && isLeft) m_ResizeMode = ResizeMode::TopLeft;
                        else if (isTop && isRight) m_ResizeMode = ResizeMode::TopRight;
                        else if (isBottom && isLeft) m_ResizeMode = ResizeMode::BottomLeft;
                        else if (isBottom && isRight) m_ResizeMode = ResizeMode::BottomRight;
                        else if (isLeft) m_ResizeMode = ResizeMode::Left;
                        else if (isRight) m_ResizeMode = ResizeMode::Right;
                        else if (isTop) m_ResizeMode = ResizeMode::Top;
                        else if (isBottom) m_ResizeMode = ResizeMode::Bottom;

                        auto [screenX, screenY] = m_OwnerWindow->GetScreenMousePosition();
                        m_ResizeLastMouseX = (float)screenX;
                        m_ResizeLastMouseY = (float)screenY;
                        Widget::BeginMouseInteraction(this);

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
                        Widget::BeginMouseInteraction(this);
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

                    if (m_DockingEnabled && m_Parent != nullptr && m_Parent->GetName() == "DockRoot" && DetachFromFloatingGroup())
                    {
                        e.Handled = true;
                        return true;
                    }

                    if (isTornOff)
                    {
                        m_DockIgnoreWindow = nullptr;
                        m_IsDragging = true;
                        m_IsMovingOwnerWindow = false;
                        auto [screenX, screenY] = m_OwnerWindow->GetScreenMousePosition();
                        HWND hwnd = static_cast<HWND>(m_OwnerWindow->GetNativeWindow());
                        RECT rect;
                        GetWindowRect(hwnd, &rect);
                        m_DragOffsetX = (float)screenX - (float)rect.left;
                        m_DragOffsetY = (float)screenY - (float)rect.top;
                        Widget::BeginMouseInteraction(this);

                        e.Handled = true;
                        return true;
                    }
                    else
                    {
                        // 메인 창 내부에 도킹된 상태: 엔진 내부 가상 창 이동
                        DockManager::ClearPreview();
                        DockManager::RemoveRelations(this);
                        m_DockIgnoreWindow = nullptr;
                        m_IsMovingOwnerWindow = false;

                        m_IsDragging = true;
                        m_LastMouseX = e.GetX();
                        m_LastMouseY = e.GetY();
                        m_DragOffsetX = e.GetX() - m_CalculatedPos.x;
                        m_DragOffsetY = e.GetY() - m_CalculatedPos.y;

                        BringToFront();

                        // 재도킹/재분리 반복 시 이전 상태가 남지 않도록
                        // 매 드래그 시작마다 현재 화면 사각형을 새 플로팅 기준으로 삼는다.
                        m_IsFloating = true;
                        SetAnchorMin(0.0f, 0.0f); SetAnchorMax(0.0f, 0.0f);
                        SetOffsetMin(m_CalculatedPos.x, m_CalculatedPos.y);
                        SetOffsetMax(m_CalculatedPos.x + m_CalculatedSize.x, m_CalculatedPos.y + m_CalculatedSize.y);
                        Widget::BeginMouseInteraction(this);
                        e.Handled = true;
                        return true;
                    }
                }
            }
            return false;
        }

        bool WindowPanel::DetachFromFloatingGroup()
        {
            if (!m_Parent || m_Parent->GetName() != "DockRoot")
                return false;

            auto app = Application::Get();
            if (!app)
                return false;

            Window* oldWindow = m_OwnerWindow ? m_OwnerWindow : &app->GetWindow();
            Widget* oldRoot = oldWindow->GetRootUI();
            Widget* oldParent = m_Parent;
            if (!oldRoot || oldRoot == this)
                return false;

            auto panelSize = m_PreferredFloatingSize;
            panelSize.x = (std::max)(panelSize.x, 220.0f);
            panelSize.y = (std::max)(panelSize.y, 160.0f);

            POINT cursor;
            GetCursorPos(&cursor);
            float detachOffsetX = (float)cursor.x;
            float detachOffsetY = (float)cursor.y;
            if (oldWindow)
            {
                HWND oldHwnd = static_cast<HWND>(oldWindow->GetNativeWindow());
                RECT oldRect;
                GetWindowRect(oldHwnd, &oldRect);
                detachOffsetX = (float)cursor.x - (float)oldRect.left;
                detachOffsetY = (float)cursor.y - (float)oldRect.top;
            }

            DockManager::ClearPreview();
            DockManager::RemoveRelations(this);
            m_DockIgnoreWindow = oldWindow;

            oldParent->RemoveChild(this);
            DockManager::DetachFromAllWindowRoots(this);

            const auto& remainingChildren = oldParent->GetChildren();
            if (oldRoot == oldParent && remainingChildren.size() == 1)
            {
                Widget* remaining = remainingChildren.front();
                oldParent->RemoveChild(remaining);
                oldWindow->SetRootUI(remaining);
                DockManager::FillWindowRoot(remaining);
                DockManager::SetOwnerWindowRecursive(remaining, oldWindow);
            }
            else
            {
                DockManager::CollapseDockRoot(oldParent);
                Window* remainingOwner = (oldWindow == &app->GetWindow()) ? nullptr : oldWindow;
                DockManager::SetOwnerWindowRecursive(oldRoot, remainingOwner);
            }

            Window* newWindow = Application::Get()->CreateSecondaryWindow(m_Name, (uint32_t)panelSize.x, (uint32_t)panelSize.y);
            DockManager::DetachFromAllWindowRoots(this);
            newWindow->SetRootUI(this);
            DockManager::DetachFromOtherWindowRoots(this, newWindow);

            SetOwnerWindow(newWindow);
            SetAnchorMin(0.0f, 0.0f);
            SetAnchorMax(1.0f, 1.0f);
            SetOffsetMin(0.0f, 0.0f);
            SetOffsetMax(0.0f, 0.0f);
            m_CalculatedPos = { 0.0f, 0.0f };
            m_CalculatedSize = panelSize;

            m_IsFloating = true;
            m_IsDragging = true;
            m_IsMovingOwnerWindow = false;
            m_ResizeMode = ResizeMode::None;
            Widget::BeginMouseInteraction(this);
            m_DragOffsetX = (std::clamp)(detachOffsetX, 16.0f, panelSize.x - 16.0f);
            m_DragOffsetY = (std::clamp)(detachOffsetY, 8.0f, 24.0f);

            newWindow->SetPosition(cursor.x - (int)m_DragOffsetX, cursor.y - (int)m_DragOffsetY);

            auto [screenX, screenY] = newWindow->GetScreenMousePosition();
            m_LastMouseX = (float)screenX;
            m_LastMouseY = (float)screenY;

            DockManager::NormalizeWindowPanelOwnership();

            return true;
        }

        bool WindowPanel::OnMouseMoved(MouseMovedEvent& e)
        {
            float edge = 8.0f;
            float outerEdge = 6.0f;

            // OS 이벤트 유실에 대비해 실제 마우스 버튼 상태로 드래그 상태를 보정합니다.
            if (m_ResizeMode != ResizeMode::None || m_IsDragging)
            {
                Widget::BeginMouseInteraction(this);
                bool isLMBDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                if (!isLMBDown)
                {
                    m_ResizeMode = ResizeMode::None;
                    m_IsDragging = false;
                    Widget::EndMouseInteraction(this);

                    if (m_IsMovingOwnerWindow)
                    {
                        DockManager::ClearPreview();
                        m_IsMovingOwnerWindow = false;
                        m_DockIgnoreWindow = nullptr;
                        return false;
                    }

                    // OS 이벤트/캡처 타이밍 때문에 MouseButtonReleased가 현재 패널까지
                    // 도달하지 않을 수 있으므로, MouseMoved 보정 경로에서도 드롭을 확정한다.
                    if (m_IsFloating && m_DockingEnabled)
                    {
                        DockManager::UpdatePreview(this, 0.0f, 0.0f);
                        if (!DockManager::ApplyPreview(this))
                            DockManager::ClearPreview();
                        m_DockIgnoreWindow = nullptr;
                    }
                    else
                    {
                        DockManager::ClearPreview();
                        m_DockIgnoreWindow = nullptr;
                    }
                    return false;
                }

                if (m_OwnerWindow != nullptr)
                {
                    e.Handled = true;
                    return true;
                }
            }

            // 테두리 위에 마우스가 있을 때 리사이즈 커서를 표시합니다.
            if (m_ResizeMode == ResizeMode::None && !m_IsDragging)
            {
                bool isLeft = e.GetX() >= m_CalculatedPos.x - outerEdge && e.GetX() <= m_CalculatedPos.x + edge;
                bool isRight = e.GetX() >= m_CalculatedPos.x + m_CalculatedSize.x - edge && e.GetX() <= m_CalculatedPos.x + m_CalculatedSize.x + outerEdge;
                bool isTop = e.GetY() >= m_CalculatedPos.y - outerEdge && e.GetY() <= m_CalculatedPos.y + edge;
                bool isBottom = e.GetY() >= m_CalculatedPos.y + m_CalculatedSize.y - edge && e.GetY() <= m_CalculatedPos.y + m_CalculatedSize.y + outerEdge;

                if (IsPointInside(e.GetX(), e.GetY()))
                {
                    if ((isTop && isLeft) || (isBottom && isRight)) SetCursor(LoadCursor(nullptr, IDC_SIZENWSE));
                    else if ((isTop && isRight) || (isBottom && isLeft)) SetCursor(LoadCursor(nullptr, IDC_SIZENESW));
                    else if (isLeft || isRight) SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
                    else if (isTop || isBottom) SetCursor(LoadCursor(nullptr, IDC_SIZENS));
                }
            }

            // 도킹된 패널의 리사이즈 오프셋을 갱신합니다.
            if (m_ResizeMode != ResizeMode::None)
            {
                float deltaX = e.GetX() - m_ResizeLastMouseX;
                float deltaY = e.GetY() - m_ResizeLastMouseY;
                float minWidth = 150.0f; float minHeight = 100.0f;

                if (DockManager::ResizeRelation(this, (int)m_ResizeMode, deltaX, deltaY))
                {
                    m_ResizeLastMouseX = e.GetX();
                    m_ResizeLastMouseY = e.GetY();

                    e.Handled = true;
                    return true;
                }

                if (m_Parent != nullptr && m_Parent->GetName() == "DockRoot")
                {
                    // DockRoot 안의 패널은 혼자 크기를 바꾸면 그룹 밖으로 튀어나온다.
                    // 공유 경계를 못 찾은 경우에는 자유 리사이즈로 빠지지 않고 입력만 소모한다.
                    m_ResizeLastMouseX = e.GetX();
                    m_ResizeLastMouseY = e.GetY();
                    e.Handled = true;
                    return true;
                }

                float newMinX = m_OffsetMin.x;
                float newMaxX = m_OffsetMax.x;
                float newMinY = m_OffsetMin.y;
                float newMaxY = m_OffsetMax.y;

                if (m_ResizeMode == ResizeMode::Left || m_ResizeMode == ResizeMode::TopLeft || m_ResizeMode == ResizeMode::BottomLeft) newMinX += deltaX;
                if (m_ResizeMode == ResizeMode::Right || m_ResizeMode == ResizeMode::TopRight || m_ResizeMode == ResizeMode::BottomRight) newMaxX += deltaX;
                if (m_ResizeMode == ResizeMode::Top || m_ResizeMode == ResizeMode::TopLeft || m_ResizeMode == ResizeMode::TopRight) newMinY += deltaY;
                if (m_ResizeMode == ResizeMode::Bottom || m_ResizeMode == ResizeMode::BottomLeft || m_ResizeMode == ResizeMode::BottomRight) newMaxY += deltaY;

                // 최소 크기를 침범하지 못하도록 보정
                if (newMaxX - newMinX < minWidth)
                {
                    if (m_ResizeMode == ResizeMode::Left || m_ResizeMode == ResizeMode::TopLeft || m_ResizeMode == ResizeMode::BottomLeft) newMinX = newMaxX - minWidth;
                    if (m_ResizeMode == ResizeMode::Right || m_ResizeMode == ResizeMode::TopRight || m_ResizeMode == ResizeMode::BottomRight) newMaxX = newMinX + minWidth;
                }
                if (newMaxY - newMinY < minHeight)
                {
                    if (m_ResizeMode == ResizeMode::Top || m_ResizeMode == ResizeMode::TopLeft || m_ResizeMode == ResizeMode::TopRight) newMinY = newMaxY - minHeight;
                    if (m_ResizeMode == ResizeMode::Bottom || m_ResizeMode == ResizeMode::BottomLeft || m_ResizeMode == ResizeMode::BottomRight) newMaxY = newMinY + minHeight;
                }

                // 확정된 오프셋 적용
                m_OffsetMin.x = newMinX;
                m_OffsetMax.x = newMaxX;
                m_OffsetMin.y = newMinY;
                m_OffsetMax.y = newMaxY;

                // 다음 프레임의 델타 계산을 위해 현재 마우스 좌표를 저장합니다.
                m_ResizeLastMouseX = e.GetX();
                m_ResizeLastMouseY = e.GetY();

                e.Handled = true;
                return true;
            }

            // 도킹된 패널의 이동과 분리 여부를 처리합니다.
            if (m_IsDragging)
            {
                float deltaX = e.GetX() - m_LastMouseX;
                float deltaY = e.GetY() - m_LastMouseY;
                m_OffsetMin.x += deltaX; m_OffsetMin.y += deltaY;
                m_OffsetMax.x += deltaX; m_OffsetMax.y += deltaY;

                // 다음 이동 델타 계산을 위해 현재 마우스 좌표를 저장합니다.
                m_LastMouseX = e.GetX();
                m_LastMouseY = e.GetY();

                auto& mainWindow = Application::Get()->GetWindow();
                float displayWidth = (float)mainWindow.GetWidth();
                float displayHeight = (float)mainWindow.GetHeight();
                if (m_DockingEnabled)
                    DockManager::UpdatePreview(this, e.GetX(), e.GetY());

                // 메인 창 밖으로 드래그하면 별도 창으로 분리합니다.
                if (m_DockingEnabled && (e.GetX() < 0 || e.GetX() > displayWidth || e.GetY() < 0 || e.GetY() > displayHeight))
                {
                    DockManager::ClearPreview();
                    DockManager::RemoveRelations(this);
                    m_IsDragging = false;
                    m_IsMovingOwnerWindow = false;

                    Widget* oldParent = m_Parent;
                    if (oldParent)
                    {
                        oldParent->RemoveChild(this);
                        DockManager::CollapseDockRoot(oldParent);
                    }

                    auto newSecWindow = Application::Get()->CreateSecondaryWindow(m_Name, (uint32_t)m_CalculatedSize.x, (uint32_t)m_CalculatedSize.y);
                    m_PreferredFloatingSize = { m_CalculatedSize.x, m_CalculatedSize.y };

                    SetAnchorMin(0.0f, 0.0f); SetAnchorMax(1.0f, 1.0f);
                    SetOffsetMin(0.0f, 0.0f); SetOffsetMax(0.0f, 0.0f);
                    m_CalculatedPos = { 0.0f, 0.0f };

                    newSecWindow->SetRootUI(this);
                    DockManager::DetachFromOtherWindowRoots(this, newSecWindow);
                    SetOwnerWindow(newSecWindow);

                    POINT pt;
                    GetCursorPos(&pt);
                    newSecWindow->SetPosition(pt.x - (int)m_DragOffsetX, pt.y - (int)m_DragOffsetY);

                    ReleaseCapture();
                    m_IsDragging = true;
                    m_IsMovingOwnerWindow = false;

                    auto [screenX, screenY] = newSecWindow->GetScreenMousePosition();
                    m_LastMouseX = (float)screenX;
                    m_LastMouseY = (float)screenY;
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
                Widget::EndMouseInteraction(this);
                e.Handled = true; return true;
            }

            if (m_IsDragging)
            {
                m_IsDragging = false;
                Widget::EndMouseInteraction(this);
                if (m_IsMovingOwnerWindow)
                {
                    DockManager::ClearPreview();
                    m_IsMovingOwnerWindow = false;
                    m_DockIgnoreWindow = nullptr;
                    e.Handled = true; return true;
                }

                if (m_IsFloating && m_DockingEnabled && m_Parent == nullptr)
                {
                    DockManager::UpdatePreview(this, 0.0f, 0.0f);
                    if (!DockManager::ApplyPreview(this))
                        DockManager::ClearPreview();
                }
                else if (m_IsFloating && m_DockingEnabled)
                {
                    // 메인 창 내부에서만 떠 있는 패널은 마지막 MouseMoved 이후의
                    // 실제 릴리즈 좌표로 도킹 후보를 다시 계산해야 한다.
                    // 그렇지 않으면 내부 플로팅 상태에서는 이전 프리뷰가 없거나
                    // 오래된 프리뷰를 사용해서 두 번째 도킹이 실패할 수 있다.
                    DockManager::UpdatePreview(this, e.GetX(), e.GetY());
                    if (!DockManager::ApplyPreview(this))
                        DockManager::ClearPreview();
                }
                m_DockIgnoreWindow = nullptr;
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

            // 비정상적인 창 크기로 복귀하지 않도록 기본 크기를 보정합니다.
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
