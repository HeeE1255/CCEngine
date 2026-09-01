#include "UI/AnimatorGraphPanel.h"

#include "Application.h"
#include "Animation/Animator.h"
#include "Core/AssetDatabase.h"
#include "Events/KeyEvent.h"
#include "Events/MouseEvent.h"
#include "Renderer/UIRenderer.h"

#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

namespace CCEngine::UI
{
    namespace
    {
        constexpr DirectX::XMFLOAT4 CanvasColor = { 0.045f, 0.047f, 0.052f, 1.0f };
        constexpr DirectX::XMFLOAT4 SidebarColor = { 0.090f, 0.094f, 0.102f, 1.0f };
        constexpr DirectX::XMFLOAT4 PanelStroke = { 0.23f, 0.24f, 0.27f, 1.0f };
        constexpr DirectX::XMFLOAT4 GridFine = { 0.115f, 0.120f, 0.132f, 0.42f };
        constexpr DirectX::XMFLOAT4 GridMajor = { 0.160f, 0.165f, 0.180f, 0.55f };
        constexpr DirectX::XMFLOAT4 TextStrong = { 0.88f, 0.90f, 0.94f, 1.0f };
        constexpr DirectX::XMFLOAT4 TextMuted = { 0.58f, 0.60f, 0.65f, 1.0f };
        constexpr DirectX::XMFLOAT4 AccentBlue = { 0.32f, 0.54f, 0.82f, 1.0f };
        constexpr DirectX::XMFLOAT4 AccentGreen = { 0.28f, 0.68f, 0.40f, 1.0f };
        constexpr DirectX::XMFLOAT4 AccentOrange = { 0.78f, 0.48f, 0.16f, 1.0f };
        constexpr DirectX::XMFLOAT4 AccentTeal = { 0.30f, 0.62f, 0.62f, 1.0f };
        constexpr DirectX::XMFLOAT4 AccentRed = { 0.62f, 0.14f, 0.16f, 1.0f };

        void DrawBorder(float x, float y, float w, float h, const DirectX::XMFLOAT4& color, float t = 1.0f)
        {
            UIRenderer::DrawRectFilled(x, y, w, t, color);
            UIRenderer::DrawRectFilled(x, y + h - t, w, t, color);
            UIRenderer::DrawRectFilled(x, y, t, h, color);
            UIRenderer::DrawRectFilled(x + w - t, y, t, h, color);
        }

        void DrawLine(DirectX::XMFLOAT2 a, DirectX::XMFLOAT2 b, const DirectX::XMFLOAT4& color, float thickness)
        {
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            const int steps = (std::max)(1, (int)(std::sqrt(dx * dx + dy * dy) / 4.0f));
            for (int i = 0; i <= steps; ++i)
            {
                const float t = (float)i / (float)steps;
                const float x = a.x + dx * t;
                const float y = a.y + dy * t;
                UIRenderer::DrawRectFilled(x - thickness * 0.5f, y - thickness * 0.5f, thickness, thickness, color);
            }
        }

        const char* ParameterTypeName(AnimatorComponent::Parameter::Type type)
        {
            switch (type)
            {
                case AnimatorComponent::Parameter::Type::Bool: return "Bool";
                case AnimatorComponent::Parameter::Type::Trigger: return "Trigger";
                default: return "Float";
            }
        }

        const char* ConditionModeName(AnimatorComponent::TransitionCondition::CompareMode mode)
        {
            switch (mode)
            {
                case AnimatorComponent::TransitionCondition::CompareMode::IfNot: return "If Not";
                case AnimatorComponent::TransitionCondition::CompareMode::Greater: return ">";
                case AnimatorComponent::TransitionCondition::CompareMode::Less: return "<";
                case AnimatorComponent::TransitionCondition::CompareMode::Equals: return "==";
                case AnimatorComponent::TransitionCondition::CompareMode::NotEquals: return "!=";
                default: return "If";
            }
        }

        const char* LayerBlendModeName(AnimatorComponent::Layer::BlendMode mode)
        {
            return mode == AnimatorComponent::Layer::BlendMode::Additive ? "Additive" : "Override";
        }

        void EnsureAnimatorLayers(AnimatorComponent& animator)
        {
            if (animator.Layers.empty())
                animator.Layers.push_back({});
            animator.ActiveLayerIndex = std::clamp(animator.ActiveLayerIndex, 0, (int)animator.Layers.size() - 1);

            auto& baseLayer = animator.Layers[0];
            if (baseLayer.States.empty() && !animator.States.empty())
            {
                // 예전 저장 파일은 상태/전이를 AnimatorComponent 바로 아래에 저장했다.
                // 처음 열 때만 Base Layer로 옮겨 두면 기존 씬도 새 레이어 구조에서 그대로 편집된다.
                baseLayer.States = animator.States;
                baseLayer.Transitions = animator.Transitions;
                baseLayer.ActiveStateIndex = animator.ActiveStateIndex;
                baseLayer.EntryStateIndex = animator.EntryStateIndex;
                baseLayer.SelectedTransitionIndex = animator.SelectedTransitionIndex;
            }

            for (auto& layer : animator.Layers)
            {
                if (layer.States.empty())
                {
                    layer.ActiveStateIndex = -1;
                    layer.EntryStateIndex = -1;
                    layer.SelectedTransitionIndex = -1;
                    layer.Transitions.clear();
                    continue;
                }

                layer.ActiveStateIndex = std::clamp(layer.ActiveStateIndex, 0, (int)layer.States.size() - 1);
                layer.EntryStateIndex = std::clamp(layer.EntryStateIndex, 0, (int)layer.States.size() - 1);
                layer.Transitions.erase(
                    std::remove_if(layer.Transitions.begin(), layer.Transitions.end(), [&layer](const AnimatorComponent::Transition& transition)
                    {
                        return transition.FromStateIndex < 0 || transition.ToStateIndex < 0 ||
                            transition.FromStateIndex >= (int)layer.States.size() ||
                            transition.ToStateIndex >= (int)layer.States.size();
                    }),
                    layer.Transitions.end());
                layer.SelectedTransitionIndex = std::clamp(layer.SelectedTransitionIndex, -1, (int)layer.Transitions.size() - 1);
            }
        }

        AnimatorComponent::Layer* GetActiveLayer(AnimatorComponent& animator)
        {
            EnsureAnimatorLayers(animator);
            return &animator.Layers[animator.ActiveLayerIndex];
        }

        const AnimatorComponent::Layer* GetActiveLayer(const AnimatorComponent& animator)
        {
            if (animator.Layers.empty())
                return nullptr;
            const int index = std::clamp(animator.ActiveLayerIndex, 0, (int)animator.Layers.size() - 1);
            return &animator.Layers[index];
        }

        void SyncBaseLayerToLegacyGraph(AnimatorComponent& animator)
        {
            if (animator.Layers.empty())
                return;

            const auto& baseLayer = animator.Layers[0];
            // 현재 런타임은 아직 Base Layer의 그래프만 재생한다.
            // 편집 데이터는 레이어 안에 두고, 런타임 호환용 필드에는 Base Layer만 복사한다.
            animator.States = baseLayer.States;
            animator.Transitions = baseLayer.Transitions;
            animator.ActiveStateIndex = baseLayer.ActiveStateIndex;
            animator.EntryStateIndex = baseLayer.EntryStateIndex;
            animator.SelectedTransitionIndex = baseLayer.SelectedTransitionIndex;
        }

        std::string FitText(const std::string& text, float availableWidth, float approximateCharWidth = 8.0f)
        {
            const int maxChars = (std::max)(0, (int)(availableWidth / approximateCharWidth));
            if ((int)text.size() <= maxChars)
                return text;
            if (maxChars <= 3)
                return text.substr(0, (size_t)(std::max)(0, maxChars));
            return text.substr(0, (size_t)maxChars - 3) + "...";
        }

        float DistancePointToSegment(float px, float py, DirectX::XMFLOAT2 a, DirectX::XMFLOAT2 b)
        {
            const float vx = b.x - a.x;
            const float vy = b.y - a.y;
            const float wx = px - a.x;
            const float wy = py - a.y;
            const float lenSq = vx * vx + vy * vy;
            const float t = lenSq <= 0.0001f ? 0.0f : (std::clamp)((wx * vx + wy * vy) / lenSq, 0.0f, 1.0f);
            const float cx = a.x + vx * t;
            const float cy = a.y + vy * t;
            const float dx = px - cx;
            const float dy = py - cy;
            return std::sqrt(dx * dx + dy * dy);
        }

        void DrawArrowHead(DirectX::XMFLOAT2 from, DirectX::XMFLOAT2 to, const DirectX::XMFLOAT4& color)
        {
            const float dx = to.x - from.x;
            const float dy = to.y - from.y;
            const float len = (std::max)(0.001f, std::sqrt(dx * dx + dy * dy));
            const float nx = dx / len;
            const float ny = dy / len;
            const float px = -ny;
            const float py = nx;
            DirectX::XMFLOAT2 left = { to.x - nx * 12.0f + px * 5.0f, to.y - ny * 12.0f + py * 5.0f };
            DirectX::XMFLOAT2 right = { to.x - nx * 12.0f - px * 5.0f, to.y - ny * 12.0f - py * 5.0f };
            DrawLine(to, left, color, 2.0f);
            DrawLine(to, right, color, 2.0f);
        }

        std::string TrimName(const std::string& input)
        {
            const auto begin = std::find_if_not(input.begin(), input.end(), [](unsigned char c) { return std::isspace(c); });
            const auto end = std::find_if_not(input.rbegin(), input.rend(), [](unsigned char c) { return std::isspace(c); }).base();
            if (begin >= end)
                return {};
            return std::string(begin, end);
        }
    }

    AnimatorGraphPanel::AnimatorGraphPanel(const std::string& name)
        : WindowPanel(name, "Animator")
    {
        SetClipToBounds(true);
    }

    void AnimatorGraphPanel::SetTarget(Entity entity)
    {
        m_TargetEntity = entity;
        if (AnimatorComponent* animator = GetAnimator())
        {
            EnsureAnimatorLayers(*animator);
            auto* layer = GetActiveLayer(*animator);
            for (int i = 0; layer && i < (int)layer->States.size(); ++i)
            {
                auto& pos = layer->States[i].GraphPosition;
                if (i > 0 && std::abs(pos.x - 260.0f) < 0.01f && std::abs(pos.y - 180.0f) < 0.01f)
                    pos = { 320.0f + (float)(i % 4) * 210.0f, 180.0f + (float)(i / 4) * 110.0f };
            }
            ClampAnimatorSelection(*animator);
            layer = GetActiveLayer(*animator);
            m_SelectedStateIndex = layer ? layer->ActiveStateIndex : -1;
            m_SelectedTransitionIndex = layer ? layer->SelectedTransitionIndex : -1;
            if (m_SelectedTransitionIndex >= 0)
                m_SelectedStateIndex = -1;
            m_SelectedStateIndices.clear();
            if (m_SelectedStateIndex >= 0)
                m_SelectedStateIndices.push_back(m_SelectedStateIndex);
        }
        SetVisible(true);
        BringToFront();
    }

    AnimatorComponent* AnimatorGraphPanel::GetAnimator() const
    {
        Entity entity = m_TargetEntity;
        if (!entity || !entity.HasComponent<AnimatorComponent>())
            return nullptr;
        return &entity.GetComponent<AnimatorComponent>();
    }

