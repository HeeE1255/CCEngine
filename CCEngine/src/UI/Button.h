#pragma once
#include "Core.h"
#include "Widget.h"
#include <functional>

namespace CCEngine {
    namespace UI {

        class CC_API Button : public Widget {
        public:
            Button(const std::string& name = "Button", const std::string& text = "Button");

            virtual void OnRender() override;

            void SetText(const std::string& text) { m_Text = text; }
            void SetOnClick(std::function<void()> callback) { m_OnClick = callback; }

            void SetNormalColor(UIColor color) { m_NormalColor = color; }
            void SetHoverColor(UIColor color) { m_HoverColor = color; }
            void SetClickColor(UIColor color) { m_ClickColor = color; }

            virtual bool OnMouseButtonPressed(MouseButtonPressedEvent& e) override;
            virtual bool OnMouseMoved(MouseMovedEvent& e) override;

        private:
            std::string m_Text;
            std::function<void()> m_OnClick = nullptr;

            UIColor m_NormalColor = { 0.2f, 0.2f, 0.2f, 1.0f };
            UIColor m_HoverColor = { 0.3f, 0.3f, 0.3f, 1.0f };
            UIColor m_ClickColor = { 0.1f, 0.1f, 0.1f, 1.0f };

            bool m_IsHovered = false;
            bool m_IsPressed = false;
        };

    }
}