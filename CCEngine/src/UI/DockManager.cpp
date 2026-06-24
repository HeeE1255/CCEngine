#include "DockManager.h"
#include "WindowPanel.h"
#include "Application.h"
#include "Renderer/UIRenderer.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

#define NOMINMAX
#include <windows.h>

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

            float GetHorizontalOverlap(const DirectX::XMFLOAT2& aPos, const DirectX::XMFLOAT2& aSize,
                const DirectX::XMFLOAT2& bPos, const DirectX::XMFLOAT2& bSize)
            {
                float left = (std::max)(aPos.x, bPos.x);
                float right = (std::min)(aPos.x + aSize.x, bPos.x + bSize.x);
                return (std::max)(0.0f, right - left);
            }

            float GetVerticalOverlap(const DirectX::XMFLOAT2& aPos, const DirectX::XMFLOAT2& aSize,
                const DirectX::XMFLOAT2& bPos, const DirectX::XMFLOAT2& bSize)
            {
                float top = (std::max)(aPos.y, bPos.y);
                float bottom = (std::min)(aPos.y + aSize.y, bPos.y + bSize.y);
                return (std::max)(0.0f, bottom - top);
            }

            bool IsDockRootChild(WindowPanel* panel)
            {
                return panel && panel->GetParent() && panel->GetParent()->GetName() == "DockRoot";
            }

            WindowPanel* FindSharedEdgeSibling(WindowPanel* panel, ResizeMode resizeMode)
            {
                if (!IsDockRootChild(panel))
                    return nullptr;

                Widget* parent = panel->GetParent();
                const auto panelPos = panel->GetCalculatedPosition();
                const auto panelSize = panel->GetCalculatedSize();
                const float panelLeft = panelPos.x;
                const float panelRight = panelPos.x + panelSize.x;
                const float panelTop = panelPos.y;
                const float panelBottom = panelPos.y + panelSize.y;

                constexpr float edgeTolerance = 3.0f;
                WindowPanel* bestSibling = nullptr;
                float bestOverlap = 0.0f;

                for (Widget* child : parent->GetChildren())
                {
                    WindowPanel* sibling = dynamic_cast<WindowPanel*>(child);
                    if (!sibling || sibling == panel)
                        continue;

                    const auto siblingPos = sibling->GetCalculatedPosition();
                    const auto siblingSize = sibling->GetCalculatedSize();
                    float overlap = 0.0f;
                    bool touches = false;

                    if (resizeMode == ResizeMode::Right)
                    {
                        touches = std::fabs((siblingPos.x) - panelRight) <= edgeTolerance;
                        overlap = GetVerticalOverlap(panelPos, panelSize, siblingPos, siblingSize);
                    }
                    else if (resizeMode == ResizeMode::Left)
                    {
                        touches = std::fabs((siblingPos.x + siblingSize.x) - panelLeft) <= edgeTolerance;
                        overlap = GetVerticalOverlap(panelPos, panelSize, siblingPos, siblingSize);
                    }
                    else if (resizeMode == ResizeMode::Bottom)
                    {
                        touches = std::fabs((siblingPos.y) - panelBottom) <= edgeTolerance;
                        overlap = GetHorizontalOverlap(panelPos, panelSize, siblingPos, siblingSize);
                    }
                    else if (resizeMode == ResizeMode::Top)
                    {
                        touches = std::fabs((siblingPos.y + siblingSize.y) - panelTop) <= edgeTolerance;
                        overlap = GetHorizontalOverlap(panelPos, panelSize, siblingPos, siblingSize);
                    }

                    if (touches && overlap > bestOverlap)
                    {
                        bestOverlap = overlap;
                        bestSibling = sibling;
                    }
                }

                return bestOverlap > 8.0f ? bestSibling : nullptr;
            }

            bool ResizeSharedDockRootEdge(WindowPanel* panel, ResizeMode resizeMode, float deltaX, float deltaY)
            {
                WindowPanel* sibling = FindSharedEdgeSibling(panel, resizeMode);
                if (!sibling)
                    return false;

                constexpr float minWidth = 150.0f;
                constexpr float minHeight = 100.0f;

                auto panelMin = panel->GetOffsetMin();
                auto panelMax = panel->GetOffsetMax();
                auto siblingMin = sibling->GetOffsetMin();
                auto siblingMax = sibling->GetOffsetMax();
                const auto panelSize = panel->GetCalculatedSize();
                const auto siblingSize = sibling->GetCalculatedSize();

                if (resizeMode == ResizeMode::Right || resizeMode == ResizeMode::Left)
                {
                    float delta = deltaX;
                    if (resizeMode == ResizeMode::Right)
                    {
                        delta = (std::max)(delta, minWidth - panelSize.x);
                        delta = (std::min)(delta, siblingSize.x - minWidth);
                        panelMax.x += delta;
                        siblingMin.x += delta;
                    }
                    else
                    {
                        delta = (std::max)(delta, minWidth - siblingSize.x);
                        delta = (std::min)(delta, panelSize.x - minWidth);
                        panelMin.x += delta;
                        siblingMax.x += delta;
                    }
                }
                else if (resizeMode == ResizeMode::Bottom || resizeMode == ResizeMode::Top)
                {
                    float delta = deltaY;
                    if (resizeMode == ResizeMode::Bottom)
                    {
                        delta = (std::max)(delta, minHeight - panelSize.y);
                        delta = (std::min)(delta, siblingSize.y - minHeight);
                        panelMax.y += delta;
                        siblingMin.y += delta;
                    }
                    else
                    {
                        delta = (std::max)(delta, minHeight - siblingSize.y);
                        delta = (std::min)(delta, panelSize.y - minHeight);
                        panelMin.y += delta;
                        siblingMax.y += delta;
                    }
                }
                else
                {
                    return false;
                }

                // DockRoot 안에서는 바깥 사각형을 고정하고 공유 경계만 이동한다.
                // 양쪽 패널의 오프셋을 같이 바꾸면 그룹 전체 크기는 그대로 유지된다.
                panel->SetOffsetMin(panelMin.x, panelMin.y);
                panel->SetOffsetMax(panelMax.x, panelMax.y);
                sibling->SetOffsetMin(siblingMin.x, siblingMin.y);
                sibling->SetOffsetMax(siblingMax.x, siblingMax.y);
                return true;
            }

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

                // 패널/그룹을 다른 창으로 옮기기 전에 현재 창 트리에서 정확히 제거합니다.
                // 이 단계가 빠지면 같은 WindowPanel이 두 RootUI 아래에 동시에 남아 복제처럼 보입니다.
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

                // 멀티 윈도우와 메인 윈도우를 왕복한 패널은 과거 소유 창에 남아 있을 수 있습니다.
                // 재도킹 직전에는 모든 창 루트에서 제거해 단일 소유 상태를 보장합니다.
                DetachWidgetFromWindowRoot(&app->GetWindow(), widget);
                for (Window* secondaryWindow : app->GetSecondaryWindows())
                    DetachWidgetFromWindowRoot(secondaryWindow, widget);
            }

            void DetachWidgetFromOtherWindowRoots(Widget* widget, Window* keepWindow)
            {
                auto app = Application::Get();
                if (!app || !widget)
                    return;

                // 새 소유 창으로 옮긴 직후에는 keepWindow만 남기고 나머지 창의 참조를 제거합니다.
                // 도킹 그룹에서 패널을 떼어낼 때 기존 그룹에 패널이 남는 문제를 막는 방어 코드입니다.
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

            void CollapseDockRootWidget(Widget* dockRoot)
            {
                if (!dockRoot || dockRoot->GetName() != "DockRoot")
                    return;

                const auto& children = dockRoot->GetChildren();
                if (children.size() != 1)
                    return;

                Widget* onlyChild = children.front();
                Widget* parent = dockRoot->GetParent();
                Window* ownerWindow = FindWindowOwningRoot(dockRoot);

                dockRoot->RemoveChild(onlyChild);

                if (parent)
                {
                    DirectX::XMFLOAT2 rootPos = dockRoot->GetCalculatedPosition();
                    DirectX::XMFLOAT2 rootSize = dockRoot->GetCalculatedSize();
                    DirectX::XMFLOAT2 parentPos = parent->GetCalculatedPosition();

                    parent->RemoveChild(dockRoot);
                    parent->AddChild(onlyChild);

                    onlyChild->SetAnchorMin(0.0f, 0.0f);
                    onlyChild->SetAnchorMax(0.0f, 0.0f);
                    onlyChild->SetOffsetMin(rootPos.x - parentPos.x, rootPos.y - parentPos.y);
                    onlyChild->SetOffsetMax(rootPos.x - parentPos.x + rootSize.x, rootPos.y - parentPos.y + rootSize.y);
                }
                else if (ownerWindow && ownerWindow->GetRootUI() == dockRoot)
                {
                    ownerWindow->SetRootUI(onlyChild);
                    FillWindowRoot(onlyChild);
                }

                Window* expectedOwner = nullptr;
                auto app = Application::Get();
                if (app && ownerWindow && ownerWindow != &app->GetWindow())
                    expectedOwner = ownerWindow;
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
                    // DockRoot는 일반 패널이 아니라 여러 패널을 담는 그룹 컨테이너입니다.
                    // 그래서 자식 패널보다 먼저 그룹 바 이동/그룹 리사이즈 입력을 가로챕니다.
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
                    // 그룹 이동/리사이즈 중에는 마우스가 DockRoot 밖으로 나가도 이벤트를 계속 받아야 합니다.
                    return m_IsMoving || m_ResizeMode != ResizeMode::None;
                }

                bool IsPointInside(float mouseX, float mouseY) const override
                {
                    // 메인 내부 DockRoot는 OS 창 테두리가 없어서 사용자가 왼쪽 가장자리를
                    // 살짝 바깥에서 잡는 경우가 많습니다. 이벤트 필터 단계부터 여유 폭을 둡니다.
                    constexpr float resizePadding = 10.0f;
                    return mouseX >= m_CalculatedPos.x - resizePadding &&
                        mouseX <= m_CalculatedPos.x + m_CalculatedSize.x + resizePadding &&
                        mouseY >= m_CalculatedPos.y - resizePadding &&
                        mouseY <= m_CalculatedPos.y + m_CalculatedSize.y + resizePadding;
                }

                void OnUpdate(float deltaTime) override
                {
                    Widget::OnUpdate(deltaTime);

                    if (!m_IsMoving && m_ResizeMode == ResizeMode::None)
                        return;

                    Widget::BeginMouseInteraction(this);

                    // OS 이벤트가 유실되어 MouseButtonReleased가 오지 않아도,
                    // 실제 버튼 상태를 보고 이동/리사이즈 상태를 종료합니다.
                    bool isLMBDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                    if (!isLMBDown)
                    {
                        FinishMove(FindWindowOwningRoot(this));
                        m_ResizeMode = ResizeMode::None;
                        Widget::EndMouseInteraction(this);
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
                    DrawChildSeparators();
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

                    // 그룹 바의 중앙은 이동으로 쓰되, 좌/우/하단 테두리와 코너는 리사이즈를 우선합니다.
                    // 특히 왼쪽 모서리는 그룹 바와 겹치기 쉬워 이동 판정이 먼저 먹으면 리사이즈가 막힙니다.
                    if (!IsFloatingDockRoot())
                    {
                        ResizeMode resizeMode = HitTestResizeMode(e.GetX(), e.GetY());
                        bool isSideOrBottomResize =
                            resizeMode == ResizeMode::Left ||
                            resizeMode == ResizeMode::Right ||
                            resizeMode == ResizeMode::Bottom ||
                            resizeMode == ResizeMode::TopLeft ||
                            resizeMode == ResizeMode::TopRight ||
                            resizeMode == ResizeMode::BottomLeft ||
                            resizeMode == ResizeMode::BottomRight;
                        if (resizeMode != ResizeMode::None && (!onGroupBar || isSideOrBottomResize))
                        {
                            m_ResizeMode = resizeMode;
                            m_ResizeLastMouseX = e.GetX();
                            m_ResizeLastMouseY = e.GetY();
                            Widget::BeginMouseInteraction(this);
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
                        Widget::BeginMouseInteraction(this);
                    }
                    else
                    {
                        BringToFront();
                        m_DragOffsetX = e.GetX() - m_CalculatedPos.x;
                        m_DragOffsetY = e.GetY() - m_CalculatedPos.y;
                        m_IsMoving = true;
                        Widget::BeginMouseInteraction(this);
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
                    Widget::EndMouseInteraction(this);
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

                void DrawChildSeparators() const
                {
                    constexpr float edgeTolerance = 3.0f;
                    DirectX::XMFLOAT4 separatorColor = { 0.27f, 0.27f, 0.30f, 1.0f };

                    for (size_t i = 0; i < m_Children.size(); ++i)
                    {
                        WindowPanel* first = dynamic_cast<WindowPanel*>(m_Children[i]);
                        if (!first)
                            continue;

                        auto firstPos = first->GetCalculatedPosition();
                        auto firstSize = first->GetCalculatedSize();
                        float firstRight = firstPos.x + firstSize.x;
                        float firstBottom = firstPos.y + firstSize.y;

                        for (size_t j = i + 1; j < m_Children.size(); ++j)
                        {
                            WindowPanel* second = dynamic_cast<WindowPanel*>(m_Children[j]);
                            if (!second)
                                continue;

                            auto secondPos = second->GetCalculatedPosition();
                            auto secondSize = second->GetCalculatedSize();
                            float secondRight = secondPos.x + secondSize.x;
                            float secondBottom = secondPos.y + secondSize.y;

                            float verticalOverlap = GetVerticalOverlap(firstPos, firstSize, secondPos, secondSize);
                            if (verticalOverlap > 8.0f)
                            {
                                if (std::fabs(firstRight - secondPos.x) <= edgeTolerance)
                                {
                                    float y = (std::max)(firstPos.y, secondPos.y);
                                    UIRenderer::DrawRectFilled(firstRight - 1.0f, y, 2.0f, verticalOverlap, separatorColor);
                                }
                                else if (std::fabs(secondRight - firstPos.x) <= edgeTolerance)
                                {
                                    float y = (std::max)(firstPos.y, secondPos.y);
                                    UIRenderer::DrawRectFilled(firstPos.x - 1.0f, y, 2.0f, verticalOverlap, separatorColor);
                                }
                            }

                            float horizontalOverlap = GetHorizontalOverlap(firstPos, firstSize, secondPos, secondSize);
                            if (horizontalOverlap > 8.0f)
                            {
                                if (std::fabs(firstBottom - secondPos.y) <= edgeTolerance)
                                {
                                    float x = (std::max)(firstPos.x, secondPos.x);
                                    UIRenderer::DrawRectFilled(x, firstBottom - 1.0f, horizontalOverlap, 2.0f, separatorColor);
                                }
                                else if (std::fabs(secondBottom - firstPos.y) <= edgeTolerance)
                                {
                                    float x = (std::max)(firstPos.x, secondPos.x);
                                    UIRenderer::DrawRectFilled(x, firstPos.y - 1.0f, horizontalOverlap, 2.0f, separatorColor);
                                }
                            }
                        }
                    }
                }

                ResizeMode HitTestResizeMode(float x, float y) const
                {
                    if (IsFloatingDockRoot())
                        return ResizeMode::None;

                    constexpr float edge = 10.0f;
                    bool inside = x >= m_CalculatedPos.x - edge && x <= m_CalculatedPos.x + m_CalculatedSize.x + edge &&
                        y >= m_CalculatedPos.y - edge && y <= m_CalculatedPos.y + m_CalculatedSize.y + edge;
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

                    // 메인 윈도우 안에 붙은 DockRoot는 OS 창이 아니므로 SetPosition을 쓸 수 없습니다.
                    // 대신 부모 기준 절대 오프셋을 갱신해 그룹 전체를 하나의 가상 창처럼 이동시킵니다.
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

                    // 내부 DockRoot 리사이즈는 OS 창 크기 변경이 아니라 DockRoot 사각형 변경입니다.
                    // 자식 패널은 앵커 비율로 배치되어 그룹 크기 변화에 맞춰 같이 늘고 줄어듭니다.
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
                    Widget::EndMouseInteraction(this);
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


        void DockManager::DetachFromAllWindowRoots(Widget* widget)
        {
            DetachWidgetFromAllWindowRoots(widget);
        }

        void DockManager::DetachFromOtherWindowRoots(Widget* widget, Window* keepWindow)
        {
            DetachWidgetFromOtherWindowRoots(widget, keepWindow);
        }

        void DockManager::FillWindowRoot(Widget* widget)
        {
            UI::FillWindowRoot(widget);
        }

        void DockManager::SetOwnerWindowRecursive(Widget* widget, Window* ownerWindow)
        {
            UI::SetOwnerWindowRecursive(widget, ownerWindow);
        }

        void DockManager::CollapseDockRoot(Widget* dockRoot)
        {
            CollapseDockRootWidget(dockRoot);
        }

        void DockManager::NormalizeWindowPanelOwnership()
        {
            NormalizeAllWindowPanelOwnership();
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
                Widget::EndMouseInteraction(draggedPanel);
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
                Widget::EndMouseInteraction(draggedPanel);
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
            Widget::EndMouseInteraction(draggedPanel);
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

            if (IsDockRootChild(resizedPanel) && ResizeSharedDockRootEdge(resizedPanel, resizeMode, deltaX, deltaY))
                return true;

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


    }
}
