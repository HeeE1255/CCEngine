#pragma once
#include "Core.h"
#include "Core/Window.h"
#include "Events/ApplicationEvent.h"
#include "Core/LayerStack.h" 
#include <memory>
#include <string>
#include <vector>

namespace CCEngine
{
    class CC_API Application
    {
    public:
        // RHI와 에디터 QA처럼 실행 모드를 바꾸는 옵션은 앱 시작 때 한 번만 받는다.
        // 실행 중 전역 문자열을 다시 파싱하지 않도록 Application이 원본 인자를 보관한다.
        Application(const std::vector<std::string>& commandLineArgs = {});
        virtual ~Application();

        void Run();
        virtual void OnUpdate();
        void OnEvent(Event& e);

        // ==========================================
        //  레이어 및 오버레이 장착 함수
        // ==========================================
        void PushLayer(Layer* layer);
        void PushOverlay(Layer* overlay);
        Window* CreateSecondaryWindow(const std::string& title, uint32_t width, uint32_t height);

        // 엔진 어디서든 현재 윈도우 창 객체에 접근할 수 있도록 해주는 유틸리티 함수
        inline Window& GetWindow() { return *m_Window; }
        inline const std::vector<Window*>& GetSecondaryWindows() const { return m_SecondaryWindows; }
        void ActivateInputWindow(Window* window);
        void SetModalInputWindow(Window* window);
        void ClearModalInputWindow(Window* window = nullptr);
        bool IsInputEnabledForWindow(Window* window) const;
        bool HasCommandLineFlag(const std::string& flag) const;
        const std::vector<std::string>& GetCommandLineArgs() const { return m_CommandLineArgs; }
        void SetExitCode(int exitCode) { m_ExitCode = exitCode; }
        int GetExitCode() const { return m_ExitCode; }

        inline static Application* Get() { return s_Instance; }

        inline void RequestCloseSecondaryWindowByUI(UI::Widget* rootUI);
        inline void RequestCloseSecondaryWindow(Window* window);

    private:
        void ForgetInputWindow(Window* window);

        bool m_Running = true;
        bool m_Minimized = false;

        //레이어들을 관리하는 뭉치(Stack)
        LayerStack m_LayerStack;

        // 유일무이한 자기 자신을 기억해 둘 정적 포인터 (싱글톤 패턴용)
        static Application* s_Instance;

		std::unique_ptr<Window> m_Window; // 메인 창 (스왑체인과 렌더 타겟이 있는 창)
		std::vector<Window*> m_SecondaryWindows; // 서브 창들 (스왑체인과 렌더 타겟이 없는 창, UI 용도)
        Window* m_ActiveInputWindow = nullptr;
        std::vector<Window*> m_ModalInputWindows;
        std::vector<std::string> m_CommandLineArgs;
        int m_ExitCode = 0;
    };

    // 클라이언트(Sandbox 등)에서 정의해서 엔진에 넘겨줄 팩토리 함수
    Application* CreateApplication(const std::vector<std::string>& commandLineArgs);
}
