#include "HierarchyPanel.h"
#include "Scene/Components.h"
#include "UI/HierarchyItem.h"
#include "Renderer/UIRenderer.h"

namespace CCEngine {
    namespace UI {

        HierarchyPanel::HierarchyPanel(const std::string& name)
            : UI::WindowPanel(name, "Scene Hierarchy")
        {
            SetSize(300.0f, 700.0f);
        }

        // Prototype: process deferred UI ops queued earlier.
        // NOTE: (Deferred UI OPS 아직 미적용) - this function is provided
        // for reference and is NOT automatically invoked. Call it from a safe
        // point (e.g., UpdateLayout) if you want to enable deferred execution.
        void HierarchyPanel::ProcessDeferredUIOps()
        {
            for (auto& op : m_DeferredUIOps)
            {
                if (op.Op) op.Op();
            }
            m_DeferredUIOps.clear();
        }

        void HierarchyPanel::SetContext(Scene* context)
        {
            m_Context = context;
            m_SelectionContext = {};
            // Defer heavy Refresh to next frame to avoid reentrancy during event handling
            m_NeedsRefresh = true;
        }

        void HierarchyPanel::SetSelectedEntity(Entity entity)
        {
            m_SelectionContext = entity;
            // Defer selection visuals update to avoid modifying UI tree during event handling
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

                    // ★ 선택된 엔티티가 유효할 때만 Tag 검사 (크래시 방지)
                    if ((uint32_t)m_SelectionContext != (uint32_t)entt::null)
                    {
                        isMatch = (item->GetName() == m_SelectionContext.GetComponent<TagComponent>().Tag);
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

                if (isRoot)
                {
                    // 루트 엔티티들은 패널(this)의 자식으로 등록
                    BuildEntityTree(entity, 0, this);
                }
            }
        }

        void HierarchyPanel::UpdateLayout(const UIVec2& parentPos, const UIVec2& parentSize)
        {
            if (!m_IsVisible) return;

            // 1. 창 껍데기(WindowPanel) 자체의 위치와 크기를 먼저 계산합니다.
            WindowPanel::UpdateLayout(parentPos, parentSize);

            // 2. 씬이 존재할 때만 내부 아이템들을 세로로 정렬합니다.
            if (m_Context)
            {
                // Perform deferred refresh if requested
                if (m_NeedsRefresh)
                {
                    Refresh();
                    m_NeedsRefresh = false;
                }
                if (m_NeedsSelectionUpdate)
                {
                    UpdateSelectionVisuals(this);
                    m_NeedsSelectionUpdate = false;
                }

                // NOTE: Deferred UI OPS processing is currently disabled by default.
                // To enable queued deferred ops, uncomment the following call.
                // ProcessDeferredUIOps(this);
                float localStartX = 5.0f;
                float localCurrentY = m_startYOffset; // (헤더에 선언하신 변수 사용)

                // ★ 패널의 진짜 레이아웃 계산이 끝난 후의 넓이에서 여백을 뺌
                float itemWidth = m_CalculatedSize.x - 10.0f;
                if (itemWidth < 10.0f) itemWidth = 10.0f; // 마이너스 방지 안전장치

                for (auto child : m_Children)
                {
                    if (child->IsVisible())
                    {
                        child->SetPosition(localStartX, localCurrentY);

                        // ★ [핵심 픽스] 자식에게 정상적인 넓이를 강제로 부여합니다!
                        child->SetSize(itemWidth, child->GetCalculatedSize().y);

                        child->UpdateLayout(m_CalculatedPos, m_CalculatedSize);
                        localCurrentY += child->GetCalculatedSize().y + m_itemSpacing;
                    }
                }
            }
        }

        void HierarchyPanel::BuildEntityTree(Entity entity, int depth, Widget* parentWidget)
        {
            auto& tag = entity.GetComponent<TagComponent>().Tag;
            uint32_t id = (uint32_t)entity;

            auto item = new UI::HierarchyItem(tag, tag);
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

            item->SetOnSelect([this, entity]() {
                SetSelectedEntity(entity);
                });

            item->SetOnToggleExpand([this, id]() {
                m_ExpandedStates[id] = !m_ExpandedStates[id];
                // Defer the actual Refresh to avoid reentrancy while handling events
                m_NeedsRefresh = true;
                // Optionally you could enqueue a deferred UI op instead:
                // m_DeferredUIOps.push_back({ [this]() { Refresh(); } });
                });

            // 지정된 부모(패널 또는 상위 HierarchyItem)에게 나를 입양시킴!
            parentWidget->AddChild(item);

            if (hasChildren /* && item->GetExpanded() 구조 전체를 만들려면 주석 해제 없이 다 만듭니다 */)
            {
                auto& rel = entity.GetComponent<RelationshipComponent>();
                for (auto childID : rel.Children)
                {
                    Entity child{ childID, m_Context };
                    // ★ 중요: 내 자식 엔티티는 방금 만든 나(item)를 부모로 삼아 들어감!
                    BuildEntityTree(child, depth + 1, item);
                }
            }
        }

        void HierarchyPanel::OnRender()
        {
            if (!m_IsVisible) return;

            UI::WindowPanel::OnRender();
        }
    }
}