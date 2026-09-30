#pragma once
#include "Events/Event.h"

namespace CCEngine
{
    class CC_API KeyPressedEvent : public Event
    {
    public:
        explicit KeyPressedEvent(int keyCode, bool controlDown = false, bool shiftDown = false, bool repeat = false)
            : m_KeyCode(keyCode), m_ControlDown(controlDown), m_ShiftDown(shiftDown), m_Repeat(repeat) {}
        int GetKeyCode() const { return m_KeyCode; }
        bool IsControlDown() const { return m_ControlDown; }
        bool IsShiftDown() const { return m_ShiftDown; }
        bool IsRepeat() const { return m_Repeat; }
        EventType GetEventType() const override { return EventType::KeyPressed; }

    private:
        int m_KeyCode;
        bool m_ControlDown = false;
        bool m_ShiftDown = false;
        bool m_Repeat = false;
    };

    class CC_API TextInputEvent : public Event
    {
    public:
        explicit TextInputEvent(char character) : m_Character(character) {}
        char GetCharacter() const { return m_Character; }
        EventType GetEventType() const override { return EventType::TextInput; }

    private:
        char m_Character;
    };
}