    void AnimatorGraphPanel::OnRender()
    {
        WindowPanel::OnRender();
        if (!IsVisible())
            return;

        UIRenderer::SetClipRect(m_CalculatedPos.x, m_CalculatedPos.y, m_CalculatedSize.x, m_CalculatedSize.y);

        m_TitleContentTop = GetContentPosition().y - m_CalculatedPos.y;
        const float toolbarX = m_CalculatedPos.x;
        const float toolbarY = m_CalculatedPos.y + m_TitleContentTop;
        const float bodyY = toolbarY + m_ToolbarHeight;
        const float bodyH = (std::max)(0.0f, m_CalculatedSize.y - m_TitleContentTop - m_ToolbarHeight);

        UIRenderer::DrawRectFilled(toolbarX, toolbarY, m_CalculatedSize.x, m_ToolbarHeight, { 0.080f, 0.083f, 0.092f, 1.0f });
        UIRenderer::DrawRectFilled(toolbarX, bodyY, m_CalculatedSize.x, bodyH, CanvasColor);
        DrawToolbar(toolbarX + 8.0f, toolbarY + 4.0f, m_CalculatedSize.x - 16.0f);

        DrawSidebar(m_CalculatedPos.x, bodyY, m_SidebarWidth, bodyH);
        DrawGraph(m_CalculatedPos.x + m_SidebarWidth, bodyY, m_CalculatedSize.x - m_SidebarWidth, bodyH);
        DrawContextMenu();
        DrawClipPicker();

        UIRenderer::ClearClipRect();
    }

    bool AnimatorGraphPanel::OnEvent(Event& e)
    {
        if (!IsVisible())
            return false;

        if (e.GetEventType() == EventType::MouseScrolled)
        {
            MouseScrolledEvent& scroll = static_cast<MouseScrolledEvent&>(e);
            Window* renderWindow = Widget::GetCurrentRenderWindow();
            auto [mouseX, mouseY] = renderWindow
                ? renderWindow->GetMousePosition()
                : Application::Get()->GetWindow().GetMousePosition();

            const float graphX = m_CalculatedPos.x + m_SidebarWidth;
            const float graphY = m_CalculatedPos.y + m_TitleContentTop + m_ToolbarHeight;
            if (IsPointInRect(mouseX, mouseY, graphX, graphY, m_CalculatedSize.x - m_SidebarWidth, m_CalculatedSize.y - m_TitleContentTop - m_ToolbarHeight))
            {
                const DirectX::XMFLOAT2 before = ScreenToGraph(mouseX, mouseY);
                m_Zoom = (std::clamp)(m_Zoom + scroll.GetYOffset() * 0.08f, 0.75f, 1.35f);
                const DirectX::XMFLOAT2 after = ScreenToGraph(mouseX, mouseY);
                m_ViewOffsetX += after.x - before.x;
                m_ViewOffsetY += after.y - before.y;
                e.Handled = true;
                return true;
            }
        }

        return WindowPanel::OnEvent(e);
    }

    bool AnimatorGraphPanel::WantsMouseCapture() const
    {
        return WindowPanel::WantsMouseCapture() || m_IsDraggingState || m_IsPanningGraph || m_IsBoxSelecting || m_IsContextMenuOpen || m_IsClipPickerOpen;
    }

    bool AnimatorGraphPanel::OnMouseButtonPressed(MouseButtonPressedEvent& e)
    {
        const bool wasVisible = IsVisible();
        Window* ownerBefore = GetOwnerWindow();
        if (WindowPanel::OnMouseButtonPressed(e))
        {
            const bool closedInsideMainWindow = wasVisible && !IsVisible();
            const bool closedTornOffWindow = ownerBefore && ownerBefore->ShouldClose();
            if ((closedInsideMainWindow || closedTornOffWindow) && m_OnClosed)
                m_OnClosed();
            return true;
        }

        if (!IsPointInside(e.GetX(), e.GetY()))
            return false;

        BringToFront();
        Widget::SetKeyboardFocus(this);
        m_LastMouseX = e.GetX();
        m_LastMouseY = e.GetY();

        if (e.GetButton() == 1)
        {
            if (m_IsClipPickerOpen)
            {
                e.Handled = true;
                return true;
            }

            const float graphX = m_CalculatedPos.x + m_SidebarWidth;
            const float graphY = m_CalculatedPos.y + m_TitleContentTop + m_ToolbarHeight;
            const float graphW = m_CalculatedSize.x - m_SidebarWidth;
            const float graphH = m_CalculatedSize.y - m_TitleContentTop - m_ToolbarHeight;
            if (IsPointInRect(e.GetX(), e.GetY(), graphX, graphY, graphW, graphH))
            {
                if (AnimatorComponent* animator = GetAnimator())
                {
                    auto* layer = GetActiveLayer(*animator);
                    if (!layer)
                        return true;
                    const int transitionIndex = GetTransitionAt(e.GetX(), e.GetY());
                    const int stateIndex = GetStateAt(e.GetX(), e.GetY());
                    if (transitionIndex >= 0)
                    {
                        layer->SelectedTransitionIndex = transitionIndex;
                        m_SelectedTransitionIndex = transitionIndex;
                        m_SelectedStateIndex = -1;
                        OpenContextMenu(ContextMenuMode::Transition, e.GetX(), e.GetY(), -1, transitionIndex);
                    }
                    else if (stateIndex >= 0)
                    {
                        m_ContextSourceStateIndex = m_SelectedStateIndex;
                        if (m_ContextSourceStateIndex < 0)
                            m_ContextSourceStateIndex = layer->ActiveStateIndex;
                        m_SelectedTransitionIndex = -1;
                        layer->SelectedTransitionIndex = -1;
                        OpenContextMenu(ContextMenuMode::ReplaceState, e.GetX(), e.GetY(), stateIndex, -1);
                    }
                    else
                    {
                        OpenContextMenu(ContextMenuMode::AddState, e.GetX(), e.GetY(), -1, -1);
                    }
                }
                e.Handled = true;
                return true;
            }
        }

        if (e.GetButton() == 2)
        {
            const float graphX = m_CalculatedPos.x + m_SidebarWidth;
            const float graphY = m_CalculatedPos.y + m_TitleContentTop + m_ToolbarHeight;
            const float graphW = m_CalculatedSize.x - m_SidebarWidth;
            const float graphH = m_CalculatedSize.y - m_TitleContentTop - m_ToolbarHeight;
            if (!IsPointInRect(e.GetX(), e.GetY(), graphX, graphY, graphW, graphH))
                return true;

            CloseContextMenu();
            m_IsPanningGraph = true;
            m_PanStartMouseX = e.GetX();
            m_PanStartMouseY = e.GetY();
            m_PanStartOffsetX = m_ViewOffsetX;
            m_PanStartOffsetY = m_ViewOffsetY;
            Widget::BeginMouseInteraction(this);
            e.Handled = true;
            return true;
        }

        if (e.GetButton() != 0)
            return false;

        if (m_IsClipPickerOpen)
        {
            HandleClipPickerClick(e.GetX(), e.GetY());
            e.Handled = true;
            return true;
        }

        if (m_IsContextMenuOpen)
        {
            if (!HandleContextMenuClick(e.GetX(), e.GetY()))
                CloseContextMenu();
            e.Handled = true;
            return true;
        }

        if (HandleToolbarClick(e.GetX(), e.GetY()) || HandleSidebarClick(e.GetX(), e.GetY()))
        {
            CloseContextMenu();
            e.Handled = true;
            return true;
        }

        if (AnimatorComponent* animator = GetAnimator())
        {
            auto* layer = GetActiveLayer(*animator);
            if (!layer)
                return true;
            const float graphX = m_CalculatedPos.x + m_SidebarWidth;
            const float graphY = m_CalculatedPos.y + m_TitleContentTop + m_ToolbarHeight;
            const float graphW = m_CalculatedSize.x - m_SidebarWidth;
            const float graphH = m_CalculatedSize.y - m_TitleContentTop - m_ToolbarHeight;
            if (!IsPointInRect(e.GetX(), e.GetY(), graphX, graphY, graphW, graphH))
                return true;

            const int transitionIndex = GetTransitionAt(e.GetX(), e.GetY());
            if (transitionIndex >= 0)
            {
                m_SelectedStateIndices.clear();
                m_SelectedTransitionIndex = transitionIndex;
                layer->SelectedTransitionIndex = transitionIndex;
                m_SelectedStateIndex = -1;
                m_SelectedStateIndices.clear();
                e.Handled = true;
                return true;
            }

            int stateIndex = GetStateAt(e.GetX(), e.GetY());
            if (stateIndex >= 0 && stateIndex < (int)layer->States.size())
            {
                if (!IsStateSelected(stateIndex))
                    SelectOnlyState(*animator, stateIndex);
                else
                {
                    // 이미 선택된 묶음 안의 노드를 다시 누르면 선택 묶음은 유지하고
                    // Inspector에 표시할 대표 상태만 바꾼다.
                    layer->ActiveStateIndex = stateIndex;
                    m_SelectedStateIndex = stateIndex;
                }
                layer->SelectedTransitionIndex = -1;
                m_SelectedTransitionIndex = -1;
                SyncBaseLayerToLegacyGraph(*animator);
                ResetRuntime(*animator);

                DirectX::XMFLOAT2 graphMouse = ScreenToGraph(e.GetX(), e.GetY());
                m_DraggingStateIndex = stateIndex;
                m_IsDraggingState = true;
                m_DragOffsetX = graphMouse.x - layer->States[stateIndex].GraphPosition.x;
                m_DragOffsetY = graphMouse.y - layer->States[stateIndex].GraphPosition.y;
                Widget::BeginMouseInteraction(this);
            }
            else
            {
                // 빈 그래프 영역에서 왼쪽 드래그를 시작하면 박스 선택 모드로 들어간다.
                // 선택 박스는 화면 좌표로 그리고, 판정할 때만 노드 사각형과 겹침을 계산한다.
                ClearStateSelection(*animator);
                m_IsBoxSelecting = true;
                m_BoxSelectStart = { e.GetX(), e.GetY() };
                m_BoxSelectEnd = m_BoxSelectStart;
                Widget::BeginMouseInteraction(this);
            }
        }

        e.Handled = true;
        return true;
    }

    bool AnimatorGraphPanel::OnMouseMoved(MouseMovedEvent& e)
    {
        if (WindowPanel::OnMouseMoved(e))
            return true;

        m_LastMouseX = e.GetX();
        m_LastMouseY = e.GetY();

        if (m_IsPanningGraph)
        {
            m_ViewOffsetX = m_PanStartOffsetX + (e.GetX() - m_PanStartMouseX) / m_Zoom;
            m_ViewOffsetY = m_PanStartOffsetY + (e.GetY() - m_PanStartMouseY) / m_Zoom;
            e.Handled = true;
            return true;
        }

        if (m_IsDraggingState)
        {
            if (AnimatorComponent* animator = GetAnimator())
            {
                auto* layer = GetActiveLayer(*animator);
                if (layer && m_DraggingStateIndex >= 0 && m_DraggingStateIndex < (int)layer->States.size())
                {
                    DirectX::XMFLOAT2 graphMouse = ScreenToGraph(e.GetX(), e.GetY());
                    auto& pos = layer->States[m_DraggingStateIndex].GraphPosition;
                    const float dx = graphMouse.x - m_DragOffsetX - pos.x;
                    const float dy = graphMouse.y - m_DragOffsetY - pos.y;
                    pos.x = graphMouse.x - m_DragOffsetX;
                    pos.y = graphMouse.y - m_DragOffsetY;
                    if (IsStateSelected(m_DraggingStateIndex))
                    {
                        for (int selectedIndex : m_SelectedStateIndices)
                        {
                            if (selectedIndex == m_DraggingStateIndex || selectedIndex < 0 || selectedIndex >= (int)layer->States.size())
                                continue;
                            layer->States[selectedIndex].GraphPosition.x += dx;
                            layer->States[selectedIndex].GraphPosition.y += dy;
                        }
                    }
                    SyncBaseLayerToLegacyGraph(*animator);
                }
            }
            e.Handled = true;
            return true;
        }

        if (m_IsBoxSelecting)
        {
            m_BoxSelectEnd = { e.GetX(), e.GetY() };
            if (AnimatorComponent* animator = GetAnimator())
                SelectStatesInBox(*animator);
            e.Handled = true;
            return true;
        }

        return false;
    }

