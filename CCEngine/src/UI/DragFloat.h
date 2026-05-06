#pragma once
#include "UI/Widget.h"
#include "Core.h"
#include <functional>
#include <string>

namespace CCEngine 
{
    namespace UI 
    {
        class CC_API DragFloat : public Widget 
        {
        public:
            DragFloat(const std::string& name, const std::string& label,
                std::function<float()> getter, std::function<void(float)> setter);
            virtual void OnRender() override;
            virtual bool OnMouseButtonPressed(MouseButtonPressedEvent& e) override;

        private:
            std::string m_Label;
            std::function<float()> m_Getter;
            std::function<void(float)> m_Setter;

            bool m_IsDragging = false;
            float m_LastMouseX = 0.0f;
            float m_Sensitivity = 0.1f;

            bool m_IsEditing = false;
            std::string m_InputBuffer;
        };
    }
}