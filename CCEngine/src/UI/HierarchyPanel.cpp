#include "HierarchyPanel.h"
#include "Scene/Components.h"
#include "UI/HierarchyItem.h"
#include "Renderer/UIRenderer.h"
#include "Renderer/Font.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Renderer2D.h"
#include "Application.h"
#include <algorithm>
#include <vector>
#include <windows.h>

namespace CCEngine {
    namespace UI {

        HierarchyPanel::HierarchyPanel(const std::string& name)
            : UI::WindowPanel(name, "Scene Hierarchy")
        {
            SetSize(300.0f, 700.0f);
            SetClipToBounds(true);
        }


        void HierarchyPanel::ProcessDeferredUIOps()
        {
            for (auto& op : m_DeferredUIOps) { if (op.Op) op.Op(); }
            m_DeferredUIOps.clear();
        }

        void HierarchyPanel::SetContext(Scene* context)
        {
            m_Context = context;
            ClearSelection();
            m_NeedsRefresh = true;
        }

        void HierarchyPanel::SetSelectedEntity(Entity entity)
        {
            m_SelectedEntities.clear();
            m_SelectedEntitySet.clear();

            if (entity && m_Context && entity.GetScene() == m_Context &&
                m_Context->GetRegistry().valid((entt::entity)entity))
            {
                entt::entity handle = (entt::entity)entity;
                m_SelectedEntities.push_back(handle);
                m_SelectedEntitySet.insert(handle);
                m_SelectionContext = entity;
                m_SelectionAnchor = handle;
            }
            else
            {
                m_SelectionContext = {};
                m_SelectionAnchor = entt::null;
            }

            m_NeedsSelectionUpdate = true;
        }

        void HierarchyPanel::SetSelectedEntities(const std::vector<Entity>& entities, Entity activeEntity)
        {
            m_SelectedEntities.clear();
            m_SelectedEntitySet.clear();

            for (Entity entity : entities)
            {
                if (!entity || !m_Context || entity.GetScene() != m_Context)
                    continue;

                entt::entity handle = (entt::entity)entity;
                if (!m_Context->GetRegistry().valid(handle))
                    continue;

                if (m_SelectedEntitySet.insert(handle).second)
                    m_SelectedEntities.push_back(handle);
            }

            if (activeEntity && m_SelectedEntitySet.find((entt::entity)activeEntity) != m_SelectedEntitySet.end())
                m_SelectionContext = activeEntity;
            else if (!m_SelectedEntities.empty())
                m_SelectionContext = Entity{ m_SelectedEntities.back(), m_Context };
            else
                m_SelectionContext = {};

            m_SelectionAnchor = m_SelectionContext ? (entt::entity)m_SelectionContext : entt::null;
            m_NeedsSelectionUpdate = true;
        }

        void HierarchyPanel::ClearSelection()
        {
            m_SelectionContext = {};
            m_SelectedEntities.clear();
            m_SelectedEntitySet.clear();
            m_SelectionAnchor = entt::null;
            m_NeedsSelectionUpdate = true;
        }

        std::vector<Entity> HierarchyPanel::GetSelectedEntities() const
        {
            std::vector<Entity> result;
            if (!m_Context)
                return result;

            result.reserve(m_SelectedEntities.size());
            for (entt::entity handle : m_SelectedEntities)
            {
                if (handle != entt::null && m_Context->GetRegistry().valid(handle))
                    result.emplace_back(handle, m_Context);
            }
            return result;
        }

        bool HierarchyPanel::IsSelected(Entity entity) const
        {
            if (!entity)
                return false;

            return m_SelectedEntitySet.find((entt::entity)entity) != m_SelectedEntitySet.end();
        }

        Entity HierarchyPanel::GetEntityAt(float mouseX, float mouseY) const
        {
            if (!m_Context)
                return {};

            return FindEntityAtRecursive(const_cast<HierarchyPanel*>(this), mouseX, mouseY);
        }

