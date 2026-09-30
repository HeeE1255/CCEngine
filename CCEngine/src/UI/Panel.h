#pragma once
#include "Core.h"
#include "UI/Widget.h"
#include <DirectXMath.h>
#include <functional>

namespace CCEngine
{
    namespace UI
    {
        class CC_API Panel : public Widget
        {
        public:
            Panel(const std::string& name = "Panel", DirectX::XMFLOAT4 color = { 0.2f, 0.2f, 0.2f, 1.0f });

            virtual void OnRender() override;
            virtual bool OnKeyPressed(KeyPressedEvent& e) override;

            void SetColor(DirectX::XMFLOAT4 color){m_Color = color;}

            void SetHovered(bool isHovered){m_IsHovered = isHovered;}
            void SetOnKeyPressed(std::function<bool(KeyPressedEvent&)> callback) { m_OnKeyPressed = std::move(callback); }

        private:
            DirectX::XMFLOAT4 m_Color;
            DirectX::XMFLOAT4 m_HoverColor = { 0.3f, 0.3f, 0.3f, 1.0f };
            bool m_IsHovered = false;
            std::function<bool(KeyPressedEvent&)> m_OnKeyPressed;
        };
    }
}