    bool AnimatorGraphPanel::OnMouseButtonReleased(MouseButtonReleasedEvent& e)
    {
        if (WindowPanel::OnMouseButtonReleased(e))
            return true;

        if ((m_IsDraggingState && e.GetButton() == 0) || (m_IsPanningGraph && e.GetButton() == 2) || (m_IsBoxSelecting && e.GetButton() == 0))
        {
            m_IsDraggingState = false;
            m_IsPanningGraph = false;
            m_IsBoxSelecting = false;
            m_DraggingStateIndex = -1;
            Widget::EndMouseInteraction(this);
            e.Handled = true;
            return true;
        }

        return false;
    }

    bool AnimatorGraphPanel::OnKeyPressed(KeyPressedEvent& e)
    {
        if (!IsVisible() || !Widget::IsKeyboardFocusOwner(this))
            return false;

        if (m_EditingParameterIndex >= 0)
        {
            if (e.GetKeyCode() == 8 && !m_ParameterEditBuffer.empty())
                m_ParameterEditBuffer.pop_back();
            else if (e.GetKeyCode() == 13)
                CommitParameterRename();
            else if (e.GetKeyCode() == 27)
                CancelParameterRename();

            e.Handled = true;
            return true;
        }

        if (e.GetKeyCode() == 0x2E)
        {
            if (AnimatorComponent* animator = GetAnimator())
            {
                auto* layer = GetActiveLayer(*animator);
                if (layer && m_SelectedTransitionIndex >= 0 && m_SelectedTransitionIndex < (int)layer->Transitions.size())
                {
                    layer->Transitions.erase(layer->Transitions.begin() + m_SelectedTransitionIndex);
                m_SelectedTransitionIndex = -1;
                layer->SelectedTransitionIndex = -1;
                m_SelectedStateIndices.clear();
                SyncBaseLayerToLegacyGraph(*animator);
                }
                else
                {
                    DeleteSelectedState();
                }
            }
            e.Handled = true;
            return true;
        }
        return false;
    }

    bool AnimatorGraphPanel::OnTextInput(TextInputEvent& e)
    {
        if (!IsVisible() || !Widget::IsKeyboardFocusOwner(this) || m_EditingParameterIndex < 0)
            return false;

        const char c = e.GetCharacter();
        if ((std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ' ') && m_ParameterEditBuffer.size() < 32)
            m_ParameterEditBuffer.push_back(c);
        e.Handled = true;
        return true;
    }

    void AnimatorGraphPanel::DrawToolbar(float x, float y, float)
    {
        struct ButtonDef { const char* Label; float W; };
        const ButtonDef buttons[] =
        {
            { "Add State", 92.0f }, { "Entry", 62.0f }, { "Clip", 58.0f },
            { "Preview", 72.0f }, { "Auto", 56.0f }, { "Delete", 68.0f }
        };

        float bx = x;
        for (int i = 0; i < 6; ++i)
        {
            const bool danger = i == 5;
            const bool hover = IsPointInRect(m_LastMouseX, m_LastMouseY, bx, y, buttons[i].W, 25.0f);
            DirectX::XMFLOAT4 fill = danger ? DirectX::XMFLOAT4{ 0.24f, 0.10f, 0.12f, 1.0f } : DirectX::XMFLOAT4{ 0.125f, 0.130f, 0.145f, 1.0f };
            if (hover)
                fill = danger ? DirectX::XMFLOAT4{ 0.34f, 0.13f, 0.16f, 1.0f } : DirectX::XMFLOAT4{ 0.18f, 0.22f, 0.30f, 1.0f };
            DirectX::XMFLOAT4 stroke = danger ? DirectX::XMFLOAT4{ 0.55f, 0.18f, 0.22f, 1.0f } : PanelStroke;
            UIRenderer::DrawRectFilled(bx, y, buttons[i].W, 25.0f, fill);
            DrawBorder(bx, y, buttons[i].W, 25.0f, stroke);
            UIRenderer::DrawString(buttons[i].Label, bx + 8.0f, y + 18.0f, TextStrong);
            bx += buttons[i].W + 7.0f;
        }

        if (AnimatorComponent* animator = GetAnimator())
        {
            const auto* layer = GetActiveLayer(*animator);
            const std::string info = (!layer || layer->States.empty()) ? "No states" : ("States " + std::to_string(layer->States.size()));
            UIRenderer::DrawString(info, bx + 10.0f, y + 18.0f, TextMuted);
        }
    }

    void AnimatorGraphPanel::DrawSidebar(float x, float y, float w, float h)
    {
        UIRenderer::DrawRectFilled(x, y, w, h, SidebarColor);
        UIRenderer::DrawRectFilled(x + w - 1.0f, y, 1.0f, h, PanelStroke);

        auto drawTab = [&](const char* label, float tx, bool active)
        {
            const bool hover = IsPointInRect(m_LastMouseX, m_LastMouseY, tx, y + 8.0f, 92.0f, 26.0f);
            DirectX::XMFLOAT4 fill = active ? DirectX::XMFLOAT4{ 0.18f, 0.25f, 0.34f, 1.0f } : DirectX::XMFLOAT4{ 0.12f, 0.125f, 0.14f, 1.0f };
            if (hover && !active)
                fill = { 0.16f, 0.17f, 0.19f, 1.0f };
            UIRenderer::DrawRectFilled(tx, y + 8.0f, 92.0f, 26.0f, fill);
            DrawBorder(tx, y + 8.0f, 92.0f, 26.0f, active ? AccentBlue : PanelStroke);
            UIRenderer::DrawString(label, tx + 10.0f, y + 26.0f, active ? TextStrong : TextMuted);
        };
        drawTab("Layers", x + 10.0f, m_SidebarPage == SidebarPage::Layers);
        drawTab("Parameters", x + 106.0f, m_SidebarPage == SidebarPage::Parameters);

        if (AnimatorComponent* animator = GetAnimator())
        {
            EnsureAnimatorLayers(*animator);
            if (m_SidebarPage == SidebarPage::Layers)
            {
                UIRenderer::DrawString("Layers", x + 18.0f, y + 62.0f, TextStrong);
                const bool addLayerHover = IsPointInRect(m_LastMouseX, m_LastMouseY, x + w - 42.0f, y + 45.0f, 24.0f, 22.0f);
                UIRenderer::DrawRectFilled(x + w - 42.0f, y + 45.0f, 24.0f, 22.0f, addLayerHover ? DirectX::XMFLOAT4{ 0.18f, 0.25f, 0.34f, 1.0f } : DirectX::XMFLOAT4{ 0.13f, 0.135f, 0.145f, 1.0f });
                DrawBorder(x + w - 42.0f, y + 45.0f, 24.0f, 22.0f, addLayerHover ? AccentBlue : PanelStroke);
                UIRenderer::DrawString("+", x + w - 35.0f, y + 62.0f, TextStrong);

                float layerY = y + 74.0f;
                for (int i = 0; i < (int)animator->Layers.size(); ++i)
                {
                    const auto& layer = animator->Layers[i];
                    const bool active = i == animator->ActiveLayerIndex;
                    const bool hover = IsPointInRect(m_LastMouseX, m_LastMouseY, x + 12.0f, layerY, w - 25.0f, 27.0f);
                    DirectX::XMFLOAT4 fill = active ? DirectX::XMFLOAT4{ 0.18f, 0.31f, 0.48f, 1.0f } : DirectX::XMFLOAT4{ 0.13f, 0.135f, 0.145f, 1.0f };
                    if (hover && !active)
                        fill = { 0.17f, 0.18f, 0.20f, 1.0f };
                    UIRenderer::DrawRectFilled(x + 12.0f, layerY, w - 25.0f, 27.0f, fill);
                    UIRenderer::DrawString(FitText(layer.Name, w - 98.0f), x + 22.0f, layerY + 19.0f, active ? TextStrong : TextMuted);
                    UIRenderer::DrawString(std::to_string((int)(layer.Weight * 100.0f)) + "%", x + w - 76.0f, layerY + 19.0f, TextMuted);
                    UIRenderer::DrawString("*", x + w - 30.0f, layerY + 19.0f, active ? AccentGreen : TextMuted);
                    layerY += 30.0f;
                }

                auto& layer = animator->Layers[animator->ActiveLayerIndex];
                const float settingsY = layerY + 8.0f;
                UIRenderer::DrawRectFilled(x + 10.0f, settingsY, w - 22.0f, 118.0f, { 0.070f, 0.074f, 0.082f, 1.0f });
                DrawBorder(x + 10.0f, settingsY, w - 22.0f, 118.0f, PanelStroke);
                UIRenderer::DrawString("Layer Settings", x + 20.0f, settingsY + 22.0f, TextStrong);
                UIRenderer::DrawString("Weight", x + 20.0f, settingsY + 48.0f, TextMuted);
                UIRenderer::DrawRectFilled(x + 86.0f, settingsY + 33.0f, 92.0f, 18.0f, { 0.10f, 0.105f, 0.115f, 1.0f });
                UIRenderer::DrawRectFilled(x + 86.0f, settingsY + 33.0f, 92.0f * std::clamp(layer.Weight, 0.0f, 1.0f), 18.0f, { 0.22f, 0.40f, 0.62f, 1.0f });
                UIRenderer::DrawString("-", x + w - 63.0f, settingsY + 49.0f, TextStrong);
                UIRenderer::DrawString("+", x + w - 37.0f, settingsY + 49.0f, TextStrong);
                UIRenderer::DrawString(std::string("Blend: ") + LayerBlendModeName(layer.Blending), x + 20.0f, settingsY + 74.0f, TextMuted);
                UIRenderer::DrawString(layer.IKPass ? "[v] IK Pass" : "[ ] IK Pass", x + 20.0f, settingsY + 99.0f, layer.IKPass ? AccentGreen : TextMuted);

                float rowY = settingsY + 138.0f;
                UIRenderer::DrawString("States", x + 18.0f, rowY - 10.0f, TextStrong);
                for (int i = 0; i < (int)layer.States.size(); ++i)
                {
                    const bool active = i == layer.ActiveStateIndex;
                    const bool hover = IsPointInRect(m_LastMouseX, m_LastMouseY, x + 12.0f, rowY, w - 25.0f, 27.0f);
                    DirectX::XMFLOAT4 fill = active ? DirectX::XMFLOAT4{ 0.18f, 0.31f, 0.48f, 1.0f } : DirectX::XMFLOAT4{ 0.13f, 0.135f, 0.145f, 1.0f };
                    if (hover && !active)
                        fill = { 0.17f, 0.18f, 0.20f, 1.0f };
                    UIRenderer::DrawRectFilled(x + 12.0f, rowY, w - 25.0f, 27.0f, fill);
                    UIRenderer::DrawString(FitText(layer.States[i].Name, w - 92.0f), x + 22.0f, rowY + 19.0f, active ? TextStrong : TextMuted);
                    if (i == layer.EntryStateIndex)
                        UIRenderer::DrawString("Entry", x + w - 58.0f, rowY + 19.0f, AccentGreen);
                    rowY += 30.0f;
                }

                if (m_SelectedStateIndex >= 0 && m_SelectedStateIndex < (int)layer.States.size())
                {
                    const auto& state = layer.States[m_SelectedStateIndex];
                    const float propY = y + h - 150.0f;
                    UIRenderer::DrawRectFilled(x + 10.0f, propY, w - 22.0f, 136.0f, { 0.070f, 0.074f, 0.082f, 1.0f });
                    DrawBorder(x + 10.0f, propY, w - 22.0f, 136.0f, PanelStroke);
                    UIRenderer::DrawString("Selected State", x + 20.0f, propY + 24.0f, TextStrong);
                    UIRenderer::DrawString(FitText(state.Name, w - 48.0f), x + 20.0f, propY + 50.0f, TextMuted);
                    UIRenderer::DrawString(state.Loop ? "Loop: On" : "Loop: Off", x + 20.0f, propY + 78.0f, state.Loop ? AccentGreen : TextMuted);
                    // Write Defaults는 별도 상태가 아니라, 선택한 상태 안의 옵션이다.
                    // Unity처럼 한 State를 고른 뒤 체크박스로 켜고 끄는 흐름을 유지한다.
                    UIRenderer::DrawRectFilled(x + 20.0f, propY + 88.0f, 15.0f, 15.0f, { 0.10f, 0.105f, 0.115f, 1.0f });
                    DrawBorder(x + 20.0f, propY + 88.0f, 15.0f, 15.0f, state.WriteDefaults ? AccentGreen : PanelStroke);
                    if (state.WriteDefaults)
                        UIRenderer::DrawString("v", x + 24.0f, propY + 102.0f, AccentGreen);
                    UIRenderer::DrawString("Write Defaults", x + 42.0f, propY + 104.0f, state.WriteDefaults ? AccentGreen : TextMuted);
                    UIRenderer::DrawString(("Speed: " + std::to_string(state.Speed)).substr(0, 13), x + 20.0f, propY + 130.0f, TextMuted);
                    UIRenderer::DrawRectFilled(x + w - 72.0f, propY + 112.0f, 24.0f, 20.0f, { 0.12f, 0.125f, 0.14f, 1.0f });
                    UIRenderer::DrawRectFilled(x + w - 44.0f, propY + 112.0f, 24.0f, 20.0f, { 0.12f, 0.125f, 0.14f, 1.0f });
                    UIRenderer::DrawString("-", x + w - 64.0f, propY + 128.0f, TextStrong);
                    UIRenderer::DrawString("+", x + w - 37.0f, propY + 128.0f, TextStrong);
                }
            }
            else
            {
                UIRenderer::DrawString("Parameters", x + 18.0f, y + 64.0f, TextStrong);
                UIRenderer::DrawString("+ Float   + Bool   + Trigger", x + 18.0f, y + 92.0f, AccentBlue);

                float rowY = y + 118.0f;
                if (animator->Parameters.empty())
                    UIRenderer::DrawString("No parameters yet.", x + 18.0f, rowY + 18.0f, TextMuted);
                for (int i = 0; i < (int)animator->Parameters.size(); ++i)
                {
                    const auto& parameter = animator->Parameters[i];
                    UIRenderer::DrawRectFilled(x + 12.0f, rowY, w - 25.0f, 27.0f, { 0.13f, 0.135f, 0.145f, 1.0f });
                    if (m_EditingParameterIndex == i)
                    {
                        UIRenderer::DrawRectFilled(x + 18.0f, rowY + 4.0f, w - 105.0f, 19.0f, { 0.09f, 0.15f, 0.22f, 1.0f });
                        UIRenderer::DrawString(m_ParameterEditBuffer + "_", x + 22.0f, rowY + 19.0f, TextStrong);
                    }
                    else
                    {
                        UIRenderer::DrawString(parameter.Name, x + 22.0f, rowY + 19.0f, TextStrong);
                    }
                    UIRenderer::DrawString(ParameterTypeName(parameter.ParamType), x + w - 76.0f, rowY + 19.0f, TextMuted);
                    rowY += 30.0f;
                }
            }

            DrawTransitionInspector(x + 10.0f, y + h - 170.0f, w - 22.0f, 156.0f);
        }
    }