        Entity HierarchyPanel::FindEntityAtRecursive(Widget* widget, float mouseX, float mouseY) const
        {
            if (!widget || !widget->IsVisible())
                return {};

            const auto& children = widget->GetChildren();
            for (auto it = children.rbegin(); it != children.rend(); ++it)
            {
                if (Entity childHit = FindEntityAtRecursive(*it, mouseX, mouseY))
                    return childHit;
            }

            auto item = dynamic_cast<UI::HierarchyItem*>(widget);
            if (!item)
                return {};

            float headerHeight = UIRenderer::GetDefaultFont() ? UIRenderer::GetDefaultFont()->GetFontSize() : 24.0f;
            headerHeight += 8.0f;
            auto pos = item->GetCalculatedPosition();
            auto size = item->GetCalculatedSize();
            bool insideHeader = mouseX >= pos.x && mouseX <= pos.x + size.x &&
                mouseY >= pos.y && mouseY <= pos.y + headerHeight;
            if (!insideHeader)
                return {};

            entt::entity entityID = (entt::entity)item->GetEntityID();
            if (entityID == entt::null || !m_Context->GetRegistry().valid(entityID))
                return {};

            return Entity{ entityID, m_Context };
        }

        void HierarchyPanel::UpdateSelectionVisuals(Widget* widget)
        {
            for (auto child : widget->GetChildren())
            {
                auto item = dynamic_cast<UI::HierarchyItem*>(child);
                if (item)
                {
                    bool isMatch = m_SelectedEntitySet.find((entt::entity)item->GetEntityID()) != m_SelectedEntitySet.end();
                    item->SetSelected(isMatch);
                    UpdateSelectionVisuals(item);
                }
            }
        }

        void HierarchyPanel::UpdateActiveVisuals(Widget* widget)
        {
            if (!widget || !m_Context)
                return;

            for (auto child : widget->GetChildren())
            {
                auto item = dynamic_cast<UI::HierarchyItem*>(child);
                if (item)
                {
                    entt::entity handle = (entt::entity)item->GetEntityID();
                    bool activeInHierarchy = true;
                    if (handle != entt::null && m_Context->GetRegistry().valid(handle))
                    {
                        // 부모가 꺼진 경우 자식도 함께 비활성처럼 보여야 한다.
                        // 저장되는 값은 ActiveSelf이고, 화면 표시는 부모 체인까지 포함한 결과를 쓴다.
                        activeInHierarchy = m_Context->IsEntityActiveInHierarchy(Entity{ handle, m_Context });
                    }
                    item->SetActiveInHierarchy(activeInHierarchy);
                    UpdateActiveVisuals(item);
                }
            }
        }

        void HierarchyPanel::Refresh()
        {
            if (!m_Context) return;
            SanitizeSelection();
            ClearChildren();

            auto view = m_Context->GetRegistry().view<TagComponent>();
            for (auto entityID : view)
            {
                Entity entity{ entityID, m_Context };
                bool isRoot = true;
                if (entity.HasComponent<RelationshipComponent>())
                {
                    if (entity.GetComponent<RelationshipComponent>().Parent != entt::null)
                        isRoot = false;
                }
                if (isRoot) BuildEntityTree(entity, 0, this);
            }
        }

        void HierarchyPanel::UpdateLayout(const DirectX::XMFLOAT2& parentPos, const DirectX::XMFLOAT2& parentSize)
        {
            if (!m_IsVisible) return;

            WindowPanel::UpdateLayout(parentPos, parentSize);

            if (m_Context)
            {
                if (m_NeedsRefresh) { Refresh(); m_NeedsRefresh = false; }
                if (m_NeedsSelectionUpdate) { UpdateSelectionVisuals(this); m_NeedsSelectionUpdate = false; }

                float localStartX = 5.0f;
                float localCurrentY = m_startYOffset - m_ScrollState.ScrollY;
                float itemWidth = m_CalculatedSize.x - 35.0f;
                if (itemWidth < 10.0f) itemWidth = 10.0f;

                for (auto child : m_Children)
                {
                    if (child->IsVisible())
                    {
                        child->SetPosition(localStartX, localCurrentY);
                        child->SetSize(itemWidth, child->GetCalculatedSize().y);
                        child->UpdateLayout(m_CalculatedPos, m_CalculatedSize);
                        localCurrentY += child->GetCalculatedSize().y + m_itemSpacing;
                    }
                }

                m_ScrollState.ContentHeight = (localCurrentY + m_ScrollState.ScrollY) - m_startYOffset;
                m_ScrollState.ViewportHeight = m_CalculatedSize.y - m_startYOffset;
            }
        }

