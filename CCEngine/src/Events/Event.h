#pragma once
#include "Core.h"

namespace CCEngine {

    // 이벤트의 종류를 구분하는 이름표
    enum class CC_API EventType
    {
        None = 0,
        WindowClose, WindowResize,
        MouseMoved, MouseButtonPressed, MouseButtonReleased
    };

    class CC_API Event
    {
    public:
        virtual ~Event() = default;
        virtual EventType GetEventType() const = 0;

        // 이 이벤트가 이미 처리되었는가?
        bool Handled = false; 
    };

}