    void AnimatorGraphPanel::DrawGraph(float x, float y, float w, float h)
    {
        UIRenderer::SetClipRect(x, y, w, h);
        DrawGrid(x, y, w, h);

        DirectX::XMFLOAT2 entry = GraphToScreen({ 120.0f, 180.0f });
        DirectX::XMFLOAT2 any = GraphToScreen({ 120.0f, 320.0f });
        DirectX::XMFLOAT2 exit = GraphToScreen({ 120.0f, 460.0f });

        auto drawSmallNode = [&](const char* name, DirectX::XMFLOAT2 p, DirectX::XMFLOAT4 accent)
        {
            UIRenderer::DrawRectFilled(p.x, p.y, 112.0f, 36.0f, { 0.13f, 0.14f, 0.15f, 1.0f });
            UIRenderer::DrawRectFilled(p.x, p.y, 4.0f, 36.0f, accent);
            DrawBorder(p.x, p.y, 112.0f, 36.0f, accent);
            UIRenderer::DrawString(name, p.x + 14.0f, p.y + 24.0f, TextStrong);
        };

        drawSmallNode("Entry", entry, AccentGreen);
        drawSmallNode("Any State", any, AccentTeal);
        drawSmallNode("Exit", exit, AccentRed);

        if (AnimatorComponent* animator = GetAnimator())
        {
            const auto* layer = GetActiveLayer(*animator);
            if (layer && !layer->States.empty())
            {
                int entryIndex = std::clamp(layer->EntryStateIndex, 0, (int)layer->States.size() - 1);
                StateNodeRect entryState = GetStateRect(entryIndex);
                DrawNodeConnection({ entry.x + 112.0f, entry.y + 18.0f }, { entryState.X, entryState.Y + entryState.H * 0.5f }, AccentGreen);
            }

            if (!layer)
            {
                UIRenderer::ClearClipRect();
                return;
            }

            for (int i = 0; i < (int)layer->Transitions.size(); ++i)
                DrawTransitionArrow(i, layer->Transitions[i], i == m_SelectedTransitionIndex || i == layer->SelectedTransitionIndex);

            for (int i = 0; i < (int)layer->States.size(); ++i)
            {
                StateNodeRect r = GetStateRect(i);
                const bool selected = IsStateSelected(i);
                const bool hover = IsPointInRect(m_LastMouseX, m_LastMouseY, r.X, r.Y, r.W, r.H);
                const bool entryState = i == layer->EntryStateIndex;
                DirectX::XMFLOAT4 accent = entryState ? AccentOrange : AccentBlue;
                UIRenderer::DrawRectFilled(r.X + 4.0f, r.Y + 5.0f, r.W, r.H, { 0.010f, 0.012f, 0.016f, 0.82f });
                DirectX::XMFLOAT4 nodeFill = selected ? DirectX::XMFLOAT4{ 0.15f, 0.18f, 0.22f, 1.0f } : DirectX::XMFLOAT4{ 0.12f, 0.125f, 0.14f, 1.0f };
                if (hover && !selected)
                    nodeFill = { 0.16f, 0.17f, 0.19f, 1.0f };
                UIRenderer::DrawRectFilled(r.X, r.Y, r.W, r.H, nodeFill);
                UIRenderer::DrawRectFilled(r.X, r.Y, r.W, 23.0f, { 0.16f, 0.165f, 0.18f, 1.0f });
                UIRenderer::DrawRectFilled(r.X, r.Y, 4.0f, r.H, accent);
                DrawBorder(r.X, r.Y, r.W, r.H, selected ? accent : PanelStroke, selected ? 2.0f : 1.0f);
                // 노드 이름은 저장 데이터 그대로 두고, 화면에 그릴 때만 폭에 맞춰 줄인다.
                // 긴 클립 이름이 노드 밖으로 삐져나가면 연결선과 다른 노드를 가려 편집성이 떨어진다.
                UIRenderer::DrawString(FitText(layer->States[i].Name, r.W - 88.0f), r.X + 14.0f, r.Y + 18.0f, TextStrong);
                UIRenderer::DrawString(entryState ? "ENTRY" : "STATE", r.X + r.W - 70.0f, r.Y + 18.0f, accent);
                UIRenderer::DrawString("Clip " + std::to_string(layer->States[i].ClipIndex), r.X + 14.0f, r.Y + 45.0f, TextMuted);
                UIRenderer::DrawString(layer->States[i].Loop ? "Loop" : "Once", r.X + 88.0f, r.Y + 45.0f, TextMuted);
            }

            if (m_IsBoxSelecting)
            {
                const float sx = (std::min)(m_BoxSelectStart.x, m_BoxSelectEnd.x);
                const float sy = (std::min)(m_BoxSelectStart.y, m_BoxSelectEnd.y);
                const float sw = std::abs(m_BoxSelectEnd.x - m_BoxSelectStart.x);
                const float sh = std::abs(m_BoxSelectEnd.y - m_BoxSelectStart.y);
                // 드래그 선택 영역은 실제 선택 결과를 예측하게 해 주는 편집 피드백이다.
                // 그래프 클립 안에서만 그려 다른 패널 위로 삐져나가지 않게 한다.
                UIRenderer::DrawRectFilled(sx, sy, sw, sh, { 0.28f, 0.52f, 0.90f, 0.18f });
                DrawBorder(sx, sy, sw, sh, { 0.46f, 0.68f, 1.0f, 0.95f }, 1.0f);
            }
        }

        UIRenderer::ClearClipRect();
    }

    void AnimatorGraphPanel::DrawGrid(float x, float y, float w, float h) const
    {
        UIRenderer::DrawRectFilled(x, y, w, h, CanvasColor);
        const float fineStep = 32.0f * m_Zoom;
        const float majorStep = 128.0f * m_Zoom;
        const float fineStartX = x + std::fmod(m_ViewOffsetX * m_Zoom, fineStep);
        const float fineStartY = y + std::fmod(m_ViewOffsetY * m_Zoom, fineStep);
        for (float gx = fineStartX; gx < x + w; gx += fineStep)
            UIRenderer::DrawRectFilled(gx, y, 1.0f, h, GridFine);
        for (float gy = fineStartY; gy < y + h; gy += fineStep)
            UIRenderer::DrawRectFilled(x, gy, w, 1.0f, GridFine);
        for (float gx = x + std::fmod(m_ViewOffsetX * m_Zoom, majorStep); gx < x + w; gx += majorStep)
            UIRenderer::DrawRectFilled(gx, y, 1.0f, h, GridMajor);
        for (float gy = y + std::fmod(m_ViewOffsetY * m_Zoom, majorStep); gy < y + h; gy += majorStep)
            UIRenderer::DrawRectFilled(x, gy, w, 1.0f, GridMajor);
    }