        void HierarchyPanel::BuildEntityTree(Entity entity, int depth, Widget* parentWidget)
        {
            auto& tag = entity.GetComponent<TagComponent>().Tag;
            uint32_t id = (uint32_t)entity;

            auto item = new UI::HierarchyItem(tag, tag);
            item->SetEntityID(id);
            item->SetSize(m_CalculatedSize.x - 10.0f, 24.0f);
            item->SetIndentLevel((float)depth);
            item->SetSelected(IsSelected(entity));
            item->SetActiveInHierarchy(m_Context ? m_Context->IsEntityActiveInHierarchy(entity) : true);

            bool hasChildren = false;
            if (entity.HasComponent<RelationshipComponent>())
            {
                hasChildren = entity.GetComponent<RelationshipComponent>().Children.size() > 0;
            }
            item->SetHasChildren(hasChildren);
            item->SetExpanded(m_ExpandedStates[id]);

            item->SetOnSelect([this, entity]() { SelectEntityFromClick(entity); });
            item->SetOnToggleExpand([this, id]() { m_ExpandedStates[id] = !m_ExpandedStates[id]; m_NeedsRefresh = true; });

            parentWidget->AddChild(item);

            if (hasChildren)
            {
                auto& rel = entity.GetComponent<RelationshipComponent>();
                for (auto childID : rel.Children)
                {
                    Entity child{ childID, m_Context };
                    BuildEntityTree(child, depth + 1, item);
                }
            }
        }

        void HierarchyPanel::SelectEntityFromClick(Entity entity)
        {
            if (!entity || !m_Context)
                return;

            const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            entt::entity clickedHandle = (entt::entity)entity;

            if (shift)
            {
                std::vector<entt::entity> visibleOrder;
                CollectVisibleEntities(this, visibleOrder);

                entt::entity anchor = m_SelectionAnchor;
                if (anchor == entt::null || std::find(visibleOrder.begin(), visibleOrder.end(), anchor) == visibleOrder.end())
                    anchor = clickedHandle;

                auto anchorIt = std::find(visibleOrder.begin(), visibleOrder.end(), anchor);
                auto clickedIt = std::find(visibleOrder.begin(), visibleOrder.end(), clickedHandle);

                std::vector<Entity> nextSelection;
                if (ctrl)
                    nextSelection = GetSelectedEntities();

                if (anchorIt != visibleOrder.end() && clickedIt != visibleOrder.end())
                {
                    if (clickedIt < anchorIt)
                        std::swap(anchorIt, clickedIt);

                    for (auto it = anchorIt; it <= clickedIt; ++it)
                        nextSelection.emplace_back(*it, m_Context);
                }
                else
                {
                    nextSelection.push_back(entity);
                }

                // Shift 범위 선택은 화면에 펼쳐져 보이는 줄만 대상으로 삼는다.
                // 접힌 자식까지 선택하면 사용자는 어떤 항목이 바뀌었는지 눈으로 확인할 수 없다.
                SetSelectedEntities(nextSelection, entity);
                m_SelectionAnchor = anchor;
                return;
            }

            if (ctrl)
            {
                std::vector<Entity> nextSelection = GetSelectedEntities();
                auto existing = std::find_if(nextSelection.begin(), nextSelection.end(),
                    [clickedHandle](Entity selected) { return (entt::entity)selected == clickedHandle; });

                if (existing != nextSelection.end())
                    nextSelection.erase(existing);
                else
                    nextSelection.push_back(entity);

                Entity active = {};
                if (m_SelectedEntitySet.find(clickedHandle) == m_SelectedEntitySet.end())
                    active = entity;
                else if (!nextSelection.empty())
                    active = nextSelection.back();

                SetSelectedEntities(nextSelection, active);
                return;
            }

            SetSelectedEntity(entity);
        }

        void HierarchyPanel::CollectVisibleEntities(Widget* widget, std::vector<entt::entity>& outEntities) const
        {
            if (!widget || !widget->IsVisible())
                return;

            if (auto item = dynamic_cast<UI::HierarchyItem*>(widget))
            {
                entt::entity handle = (entt::entity)item->GetEntityID();
                if (handle != entt::null && m_Context && m_Context->GetRegistry().valid(handle))
                    outEntities.push_back(handle);

                if (!item->GetExpanded())
                    return;
            }

            for (Widget* child : widget->GetChildren())
                CollectVisibleEntities(child, outEntities);
        }

