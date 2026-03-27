#pragma once
#include "Core.h"
#include "Core/Window.h"
#include "Core/ApplicationEvent.h"
#include <memory>

namespace CCEngine
{
    class CC_API Application
    {
    public:
        Application();
        virtual ~Application();

        void Run();
        virtual void OnUpdate();
        void OnWindowResize(WindowResizeEvent& e);

        inline static Application* Get() { return s_Instance; }

    protected:
        std::unique_ptr<Window> m_Window;

    private:
        bool m_Running = true;
        bool m_Minimized = false;

        // ==========================================
        // [🔥 추가] 유일무이한 자기 자신을 기억해 둘 정적 포인터
        // ==========================================
        static Application* s_Instance;
    };

    Application* CreateApplication();
}