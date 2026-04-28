#pragma once
#include "UI/Panel.h"
#include "Application.h"
#include "Events/MouseEvent.h"

namespace CCEngine {
    namespace UI {
        class EditorRootUI : public Panel
        {
        public:
            EditorRootUI(const std::string& name);

            virtual void OnRender() override;
            virtual bool OnMouseButtonPressed(MouseButtonPressedEvent& e) override;
        };
    }
}