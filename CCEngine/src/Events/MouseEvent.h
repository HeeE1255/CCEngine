#pragma once
#include "Event.h"

namespace CCEngine {

    class CC_API MouseMovedEvent : public Event
    {
    public:
        MouseMovedEvent(float x, float y) : m_MouseX(x), m_MouseY(y) {}

        float GetX() const { return m_MouseX; }
        float GetY() const { return m_MouseY; }

        virtual EventType GetEventType() const override { return EventType::MouseMoved; }

    private:
        float m_MouseX, m_MouseY;
    };

    class CC_API MouseButtonPressedEvent : public Event
    {
        public:
            // button: 0(Left), 1(Right), 2(Middle)
            MouseButtonPressedEvent(int button, float x, float y)
                : m_Button(button), m_MouseX(x), m_MouseY(y) {
            }

            int GetButton() const { return m_Button; }
            float GetX() const { return m_MouseX; }
            float GetY() const { return m_MouseY; }

            virtual EventType GetEventType() const override { return EventType::MouseButtonPressed; }

        private:
            int m_Button;
            float m_MouseX, m_MouseY;
    };

    class CC_API MouseButtonReleasedEvent : public Event
    {
        public:
            MouseButtonReleasedEvent(int button, float x = 0.0f, float y = 0.0f)
                : m_Button(button), m_MouseX(x), m_MouseY(y) {
            }
            int GetButton() const { return m_Button; }
            float GetX() const { return m_MouseX; }
            float GetY() const { return m_MouseY; }
            virtual EventType GetEventType() const override { return EventType::MouseButtonReleased; }
        private:
            int m_Button;
            float m_MouseX, m_MouseY;
	};

}