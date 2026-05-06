#pragma once
#include "UI/Widget.h"
#include <functional>
#include <string>
#include <DirectXMath.h>

namespace CCEngine {
    namespace UI {

        class DragFloat4 : public Widget
        {
        public:
            DragFloat4(const std::string& name, const std::string& label,
                std::function<DirectX::XMFLOAT4()> getter,
                std::function<void(DirectX::XMFLOAT4)> setter);

            virtual void OnRender() override;
            virtual bool OnMouseButtonPressed(MouseButtonPressedEvent& e) override;

        private:
            std::string m_Label;
            std::function<DirectX::XMFLOAT4()> m_Getter;
            std::function<void(DirectX::XMFLOAT4)> m_Setter;

            int m_DraggingAxis = -1; // 0:R, 1:G, 2:B, 3:A
            float m_LastMouseX = 0.0f;
            float m_Sensitivity = 0.01f; // 0~1 색상이므로 세밀하게

            int m_EditingAxis = -1;
            std::string m_InputBuffer;
        };

    }
}