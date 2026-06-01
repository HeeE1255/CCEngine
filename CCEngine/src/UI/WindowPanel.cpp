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
        namespace
        {
            struct DockPreviewState
            {
                // Active는 실제 도킹이 가능한 상태, GuideVisible은 가이드 UI만 보여주는 상태입니다.
                // 마우스가 L/R/T/B 가이드 박스 위에 올라가기 전까지는 Active를 켜지 않아
                // 애매한 좌표 판정으로 창이 의도치 않게 붙는 문제를 막습니다.
                bool Active = false;
                bool GuideVisible = false;
                WindowPanel* Dragged = nullptr;
                WindowPanel* Target = nullptr;
                Window* TargetWindow = nullptr;
                Widget* TargetRoot = nullptr;
                DockDropMode Mode = DockDropMode::None;
                DirectX::XMFLOAT4 Rect = { 0.0f, 0.0f, 0.0f, 0.0f };
                DirectX::XMFLOAT4 GuideCenter = { 0.0f, 0.0f, 0.0f, 0.0f };
                DirectX::XMFLOAT4 GuideLeft = { 0.0f, 0.0f, 0.0f, 0.0f };
                DirectX::XMFLOAT4 GuideRight = { 0.0f, 0.0f, 0.0f, 0.0f };
                DirectX::XMFLOAT4 GuideTop = { 0.0f, 0.0f, 0.0f, 0.0f };
                DirectX::XMFLOAT4 GuideBottom = { 0.0f, 0.0f, 0.0f, 0.0f };
                bool IsRootTarget = false;
            };

            struct DockRelation
            {
                WindowPanel* First = nullptr;
                WindowPanel* Second = nullptr;
                bool Horizontal = true;
            };

            DockPreviewState s_DockPreview;
            std::vector<DockRelation> s_DockRelations;

            bool IsPointInsideRect(float x, float y, const DirectX::XMFLOAT4& rect)
            {
                return x >= rect.x && x <= rect.x + rect.z &&
                    y >= rect.y && y <= rect.y + rect.w;
            }

            DirectX::XMFLOAT4 MakeRect(float centerX, float centerY, float width, float height)
            {
                return { centerX - width * 0.5f, centerY - height * 0.5f, width, height };
            }

            void BuildDockGuideRects(DockPreviewState& state, const DirectX::XMFLOAT2& targetPos, const DirectX::XMFLOAT2& targetSize)
            {
                // Visual Studio처럼 타겟 패널 중앙에 도킹 방향 버튼을 배치합니다.
                // 실제 도킹 방향은 거리 계산이 아니라 이 사각형들의 히트 테스트로만 결정됩니다.
                constexpr float guideSize = 28.0f;
                constexpr float guideGap = 8.0f;
                float centerX = targetPos.x + targetSize.x * 0.5f;
                float centerY = targetPos.y + targetSize.y * 0.5f;

                state.GuideCenter = MakeRect(centerX, centerY, guideSize, guideSize);
                state.GuideLeft = MakeRect(centerX - guideSize - guideGap, centerY, guideSize, guideSize);
                state.GuideRight = MakeRect(centerX + guideSize + guideGap, centerY, guideSize, guideSize);
                state.GuideTop = MakeRect(centerX, centerY - guideSize - guideGap, guideSize, guideSize);
                state.GuideBottom = MakeRect(centerX, centerY + guideSize + guideGap, guideSize, guideSize);
            }

            DockDropMode GetDockGuideHitMode(const DockPreviewState& state, float mouseX, float mouseY)
            {
                // 도킹 확정 조건의 단일 진입점입니다.
                // 여기서 None이면 프리뷰는 안내 UI만 표시하고 ApplyPreview는 실패합니다.
                if (IsPointInsideRect(mouseX, mouseY, state.GuideLeft))
                    return DockDropMode::Left;
                if (IsPointInsideRect(mouseX, mouseY, state.GuideRight))
                    return DockDropMode::Right;
                if (IsPointInsideRect(mouseX, mouseY, state.GuideTop))
                    return DockDropMode::Top;
                if (IsPointInsideRect(mouseX, mouseY, state.GuideBottom))
                    return DockDropMode::Bottom;

                return DockDropMode::None;
            }

            bool IsMouseInsideWindow(Window* window, float mouseX, float mouseY)
            {
                return window &&
                    mouseX >= 0.0f && mouseX <= (float)window->GetWidth() &&
                    mouseY >= 0.0f && mouseY <= (float)window->GetHeight();
            }

            DirectX::XMFLOAT4 GetDockWorkspace(Window* window)
            {
                auto& mainWindow = Application::Get()->GetWindow();
                float topReservedHeight = (window == &mainWindow) ? 48.0f : 0.0f;
                return {
                    0.0f,
                    topReservedHeight,
                    window ? (float)window->GetWidth() : 0.0f,
                    window ? (std::max)(0.0f, (float)window->GetHeight() - topReservedHeight) : 0.0f
                };
            }

            DockDropMode GetGroupRootDropMode(Window* targetWindow, float mouseX, float mouseY)
            {
                DirectX::XMFLOAT4 workspace = GetDockWorkspace(targetWindow);
                float width = workspace.z;
                float height = workspace.w;
                if (width <= 0.0f || height <= 0.0f || !IsMouseInsideWindow(targetWindow, mouseX, mouseY))
                    return DockDropMode::None;

                if (mouseX < workspace.x || mouseX > workspace.x + workspace.z ||
                    mouseY < workspace.y || mouseY > workspace.y + workspace.w)
                    return DockDropMode::None;

                float localX = mouseX - workspace.x;
                float localY = mouseY - workspace.y;
                float leftDistance = localX;
                float rightDistance = width - localX;
                float topDistance = localY;
                float bottomDistance = height - localY;
                float minDistance = leftDistance;
                DockDropMode mode = DockDropMode::Left;

                if (rightDistance < minDistance) { minDistance = rightDistance; mode = DockDropMode::Right; }
                if (topDistance < minDistance) { minDistance = topDistance; mode = DockDropMode::Top; }
                if (bottomDistance < minDistance) { minDistance = bottomDistance; mode = DockDropMode::Bottom; }

                float edgeZoneX = width * 0.20f;
                float edgeZoneY = height * 0.20f;
                if ((mode == DockDropMode::Left || mode == DockDropMode::Right) && minDistance > edgeZoneX)
                    return DockDropMode::None;
                if ((mode == DockDropMode::Top || mode == DockDropMode::Bottom) && minDistance > edgeZoneY)
                    return DockDropMode::None;

                return mode;
            }

            DirectX::XMFLOAT4 GetGroupRootDockRect(Window* targetWindow, DockDropMode mode)
            {
                DirectX::XMFLOAT4 rect = GetDockWorkspace(targetWindow);
                if (mode == DockDropMode::Left)
                    rect.z *= 0.5f;
                else if (mode == DockDropMode::Right)
                {
                    rect.x += rect.z * 0.5f;
                    rect.z *= 0.5f;
                }
                else if (mode == DockDropMode::Top)
                    rect.w *= 0.5f;
                else if (mode == DockDropMode::Bottom)
                {
                    rect.y += rect.w * 0.5f;
                    rect.w *= 0.5f;
                }
                return rect;
            }

            void SetOwnerWindowRecursive(Widget* widget, Window* ownerWindow)
            {
                if (!widget)
                    return;

                if (WindowPanel* panel = dynamic_cast<WindowPanel*>(widget))
                    panel->SetOwnerWindow(ownerWindow);

                for (Widget* child : widget->GetChildren())
                    SetOwnerWindowRecursive(child, ownerWindow);
            }

            Window* FindWindowOwningRoot(Widget* root)
            {
                auto app = Application::Get();
                if (!app || !root)
                    return nullptr;

                Window* mainWindow = &app->GetWindow();
                if (mainWindow->GetRootUI() == root)
                    return mainWindow;

                for (Window* secondaryWindow : app->GetSecondaryWindows())
                {
                    if (secondaryWindow && secondaryWindow->GetRootUI() == root)
                        return secondaryWindow;
                }

                return nullptr;
            }

            void FillWindowRoot(Widget* widget)
            {
                if (!widget)
                    return;

                // OS 창의 RootUI로 쓰는 위젯은 반드시 창 전체를 채워야 합니다.
                // DockRoot가 0 크기로 접히면 내부 Scene/Game View가 사라진 것처럼 보입니다.
                widget->SetAnchorMin(0.0f, 0.0f);
                widget->SetAnchorMax(1.0f, 1.0f);
                widget->SetOffsetMin(0.0f, 0.0f);
                widget->SetOffsetMax(0.0f, 0.0f);
            }

            void ApplyDockRootAnchorRect(WindowPanel* panel, float minX, float minY, float maxX, float maxY)
            {
                if (!panel)
                    return;

                // DockRoot 내부 패널은 픽셀 좌표 대신 0~1 앵커 비율로 보관합니다.
                // 이렇게 해야 Dock Group 창을 리사이즈해도 자식 패널들이 함께 비율 유지됩니다.
                panel->SetAnchorMin((std::clamp)(minX, 0.0f, 1.0f), (std::clamp)(minY, 0.0f, 1.0f));
                panel->SetAnchorMax((std::clamp)(maxX, 0.0f, 1.0f), (std::clamp)(maxY, 0.0f, 1.0f));
                panel->SetOffsetMin(0.0f, 0.0f);
                panel->SetOffsetMax(0.0f, 0.0f);
            }

            void ApplyWidgetAbsoluteRectInParent(Widget* widget, Widget* parent, float x, float y, float width, float height)
            {
                if (!widget)
                    return;

                // 메인 윈도우 내부에 DockRoot를 자식 컨테이너로 만들 때 사용합니다.
                // 화면 좌표로 받은 사각형을 부모 로컬 오프셋으로 변환해 기존 패널 위치를 보존합니다.
                DirectX::XMFLOAT2 parentPos = { 0.0f, 0.0f };
                if (parent)
                    parentPos = parent->GetCalculatedPosition();

                widget->SetAnchorMin(0.0f, 0.0f);
                widget->SetAnchorMax(0.0f, 0.0f);
                widget->SetOffsetMin(x - parentPos.x, y - parentPos.y);
                widget->SetOffsetMax(x - parentPos.x + width, y - parentPos.y + height);
            }

            void DetachWidgetFromWindowRoot(Window* window, Widget* widget)
            {
                if (!window || !widget)
                    return;

                Widget* root = window->GetRootUI();
                if (!root)
                    return;
                 
                if (root == widget)
                {
                    window->SetRootUI(nullptr);
                    return;
                }

                root->RemoveDescendant(widget);
            }

            void DetachWidgetFromAllWindowRoots(Widget* widget)
            {
                auto app = Application::Get();
                if (!app || !widget)
                    return;

                DetachWidgetFromWindowRoot(&app->GetWindow(), widget);
                for (Window* secondaryWindow : app->GetSecondaryWindows())
                    DetachWidgetFromWindowRoot(secondaryWindow, widget);
            }

            void DetachWidgetFromOtherWindowRoots(Widget* widget, Window* keepWindow)
            {
                auto app = Application::Get();
                if (!app || !widget)
                    return;

                Window* mainWindow = &app->GetWindow();
                if (mainWindow != keepWindow)
                    DetachWidgetFromWindowRoot(mainWindow, widget);

                for (Window* secondaryWindow : app->GetSecondaryWindows())
                {
                    if (secondaryWindow != keepWindow)
                        DetachWidgetFromWindowRoot(secondaryWindow, widget);
                }
            }

            void CollectWrongOwnerPanels(Window* window, Widget* widget, std::vector<WindowPanel*>& out)
            {
                if (!window || !widget)
                    return;

                Window* expectedOwner = (window == &Application::Get()->GetWindow()) ? nullptr : window;
                if (WindowPanel* panel = dynamic_cast<WindowPanel*>(widget))
                {
                    if (panel->GetOwnerWindow() != expectedOwner)
                        out.push_back(panel);
                }

                for (Widget* child : widget->GetChildren())
                    CollectWrongOwnerPanels(window, child, out);
            }

            void CollapseSingleChildDockRoot(Window* window)
            {
                if (!window)
                    return;

                // Dock Group에서 패널 하나가 떨어져 나가고 자식이 하나만 남으면
                // 불필요한 DockRoot 껍데기를 제거해 일반 단일 창 상태로 되돌립니다.
                Widget* root = window->GetRootUI();
                if (!root || root->GetName() != "DockRoot")
                    return;

                const auto& children = root->GetChildren();
                if (children.size() != 1)
                    return;

                Widget* onlyChild = children.front();
                root->RemoveChild(onlyChild);
                window->SetRootUI(onlyChild);
                FillWindowRoot(onlyChild);
                Window* expectedOwner = (window == &Application::Get()->GetWindow()) ? nullptr : window;
                SetOwnerWindowRecursive(onlyChild, expectedOwner);
            }

            void NormalizeAllWindowPanelOwnership()
            {
                auto app = Application::Get();
                if (!app)
                    return;

                std::vector<Window*> windows;
                windows.push_back(&app->GetWindow());
                for (Window* secondaryWindow : app->GetSecondaryWindows())
                    windows.push_back(secondaryWindow);

                for (Window* window : windows)
                {
                    if (!window || !window->GetRootUI())
                        continue;

                    std::vector<WindowPanel*> wrongOwnerPanels;
                    CollectWrongOwnerPanels(window, window->GetRootUI(), wrongOwnerPanels);
                    for (WindowPanel* panel : wrongOwnerPanels)
                    {
                        if (window->GetRootUI() == panel)
                            window->SetRootUI(nullptr);
                        else
                            window->GetRootUI()->RemoveDescendant(panel);
                    }

                    CollapseSingleChildDockRoot(window);
                }
            }

            class DockRootWidget : public Widget
            {
            public:
                DockRootWidget(bool fillWindow) : Widget("DockRoot")
                {
                    // 멀티 윈도우의 RootUI로 쓰일 때는 창 전체를 채우고,
                    // 메인 윈도우 내부 그룹으로 쓰일 때는 부모가 준 사각형만 차지합니다.
                    if (fillWindow)
                        FillWindowRoot(this);
                }

                bool OnEvent(Event& e) override
                {
                    if (e.GetEventType() == EventType::MouseButtonPressed)
                    {
                        MouseButtonPressedEvent& mouseEvent = static_cast<MouseButtonPressedEvent&>(e);
                        if (IsPointOnResizeBorder(mouseEvent.GetX(), mouseEvent.GetY()) ||
                            IsPointOnGroupBar(mouseEvent.GetX(), mouseEvent.GetY()))
                        {
                            return OnMouseButtonPressed(mouseEvent);
                        }
                    }
                    else if (e.GetEventType() == EventType::MouseButtonReleased && (m_IsMoving || m_ResizeMode != ResizeMode::None))
                    {
                        return OnMouseButtonReleased(static_cast<MouseButtonReleasedEvent&>(e));
                    }
                    else if (e.GetEventType() == EventType::MouseMoved && (m_IsMoving || m_ResizeMode != ResizeMode::None))
                    {
                        MouseMovedEvent& mouseEvent = static_cast<MouseMovedEvent&>(e);
                        if (m_ResizeMode != ResizeMode::None)
                            ResizeInternalDockRoot(mouseEvent.GetX(), mouseEvent.GetY());
                        else if (IsFloatingDockRoot())
                            MoveOwnerWindow();
                        else
                            MoveInternalDockRoot(mouseEvent.GetX(), mouseEvent.GetY());

                        e.Handled = true;
                        return true;
                    }

                    return Widget::OnEvent(e);
                }

                bool WantsMouseCapture() const override
                {
                    return m_IsMoving || m_ResizeMode != ResizeMode::None;
                }

                void OnUpdate(float deltaTime) override
                {
                    Widget::OnUpdate(deltaTime);

                    if (!m_IsMoving && m_ResizeMode == ResizeMode::None)
                        return;

                    bool isLMBDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                    if (!isLMBDown)
                    {
                        FinishMove(FindWindowOwningRoot(this));
                        m_ResizeMode = ResizeMode::None;
                        return;
                    }

                    if (m_ResizeMode != ResizeMode::None)
                    {
                        auto app = Application::Get();
                        if (app)
                        {
                            auto [mouseX, mouseY] = app->GetWindow().GetMousePosition();
                            ResizeInternalDockRoot(mouseX, mouseY);
                        }
                        return;
                    }

                    if (IsFloatingDockRoot())
                    {
                        MoveOwnerWindow();
                    }
                    else
                    {
                        auto app = Application::Get();
                        if (app)
                        {
                            auto [mouseX, mouseY] = app->GetWindow().GetMousePosition();
                            MoveInternalDockRoot(mouseX, mouseY);
                        }
                    }
                }

                void OnRender() override
                {
                    if (ShouldShowGroupBar())
                    {
                        UIRenderer::DrawRectFilled(m_CalculatedPos.x, m_CalculatedPos.y, m_CalculatedSize.x, GroupBarHeight, { 0.12f, 0.12f, 0.14f, 1.0f });
                        UIRenderer::DrawString("Dock Group", m_CalculatedPos.x + 10.0f, m_CalculatedPos.y + GroupBarHeight * 0.7f, { 0.75f, 0.75f, 0.78f, 1.0f });

                        if (IsFloatingDockRoot())
                        {
                            float closeBtnX = m_CalculatedPos.x + m_CalculatedSize.x - 30.0f;
                            UIRenderer::DrawRectFilled(closeBtnX, m_CalculatedPos.y, 30.0f, GroupBarHeight, { 0.55f, 0.16f, 0.18f, 1.0f });
                            UIRenderer::DrawString("X", closeBtnX + 10.0f, m_CalculatedPos.y + GroupBarHeight * 0.7f, { 1.0f, 1.0f, 1.0f, 1.0f });
                        }
                    }

                    Widget::OnRender();
                }

                void UpdateLayout(const DirectX::XMFLOAT2& parentPos, const DirectX::XMFLOAT2& parentSize) override
                {
                    if (!m_IsVisible)
                        return;

                    // DockRoot는 일반 Widget::UpdateLayout을 그대로 쓰지 않습니다.
                    // 메인 내부 그룹과 플로팅 그룹 모두 상단 그룹 바 24px을 자식 패널 영역에서 제외합니다.
                    float anchorLeft = parentPos.x + (parentSize.x * m_AnchorMin.x);
                    float anchorTop = parentPos.y + (parentSize.y * m_AnchorMin.y);
                    float anchorRight = parentPos.x + (parentSize.x * m_AnchorMax.x);
                    float anchorBottom = parentPos.y + (parentSize.y * m_AnchorMax.y);

                    float finalLeft = anchorLeft + m_OffsetMin.x;
                    float finalTop = anchorTop + m_OffsetMin.y;
                    float finalRight = anchorRight + m_OffsetMax.x;
                    float finalBottom = anchorBottom + m_OffsetMax.y;

                    m_CalculatedPos = { finalLeft, finalTop };
                    m_CalculatedSize = { finalRight - finalLeft, finalBottom - finalTop };

                    float groupBarHeight = ShouldShowGroupBar() ? GroupBarHeight : 0.0f;
                    DirectX::XMFLOAT2 contentPos = { m_CalculatedPos.x, m_CalculatedPos.y + groupBarHeight };
                    DirectX::XMFLOAT2 contentSize = { m_CalculatedSize.x, (std::max)(0.0f, m_CalculatedSize.y - groupBarHeight) };
                    for (Widget* child : m_Children)
                    {
                        if (child)
                            child->UpdateLayout(contentPos, contentSize);
                    }
                }

            protected:
                bool OnMouseButtonPressed(MouseButtonPressedEvent& e) override
                {
                    if (e.GetButton() != 0)
                        return false;

                    bool onGroupBar = IsPointOnGroupBar(e.GetX(), e.GetY());

                    if (!IsFloatingDockRoot() && !onGroupBar)
                    {
                        ResizeMode resizeMode = HitTestResizeMode(e.GetX(), e.GetY());
                        if (resizeMode != ResizeMode::None)
                        {
                            m_ResizeMode = resizeMode;
                            m_ResizeLastMouseX = e.GetX();
                            m_ResizeLastMouseY = e.GetY();
                            e.Handled = true;
                            return true;
                        }
                    }

                    if (!onGroupBar)
                        return false;

                    if (IsFloatingDockRoot())
                    {
                        Window* ownerWindow = FindWindowOwningRoot(this);
                        if (!ownerWindow)
                            return false;

                        float closeBtnX = m_CalculatedPos.x + m_CalculatedSize.x - 30.0f;
                        if (e.GetX() >= closeBtnX)
                        {
                            Application::Get()->RequestCloseSecondaryWindow(ownerWindow);
                            e.Handled = true;
                            return true;
                        }

                        auto [screenX, screenY] = ownerWindow->GetScreenMousePosition();
                        HWND hwnd = static_cast<HWND>(ownerWindow->GetNativeWindow());
                        RECT rect;
                        GetWindowRect(hwnd, &rect);
                        m_DragOffsetX = (float)screenX - (float)rect.left;
                        m_DragOffsetY = (float)screenY - (float)rect.top;
                        m_IsMoving = true;
                        MoveOwnerWindow();
                    }
                    else
                    {
                        BringToFront();
                        m_DragOffsetX = e.GetX() - m_CalculatedPos.x;
                        m_DragOffsetY = e.GetY() - m_CalculatedPos.y;
                        m_IsMoving = true;
                        MoveInternalDockRoot(e.GetX(), e.GetY());
                    }

                    e.Handled = true;
                    return true;
                }

                bool OnMouseButtonReleased(MouseButtonReleasedEvent& e) override
                {
                    if (!m_IsMoving && m_ResizeMode == ResizeMode::None)
                        return false;

                    if (m_IsMoving)
                        FinishMove(FindWindowOwningRoot(this));
                    m_ResizeMode = ResizeMode::None;
                    e.Handled = true;
                    return true;
                }

            private:
                bool IsFloatingDockRoot() const
                {
                    auto app = Application::Get();
                    Window* ownerWindow = FindWindowOwningRoot(const_cast<DockRootWidget*>(this));
                    return app && ownerWindow && ownerWindow != &app->GetWindow();
                }

                bool ShouldShowGroupBar() const
                {
                    // DockRoot는 메인 내부 그룹이든 멀티 윈도우 그룹이든 항상 그룹 헤더를 표시합니다.
                    // 그래야 최초 메인 내부 도킹과 멀티 윈도우 왕복 후 도킹의 UI가 동일해집니다.
                    return true;
                }

                bool IsPointOnGroupBar(float x, float y) const
                {
                    return x >= m_CalculatedPos.x &&
                        x <= m_CalculatedPos.x + m_CalculatedSize.x &&
                        y >= m_CalculatedPos.y &&
                        y <= m_CalculatedPos.y + GroupBarHeight;
                }

                bool IsPointOnResizeBorder(float x, float y) const
                {
                    return !IsFloatingDockRoot() && HitTestResizeMode(x, y) != ResizeMode::None;
                }

                ResizeMode HitTestResizeMode(float x, float y) const
                {
                    if (IsFloatingDockRoot())
                        return ResizeMode::None;

                    constexpr float edge = 8.0f;
                    bool inside = x >= m_CalculatedPos.x && x <= m_CalculatedPos.x + m_CalculatedSize.x &&
                        y >= m_CalculatedPos.y && y <= m_CalculatedPos.y + m_CalculatedSize.y;
                    if (!inside)
                        return ResizeMode::None;

                    bool isLeft = x <= m_CalculatedPos.x + edge;
                    bool isRight = x >= m_CalculatedPos.x + m_CalculatedSize.x - edge;
                    bool isTop = y <= m_CalculatedPos.y + edge;
                    bool isBottom = y >= m_CalculatedPos.y + m_CalculatedSize.y - edge;

                    if (isTop && isLeft) return ResizeMode::TopLeft;
                    if (isTop && isRight) return ResizeMode::TopRight;
                    if (isBottom && isLeft) return ResizeMode::BottomLeft;
                    if (isBottom && isRight) return ResizeMode::BottomRight;
                    if (isLeft) return ResizeMode::Left;
                    if (isRight) return ResizeMode::Right;
                    if (isTop) return ResizeMode::Top;
                    if (isBottom) return ResizeMode::Bottom;
                    return ResizeMode::None;
                }

                void MoveOwnerWindow()
                {
                    Window* ownerWindow = FindWindowOwningRoot(this);
                    if (!ownerWindow)
                        return;

                    auto [screenX, screenY] = ownerWindow->GetScreenMousePosition();
                    ownerWindow->SetPosition((int)(screenX - m_DragOffsetX), (int)(screenY - m_DragOffsetY));
                }

                void ApplyInternalRect(float left, float top, float right, float bottom)
                {
                    Widget* parent = GetParent();
                    DirectX::XMFLOAT2 parentPos = parent ? parent->GetCalculatedPosition() : DirectX::XMFLOAT2{ 0.0f, 0.0f };

                    SetAnchorMin(0.0f, 0.0f);
                    SetAnchorMax(0.0f, 0.0f);
                    SetOffsetMin(left - parentPos.x, top - parentPos.y);
                    SetOffsetMax(right - parentPos.x, bottom - parentPos.y);
                }

                void MoveInternalDockRoot(float mouseX, float mouseY)
                {
                    if (IsFloatingDockRoot())
                        return;

                    float width = (std::max)(m_CalculatedSize.x, 220.0f);
                    float height = (std::max)(m_CalculatedSize.y, 160.0f);
                    float left = mouseX - m_DragOffsetX;
                    float top = mouseY - m_DragOffsetY;
                    ApplyInternalRect(left, top, left + width, top + height);
                }

                void ResizeInternalDockRoot(float mouseX, float mouseY)
                {
                    if (IsFloatingDockRoot() || m_ResizeMode == ResizeMode::None)
                        return;

                    float deltaX = mouseX - m_ResizeLastMouseX;
                    float deltaY = mouseY - m_ResizeLastMouseY;
                    float left = m_CalculatedPos.x;
                    float top = m_CalculatedPos.y;
                    float right = m_CalculatedPos.x + m_CalculatedSize.x;
                    float bottom = m_CalculatedPos.y + m_CalculatedSize.y;

                    if (m_ResizeMode == ResizeMode::Left || m_ResizeMode == ResizeMode::TopLeft || m_ResizeMode == ResizeMode::BottomLeft) left += deltaX;
                    if (m_ResizeMode == ResizeMode::Right || m_ResizeMode == ResizeMode::TopRight || m_ResizeMode == ResizeMode::BottomRight) right += deltaX;
                    if (m_ResizeMode == ResizeMode::Top || m_ResizeMode == ResizeMode::TopLeft || m_ResizeMode == ResizeMode::TopRight) top += deltaY;
                    if (m_ResizeMode == ResizeMode::Bottom || m_ResizeMode == ResizeMode::BottomLeft || m_ResizeMode == ResizeMode::BottomRight) bottom += deltaY;

                    constexpr float minWidth = 220.0f;
                    constexpr float minHeight = 160.0f;
                    if (right - left < minWidth)
                    {
                        if (m_ResizeMode == ResizeMode::Left || m_ResizeMode == ResizeMode::TopLeft || m_ResizeMode == ResizeMode::BottomLeft)
                            left = right - minWidth;
                        else
                            right = left + minWidth;
                    }
                    if (bottom - top < minHeight)
                    {
                        if (m_ResizeMode == ResizeMode::Top || m_ResizeMode == ResizeMode::TopLeft || m_ResizeMode == ResizeMode::TopRight)
                            top = bottom - minHeight;
                        else
                            bottom = top + minHeight;
                    }

                    ApplyInternalRect(left, top, right, bottom);
                    m_ResizeLastMouseX = mouseX;
                    m_ResizeLastMouseY = mouseY;
                }

                void FinishMove(Window* ownerWindow)
                {
                    if (!m_IsMoving)
                        return;

                    m_IsMoving = false;
                    if (IsFloatingDockRoot())
                        DockGroupIntoMain(ownerWindow);
                }

                void DockGroupIntoMain(Window* ownerWindow)
                {
                    auto app = Application::Get();
                    if (!app || !ownerWindow || ownerWindow == &app->GetWindow())
                        return;

                    // 플로팅 Dock Group의 그룹 바를 잡고 메인 윈도우 가장자리로 끌어오면
                    // 그룹 전체를 하나의 컨테이너로 메인 루트 아래에 다시 붙입니다.
                    Window& mainWindow = app->GetWindow();
                    auto [mouseX, mouseY] = mainWindow.GetMousePosition();
                    DockDropMode mode = GetGroupRootDropMode(&mainWindow, mouseX, mouseY);
                    if (mode == DockDropMode::None)
                        return;

                    DirectX::XMFLOAT4 targetRect = GetGroupRootDockRect(&mainWindow, mode);
                    DirectX::XMFLOAT2 rootPos = GetCalculatedPosition();
                    DirectX::XMFLOAT2 rootSize = GetCalculatedSize();
                    float oldContentY = IsFloatingDockRoot() ? GroupBarHeight : 0.0f;
                    float oldContentW = (std::max)(rootSize.x, 1.0f);
                    float oldContentH = (std::max)(rootSize.y - oldContentY, 1.0f);

                    for (Widget* child : m_Children)
                    {
                        if (!child)
                            continue;

                        auto childPos = child->GetCalculatedPosition();
                        auto childSize = child->GetCalculatedSize();
                        float localX = childPos.x - rootPos.x;
                        float localY = childPos.y - rootPos.y - oldContentY;
                        child->SetAnchorMin(0.0f, 0.0f);
                        child->SetAnchorMax(0.0f, 0.0f);
                        child->SetOffsetMin((localX / oldContentW) * targetRect.z, (localY / oldContentH) * targetRect.w);
                        child->SetOffsetMax(((localX + childSize.x) / oldContentW) * targetRect.z, ((localY + childSize.y) / oldContentH) * targetRect.w);
                    }

                    app->RequestCloseSecondaryWindow(ownerWindow);
                    SetAnchorMin(0.0f, 0.0f);
                    SetAnchorMax(0.0f, 0.0f);
                    SetOffsetMin(targetRect.x, targetRect.y);
                    SetOffsetMax(targetRect.x + targetRect.z, targetRect.y + targetRect.w);
                    SetOwnerWindowRecursive(this, nullptr);
                    mainWindow.GetRootUI()->AddChild(this);
                }

                static constexpr float GroupBarHeight = 24.0f;
                bool m_IsMoving = false;
                ResizeMode m_ResizeMode = ResizeMode::None;
                float m_DragOffsetX = 0.0f;
                float m_DragOffsetY = 0.0f;
                float m_ResizeLastMouseX = 0.0f;
                float m_ResizeLastMouseY = 0.0f;
            };

            Widget* CreateDockRoot(bool fillWindow = true)
            {
                Widget* dockRoot = new DockRootWidget(fillWindow);
                if (fillWindow)
                    FillWindowRoot(dockRoot);
                return dockRoot;
            }
        }

        WindowPanel::WindowPanel(const std::string& name, const std::string& title)
            : Panel(name, { 0.1f, 0.1f, 0.11f, 1.0f }), m_Title(title)
        {
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

                    if (!m_IsMovingOwnerWindow)
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

                    if (!m_IsMovingOwnerWindow)
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
                        if (m_Parent != nullptr && DetachFromFloatingGroup())
                        {
                            e.Handled = true;
                            return true;
                        }

                        m_DockIgnoreWindow = nullptr;
                        m_IsDragging = true;
                        m_IsMovingOwnerWindow = false;
                        auto [screenX, screenY] = m_OwnerWindow->GetScreenMousePosition();
                        HWND hwnd = static_cast<HWND>(m_OwnerWindow->GetNativeWindow());
                        RECT rect;
                        GetWindowRect(hwnd, &rect);
                        m_DragOffsetX = (float)screenX - (float)rect.left;
                        m_DragOffsetY = (float)screenY - (float)rect.top;

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
                        e.Handled = true;
                        return true;
                    }
                }
            }
            return false;
        }

        bool WindowPanel::DetachFromFloatingGroup()
        {
            if (!m_OwnerWindow || !m_Parent)
                return false;

            Window* oldWindow = m_OwnerWindow;
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
            DetachWidgetFromAllWindowRoots(this);

            const auto& remainingChildren = oldParent->GetChildren();
            if (oldRoot == oldParent && remainingChildren.size() == 1)
            {
                Widget* remaining = remainingChildren.front();
                oldParent->RemoveChild(remaining);
                oldWindow->SetRootUI(remaining);
                FillWindowRoot(remaining);
                SetOwnerWindowRecursive(remaining, oldWindow);
            }
            else
            {
                SetOwnerWindowRecursive(oldRoot, oldWindow);
            }

            Window* newWindow = Application::Get()->CreateSecondaryWindow(m_Name, (uint32_t)panelSize.x, (uint32_t)panelSize.y);
            DetachWidgetFromAllWindowRoots(this);
            newWindow->SetRootUI(this);
            DetachWidgetFromOtherWindowRoots(this, newWindow);

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
            m_DragOffsetX = (std::clamp)(detachOffsetX, 16.0f, panelSize.x - 16.0f);
            m_DragOffsetY = (std::clamp)(detachOffsetY, 8.0f, 24.0f);

            newWindow->SetPosition(cursor.x - (int)m_DragOffsetX, cursor.y - (int)m_DragOffsetY);

            auto [screenX, screenY] = newWindow->GetScreenMousePosition();
            m_LastMouseX = (float)screenX;
            m_LastMouseY = (float)screenY;

            NormalizeAllWindowPanelOwnership();

            return true;
        }

        bool WindowPanel::OnMouseMoved(MouseMovedEvent& e)
        {
            float edge = 8.0f;

            // OS 이벤트 유실에 대비해 실제 마우스 버튼 상태로 드래그 상태를 보정합니다.
            if (m_ResizeMode != ResizeMode::None || m_IsDragging)
            {
                bool isLMBDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                if (!isLMBDown)
                {
                    m_ResizeMode = ResizeMode::None;
                    m_IsDragging = false;

                    if (m_IsMovingOwnerWindow)
                    {
                        DockManager::ClearPreview();
                        m_IsMovingOwnerWindow = false;
                        m_DockIgnoreWindow = nullptr;
                        return false;
                    }

                    // OS 이벤트/캡처 타이밍 때문에 MouseButtonReleased가 현재 패널까지
                    // 도달하지 않을 수 있으므로, MouseMoved 보정 경로에서도 드롭을 확정한다.
                    if (m_IsFloating)
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
                DockManager::UpdatePreview(this, e.GetX(), e.GetY());

                // 메인 창 밖으로 드래그하면 별도 창으로 분리합니다.
                if (e.GetX() < 0 || e.GetX() > displayWidth || e.GetY() < 0 || e.GetY() > displayHeight)
                {
                    DockManager::ClearPreview();
                    DockManager::RemoveRelations(this);
                    m_IsDragging = false;
                    m_IsMovingOwnerWindow = false;

                    if (m_Parent) m_Parent->RemoveChild(this);

                    auto newSecWindow = Application::Get()->CreateSecondaryWindow(m_Name, (uint32_t)m_CalculatedSize.x, (uint32_t)m_CalculatedSize.y);
                    m_PreferredFloatingSize = { m_CalculatedSize.x, m_CalculatedSize.y };

                    SetAnchorMin(0.0f, 0.0f); SetAnchorMax(1.0f, 1.0f);
                    SetOffsetMin(0.0f, 0.0f); SetOffsetMax(0.0f, 0.0f);
                    m_CalculatedPos = { 0.0f, 0.0f };

                    newSecWindow->SetRootUI(this);
                    DetachWidgetFromOtherWindowRoots(this, newSecWindow);
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
                e.Handled = true; return true;
            }

            if (m_IsDragging)
            {
                m_IsDragging = false;
                if (m_IsMovingOwnerWindow)
                {
                    DockManager::ClearPreview();
                    m_IsMovingOwnerWindow = false;
                    m_DockIgnoreWindow = nullptr;
                    e.Handled = true; return true;
                }

                if (m_IsFloating && m_Parent == nullptr)
                {
                    DockManager::UpdatePreview(this, 0.0f, 0.0f);
                    if (!DockManager::ApplyPreview(this))
                        DockManager::ClearPreview();
                }
                else if (m_IsFloating)
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

        void DockManager::ClearPreview()
        {
            s_DockPreview = {};
        }

        void DockManager::DrawPreview(Window* renderWindow)
        {
            if ((!s_DockPreview.GuideVisible && !s_DockPreview.Active) || s_DockPreview.TargetWindow != renderWindow)
                return;

            // 가이드 UI는 항상 먼저 그립니다. Active가 아니어도 표시되어야 사용자가
            // 어느 방향으로 놓을 수 있는지 알 수 있습니다.
            auto drawGuide = [](const DirectX::XMFLOAT4& rect, const char* label, bool active)
                {
                    DirectX::XMFLOAT4 fill = active ? DirectX::XMFLOAT4{ 0.25f, 0.48f, 0.95f, 0.82f } : DirectX::XMFLOAT4{ 0.12f, 0.13f, 0.15f, 0.88f };
                    DirectX::XMFLOAT4 border = active ? DirectX::XMFLOAT4{ 0.55f, 0.75f, 1.0f, 1.0f } : DirectX::XMFLOAT4{ 0.55f, 0.58f, 0.62f, 0.9f };
                    UIRenderer::DrawRectFilled(rect.x, rect.y, rect.z, rect.w, fill);
                    UIRenderer::DrawRect(rect.x, rect.y, rect.z, rect.w, border);
                    UIRenderer::DrawString(label, rect.x + 9.0f, rect.y + 20.0f, { 0.92f, 0.94f, 0.98f, 1.0f });
                };

            drawGuide(s_DockPreview.GuideCenter, "+", false);
            drawGuide(s_DockPreview.GuideLeft, "L", s_DockPreview.Mode == DockDropMode::Left);
            drawGuide(s_DockPreview.GuideRight, "R", s_DockPreview.Mode == DockDropMode::Right);
            drawGuide(s_DockPreview.GuideTop, "T", s_DockPreview.Mode == DockDropMode::Top);
            drawGuide(s_DockPreview.GuideBottom, "B", s_DockPreview.Mode == DockDropMode::Bottom);

            if (!s_DockPreview.Active)
                return;

            // 마우스가 방향 버튼 위에 올라간 순간에만 실제 도킹 예정 영역을 반투명으로 보여줍니다.
            UIRenderer::DrawRectFilled(
                s_DockPreview.Rect.x,
                s_DockPreview.Rect.y,
                s_DockPreview.Rect.z,
                s_DockPreview.Rect.w,
                { 0.15f, 0.45f, 0.95f, 0.28f }
            );
            UIRenderer::DrawRect(
                s_DockPreview.Rect.x,
                s_DockPreview.Rect.y,
                s_DockPreview.Rect.z,
                s_DockPreview.Rect.w,
                { 0.35f, 0.65f, 1.0f, 0.9f }
            );
        }

        void DockManager::UpdatePreview(WindowPanel* draggedPanel, float mouseX, float mouseY)
        {
            s_DockPreview = {};
            if (!draggedPanel)
                return;

            auto app = Application::Get();
            if (!app)
                return;

            struct DockCandidate
            {
                Window* WindowPtr = nullptr;
                Widget* Root = nullptr;
                float MouseX = 0.0f;
                float MouseY = 0.0f;
            };

            std::vector<DockCandidate> candidates;
            for (Window* secondaryWindow : app->GetSecondaryWindows())
                candidates.push_back({ secondaryWindow, secondaryWindow ? secondaryWindow->GetRootUI() : nullptr, 0.0f, 0.0f });
            Window& mainWindow = app->GetWindow();
            candidates.push_back({ &mainWindow, mainWindow.GetRootUI(), 0.0f, 0.0f });

            // 현재 마우스가 올라가 있는 창 하나만 도킹 후보로 삼습니다.
            // 소스 창과 ignore 창은 제외해 방금 떼어낸 창으로 즉시 재도킹되는 루프를 막습니다.
            DockCandidate candidate = {};
            bool hasCandidate = false;
            for (DockCandidate& current : candidates)
            {
                if (!current.WindowPtr || !current.Root)
                    continue;

                if (current.WindowPtr == draggedPanel->GetOwnerWindow())
                    continue;
                if (current.WindowPtr == draggedPanel->m_DockIgnoreWindow)
                    continue;

                auto [localX, localY] = current.WindowPtr->GetMousePosition();
                if (!IsMouseInsideWindow(current.WindowPtr, localX, localY))
                    continue;

                if (current.Root == draggedPanel)
                    continue;

                current.MouseX = localX;
                current.MouseY = localY;
                candidate = current;
                hasCandidate = true;
                break;
            }

            if (!hasCandidate)
                return;

            Widget* root = candidate.Root;
            Window* targetWindow = candidate.WindowPtr;
            mouseX = candidate.MouseX;
            mouseY = candidate.MouseY;

            WindowPanel* target = FindDockTarget(root, draggedPanel, mouseX, mouseY);
            DockDropMode mode = DockDropMode::None;
            bool isRootTarget = false;
            DirectX::XMFLOAT2 targetPos = { 0.0f, 0.0f };
            DirectX::XMFLOAT2 targetSize = { 0.0f, 0.0f };

            DirectX::XMFLOAT4 draggedRect = {
                draggedPanel->m_OffsetMin.x,
                draggedPanel->m_OffsetMin.y,
                draggedPanel->m_OffsetMax.x - draggedPanel->m_OffsetMin.x,
                draggedPanel->m_OffsetMax.y - draggedPanel->m_OffsetMin.y
            };

            if (!target && draggedPanel->m_IsFloating && draggedPanel->m_Parent != nullptr && draggedPanel->m_Parent == root)
            {
                target = FindDockTargetByOverlap(root, draggedPanel, draggedRect);
            }

            if (!target)
            {
                // 패널 위가 아니라 빈 작업 영역 위라면 루트 영역 도킹 후보로 전환합니다.
                isRootTarget = true;
                DirectX::XMFLOAT4 workspace = GetDockWorkspace(targetWindow);
                targetPos = { workspace.x, workspace.y };
                targetSize = { workspace.z, workspace.w };
            }
            else
            {
                targetPos = target->GetCalculatedPosition();
                targetSize = target->GetCalculatedSize();
            }

            if (targetSize.x <= 40.0f || targetSize.y <= 40.0f)
                return;

            DockPreviewState nextPreview = {};
            nextPreview.GuideVisible = true;
            nextPreview.Dragged = draggedPanel;
            nextPreview.Target = target;
            nextPreview.TargetWindow = targetWindow;
            nextPreview.TargetRoot = root;
            nextPreview.IsRootTarget = isRootTarget;
            BuildDockGuideRects(nextPreview, targetPos, targetSize);

            // 이전 구현은 target 가장자리와 마우스 거리로 방향을 추정했습니다.
            // 지금은 Visual Studio 방식처럼 가이드 박스 위에 있을 때만 방향을 확정합니다.
            mode = GetDockGuideHitMode(nextPreview, mouseX, mouseY);
            nextPreview.Mode = mode;

            if (mode == DockDropMode::None)
            {
                s_DockPreview = nextPreview;
                return;
            }

            float previewX = targetPos.x;
            float previewY = targetPos.y;
            float previewW = targetSize.x;
            float previewH = targetSize.y;

            if (mode == DockDropMode::Left)
                previewW *= 0.5f;
            else if (mode == DockDropMode::Right)
            {
                previewX += targetSize.x * 0.5f;
                previewW *= 0.5f;
            }
            else if (mode == DockDropMode::Top)
                previewH *= 0.5f;
            else if (mode == DockDropMode::Bottom)
            {
                previewY += targetSize.y * 0.5f;
                previewH *= 0.5f;
            }

            nextPreview.Active = true;
            nextPreview.Rect = { previewX, previewY, previewW, previewH };
            s_DockPreview = nextPreview;
        }

        bool DockManager::ApplyPreview(WindowPanel* draggedPanel)
        {
            if (!draggedPanel || !s_DockPreview.Active || s_DockPreview.Dragged != draggedPanel)
                return false;

            WindowPanel* target = s_DockPreview.Target;
            DockDropMode mode = s_DockPreview.Mode;
            if (mode == DockDropMode::None)
                return false;

            auto app = Application::Get();
            Widget* root = s_DockPreview.TargetRoot;
            Window* targetWindow = s_DockPreview.TargetWindow;
            Window* sourceWindow = draggedPanel->GetOwnerWindow();
            if (!root)
                return false;

            if (sourceWindow && sourceWindow->GetRootUI() == draggedPanel)
                draggedPanel->m_PreferredFloatingSize = { (float)sourceWindow->GetWidth(), (float)sourceWindow->GetHeight() };
            else if (!sourceWindow)
                draggedPanel->m_PreferredFloatingSize = draggedPanel->m_CalculatedSize;

            if (target && targetWindow && targetWindow->GetRootUI() == target)
                target->m_PreferredFloatingSize = { (float)targetWindow->GetWidth(), (float)targetWindow->GetHeight() };

            if (sourceWindow && sourceWindow != targetWindow)
                app->RequestCloseSecondaryWindow(sourceWindow);
            else
                app->RequestCloseSecondaryWindowByUI(draggedPanel);

            draggedPanel->SetOwnerWindow(targetWindow == &app->GetWindow() ? nullptr : targetWindow);

            if (draggedPanel->m_Parent) draggedPanel->m_Parent->RemoveChild(draggedPanel);
            draggedPanel->m_Parent = nullptr;
            DetachWidgetFromAllWindowRoots(draggedPanel);

            if (target && root == target)
            {
                // 타겟 패널 자체가 OS 창의 RootUI인 경우, 먼저 DockRoot로 감싼 뒤
                // 기존 타겟과 드래그 패널을 같은 그룹의 형제 패널로 만듭니다.
                Widget* dockRoot = CreateDockRoot();
                targetWindow->SetRootUI(dockRoot);

                target->SetOwnerWindow(targetWindow == &app->GetWindow() ? nullptr : targetWindow);
                if (target->m_Parent) target->m_Parent->RemoveChild(target);
                target->m_Parent = nullptr;
                dockRoot->AddChild(target);
                ApplyDockRootAnchorRect(target, 0.0f, 0.0f, 1.0f, 1.0f);
                root = dockRoot;
                s_DockPreview.TargetRoot = root;
            }

            if (target && root && root->GetName() != "DockRoot")
            {
                Widget* targetParent = target->GetParent();
                if (targetParent)
                {
                    // 메인 윈도우 내부의 일반 패널끼리 도킹할 때도 DockRoot를 만듭니다.
                    // 이렇게 해야 이후 분리/재도킹/비율 리사이즈를 멀티 윈도우 그룹과 같은 코드로 처리할 수 있습니다.
                    auto targetPos = target->GetCalculatedPosition();
                    auto targetSize = target->GetCalculatedSize();
                    Widget* dockRoot = CreateDockRoot(false);

                    targetParent->AddChild(dockRoot);
                    ApplyWidgetAbsoluteRectInParent(dockRoot, targetParent, targetPos.x, targetPos.y, targetSize.x, targetSize.y);

                    if (target->m_Parent)
                        target->m_Parent->RemoveChild(target);
                    target->m_Parent = nullptr;

                    dockRoot->AddChild(target);
                    ApplyDockRootAnchorRect(target, 0.0f, 0.0f, 1.0f, 1.0f);

                    root = dockRoot;
                    s_DockPreview.TargetRoot = root;
                }
            }

            root->AddChild(draggedPanel);
            DetachWidgetFromOtherWindowRoots(draggedPanel, targetWindow);
            SetOwnerWindowRecursive(root, targetWindow == &app->GetWindow() ? nullptr : targetWindow);
            draggedPanel->BringToFront();
            NormalizeAllWindowPanelOwnership();

            RemoveRelations(draggedPanel);

            if (s_DockPreview.IsRootTarget)
            {
                ApplyAbsoluteRect(draggedPanel, s_DockPreview.Rect.x, s_DockPreview.Rect.y, s_DockPreview.Rect.z, s_DockPreview.Rect.w);

                draggedPanel->m_IsFloating = false;
                draggedPanel->m_IsDragging = false;
                draggedPanel->m_DockIgnoreWindow = nullptr;
                draggedPanel->m_ResizeMode = ResizeMode::None;
                s_DockPreview = {};

                std::cout << "[Docking] " << draggedPanel->m_Name << " 패널이 메인 윈도우 가장자리로 도킹되었습니다." << std::endl;
                return true;
            }

            if (!target)
                return false;

            auto targetPos = target->GetCalculatedPosition();
            auto targetSize = target->GetCalculatedSize();
            float halfW = targetSize.x * 0.5f;
            float halfH = targetSize.y * 0.5f;

            if (root && root->GetName() == "DockRoot")
            {
                // DockRoot 내부에서는 타겟 패널의 앵커 사각형을 반으로 나눠 새 패널을 배치합니다.
                // 픽셀 기반 분할보다 리사이즈와 재도킹에 강합니다.
                auto anchorMin = target->GetAnchorMin();
                auto anchorMax = target->GetAnchorMax();
                if (anchorMax.x <= anchorMin.x || anchorMax.y <= anchorMin.y)
                {
                    anchorMin = { 0.0f, 0.0f };
                    anchorMax = { 1.0f, 1.0f };
                }

                float midX = (anchorMin.x + anchorMax.x) * 0.5f;
                float midY = (anchorMin.y + anchorMax.y) * 0.5f;

                if (mode == DockDropMode::Left)
                {
                    ApplyDockRootAnchorRect(draggedPanel, anchorMin.x, anchorMin.y, midX, anchorMax.y);
                    ApplyDockRootAnchorRect(target, midX, anchorMin.y, anchorMax.x, anchorMax.y);
                    RegisterRelation(draggedPanel, target, true);
                }
                else if (mode == DockDropMode::Right)
                {
                    ApplyDockRootAnchorRect(target, anchorMin.x, anchorMin.y, midX, anchorMax.y);
                    ApplyDockRootAnchorRect(draggedPanel, midX, anchorMin.y, anchorMax.x, anchorMax.y);
                    RegisterRelation(target, draggedPanel, true);
                }
                else if (mode == DockDropMode::Top)
                {
                    ApplyDockRootAnchorRect(draggedPanel, anchorMin.x, anchorMin.y, anchorMax.x, midY);
                    ApplyDockRootAnchorRect(target, anchorMin.x, midY, anchorMax.x, anchorMax.y);
                    RegisterRelation(draggedPanel, target, false);
                }
                else if (mode == DockDropMode::Bottom)
                {
                    ApplyDockRootAnchorRect(target, anchorMin.x, anchorMin.y, anchorMax.x, midY);
                    ApplyDockRootAnchorRect(draggedPanel, anchorMin.x, midY, anchorMax.x, anchorMax.y);
                    RegisterRelation(target, draggedPanel, false);
                }

                draggedPanel->m_IsFloating = false;
                draggedPanel->m_IsDragging = false;
                draggedPanel->m_DockIgnoreWindow = nullptr;
                draggedPanel->m_ResizeMode = ResizeMode::None;
                s_DockPreview = {};

                std::cout << "[Docking] " << draggedPanel->m_Name << " 패널이 " << target->GetName() << " 기준으로 도킹되었습니다." << std::endl;
                return true;
            }

            if (mode == DockDropMode::Left)
            {
                ApplyAbsoluteRect(draggedPanel, targetPos.x, targetPos.y, halfW, targetSize.y);
                ApplyAbsoluteRect(target, targetPos.x + halfW, targetPos.y, halfW, targetSize.y);
                RegisterRelation(draggedPanel, target, true);
            }
            else if (mode == DockDropMode::Right)
            {
                ApplyAbsoluteRect(target, targetPos.x, targetPos.y, halfW, targetSize.y);
                ApplyAbsoluteRect(draggedPanel, targetPos.x + halfW, targetPos.y, halfW, targetSize.y);
                RegisterRelation(target, draggedPanel, true);
            }
            else if (mode == DockDropMode::Top)
            {
                ApplyAbsoluteRect(draggedPanel, targetPos.x, targetPos.y, targetSize.x, halfH);
                ApplyAbsoluteRect(target, targetPos.x, targetPos.y + halfH, targetSize.x, halfH);
                RegisterRelation(draggedPanel, target, false);
            }
            else if (mode == DockDropMode::Bottom)
            {
                ApplyAbsoluteRect(target, targetPos.x, targetPos.y, targetSize.x, halfH);
                ApplyAbsoluteRect(draggedPanel, targetPos.x, targetPos.y + halfH, targetSize.x, halfH);
                RegisterRelation(target, draggedPanel, false);
            }

            draggedPanel->m_IsFloating = false;
            draggedPanel->m_IsDragging = false;
            draggedPanel->m_DockIgnoreWindow = nullptr;
            draggedPanel->m_ResizeMode = ResizeMode::None;
            s_DockPreview = {};

            std::cout << "[Docking] " << draggedPanel->m_Name << " 패널이 " << target->GetName() << " 기준으로 도킹되었습니다." << std::endl;
            return true;
        }

        WindowPanel* DockManager::FindDockTarget(Widget* root, WindowPanel* draggedPanel, float mouseX, float mouseY)
        {
            if (!root)
                return nullptr;

            // DockRoot 안에 다시 DockRoot가 들어갈 수 있으므로 자식 위젯을 재귀적으로 탐색합니다.
            // 뒤에서부터 확인해 화면상 가장 위에 그려진 패널을 우선 타겟으로 선택합니다.
            WindowPanel* rootPanel = dynamic_cast<WindowPanel*>(root);
            if (rootPanel && rootPanel != draggedPanel && rootPanel->IsVisible() && rootPanel->IsPointInside(mouseX, mouseY))
                return rootPanel;

            const auto& children = root->GetChildren();
            for (auto it = children.rbegin(); it != children.rend(); ++it)
            {
                Widget* child = *it;
                if (!child || !child->IsVisible() || !child->IsPointInside(mouseX, mouseY))
                    continue;

                if (WindowPanel* panel = dynamic_cast<WindowPanel*>(child))
                {
                    if (panel != draggedPanel)
                        return panel;
                    continue;
                }

                if (WindowPanel* nestedTarget = FindDockTarget(child, draggedPanel, mouseX, mouseY))
                    return nestedTarget;
            }

            return nullptr;
        }

        WindowPanel* DockManager::FindDockTargetByOverlap(Widget* root, WindowPanel* draggedPanel, const DirectX::XMFLOAT4& draggedRect)
        {
            if (!root)
                return nullptr;

            WindowPanel* bestTarget = nullptr;
            float bestArea = 0.0f;
            WindowPanel* rootPanel = dynamic_cast<WindowPanel*>(root);
            if (rootPanel && rootPanel != draggedPanel && rootPanel->IsVisible())
            {
                auto pos = rootPanel->GetCalculatedPosition();
                auto size = rootPanel->GetCalculatedSize();
                float left = (std::max)(draggedRect.x, pos.x);
                float top = (std::max)(draggedRect.y, pos.y);
                float right = (std::min)(draggedRect.x + draggedRect.z, pos.x + size.x);
                float bottom = (std::min)(draggedRect.y + draggedRect.w, pos.y + size.y);
                float overlapW = right - left;
                float overlapH = bottom - top;
                if (overlapW > 0.0f && overlapH > 0.0f)
                {
                    bestArea = overlapW * overlapH;
                    bestTarget = rootPanel;
                }
            }

            const auto& children = root->GetChildren();
            for (auto it = children.rbegin(); it != children.rend(); ++it)
            {
                WindowPanel* panel = dynamic_cast<WindowPanel*>(*it);
                if (!panel || panel == draggedPanel || !panel->IsVisible())
                    continue;

                auto pos = panel->GetCalculatedPosition();
                auto size = panel->GetCalculatedSize();

                float left = (std::max)(draggedRect.x, pos.x);
                float top = (std::max)(draggedRect.y, pos.y);
                float right = (std::min)(draggedRect.x + draggedRect.z, pos.x + size.x);
                float bottom = (std::min)(draggedRect.y + draggedRect.w, pos.y + size.y);
                float overlapW = right - left;
                float overlapH = bottom - top;

                if (overlapW <= 0.0f || overlapH <= 0.0f)
                    continue;

                float area = overlapW * overlapH;
                if (area > bestArea)
                {
                    bestArea = area;
                    bestTarget = panel;
                }
            }

            return bestTarget;
        }

        DockDropMode DockManager::GetDockDropMode(WindowPanel* target, float mouseX, float mouseY)
        {
            if (!target)
                return DockDropMode::None;

            auto targetPos = target->GetCalculatedPosition();
            auto targetSize = target->GetCalculatedSize();
            if (targetSize.x <= 0.0f || targetSize.y <= 0.0f)
                return DockDropMode::None;

            float localX = mouseX - targetPos.x;
            float localY = mouseY - targetPos.y;
            float leftDistance = localX;
            float rightDistance = targetSize.x - localX;
            float topDistance = localY;
            float bottomDistance = targetSize.y - localY;
            float minDistance = leftDistance;
            DockDropMode mode = DockDropMode::Left;

            if (rightDistance < minDistance) { minDistance = rightDistance; mode = DockDropMode::Right; }
            if (topDistance < minDistance) { minDistance = topDistance; mode = DockDropMode::Top; }
            if (bottomDistance < minDistance) { minDistance = bottomDistance; mode = DockDropMode::Bottom; }

            float edgeZoneX = targetSize.x * 0.33f;
            float edgeZoneY = targetSize.y * 0.33f;
            if ((mode == DockDropMode::Left || mode == DockDropMode::Right) && minDistance > edgeZoneX)
                return DockDropMode::None;
            if ((mode == DockDropMode::Top || mode == DockDropMode::Bottom) && minDistance > edgeZoneY)
                return DockDropMode::None;

            return mode;
        }

        DockDropMode DockManager::GetDockDropModeFromOverlap(WindowPanel* target, const DirectX::XMFLOAT4& draggedRect)
        {
            if (!target)
                return DockDropMode::None;

            auto targetPos = target->GetCalculatedPosition();
            auto targetSize = target->GetCalculatedSize();
            if (targetSize.x <= 0.0f || targetSize.y <= 0.0f)
                return DockDropMode::None;

            float draggedCenterX = draggedRect.x + draggedRect.z * 0.5f;
            float draggedCenterY = draggedRect.y + draggedRect.w * 0.5f;
            float targetCenterX = targetPos.x + targetSize.x * 0.5f;
            float targetCenterY = targetPos.y + targetSize.y * 0.5f;

            float normalizedX = (draggedCenterX - targetCenterX) / targetSize.x;
            float normalizedY = (draggedCenterY - targetCenterY) / targetSize.y;

            if (std::fabs(normalizedX) >= std::fabs(normalizedY))
                return normalizedX < 0.0f ? DockDropMode::Left : DockDropMode::Right;

            return normalizedY < 0.0f ? DockDropMode::Top : DockDropMode::Bottom;
        }

        DockDropMode DockManager::GetRootDockDropMode(Window* targetWindow, float mouseX, float mouseY)
        {
            DirectX::XMFLOAT4 workspace = GetDockWorkspace(targetWindow);
            float width = workspace.z;
            float height = workspace.w;
            if (width <= 0.0f || height <= 0.0f || !IsMouseInsideWindow(targetWindow, mouseX, mouseY))
                return DockDropMode::None;

            if (mouseX < workspace.x || mouseX > workspace.x + workspace.z ||
                mouseY < workspace.y || mouseY > workspace.y + workspace.w)
                return DockDropMode::None;

            float localX = mouseX - workspace.x;
            float localY = mouseY - workspace.y;
            float leftDistance = localX;
            float rightDistance = width - localX;
            float topDistance = localY;
            float bottomDistance = height - localY;
            float minDistance = leftDistance;
            DockDropMode mode = DockDropMode::Left;

            if (rightDistance < minDistance) { minDistance = rightDistance; mode = DockDropMode::Right; }
            if (topDistance < minDistance) { minDistance = topDistance; mode = DockDropMode::Top; }
            if (bottomDistance < minDistance) { minDistance = bottomDistance; mode = DockDropMode::Bottom; }

            float edgeZoneX = width * 0.20f;
            float edgeZoneY = height * 0.20f;
            if ((mode == DockDropMode::Left || mode == DockDropMode::Right) && minDistance > edgeZoneX)
                return DockDropMode::None;
            if ((mode == DockDropMode::Top || mode == DockDropMode::Bottom) && minDistance > edgeZoneY)
                return DockDropMode::None;

            return mode;
        }

        void DockManager::ApplyAbsoluteRect(WindowPanel* panel, float x, float y, float width, float height)
        {
            if (!panel)
                return;

            // 레거시 절대 좌표 경로입니다. 부모가 DockRoot라면 즉시 앵커 비율로 변환해
            // 그룹 리사이즈 시 픽셀 오프셋이 깨지지 않도록 합니다.
            width = (std::max)(width, 150.0f);
            height = (std::max)(height, 100.0f);

            Widget* parent = panel->GetParent();
            if (parent && parent->GetName() == "DockRoot")
            {
                Window* ownerWindow = FindWindowOwningRoot(parent);

                auto parentPos = parent->GetCalculatedPosition();
                auto parentSize = parent->GetCalculatedSize();
                if ((parentSize.x <= 1.0f || parentSize.y <= 1.0f) && ownerWindow)
                {
                    parentPos = { 0.0f, 0.0f };
                    parentSize = { (float)ownerWindow->GetWidth(), (float)ownerWindow->GetHeight() };
                }

                float contentX = parentPos.x;
                float contentY = parentPos.y + 24.0f;
                float contentW = parentSize.x;
                float contentH = parentSize.y - 24.0f;

                if (contentW > 1.0f && contentH > 1.0f)
                {
                    float minX = (std::clamp)((x - contentX) / contentW, 0.0f, 1.0f);
                    float minY = (std::clamp)((y - contentY) / contentH, 0.0f, 1.0f);
                    float maxX = (std::clamp)((x + width - contentX) / contentW, 0.0f, 1.0f);
                    float maxY = (std::clamp)((y + height - contentY) / contentH, 0.0f, 1.0f);

                    panel->SetAnchorMin(minX, minY);
                    panel->SetAnchorMax(maxX, maxY);
                    panel->SetOffsetMin(0.0f, 0.0f);
                    panel->SetOffsetMax(0.0f, 0.0f);
                    return;
                }
            }

            panel->SetAnchorMin(0.0f, 0.0f);
            panel->SetAnchorMax(0.0f, 0.0f);
            panel->SetOffsetMin(x, y);
            panel->SetOffsetMax(x + width, y + height);
        }

        void DockManager::RegisterRelation(WindowPanel* first, WindowPanel* second, bool horizontal)
        {
            if (!first || !second || first == second)
                return;

            RemoveRelations(first);
            s_DockRelations.erase(
                std::remove_if(
                    s_DockRelations.begin(),
                    s_DockRelations.end(),
                    [first, second](const DockRelation& relation)
                    {
                        return (relation.First == first && relation.Second == second) ||
                            (relation.First == second && relation.Second == first);
                    }),
                s_DockRelations.end()
            );
            s_DockRelations.push_back({ first, second, horizontal });
        }

        void DockManager::RemoveRelations(WindowPanel* panel)
        {
            if (!panel)
                return;

            s_DockRelations.erase(
                std::remove_if(
                    s_DockRelations.begin(),
                    s_DockRelations.end(),
                    [panel](const DockRelation& relation)
                    {
                        return relation.First == panel || relation.Second == panel;
                    }),
                s_DockRelations.end()
            );
        }

        bool DockManager::ResizeRelation(WindowPanel* resizedPanel, int resizeModeValue, float deltaX, float deltaY)
        {
            if (!resizedPanel)
                return false;

            // 서로 맞닿은 두 패널 사이의 공유 경계를 이동시킵니다.
            // 한쪽 패널만 커지는 것이 아니라 반대편 패널도 같이 줄어들어 VS식 분할 리사이즈처럼 보입니다.
            constexpr float minWidth = 150.0f;
            constexpr float minHeight = 100.0f;
            ResizeMode resizeMode = (ResizeMode)resizeModeValue;

            for (DockRelation& relation : s_DockRelations)
            {
                if (relation.First != resizedPanel && relation.Second != resizedPanel)
                    continue;

                WindowPanel* first = relation.First;
                WindowPanel* second = relation.Second;
                if (!first || !second)
                    continue;

                auto firstPos = first->GetCalculatedPosition();
                auto firstSize = first->GetCalculatedSize();
                auto secondPos = second->GetCalculatedPosition();
                auto secondSize = second->GetCalculatedSize();

                if (relation.Horizontal)
                {
                    bool isSharedEdgeResize =
                        (resizedPanel == first && resizeMode == ResizeMode::Right) ||
                        (resizedPanel == second && resizeMode == ResizeMode::Left);

                    if (!isSharedEdgeResize)
                        continue;

                    float left = firstPos.x;
                    float right = secondPos.x + secondSize.x;
                    float top = (std::min)(firstPos.y, secondPos.y);
                    float bottom = (std::max)(firstPos.y + firstSize.y, secondPos.y + secondSize.y);
                    float boundary = firstPos.x + firstSize.x + deltaX;

                    boundary = (std::max)(boundary, left + minWidth);
                    boundary = (std::min)(boundary, right - minWidth);

                    ApplyAbsoluteRect(first, left, top, boundary - left, bottom - top);
                    ApplyAbsoluteRect(second, boundary, top, right - boundary, bottom - top);
                    return true;
                }

                bool isSharedEdgeResize =
                    (resizedPanel == first && resizeMode == ResizeMode::Bottom) ||
                    (resizedPanel == second && resizeMode == ResizeMode::Top);

                if (!isSharedEdgeResize)
                    continue;

                float left = (std::min)(firstPos.x, secondPos.x);
                float right = (std::max)(firstPos.x + firstSize.x, secondPos.x + secondSize.x);
                float top = firstPos.y;
                float bottom = secondPos.y + secondSize.y;
                float boundary = firstPos.y + firstSize.y + deltaY;

                boundary = (std::max)(boundary, top + minHeight);
                boundary = (std::min)(boundary, bottom - minHeight);

                ApplyAbsoluteRect(first, left, top, right - left, boundary - top);
                ApplyAbsoluteRect(second, left, boundary, right - left, bottom - boundary);
                return true;
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
