#include "HierarchyPanel.h"
#include "Scene/Components.h"
#include "UI/HierarchyItem.h"
#include "Renderer/UIRenderer.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Renderer2D.h"
#include "Application.h"
#include <vector>

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
            m_SelectionContext = {};
            m_NeedsRefresh = true;
        }

        void HierarchyPanel::SetSelectedEntity(Entity entity)
        {
            m_SelectionContext = entity;
            m_NeedsSelectionUpdate = true;
        }

        void HierarchyPanel::UpdateSelectionVisuals(Widget* widget)
        {
            for (auto child : widget->GetChildren())
            {
                auto item = dynamic_cast<UI::HierarchyItem*>(child);
                if (item)
                {
                    bool isMatch = false;
                    if ((uint32_t)m_SelectionContext != (uint32_t)entt::null)
                    {
                        isMatch = (item->GetEntityID() == (uint32_t)m_SelectionContext);
                    }
                    item->SetSelected(isMatch);
                    UpdateSelectionVisuals(item);
                }
            }
        }

        void HierarchyPanel::Refresh()
        {
            if (!m_Context) return;
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
            item->SetSelected(m_SelectionContext == entity);

            bool hasChildren = false;
            if (entity.HasComponent<RelationshipComponent>())
            {
                hasChildren = entity.GetComponent<RelationshipComponent>().Children.size() > 0;
            }
            item->SetHasChildren(hasChildren);
            item->SetExpanded(m_ExpandedStates[id]);

            item->SetOnSelect([this, entity]() { SetSelectedEntity(entity); });
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

        void HierarchyPanel::OnRender()
        {
            if (!m_IsVisible) return;

            float rightPadding = (m_ScrollState.GetMaxScroll() > 0) ? 22.0f : 0.0f;
            SetClipPadding(0.0f, m_startYOffset, rightPadding, 0.0f);

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
                            return true;
                        }
                    }
                }
            }

            if (e.GetEventType() == EventType::MouseMoved && m_IsDraggingScrollbar)
            {
                auto& me = static_cast<MouseMovedEvent&>(e);
                float deltaY = me.GetY() - m_DragMouseStartY;
                float trackSpace = m_ScrollState.ViewportHeight - m_ScrollState.GetThumbHeight();

                if (trackSpace > 0.0f)
                {
                    float scrollDelta = (deltaY / trackSpace) * m_ScrollState.GetMaxScroll();
                    m_ScrollState.ScrollY = m_DragScrollStartY + scrollDelta;

                    if (m_ScrollState.ScrollY < 0.0f) m_ScrollState.ScrollY = 0.0f;
                    if (m_ScrollState.ScrollY > m_ScrollState.GetMaxScroll()) m_ScrollState.ScrollY = m_ScrollState.GetMaxScroll();

                }
                return true;
            }

            if (e.GetEventType() == EventType::MouseButtonReleased)
            {
                auto& me = static_cast<MouseButtonReleasedEvent&>(e);
                if (me.GetButton() == 0 && m_IsDraggingScrollbar)
                {
                    m_IsDraggingScrollbar = false;
                    return true;
                }
            }

            return UI::WindowPanel::OnEvent(e);
        }
    }
}
