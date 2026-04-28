#pragma once
#include "Core.h"
#include "UI/Widget.h"

namespace CCEngine 
{
    namespace UI 
    {
        class CC_API VBoxContainer : public Widget
        {
        public:
            VBoxContainer(const std::string& name = "VBoxContainer");

            // 자식들을 세로로 정렬하며 자신의 전체 높이를 계산합니다.
            virtual void UpdateLayout(const UIVec2& parentPos, const UIVec2& parentSize) override;

        protected:
            float m_Spacing = 0.0f; // 아이템 간의 간격
        };
    }
}