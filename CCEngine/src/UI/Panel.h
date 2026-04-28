#pragma once
#include "Core.h"
#include "UI/Widget.h"

namespace CCEngine
{
    namespace UI
    {
        class CC_API Panel : public Widget
        {
        public:
            Panel(const std::string& name = "Panel", UIColor color = { 0.2f, 0.2f, 0.2f, 1.0f });

            virtual void OnRender() override;

            void SetColor(UIColor color){m_Color = color;}

            void SetHovered(bool isHovered){m_IsHovered = isHovered;}

        private:
            UIColor m_Color;
            UIColor m_HoverColor = { 0.3f, 0.3f, 0.3f, 1.0f };
            bool m_IsHovered = false;
        };
    }
}