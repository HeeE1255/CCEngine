#pragma once
#include "UI/Widget.h"
#include <functional>
#include <string>

namespace CCEngine {
    namespace UI {

        class DragFloat : public Widget
        {
        public:
            // 값을 읽어오는 함수(getter)와 값을 쓰는 함수(setter)를 받습니다.
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
            float m_Sensitivity = 0.02f; // 드래그 시 값이 변하는 속도
        };

    }
}