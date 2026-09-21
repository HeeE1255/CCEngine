#pragma once

#include "Core.h"
#include "Scene/Entity.h"

#include <functional>

namespace CCEngine::UI
{
    class AnimatorGraphPanel;
    class Panel;
}

namespace CCEngine
{
    class CC_API AnimatorEditorService
    {
    public:
        struct Callbacks
        {
            std::function<void(Entity, int, int)> OnStateSelected;
            std::function<void(Entity, int, int)> OnTransitionSelected;
        };

        void Open(UI::Panel* rootUI, Entity entity, Callbacks callbacks);
        void Shutdown();

        UI::AnimatorGraphPanel* GetPanel() const { return m_Panel; }
        void BringEmbeddedPanelToFront(UI::Panel* rootUI);

    private:
        UI::AnimatorGraphPanel* m_Panel = nullptr;
    };
}
