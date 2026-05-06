#pragma once
#include "UI/Widget.h"
#include <functional>
#include <string>
#include <DirectXMath.h>

namespace CCEngine {
    namespace UI {

        class DragFloat3 : public Widget
        {
        public:
            DragFloat3(const std::string& name, const std::string& label,
                std::function<DirectX::XMFLOAT3()> getter,
                std::function<void(DirectX::XMFLOAT3)> setter);

            virtual void OnRender() override;
            virtual bool OnMouseButtonPressed(MouseButtonPressedEvent& e) override;

        private:
            std::string m_Label;
            std::function<DirectX::XMFLOAT3()> m_Getter;
            std::function<void(DirectX::XMFLOAT3)> m_Setter;

            // 드래그 상태
            int m_DraggingAxis = -1; // 0:X, 1:Y, 2:Z
            float m_LastMouseX = 0.0f;
            float m_Sensitivity = 0.1f;

            // 텍스트 편집 상태
            int m_EditingAxis = -1;
            std::string m_InputBuffer;
        };

    }
}