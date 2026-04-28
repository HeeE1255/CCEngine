#pragma once
#include "Core.h"
#include "Events/Event.h"
#include <string>

namespace CCEngine {
    class CC_API Layer
    {
    public:
        Layer(const std::string& name = "Layer");
        virtual ~Layer(); 

        virtual void OnAttach() {}
        virtual void OnDetach() {}
        virtual void OnUpdate(float deltaTime) {}
        virtual void OnEvent(Event& e) {}
        virtual void OnImGuiRender() {}

        inline const std::string& GetName() const { return m_DebugName; }
    protected:
        std::string m_DebugName;
    };
}