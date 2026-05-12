#include "HierarchyPanel.h"
#include "Scene/Components.h"
#include "UI/HierarchyItem.h"
#include "Renderer/UIRenderer.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Renderer2D.h"
#include "Application.h"

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

        void HierarchyPanel::UpdateLayout(const DirectX::XMFLOAT2& parentPos, const DirectX::XMFLOAT2& parentSize)
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

                float localStartX = 5.0f;
                // ★ 1. 현재 Y위치에서 스크롤 값(ScrollY)을 빼서 항목들을 위로 끌어올림
                float localCurrentY = m_startYOffset - m_ScrollState.ScrollY;
                // ★ 2. 오른쪽에 스크롤바(약 15px)가 들어갈 공간을 위해 너비를 더 줄임 (-35.0f)
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

				// ★ 3. 스크롤 상태 업데이트: 전체 콘텐츠 높이와 뷰포트 높이를 계산해서 스크롤바가 제대로 작동하도록 함
                m_ScrollState.ContentHeight = (localCurrentY + m_ScrollState.ScrollY) - m_startYOffset;
                m_ScrollState.ViewportHeight = m_CalculatedSize.y - m_startYOffset;
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

            // 1. 패널 배경 버퍼에 쌓기
            UI::WindowPanel::OnRender();

            // 2. 지금까지 쌓인 배경을 강제로 다 그려버림
            Renderer2D::EndScene();

            auto window = &(CCEngine::Application::Get()->GetWindow());
            UIRenderer::BeginUI(window->GetWidth(), window->GetHeight());

            // =========================================================
            // ★ 1차 방어: 하드웨어 가위질 (언더플로우 42억 폭발 완벽 방지)
            // =========================================================
            int sX = (std::max)(0, (int)m_CalculatedPos.x);
            int sY = (std::max)(0, (int)(m_CalculatedPos.y + m_startYOffset));
            int sW = (std::max)(0, (int)m_CalculatedSize.x);
            int sH = (std::max)(0, (int)(m_CalculatedSize.y - m_startYOffset));

            RenderCommand::SetScissorEnable(true);
            RenderCommand::SetScissor((uint32_t)sX, (uint32_t)sY, (uint32_t)sW, (uint32_t)sH);

            // =========================================================
            // ★ 2차 방어: CPU 논리 클리핑 (보이지 않는 건 GPU에 안 넘김)
            // =========================================================
            float clipTop = m_CalculatedPos.y + m_startYOffset;
            float clipBottom = m_CalculatedPos.y + m_CalculatedSize.y;

            for (auto child : m_Children)
            {
                float childY = child->GetCalculatedPosition().y;
                float childH = child->GetCalculatedSize().y;

                // 항목이 패널 위나 아래로 "완전히" 벗어났다면 렌더링 스킵! (절대 뚫릴 일 없음)
                if (childY + childH < clipTop || childY > clipBottom)
                {
                    continue;
                }

                child->OnRender();
            }

            // 가위질이 켜져 있는 지금 강제로 그려버림
            Renderer2D::EndScene();
            RenderCommand::SetScissorEnable(false);

            // 스크롤바 등 남은 UI를 그리기 위해 배치 다시 열기
            UIRenderer::BeginUI(window->GetWidth(), window->GetHeight());

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

            // 1. 마우스 휠 스크롤 처리
            if (e.GetEventType() == EventType::MouseScrolled)
            {
                auto& se = static_cast<MouseScrolledEvent&>(e);

                // 마우스가 현재 패널 영역 안에 있을 때만 휠 작동 (패널 X, Y, Width, Height 기준)
                // (이 부분은 마우스 위치를 가져오는 엔진 함수에 맞게 조건문을 걸어주시면 더 좋습니다)
                m_ScrollState.ApplyScroll(se.GetYOffset() * -1.0f); // 휠 방향 보정 (필요시 부호 변경)
                return true;
            }

            // 2. 마우스 클릭 (스크롤바 손잡이 잡기)
            if (e.GetEventType() == EventType::MouseButtonPressed)
            {
                auto& me = static_cast<MouseButtonPressedEvent&>(e);
                if (me.GetButton() == 0) // 좌클릭
                {
                    float mouseX = me.GetX();
                    float mouseY = me.GetY();

                    // 스크롤바가 존재할 때만 판정
                    if (m_ScrollState.GetMaxScroll() > 0)
                    {
                        float thumbH = m_ScrollState.GetThumbHeight();
                        float thumbY = m_ScrollState.GetThumbY(m_CalculatedPos.y + m_startYOffset);
                        float thumbX = m_CalculatedPos.x + m_CalculatedSize.x - 20.0f;

                        // 마우스가 손잡이(Thumb) 영역(너비 8, 높이 thumbH) 안에 있는지 검사
                        if (mouseX >= thumbX && mouseX <= thumbX + 8.0f &&
                            mouseY >= thumbY && mouseY <= thumbY + thumbH)
                        {
                            m_IsDraggingScrollbar = true;
                            m_DragMouseStartY = mouseY;
                            m_DragScrollStartY = m_ScrollState.ScrollY;
                            return true; // 이벤트 소모
                        }
                    }
                }
            }

            // 3. 마우스 드래그 (스크롤바 내리기/올리기)
            if (e.GetEventType() == EventType::MouseMoved && m_IsDraggingScrollbar)
            {
                auto& me = static_cast<MouseMovedEvent&>(e);
                float mouseY = me.GetY();

                // 마우스가 움직인 거리
                float deltaY = mouseY - m_DragMouseStartY;

                // 스크롤바가 움직일 수 있는 전체 트랙 공간
                float trackSpace = m_ScrollState.ViewportHeight - m_ScrollState.GetThumbHeight();

                if (trackSpace > 0.0f)
                {
                    // 마우스 이동량을 실제 콘텐츠 스크롤 이동량으로 변환 (비례식)
                    float scrollDelta = (deltaY / trackSpace) * m_ScrollState.GetMaxScroll();

                    // 시작 스크롤 값에 변환된 이동량을 더함
                    m_ScrollState.ScrollY = m_DragScrollStartY + scrollDelta;

                    // 범위 밖으로 나가지 않게 고정
                    if (m_ScrollState.ScrollY < 0.0f) m_ScrollState.ScrollY = 0.0f;
                    if (m_ScrollState.ScrollY > m_ScrollState.GetMaxScroll()) m_ScrollState.ScrollY = m_ScrollState.GetMaxScroll();
                }
                return true;
            }

            // 4. 마우스 클릭 해제 (드래그 종료)
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