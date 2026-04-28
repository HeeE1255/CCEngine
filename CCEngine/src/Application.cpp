#include "Application.h"
#include "Core/Memory.h"
#include "Renderer/RendererAPI.h"
#include "Renderer/Renderer.h"
#include "Renderer/UIRenderer.h"
#include "UI/Widget.h"
#include <iostream>

// RHI: 각 API별 ImGui 백엔드 헤더
// #include "backends/imgui_impl_opengl3.h"

// RHI: 각 API별 컨텍스트 헤더
//#include "Platform/DirectX11/DX11Context.h"
// #include "Platform/OpenGL/OpenGLContext.h"


namespace CCEngine
{
    Application* Application::s_Instance = nullptr;

    Application::Application(const std::string& commandLineArg)
    {
        // 1. 실행 시점에 명령줄 인수를 분석하여 API 결정 (RHI 런타임 스위칭)
        if (commandLineArg == "-opengl")
            RendererAPI::SetAPI(RendererAPI::API::OpenGL);
        else if (commandLineArg == "-vulkan")
            RendererAPI::SetAPI(RendererAPI::API::Vulkan);
        else
            RendererAPI::SetAPI(RendererAPI::API::DirectX11);

        // 메모리 매니저 초기화
        MemoryManager::Init();

        s_Instance = this;

        // 이제 Window::Create를 호출하면 내부적으로 WindowsWindow가 생성
        m_Window = std::unique_ptr<Window>(Window::Create());
    }

    Application::~Application()
    {
        s_Instance = nullptr;
        // 엔진이 꺼질 때 가장 마지막에 메모리 매니저 종료 (누수 검사)
        MemoryManager::Shutdown();
    }

    void Application::PushLayer(Layer* layer)
    {
        m_LayerStack.PushLayer(layer);
    }

    void Application::PushOverlay(Layer* overlay)
    {
        m_LayerStack.PushOverlay(overlay);
    }

    Window* Application::CreateSecondaryWindow(const std::string& title, uint32_t width, uint32_t height)
    {
        WindowProps props(title, width, height);

        // 1. 추상화된 팩토리 함수로 창 생성
        Window* newWindow = Window::Create(props);
        m_SecondaryWindows.push_back(newWindow);

        // 2. [RHI 추상화 준수] 특정 API나 OS API 호출 없이 인터페이스만 사용!
        // 메인 윈도우에게 현재 마우스 위치를 물어보고, 새 윈도우를 그 위치로 이동시킵니다.
        auto [mouseX, mouseY] = m_Window->GetScreenMousePosition();

        newWindow->SetPosition(mouseX - (width / 2), mouseY - 15);

        return newWindow;
    }

    inline void Application::RequestCloseSecondaryWindowByUI(UI::Widget* rootUI)
    {
		// UI 트리 포인터로 어떤 창이 닫혀야 하는지 식별하여 닫는 함수
        for (auto it = m_SecondaryWindows.begin(); it != m_SecondaryWindows.end(); ++it)
        {
            if ((*it)->GetRootUI() == rootUI)
            {
                (*it)->SetShouldClose(true);

                (*it)->SetRootUI(nullptr);
                return;
            }
        }
    }

    void Application::OnUpdate()
    {
        // Sandbox의 기존 OnUpdate (Sandbox가 Layer로 완전히 전환되면 삭제될 예정)
    }

    void Application::Run()
    {
        std::cout << "CCEngine Started! (100% Native UI Mode)" << std::endl;

        while (!m_Window->ShouldClose()) // 게임 루프
        {
            // =========================================================
            // 1. OS 메시지 처리 (모든 창 숨쉬기)
            // =========================================================
            m_Window->OnUpdate(); // 메인 창 메시지 펌프

            for (auto it = m_SecondaryWindows.begin(); it != m_SecondaryWindows.end(); )
            {
                Window* secWin = *it;
                secWin->OnUpdate(); // 서브 창 메시지 펌프

                if (secWin->ShouldClose())
                {
                    delete secWin;
                    it = m_SecondaryWindows.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            // =========================================================
            // 2. 엔진 로직 업데이트 (기존 Sandbox 및 레이어)
            // =========================================================
            OnUpdate();

            for (Layer* layer : m_LayerStack)
            {
                layer->OnUpdate(0.016f); // 델타타임 (임시 60FPS)
            }

            // =========================================================
            // 3. 통합 렌더링 파이프라인 (메인 창 + 서브 창 동등 처리!)
            // =========================================================

            // 렌더링할 모든 창을 하나의 리스트로 묶습니다.
            std::vector<Window*> allWindows;
            allWindows.push_back(m_Window.get());
            for (Window* secWin : m_SecondaryWindows)
            {
                allWindows.push_back(secWin);
            }

            // 각 창을 순회하며 독립적인 도화지에 자체 UI를 그립니다.
            for (Window* win : allWindows)
            {
                auto context = win->GetContext();

                if (!context) continue; // 컨텍스트 안정성 검사

                // 1) 렌더링 타겟을 이 윈도우의 스왑체인으로 강제 전환
                context->MakeCurrent();
                context->BindBackBuffer();

                // 2) 도화지 지우기 
                // (투명도나 잔상 문제가 생기면 주석을 풀고 Clear를 활성화하세요)
                // context->Clear(0.15f, 0.15f, 0.15f, 1.0f);

                // 3) 이 창이 가진 고유의 자체 UI 트리 그리기!
                if (win->GetRootUI() != nullptr)
                {
					// UI 트리의 레이아웃을 현재 창 크기에 맞게 업데이트 (재계산)
                    win->GetRootUI()->UpdateLayout(
                        { 0.0f, 0.0f },
                        { (float)win->GetWidth(), (float)win->GetHeight() }
                    );

                    // 현재 창의 크기에 맞춰서 도화지(Ortho 행렬) 세팅
                    UIRenderer::BeginUI(win->GetWidth(), win->GetHeight());

                    // 이 창에 속한 UI만 딱 한 번 그립니다. (이중 루프 제거됨!)
                    win->GetRootUI()->OnRender();

                    // 화면에 진짜로 출력 (DrawIndexed 호출)
                    UIRenderer::EndUI();
                }

                // 4) 화면에 출력
                context->SwapBuffers();
            }

            // 렌더 타겟을 다시 메인 창으로 복구 (안전 장치)
            if (m_Window->GetContext())
            {
                m_Window->GetContext()->MakeCurrent();
            }

        }

        std::cout << "CCEngine Shutting Down..." << std::endl;
    }

    void Application::OnWindowResize(WindowResizeEvent& e)
    {
        // 최소화 버튼을 누르면 창 크기가 0이 되므로 렌더링 중지
        if (e.GetWidth() == 0 || e.GetHeight() == 0)
        {
            m_Minimized = true;
            return;
        }

        m_Minimized = false;

        // RHI 사령탑(Renderer)에게 리사이즈 명령 하달!
        Renderer::OnWindowResize(e.GetWidth(), e.GetHeight());
    }
}