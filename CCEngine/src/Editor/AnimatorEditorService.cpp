#include "Editor/AnimatorEditorService.h"

#include "Core/Window.h"
#include "Scene/Components.h"
#include "UI/AnimatorGraphPanel.h"
#include "UI/Panel.h"

namespace CCEngine
{
    void AnimatorEditorService::Open(UI::Panel* rootUI, Entity entity, Callbacks callbacks)
    {
        if (!rootUI || !entity || !entity.HasComponent<AnimatorComponent>())
            return;

        const bool ownerWindowClosed = m_Panel && m_Panel->GetOwnerWindow() && m_Panel->GetOwnerWindow()->ShouldClose();
        const bool orphanedPanel = m_Panel && !m_Panel->GetParent() && !m_Panel->GetOwnerWindow();

        if (orphanedPanel)
        {
            delete m_Panel;
            m_Panel = nullptr;
        }

        if (ownerWindowClosed && m_Panel)
        {
            // 보조 창은 창 시스템이 정리하므로 여기서 delete하지 않는다. 대신 이전 창의 닫힘 콜백이
            // 새 패널 포인터까지 지우지 못하도록 콜백을 먼저 끊고 소유 추적만 해제한다.
            m_Panel->SetOnClosed({});
            m_Panel->SetOnStateSelected({});
            m_Panel->SetOnTransitionSelected({});
            m_Panel = nullptr;
        }

        if (!m_Panel)
        {
            // 닫힌 보조 창의 패널은 재사용하지 않는다. 새 인스턴스를 만들어 한 번 닫은 뒤에도 다시 열리게 한다.
            m_Panel = new UI::AnimatorGraphPanel("AnimatorGraphEditorPanel");
            m_Panel->SetOnClosed([this]() { m_Panel = nullptr; });
        }

        m_Panel->SetOnStateSelected(std::move(callbacks.OnStateSelected));
        m_Panel->SetOnTransitionSelected(std::move(callbacks.OnTransitionSelected));

        if (m_Panel->GetOwnerWindow() && !m_Panel->GetOwnerWindow()->ShouldClose())
        {
            // 멀티 윈도우에 있는 패널은 parent가 없어도 정상이다. 새 창을 만들지 않고 기존 창을 다시 활성화한다.
            m_Panel->SetTarget(entity);
            m_Panel->SetVisible(true);
            UI::Widget::SetKeyboardFocus(m_Panel);
            return;
        }

        if (m_Panel->GetParent() != rootUI)
            rootUI->AddChild(m_Panel);

        m_Panel->SetOwnerWindow(nullptr);
        m_Panel->SetAnchorMin(0.0f, 0.0f);
        m_Panel->SetAnchorMax(0.0f, 0.0f);
        m_Panel->SetOffsetMin(300.0f, 120.0f);
        m_Panel->SetOffsetMax(1120.0f, 700.0f);
        m_Panel->SetDockingEnabled(true);
        m_Panel->SetTarget(entity);
    }

    void AnimatorEditorService::Shutdown()
    {
        if (m_Panel)
        {
            m_Panel->SetOnClosed({});
            m_Panel->SetOnStateSelected({});
            m_Panel->SetOnTransitionSelected({});
        }
        m_Panel = nullptr;
    }

    void AnimatorEditorService::BringEmbeddedPanelToFront(UI::Panel* rootUI)
    {
        if (!m_Panel || !m_Panel->IsVisible() || m_Panel->GetOwnerWindow() || !rootUI)
            return;

        UI::Widget* branch = m_Panel;
        while (branch && branch != rootUI)
        {
            branch->BringToFront();
            branch = branch->GetParent();
        }
    }
}
