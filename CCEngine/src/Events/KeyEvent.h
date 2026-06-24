#pragma once
#include "Events/Event.h"

namespace CCEngine
{
    class CC_API KeyPressedEvent : public Event
    {
    public:
        explicit KeyPressedEvent(int keyCode) : m_KeyCode(keyCode) {}
        int GetKeyCode() const { return m_KeyCode; }
        EventType GetEventType() const override { return EventType::KeyPressed; }

    private:
        int m_KeyCode;
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