        void HierarchyPanel::SanitizeSelection()
        {
            if (!m_Context)
            {
                ClearSelection();
                return;
            }

            std::vector<entt::entity> validSelection;
            validSelection.reserve(m_SelectedEntities.size());
            m_SelectedEntitySet.clear();

            for (entt::entity handle : m_SelectedEntities)
            {
                if (handle == entt::null || !m_Context->GetRegistry().valid(handle))
                    continue;

                if (m_SelectedEntitySet.insert(handle).second)
                    validSelection.push_back(handle);
            }

            m_SelectedEntities = std::move(validSelection);

            if (m_SelectionContext && !m_Context->GetRegistry().valid((entt::entity)m_SelectionContext))
                m_SelectionContext = {};

            if (!m_SelectionContext && !m_SelectedEntities.empty())
                m_SelectionContext = Entity{ m_SelectedEntities.back(), m_Context };

            if (m_SelectedEntities.empty())
                m_SelectionAnchor = entt::null;
        }

        void HierarchyPanel::OnRender()
        {
            if (!m_IsVisible) return;

            float rightPadding = (m_ScrollState.GetMaxScroll() > 0) ? 22.0f : 0.0f;
            SetClipPadding(0.0f, m_startYOffset, rightPadding, 0.0f);

            // 패널 scissor는 최종 픽셀만 자른다. 하이어라키 아이템은 이 범위를 받아
            // 보이지 않는 줄의 hover/문자열 처리를 건너뛴다.
            float clipTop = m_CalculatedPos.y + m_startYOffset;
            float clipBottom = m_CalculatedPos.y + m_CalculatedSize.y;
            for (auto child : m_Children)
            {
                if (auto item = dynamic_cast<UI::HierarchyItem*>(child))
                    item->SetRenderClipRange(clipTop, clipBottom);
            }

            UpdateActiveVisuals(this);
            UI::WindowPanel::OnRender();

            // 스크롤바 그리기
            if (m_ScrollState.GetMaxScroll() > 0)
            {
                float thumbH = m_ScrollState.GetThumbHeight();
                float thumbY = m_ScrollState.GetThumbY(m_CalculatedPos.y + m_startYOffset);
                float thumbX = m_CalculatedPos.x + m_CalculatedSize.x - 20.0f;

                UIRenderer::DrawRect({ thumbX, m_CalculatedPos.y + m_startYOffset }, { 8.0f, m_ScrollState.ViewportHeight }, { 0.1f, 0.1f, 0.1f, 0.5f });
                UIRenderer::DrawRect({ thumbX, thumbY }, { 8.0f, thumbH }, { 0.4f, 0.4f, 0.4f, 1.0f });
            }
        }

        bool HierarchyPanel::OnEvent(Event& e)
        {
            if (!m_IsVisible) return false;

            if (e.GetEventType() == EventType::MouseScrolled)
            {
                auto& se = static_cast<MouseScrolledEvent&>(e);
                m_ScrollState.ApplyScroll(se.GetYOffset() * -1.0f);
                return true;
            }

            if (e.GetEventType() == EventType::MouseButtonPressed)
            {
                auto& me = static_cast<MouseButtonPressedEvent&>(e);
                if (me.GetButton() == 0)
                {
                    float mouseX = me.GetX();
                    float mouseY = me.GetY();

                    if (m_ScrollState.GetMaxScroll() > 0)
                    {
                        float thumbH = m_ScrollState.GetThumbHeight();
                        float thumbY = m_ScrollState.GetThumbY(m_CalculatedPos.y + m_startYOffset);
                        float thumbX = m_CalculatedPos.x + m_CalculatedSize.x - 20.0f;

                        if (mouseX >= thumbX && mouseX <= thumbX + 8.0f &&
                            mouseY >= thumbY && mouseY <= thumbY + thumbH)
                        {
                            m_IsDraggingScrollbar = true;
                            m_DragMouseStartY = mouseY;
                            m_DragScrollStartY = m_ScrollState.ScrollY;
                            Widget::BeginMouseInteraction(this);
                            e.Handled = true;
                            return true;
                        }
                    }
                }
            }

            if (e.GetEventType() == EventType::MouseMoved && m_IsDraggingScrollbar)
            {
                auto& me = static_cast<MouseMovedEvent&>(e);
                m_ScrollState.SetFromThumbDrag(me.GetY(), m_DragMouseStartY, m_DragScrollStartY);
                e.Handled = true;
                return true;
            }

            if (e.GetEventType() == EventType::MouseButtonReleased)
            {
                auto& me = static_cast<MouseButtonReleasedEvent&>(e);
                if (me.GetButton() == 0 && m_IsDraggingScrollbar)
                {
                    m_IsDraggingScrollbar = false;
                    Widget::EndMouseInteraction(this);
                    e.Handled = true;
                    return true;
                }
            }

            return UI::WindowPanel::OnEvent(e);
        }
    }
}