    void AnimatorGraphPanel::DrawNodeConnection(DirectX::XMFLOAT2 from, DirectX::XMFLOAT2 to, const DirectX::XMFLOAT4& color) const
    {
        // Animator Graph의 연결선은 경로 자체가 의미다.
        // 꺾은 선은 노드 배치가 복잡할 때 흐름을 헷갈리게 하므로 Unity처럼 직선 화살표로 통일한다.
        DrawLine(from, to, color, 2.0f);
        DrawArrowHead(from, to, color);
    }

    void AnimatorGraphPanel::DrawTransitionArrow(int transitionIndex, const AnimatorComponent::Transition& transition, bool selected) const
    {
        const AnimatorComponent* animator = GetAnimator();
        const auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || transition.FromStateIndex < 0 || transition.ToStateIndex < 0 ||
            !layer ||
            transition.FromStateIndex >= (int)layer->States.size() ||
            transition.ToStateIndex >= (int)layer->States.size())
            return;

        const StateNodeRect fromRect = GetStateRect(transition.FromStateIndex);
        const StateNodeRect toRect = GetStateRect(transition.ToStateIndex);
        DirectX::XMFLOAT2 from = { fromRect.X + fromRect.W, fromRect.Y + fromRect.H * 0.5f };
        DirectX::XMFLOAT2 to = { toRect.X, toRect.Y + toRect.H * 0.5f };
        if (to.x < from.x)
        {
            from = { fromRect.X + fromRect.W * 0.5f, fromRect.Y + fromRect.H };
            to = { toRect.X + toRect.W * 0.5f, toRect.Y };
        }

        const DirectX::XMFLOAT4 color = selected ? DirectX::XMFLOAT4{ 0.88f, 0.92f, 1.0f, 1.0f } : DirectX::XMFLOAT4{ 0.58f, 0.64f, 0.72f, 1.0f };
        // Animator 전이는 방향성이 중요하다. 곡선 대신 직선+화살촉으로 그려 Unity Animator처럼 흐름을 바로 읽게 한다.
        DrawLine(from, to, color, selected ? 3.0f : 2.0f);
        DrawArrowHead(from, to, color);

        if (!transition.Conditions.empty())
        {
            const float labelX = (from.x + to.x) * 0.5f - 42.0f;
            const float labelY = (from.y + to.y) * 0.5f - 11.0f;
            UIRenderer::DrawRectFilled(labelX, labelY, 84.0f, 21.0f, { 0.05f, 0.055f, 0.065f, 0.92f });
            DrawBorder(labelX, labelY, 84.0f, 21.0f, PanelStroke);
            UIRenderer::DrawString("conditions", labelX + 8.0f, labelY + 16.0f, TextMuted);
        }
    }

    void AnimatorGraphPanel::DrawTransitionInspector(float x, float y, float w, float h)
    {
        AnimatorComponent* animator = GetAnimator();
        auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer || m_SelectedTransitionIndex < 0 || m_SelectedTransitionIndex >= (int)layer->Transitions.size())
            return;

        auto& transition = layer->Transitions[m_SelectedTransitionIndex];
        if (transition.FromStateIndex < 0 || transition.ToStateIndex < 0 ||
            transition.FromStateIndex >= (int)layer->States.size() ||
            transition.ToStateIndex >= (int)layer->States.size())
            return;

        UIRenderer::DrawRectFilled(x, y, w, h, { 0.070f, 0.074f, 0.082f, 1.0f });
        DrawBorder(x, y, w, h, AccentBlue);
        UIRenderer::DrawString("Transition", x + 10.0f, y + 22.0f, TextStrong);
        UIRenderer::DrawString("From: " + layer->States[transition.FromStateIndex].Name, x + 10.0f, y + 48.0f, TextMuted);
        UIRenderer::DrawString("To: " + layer->States[transition.ToStateIndex].Name, x + 10.0f, y + 72.0f, TextMuted);
        UIRenderer::DrawString(transition.HasExitTime ? "Has Exit Time: On" : "Has Exit Time: Off", x + 10.0f, y + 98.0f, transition.HasExitTime ? AccentGreen : TextMuted);
        UIRenderer::DrawString(("Blend: " + std::to_string(transition.BlendTime)).substr(0, 11), x + 10.0f, y + 122.0f, TextMuted);

        const std::string conditionText = transition.Conditions.empty()
            ? "Condition: (none)"
            : "Condition: " + transition.Conditions[0].ParameterName + " " + ConditionModeName(transition.Conditions[0].Mode);
        UIRenderer::DrawString(conditionText, x + 10.0f, y + 146.0f, TextMuted);
    }

    void AnimatorGraphPanel::DrawContextMenu()
    {
        if (!m_IsContextMenuOpen)
            return;

        bool canCreateTransition = m_ContextMenuMode == ContextMenuMode::ReplaceState &&
            m_ContextSourceStateIndex >= 0 && m_ContextStateIndex >= 0 && m_ContextSourceStateIndex != m_ContextStateIndex;
        if (AnimatorComponent* animator = GetAnimator())
        {
            const auto* layer = GetActiveLayer(*animator);
            canCreateTransition = canCreateTransition && layer &&
                m_ContextSourceStateIndex < (int)layer->States.size() &&
                m_ContextStateIndex < (int)layer->States.size();
        }
        const float itemH = 24.0f;
        const float menuW = 230.0f;
        float menuH = 8.0f;
        if (m_ContextMenuMode == ContextMenuMode::Transition)
            menuH += 4.0f * itemH;
        else
            menuH += 2.0f * itemH + 8.0f + (canCreateTransition ? itemH : 0.0f);

        const float menuX = (std::min)(m_ContextMenuX, m_CalculatedPos.x + m_CalculatedSize.x - menuW - 4.0f);
        const float menuY = (std::min)(m_ContextMenuY, m_CalculatedPos.y + m_CalculatedSize.y - menuH - 4.0f);
        UIRenderer::DrawRectFilled(menuX, menuY, menuW, menuH, { 0.115f, 0.118f, 0.130f, 0.98f });
        DrawBorder(menuX, menuY, menuW, menuH, PanelStroke);

        if (m_ContextMenuMode == ContextMenuMode::Transition)
        {
            const char* items[] = { "Toggle Exit Time", "Add First Parameter Condition", "Cycle Condition Mode", "Delete Transition" };
            for (int i = 0; i < 4; ++i)
                UIRenderer::DrawString(items[i], menuX + 10.0f, menuY + 22.0f + (float)i * itemH, i == 3 ? DirectX::XMFLOAT4{ 0.95f, 0.58f, 0.60f, 1.0f } : TextStrong);
            return;
        }

        UIRenderer::DrawString(m_ContextMenuMode == ContextMenuMode::ReplaceState ? "State Actions" : "Graph Actions", menuX + 10.0f, menuY + 22.0f, TextMuted);
        float clipStartY = menuY + 30.0f;
        if (canCreateTransition)
        {
            if (AnimatorComponent* animator = GetAnimator())
            {
                const auto* layer = GetActiveLayer(*animator);
                const std::string label = layer ? "Create Transition from " + layer->States[m_ContextSourceStateIndex].Name : "Create Transition";
                UIRenderer::DrawString(label, menuX + 10.0f, clipStartY + 18.0f, AccentGreen);
                clipStartY += itemH;
            }
        }
        UIRenderer::DrawString(m_ContextMenuMode == ContextMenuMode::ReplaceState ? "Select Replacement Clip..." : "Select Clip For New State...", menuX + 10.0f, clipStartY + 18.0f, TextStrong);
        UIRenderer::DrawString("Clips open in picker.", menuX + 10.0f, clipStartY + itemH + 18.0f, TextMuted);
    }

    void AnimatorGraphPanel::DrawClipPicker()
    {
        if (!m_IsClipPickerOpen)
            return;

        const float pickerW = (std::min)(520.0f, m_CalculatedSize.x - 48.0f);
        const float pickerH = (std::min)(420.0f, m_CalculatedSize.y - 72.0f);
        const float pickerX = m_CalculatedPos.x + (m_CalculatedSize.x - pickerW) * 0.5f;
        const float pickerY = m_CalculatedPos.y + (m_CalculatedSize.y - pickerH) * 0.5f;

        UIRenderer::DrawRectFilled(m_CalculatedPos.x, m_CalculatedPos.y, m_CalculatedSize.x, m_CalculatedSize.y, { 0.0f, 0.0f, 0.0f, 0.38f });
        UIRenderer::DrawRectFilled(pickerX, pickerY, pickerW, pickerH, { 0.105f, 0.110f, 0.122f, 0.98f });
        DrawBorder(pickerX, pickerY, pickerW, pickerH, AccentBlue, 1.0f);
        UIRenderer::DrawRectFilled(pickerX, pickerY, pickerW, 34.0f, { 0.14f, 0.15f, 0.17f, 1.0f });
        UIRenderer::DrawString(m_ClipPickerMode == ContextMenuMode::ReplaceState ? "Select Replacement Clip" : "Select Animation Clip", pickerX + 14.0f, pickerY + 23.0f, TextStrong);
        UIRenderer::DrawRectFilled(pickerX + pickerW - 74.0f, pickerY + 7.0f, 58.0f, 21.0f, { 0.20f, 0.20f, 0.22f, 1.0f });
        UIRenderer::DrawString("Cancel", pickerX + pickerW - 64.0f, pickerY + 23.0f, TextMuted);

        if (!m_ClipPickerMessage.empty())
            UIRenderer::DrawString(m_ClipPickerMessage, pickerX + 14.0f, pickerY + 58.0f, TextMuted);

        UIRenderer::SetClipRect(pickerX + 10.0f, pickerY + 68.0f, pickerW - 20.0f, pickerH - 78.0f);
        float rowY = pickerY + 72.0f;
        if (m_ClipPickerClips.empty())
        {
            UIRenderer::DrawString("No animation clips found. Assign a model/FBX source first.", pickerX + 16.0f, rowY + 20.0f, TextMuted);
        }
        for (int i = 0; i < (int)m_ClipPickerClips.size(); ++i)
        {
            const bool hover = IsPointInRect(m_LastMouseX, m_LastMouseY, pickerX + 12.0f, rowY, pickerW - 24.0f, 30.0f);
            UIRenderer::DrawRectFilled(pickerX + 12.0f, rowY, pickerW - 24.0f, 30.0f, hover ? DirectX::XMFLOAT4{ 0.18f, 0.28f, 0.42f, 1.0f } : DirectX::XMFLOAT4{ 0.13f, 0.135f, 0.15f, 1.0f });
            UIRenderer::DrawString(std::to_string(i) + ". " + m_ClipPickerClips[i].Name, pickerX + 24.0f, rowY + 21.0f, TextStrong);
            rowY += 34.0f;
        }
        UIRenderer::ClearClipRect();
    }

    bool AnimatorGraphPanel::HandleToolbarClick(float mouseX, float mouseY)
    {
        const float y = m_CalculatedPos.y + m_TitleContentTop + 4.0f;
        float x = m_CalculatedPos.x + 8.0f;
        const float widths[] = { 92.0f, 62.0f, 58.0f, 72.0f, 56.0f, 68.0f };
        for (int i = 0; i < 6; ++i)
        {
            if (IsPointInRect(mouseX, mouseY, x, y, widths[i], 25.0f))
            {
                AnimatorComponent* animator = GetAnimator();
                if (!animator)
                    return true;
                auto* layer = GetActiveLayer(*animator);
                if (!layer)
                    return true;

                if (i == 0)
                    OpenClipPicker(ContextMenuMode::AddState, -1,
                        { 360.0f + (float)(layer->States.size() % 4) * 60.0f, 180.0f + (float)(layer->States.size() % 5) * 46.0f });
                else if (i == 1 && m_SelectedStateIndex >= 0) layer->EntryStateIndex = m_SelectedStateIndex;
                else if (i == 2 && m_SelectedStateIndex >= 0 && m_SelectedStateIndex < (int)layer->States.size())
                {
                    OpenClipPicker(ContextMenuMode::ReplaceState, m_SelectedStateIndex, layer->States[m_SelectedStateIndex].GraphPosition);
                }
                else if (i == 3)
                {
                    animator->PreviewInEdit = true;
                    animator->IsPlaying = !animator->IsPlaying;
                    if (!animator->IsPlaying)
                        animator->AnimPlayer.StopAnimation();
                }
                else if (i == 4) animator->AutoPlay = !animator->AutoPlay;
                else if (i == 5)
                {
                    if (m_SelectedTransitionIndex >= 0 && m_SelectedTransitionIndex < (int)layer->Transitions.size())
                    {
                        layer->Transitions.erase(layer->Transitions.begin() + m_SelectedTransitionIndex);
                        m_SelectedTransitionIndex = -1;
                        layer->SelectedTransitionIndex = -1;
                    }
                    else
                    {
                        DeleteSelectedState();
                    }
                }
                ClampAnimatorSelection(*animator);
                SyncBaseLayerToLegacyGraph(*animator);
                return true;
            }
            x += widths[i] + 7.0f;
        }
        return false;
    }

    bool AnimatorGraphPanel::HandleSidebarClick(float mouseX, float mouseY)
    {
        const float x = m_CalculatedPos.x;
        const float y = m_CalculatedPos.y + m_TitleContentTop + m_ToolbarHeight;
        if (!IsPointInRect(mouseX, mouseY, x, y, m_SidebarWidth, m_CalculatedSize.y - m_TitleContentTop - m_ToolbarHeight))
            return false;

        if (IsPointInRect(mouseX, mouseY, x + 10.0f, y + 8.0f, 92.0f, 26.0f))
            m_SidebarPage = SidebarPage::Layers;
        else if (IsPointInRect(mouseX, mouseY, x + 106.0f, y + 8.0f, 92.0f, 26.0f))
            m_SidebarPage = SidebarPage::Parameters;
        else if (m_SidebarPage == SidebarPage::Parameters)
        {
            if (IsPointInRect(mouseX, mouseY, x + 18.0f, y + 73.0f, 58.0f, 22.0f))
                AddParameter(AnimatorComponent::Parameter::Type::Float);
            else if (IsPointInRect(mouseX, mouseY, x + 82.0f, y + 73.0f, 52.0f, 22.0f))
                AddParameter(AnimatorComponent::Parameter::Type::Bool);
            else if (IsPointInRect(mouseX, mouseY, x + 140.0f, y + 73.0f, 82.0f, 22.0f))
                AddParameter(AnimatorComponent::Parameter::Type::Trigger);
            else if (AnimatorComponent* animator = GetAnimator())
            {
                float rowY = y + 118.0f;
                for (int i = 0; i < (int)animator->Parameters.size(); ++i)
                {
                    if (IsPointInRect(mouseX, mouseY, x + 12.0f, rowY, m_SidebarWidth - 25.0f, 27.0f))
                    {
                        BeginParameterRename(i);
                        return true;
                    }
                    rowY += 30.0f;
                }
            }
        }
        else if (AnimatorComponent* animator = GetAnimator())
        {
            EnsureAnimatorLayers(*animator);
            if (IsPointInRect(mouseX, mouseY, x + m_SidebarWidth - 42.0f, y + 45.0f, 24.0f, 22.0f))
            {
                AnimatorComponent::Layer layer;
                layer.Name = "New Layer " + std::to_string(animator->Layers.size());
                layer.Weight = 1.0f;
                layer.ActiveStateIndex = -1;
                layer.EntryStateIndex = -1;
                layer.SelectedTransitionIndex = -1;
                animator->Layers.push_back(layer);
                animator->ActiveLayerIndex = (int)animator->Layers.size() - 1;
                m_SelectedStateIndex = -1;
                m_SelectedTransitionIndex = -1;
                m_SelectedStateIndices.clear();
                return true;
            }

            float layerY = y + 74.0f;
            for (int i = 0; i < (int)animator->Layers.size(); ++i)
            {
                if (IsPointInRect(mouseX, mouseY, x + 12.0f, layerY, m_SidebarWidth - 25.0f, 27.0f))
                {
                    animator->ActiveLayerIndex = i;
                    auto* selectedLayer = GetActiveLayer(*animator);
                    m_SelectedStateIndex = selectedLayer ? selectedLayer->ActiveStateIndex : -1;
                    m_SelectedTransitionIndex = selectedLayer ? selectedLayer->SelectedTransitionIndex : -1;
                    m_SelectedStateIndices.clear();
                    if (m_SelectedStateIndex >= 0)
                        m_SelectedStateIndices.push_back(m_SelectedStateIndex);
                    return true;
                }
                layerY += 30.0f;
            }

            auto& activeLayer = animator->Layers[animator->ActiveLayerIndex];
            const float settingsY = layerY + 8.0f;
            if (IsPointInRect(mouseX, mouseY, x + m_SidebarWidth - 72.0f, settingsY + 32.0f, 24.0f, 20.0f))
            {
                activeLayer.Weight = std::clamp(activeLayer.Weight - 0.1f, 0.0f, 1.0f);
                return true;
            }
            if (IsPointInRect(mouseX, mouseY, x + m_SidebarWidth - 44.0f, settingsY + 32.0f, 24.0f, 20.0f))
            {
                activeLayer.Weight = std::clamp(activeLayer.Weight + 0.1f, 0.0f, 1.0f);
                return true;
            }
            if (IsPointInRect(mouseX, mouseY, x + 10.0f, settingsY + 54.0f, m_SidebarWidth - 22.0f, 24.0f))
            {
                activeLayer.Blending = activeLayer.Blending == AnimatorComponent::Layer::BlendMode::Override
                    ? AnimatorComponent::Layer::BlendMode::Additive
                    : AnimatorComponent::Layer::BlendMode::Override;
                return true;
            }
            if (IsPointInRect(mouseX, mouseY, x + 10.0f, settingsY + 80.0f, m_SidebarWidth - 22.0f, 24.0f))
            {
                activeLayer.IKPass = !activeLayer.IKPass;
                return true;
            }

            if (m_SelectedTransitionIndex >= 0 && m_SelectedTransitionIndex < (int)activeLayer.Transitions.size())
            {
                const float propY = y + (m_CalculatedSize.y - m_TitleContentTop - m_ToolbarHeight) - 170.0f;
                auto& transition = activeLayer.Transitions[m_SelectedTransitionIndex];
                if (IsPointInRect(mouseX, mouseY, x + 10.0f, propY + 76.0f, m_SidebarWidth - 22.0f, 24.0f))
                {
                    transition.HasExitTime = !transition.HasExitTime;
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, x + 10.0f, propY + 100.0f, 80.0f, 24.0f))
                {
                    transition.BlendTime = (std::max)(0.0f, transition.BlendTime - 0.05f);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, x + 92.0f, propY + 100.0f, 80.0f, 24.0f))
                {
                    transition.BlendTime += 0.05f;
                    return true;
                }
            }

            if (m_SidebarPage == SidebarPage::Layers && m_SelectedStateIndex >= 0 && m_SelectedStateIndex < (int)activeLayer.States.size())
            {
                const float propY = y + (m_CalculatedSize.y - m_TitleContentTop - m_ToolbarHeight) - 150.0f;
                auto& state = activeLayer.States[m_SelectedStateIndex];
                if (IsPointInRect(mouseX, mouseY, x + 10.0f, propY + 58.0f, m_SidebarWidth - 22.0f, 24.0f))
                {
                    state.Loop = !state.Loop;
                    animator->Loop = state.Loop;
                    SyncBaseLayerToLegacyGraph(*animator);
                    ResetRuntime(*animator);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, x + 10.0f, propY + 84.0f, m_SidebarWidth - 22.0f, 24.0f))
                {
                    state.WriteDefaults = !state.WriteDefaults;
                    animator->AnimPlayer.SetWriteDefaults(state.WriteDefaults);
                    SyncBaseLayerToLegacyGraph(*animator);
                    ResetRuntime(*animator);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, x + m_SidebarWidth - 72.0f, propY + 112.0f, 24.0f, 20.0f))
                {
                    state.Speed = (std::max)(0.0f, state.Speed - 0.1f);
                    animator->Speed = state.Speed;
                    SyncBaseLayerToLegacyGraph(*animator);
                    ResetRuntime(*animator);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, x + m_SidebarWidth - 44.0f, propY + 112.0f, 24.0f, 20.0f))
                {
                    state.Speed += 0.1f;
                    animator->Speed = state.Speed;
                    SyncBaseLayerToLegacyGraph(*animator);
                    ResetRuntime(*animator);
                    return true;
                }
            }

            float rowY = settingsY + 138.0f;
            for (int i = 0; i < (int)activeLayer.States.size(); ++i)
            {
                if (IsPointInRect(mouseX, mouseY, x + 12.0f, rowY, m_SidebarWidth - 25.0f, 27.0f))
                {
                    SelectOnlyState(*animator, i);
                    SyncBaseLayerToLegacyGraph(*animator);
                    ResetRuntime(*animator);
                    break;
                }
                rowY += 30.0f;
            }
        }

        return true;
    }

    bool AnimatorGraphPanel::HandleContextMenuClick(float mouseX, float mouseY)
    {
        if (!m_IsContextMenuOpen)
            return false;

        const auto* animator = GetAnimator();
        const auto* readLayer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !readLayer)
            return false;

        const bool canCreateTransition = m_ContextMenuMode == ContextMenuMode::ReplaceState &&
            m_ContextSourceStateIndex >= 0 && m_ContextStateIndex >= 0 && m_ContextSourceStateIndex != m_ContextStateIndex &&
            m_ContextSourceStateIndex < (int)readLayer->States.size() &&
            m_ContextStateIndex < (int)readLayer->States.size();
        const float itemH = 24.0f;
        const float menuW = 230.0f;
        float menuH = 8.0f;
        if (m_ContextMenuMode == ContextMenuMode::Transition)
            menuH += 4.0f * itemH;
        else
            menuH += 2.0f * itemH + 8.0f + (canCreateTransition ? itemH : 0.0f);

        const float menuX = (std::min)(m_ContextMenuX, m_CalculatedPos.x + m_CalculatedSize.x - menuW - 4.0f);
        const float menuY = (std::min)(m_ContextMenuY, m_CalculatedPos.y + m_CalculatedSize.y - menuH - 4.0f);
        if (!IsPointInRect(mouseX, mouseY, menuX, menuY, menuW, menuH))
            return false;

        AnimatorComponent* mutableAnimator = GetAnimator();
        if (!mutableAnimator)
            return false;
        auto* layer = GetActiveLayer(*mutableAnimator);
        if (!layer)
            return false;

        if (m_ContextMenuMode == ContextMenuMode::Transition)
        {
            if (m_ContextTransitionIndex >= 0 && m_ContextTransitionIndex < (int)layer->Transitions.size())
            {
                auto& transition = layer->Transitions[m_ContextTransitionIndex];
                const int item = (int)((mouseY - menuY - 4.0f) / itemH);
                if (item == 0)
                    transition.HasExitTime = !transition.HasExitTime;
                else if (item == 1 && !mutableAnimator->Parameters.empty())
                {
                    AnimatorComponent::TransitionCondition condition;
                    condition.ParameterName = mutableAnimator->Parameters.front().Name;
                    condition.Mode = mutableAnimator->Parameters.front().ParamType == AnimatorComponent::Parameter::Type::Float
                        ? AnimatorComponent::TransitionCondition::CompareMode::Greater
                        : AnimatorComponent::TransitionCondition::CompareMode::If;
                    transition.Conditions.push_back(condition);
                }
                else if (item == 2 && !transition.Conditions.empty())
                {
                    auto& mode = transition.Conditions.front().Mode;
                    int next = (static_cast<int>(mode) + 1) % 6;
                    mode = static_cast<AnimatorComponent::TransitionCondition::CompareMode>(next);
                }
                else if (item == 3)
                {
                    layer->Transitions.erase(layer->Transitions.begin() + m_ContextTransitionIndex);
                    m_SelectedTransitionIndex = -1;
                    layer->SelectedTransitionIndex = -1;
                }
                SyncBaseLayerToLegacyGraph(*mutableAnimator);
            }
            CloseContextMenu();
            return true;
        }

        if (canCreateTransition && IsPointInRect(mouseX, mouseY, menuX, menuY + 30.0f, menuW, itemH))
        {
            AddTransition(m_ContextSourceStateIndex, m_ContextStateIndex);
            CloseContextMenu();
            return true;
        }

        const float clipStartY = menuY + 30.0f + (canCreateTransition ? itemH : 0.0f);
        if (IsPointInRect(mouseX, mouseY, menuX, clipStartY, menuW, itemH))
            OpenClipPicker(m_ContextMenuMode, m_ContextStateIndex, m_ContextGraphPosition);
        CloseContextMenu();
        return true;
    }

    bool AnimatorGraphPanel::HandleClipPickerClick(float mouseX, float mouseY)
    {
        if (!m_IsClipPickerOpen)
            return false;

        const float pickerW = (std::min)(520.0f, m_CalculatedSize.x - 48.0f);
        const float pickerH = (std::min)(420.0f, m_CalculatedSize.y - 72.0f);
        const float pickerX = m_CalculatedPos.x + (m_CalculatedSize.x - pickerW) * 0.5f;
        const float pickerY = m_CalculatedPos.y + (m_CalculatedSize.y - pickerH) * 0.5f;

        if (IsPointInRect(mouseX, mouseY, pickerX + pickerW - 74.0f, pickerY + 7.0f, 58.0f, 21.0f) ||
            !IsPointInRect(mouseX, mouseY, pickerX, pickerY, pickerW, pickerH))
        {
            CloseClipPicker();
            return true;
        }

        const float listY = pickerY + 72.0f;
        const int clipIndex = (int)((mouseY - listY) / 34.0f);
        if (clipIndex >= 0 && clipIndex < (int)m_ClipPickerClips.size())
        {
            if (m_ClipPickerMode == ContextMenuMode::ReplaceState)
                ReplaceStateClip(m_ClipPickerStateIndex, clipIndex, m_ClipPickerClips[clipIndex].Name);
            else
                AddStateFromClipIndex(clipIndex, m_ClipPickerClips[clipIndex].Name, m_ClipPickerGraphPosition);
            CloseClipPicker();
        }

        return true;
    }

    void AnimatorGraphPanel::OpenContextMenu(ContextMenuMode mode, float mouseX, float mouseY, int stateIndex, int transitionIndex)
    {
        if (mode != ContextMenuMode::ReplaceState)
            m_ContextSourceStateIndex = -1;
        m_IsContextMenuOpen = true;
        m_ContextMenuMode = mode;
        m_ContextMenuX = mouseX;
        m_ContextMenuY = mouseY;
        m_ContextStateIndex = stateIndex;
        m_ContextTransitionIndex = transitionIndex;
        m_ContextGraphPosition = ScreenToGraph(mouseX, mouseY);
        m_ContextClips.clear();
        if (mode == ContextMenuMode::AddState || mode == ContextMenuMode::ReplaceState)
        {
            // 실제 클립 목록은 선택 브라우저를 열 때만 읽는다.
            // 우클릭 메뉴는 가벼운 명령 목록만 담당해서 빈 Animator에서도 클릭이 먹통처럼 보이지 않게 한다.
        }
    }

    void AnimatorGraphPanel::CloseContextMenu()
    {
        m_IsContextMenuOpen = false;
        m_ContextMenuMode = ContextMenuMode::None;
        m_ContextStateIndex = -1;
        m_ContextSourceStateIndex = -1;
        m_ContextTransitionIndex = -1;
        m_ContextClips.clear();
    }

    void AnimatorGraphPanel::OpenClipPicker(ContextMenuMode mode, int stateIndex, const DirectX::XMFLOAT2& graphPosition)
    {
        AnimatorComponent* animator = GetAnimator();
        if (!animator)
            return;

        m_IsClipPickerOpen = true;
        m_ClipPickerMode = mode;
        m_ClipPickerStateIndex = stateIndex;
        m_ClipPickerGraphPosition = graphPosition;
        m_ClipPickerClips = InspectSourceClips(*animator);
        m_ClipPickerMessage.clear();
        if (m_ClipPickerClips.empty())
            m_ClipPickerMessage = "Animator has no usable clip source.";
        Widget::SetKeyboardFocus(this);
    }

    void AnimatorGraphPanel::CloseClipPicker()
    {
        m_IsClipPickerOpen = false;
        m_ClipPickerMode = ContextMenuMode::None;
        m_ClipPickerStateIndex = -1;
        m_ClipPickerClips.clear();
        m_ClipPickerMessage.clear();
    }

    void AnimatorGraphPanel::AddStateFromSelectedClip()
    {
        AnimatorComponent* animator = GetAnimator();
        if (!animator)
            return;
        auto* layer = GetActiveLayer(*animator);
        if (!layer)
            return;

        AddStateFromClipIndex((std::max)(0, animator->SelectedClipIndex), animator->SelectedClipName,
            { 360.0f + (float)(layer->States.size() % 4) * 60.0f, 180.0f + (float)(layer->States.size() % 5) * 46.0f });
    }

    void AnimatorGraphPanel::AddStateFromClipIndex(int clipIndex, const std::string& clipName, const DirectX::XMFLOAT2& graphPosition)
    {
        AnimatorComponent* animator = GetAnimator();
        if (!animator)
            return;
        auto* layer = GetActiveLayer(*animator);
        if (!layer)
            return;

        AnimatorComponent::State state;
        state.Name = clipName.empty() ? ("State " + std::to_string(layer->States.size() + 1)) : clipName;
        state.ClipIndex = (std::max)(0, clipIndex);
        state.GraphPosition = graphPosition;
        layer->States.push_back(state);
        if (layer->EntryStateIndex < 0)
            layer->EntryStateIndex = 0;
        layer->ActiveStateIndex = (int)layer->States.size() - 1;
        m_SelectedStateIndex = layer->ActiveStateIndex;
        m_SelectedTransitionIndex = -1;
        layer->SelectedTransitionIndex = -1;
        m_SelectedStateIndices.clear();
        m_SelectedStateIndices.push_back(m_SelectedStateIndex);
        SyncBaseLayerToLegacyGraph(*animator);
        ResetRuntime(*animator);
    }

    void AnimatorGraphPanel::ReplaceStateClip(int stateIndex, int clipIndex, const std::string& clipName)
    {
        AnimatorComponent* animator = GetAnimator();
        auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer || stateIndex < 0 || stateIndex >= (int)layer->States.size())
            return;

        auto& state = layer->States[stateIndex];
        state.ClipIndex = (std::max)(0, clipIndex);
        if (!clipName.empty())
            state.Name = clipName;
        animator->SelectedClipIndex = state.ClipIndex;
        animator->SelectedClipName = clipName;
        SelectOnlyState(*animator, stateIndex);
        SyncBaseLayerToLegacyGraph(*animator);
        ResetRuntime(*animator);
    }

    void AnimatorGraphPanel::AddTransition(int fromStateIndex, int toStateIndex)
    {
        AnimatorComponent* animator = GetAnimator();
        auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || fromStateIndex < 0 || toStateIndex < 0 || fromStateIndex == toStateIndex ||
            !layer || fromStateIndex >= (int)layer->States.size() || toStateIndex >= (int)layer->States.size())
            return;

        auto exists = std::find_if(layer->Transitions.begin(), layer->Transitions.end(), [fromStateIndex, toStateIndex](const AnimatorComponent::Transition& transition)
        {
            return transition.FromStateIndex == fromStateIndex && transition.ToStateIndex == toStateIndex;
        });
        if (exists != layer->Transitions.end())
            return;

        AnimatorComponent::Transition transition;
        transition.FromStateIndex = fromStateIndex;
        transition.ToStateIndex = toStateIndex;
        layer->Transitions.push_back(transition);
        m_SelectedTransitionIndex = (int)layer->Transitions.size() - 1;
        layer->SelectedTransitionIndex = m_SelectedTransitionIndex;
        m_SelectedStateIndices.clear();
        SyncBaseLayerToLegacyGraph(*animator);
    }

    void AnimatorGraphPanel::DeleteSelectedState()
    {
        AnimatorComponent* animator = GetAnimator();
        auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer || m_SelectedStateIndex < 0 || m_SelectedStateIndex >= (int)layer->States.size())
            return;

        std::vector<int> removedIndices = m_SelectedStateIndices;
        if (removedIndices.empty() || !IsStateSelected(m_SelectedStateIndex))
            removedIndices.push_back(m_SelectedStateIndex);
        removedIndices.erase(
            std::remove_if(removedIndices.begin(), removedIndices.end(), [layer](int index)
            {
                return index < 0 || index >= (int)layer->States.size();
            }),
            removedIndices.end());
        std::sort(removedIndices.begin(), removedIndices.end());
        removedIndices.erase(std::unique(removedIndices.begin(), removedIndices.end()), removedIndices.end());
        if (removedIndices.empty())
            return;

        auto isRemoved = [&removedIndices](int index)
        {
            return std::binary_search(removedIndices.begin(), removedIndices.end(), index);
        };

        layer->Transitions.erase(
            std::remove_if(layer->Transitions.begin(), layer->Transitions.end(), [&isRemoved](const AnimatorComponent::Transition& transition)
            {
                return isRemoved(transition.FromStateIndex) || isRemoved(transition.ToStateIndex);
            }),
            layer->Transitions.end());

        for (auto it = removedIndices.rbegin(); it != removedIndices.rend(); ++it)
            layer->States.erase(layer->States.begin() + *it);

        for (auto& transition : layer->Transitions)
        {
            // 상태를 여러 개 지우면 뒤쪽 인덱스가 앞으로 당겨진다.
            // 전이는 상태 번호만 들고 있으므로, 삭제된 상태보다 앞에 남은 개수만큼 다시 보정한다.
            const int fromShift = (int)std::count_if(removedIndices.begin(), removedIndices.end(), [&transition](int removed) { return removed < transition.FromStateIndex; });
            const int toShift = (int)std::count_if(removedIndices.begin(), removedIndices.end(), [&transition](int removed) { return removed < transition.ToStateIndex; });
            transition.FromStateIndex -= fromShift;
            transition.ToStateIndex -= toShift;
        }
        m_SelectedTransitionIndex = -1;
        layer->SelectedTransitionIndex = -1;
        m_SelectedStateIndices.clear();

        if (layer->States.empty())
        {
            layer->ActiveStateIndex = -1;
            layer->EntryStateIndex = -1;
            m_SelectedStateIndex = -1;
        }
        else
        {
            layer->ActiveStateIndex = std::clamp(m_SelectedStateIndex, 0, (int)layer->States.size() - 1);
            layer->EntryStateIndex = std::clamp(layer->EntryStateIndex, 0, (int)layer->States.size() - 1);
            m_SelectedStateIndex = layer->ActiveStateIndex;
            m_SelectedStateIndices.push_back(m_SelectedStateIndex);
        }
        SyncBaseLayerToLegacyGraph(*animator);
        ResetRuntime(*animator);
    }

    void AnimatorGraphPanel::SelectOnlyState(AnimatorComponent& animator, int stateIndex)
    {
        auto* layer = GetActiveLayer(animator);
        if (!layer || stateIndex < 0 || stateIndex >= (int)layer->States.size())
        {
            ClearStateSelection(animator);
            return;
        }

        m_SelectedStateIndices.clear();
        m_SelectedStateIndices.push_back(stateIndex);
        m_SelectedStateIndex = stateIndex;
        m_SelectedTransitionIndex = -1;
        layer->ActiveStateIndex = stateIndex;
        layer->SelectedTransitionIndex = -1;
        SyncBaseLayerToLegacyGraph(animator);
    }

    void AnimatorGraphPanel::ClearStateSelection(AnimatorComponent& animator)
    {
        auto* layer = GetActiveLayer(animator);
        m_SelectedStateIndices.clear();
        m_SelectedStateIndex = -1;
        m_SelectedTransitionIndex = -1;
        if (layer)
            layer->SelectedTransitionIndex = -1;
        SyncBaseLayerToLegacyGraph(animator);
    }

    void AnimatorGraphPanel::SelectStatesInBox(AnimatorComponent& animator)
    {
        auto* layer = GetActiveLayer(animator);
        if (!layer)
            return;

        const float minX = (std::min)(m_BoxSelectStart.x, m_BoxSelectEnd.x);
        const float minY = (std::min)(m_BoxSelectStart.y, m_BoxSelectEnd.y);
        const float maxX = (std::max)(m_BoxSelectStart.x, m_BoxSelectEnd.x);
        const float maxY = (std::max)(m_BoxSelectStart.y, m_BoxSelectEnd.y);

        m_SelectedStateIndices.clear();
        for (int i = 0; i < (int)layer->States.size(); ++i)
        {
            const StateNodeRect r = GetStateRect(i);
            const bool intersects = r.X <= maxX && r.X + r.W >= minX && r.Y <= maxY && r.Y + r.H >= minY;
            if (intersects)
                m_SelectedStateIndices.push_back(i);
        }

        if (m_SelectedStateIndices.empty())
        {
            // 선택이 비어도 재생용 ActiveStateIndex는 유지한다.
            // 빈 공간을 클릭했을 뿐인데 런타임 상태까지 사라지면 Preview/Play가 멈춘다.
            m_SelectedStateIndex = -1;
        }
        else
        {
            // 다중 선택에서도 대표 상태 하나는 유지한다.
            // Inspector/사이드바는 대표 상태를 보여 주고, 그래프는 선택 목록 전체를 강조한다.
            m_SelectedStateIndex = m_SelectedStateIndices.back();
            layer->ActiveStateIndex = m_SelectedStateIndex;
        }
        layer->SelectedTransitionIndex = -1;
        m_SelectedTransitionIndex = -1;
        SyncBaseLayerToLegacyGraph(animator);
    }

    bool AnimatorGraphPanel::IsStateSelected(int stateIndex) const
    {
        return std::find(m_SelectedStateIndices.begin(), m_SelectedStateIndices.end(), stateIndex) != m_SelectedStateIndices.end();
    }

    void AnimatorGraphPanel::AddParameter(AnimatorComponent::Parameter::Type type)
    {
        AnimatorComponent* animator = GetAnimator();
        if (!animator)
            return;

        AnimatorComponent::Parameter parameter;
        parameter.ParamType = type;
        parameter.Name = std::string(ParameterTypeName(type)) + " " + std::to_string(animator->Parameters.size() + 1);
        animator->Parameters.push_back(parameter);
    }

    bool AnimatorGraphPanel::BeginParameterRename(int parameterIndex)
    {
        AnimatorComponent* animator = GetAnimator();
        if (!animator || parameterIndex < 0 || parameterIndex >= (int)animator->Parameters.size())
            return false;

        m_EditingParameterIndex = parameterIndex;
        m_ParameterEditBuffer = animator->Parameters[parameterIndex].Name;
        Widget::SetKeyboardFocus(this);
        return true;
    }

    bool AnimatorGraphPanel::CommitParameterRename()
    {
        AnimatorComponent* animator = GetAnimator();
        if (!animator || m_EditingParameterIndex < 0 || m_EditingParameterIndex >= (int)animator->Parameters.size())
            return false;

        const std::string newName = TrimName(m_ParameterEditBuffer);
        if (!IsValidParameterName(*animator, newName, m_EditingParameterIndex))
            return false;

        const std::string oldName = animator->Parameters[m_EditingParameterIndex].Name;
        animator->Parameters[m_EditingParameterIndex].Name = newName;
        RenameTransitionParameterReferences(*animator, oldName, newName);
        CancelParameterRename();
        return true;
    }

    void AnimatorGraphPanel::CancelParameterRename()
    {
        m_EditingParameterIndex = -1;
        m_ParameterEditBuffer.clear();
    }

    bool AnimatorGraphPanel::IsValidParameterName(const AnimatorComponent& animator, const std::string& name, int editingIndex) const
    {
        if (name.empty() || name == "Entry" || name == "Exit" || name == "Any State")
            return false;
        for (int i = 0; i < (int)animator.Parameters.size(); ++i)
        {
            if (i != editingIndex && animator.Parameters[i].Name == name)
                return false;
        }
        return true;
    }

    void AnimatorGraphPanel::RenameTransitionParameterReferences(AnimatorComponent& animator, const std::string& oldName, const std::string& newName) const
    {
        // 조건은 파라미터 이름을 직접 들고 있다. 이름을 바꾸면 Transition 조건도 함께 바꿔야 저장 후 깨지지 않는다.
        for (auto& transition : animator.Transitions)
        {
            for (auto& condition : transition.Conditions)
            {
                if (condition.ParameterName == oldName)
                    condition.ParameterName = newName;
            }
        }
        for (auto& layer : animator.Layers)
        {
            for (auto& transition : layer.Transitions)
            {
                for (auto& condition : transition.Conditions)
                {
                    if (condition.ParameterName == oldName)
                        condition.ParameterName = newName;
                }
            }
        }
    }

    void AnimatorGraphPanel::ClampAnimatorSelection(AnimatorComponent& animator) const
    {
        EnsureAnimatorLayers(animator);
        SyncBaseLayerToLegacyGraph(animator);
    }

    void AnimatorGraphPanel::ResetRuntime(AnimatorComponent& animator) const
    {
        animator.RuntimeClip.reset();
        animator.RuntimeClipKey.clear();
        animator.AnimPlayer.StopAnimation();
        animator.IsPlaying = false;
    }

    AnimatorGraphPanel::StateNodeRect AnimatorGraphPanel::GetStateRect(int stateIndex) const
    {
        const AnimatorComponent* animator = GetAnimator();
        const auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer || stateIndex < 0 || stateIndex >= (int)layer->States.size())
            return {};

        DirectX::XMFLOAT2 screen = GraphToScreen(layer->States[stateIndex].GraphPosition);
        return { stateIndex, screen.x, screen.y, m_NodeWidth * m_Zoom, m_NodeHeight * m_Zoom };
    }

    int AnimatorGraphPanel::GetStateAt(float mouseX, float mouseY) const
    {
        const AnimatorComponent* animator = GetAnimator();
        const auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer)
            return -1;

        for (int i = (int)layer->States.size() - 1; i >= 0; --i)
        {
            StateNodeRect r = GetStateRect(i);
            if (IsPointInRect(mouseX, mouseY, r.X, r.Y, r.W, r.H))
                return i;
        }
        return -1;
    }

    int AnimatorGraphPanel::GetTransitionAt(float mouseX, float mouseY) const
    {
        const AnimatorComponent* animator = GetAnimator();
        const auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer)
            return -1;

        for (int i = (int)layer->Transitions.size() - 1; i >= 0; --i)
        {
            const auto& transition = layer->Transitions[i];
            if (transition.FromStateIndex < 0 || transition.ToStateIndex < 0 ||
                transition.FromStateIndex >= (int)layer->States.size() ||
                transition.ToStateIndex >= (int)layer->States.size())
                continue;

            const StateNodeRect fromRect = GetStateRect(transition.FromStateIndex);
            const StateNodeRect toRect = GetStateRect(transition.ToStateIndex);
            DirectX::XMFLOAT2 from = { fromRect.X + fromRect.W, fromRect.Y + fromRect.H * 0.5f };
            DirectX::XMFLOAT2 to = { toRect.X, toRect.Y + toRect.H * 0.5f };
            if (to.x < from.x)
            {
                from = { fromRect.X + fromRect.W * 0.5f, fromRect.Y + fromRect.H };
                to = { toRect.X + toRect.W * 0.5f, toRect.Y };
            }
            if (DistancePointToSegment(mouseX, mouseY, from, to) <= 8.0f)
                return i;
        }
        return -1;
    }

    std::vector<AnimationClipInfo> AnimatorGraphPanel::InspectSourceClips(const AnimatorComponent& animator) const
    {
        std::filesystem::path sourcePath = animator.SourcePath;
        if (!animator.SourceAssetGuid.empty())
        {
            std::filesystem::path guidPath = AssetDatabase::GetPathFromGuid(animator.SourceAssetGuid);
            if (!guidPath.empty())
                sourcePath = guidPath;
        }

        return sourcePath.empty() ? std::vector<AnimationClipInfo>{} : AnimationClip::InspectClips(sourcePath.string());
    }

    DirectX::XMFLOAT2 AnimatorGraphPanel::GraphToScreen(const DirectX::XMFLOAT2& graphPosition) const
    {
        const float graphX = m_CalculatedPos.x + m_SidebarWidth;
        const float graphY = m_CalculatedPos.y + m_TitleContentTop + m_ToolbarHeight;
        return { graphX + (graphPosition.x + m_ViewOffsetX) * m_Zoom, graphY + (graphPosition.y + m_ViewOffsetY) * m_Zoom };
    }

    DirectX::XMFLOAT2 AnimatorGraphPanel::ScreenToGraph(float screenX, float screenY) const
    {
        const float graphX = m_CalculatedPos.x + m_SidebarWidth;
        const float graphY = m_CalculatedPos.y + m_TitleContentTop + m_ToolbarHeight;
        return { (screenX - graphX) / m_Zoom - m_ViewOffsetX, (screenY - graphY) / m_Zoom - m_ViewOffsetY };
    }

    bool AnimatorGraphPanel::IsPointInRect(float mouseX, float mouseY, float x, float y, float w, float h)
    {
        return mouseX >= x && mouseX <= x + w && mouseY >= y && mouseY <= y + h;
    }
}
