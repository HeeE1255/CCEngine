#include "Application.h"
#include "Core/Memory.h"
#include "Events/ApplicationEvent.h"
#include "Renderer/RendererAPI.h"
#include "Renderer/Renderer.h"
#include "Renderer/UIRenderer.h"
#include "UI/Widget.h"
#include "UI/DockManager.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

#ifdef CC_PLATFORM_WINDOWS
#define NOMINMAX
#include <windows.h>
#endif

// RHI별 ImGui 백엔드 헤더
// #include "backends/imgui_impl_opengl3.h"

// RHI별 컨텍스트 헤더
//#include "Platform/DirectX11/DX11Context.h"
// #include "Platform/OpenGL/OpenGLContext.h"


namespace CCEngine
{
    Application* Application::s_Instance = nullptr;

    namespace
    {
        // 히치 원인을 다시 추적해야 할 때만 true로 바꾼다.
        // 평소에는 꺼 두어 콘솔 출력과 단계 기록 비용을 없앤다.
        constexpr bool kEnableFrameHitchProfiler = false;

        struct HitchStage
        {
            const char* Name = "";
            double Milliseconds = 0.0;
        };

        double ToMilliseconds(std::chrono::steady_clock::duration duration)
        {
            return std::chrono::duration<double, std::milli>(duration).count();
        }

        void AddHitchStage(std::vector<HitchStage>& stages, const char* name, std::chrono::steady_clock::time_point startedAt)
        {
            if (!kEnableFrameHitchProfiler)
                return;

            stages.push_back({ name, ToMilliseconds(std::chrono::steady_clock::now() - startedAt) });
        }

        void ReportFrameHitch(const char* label, std::chrono::steady_clock::time_point frameStartedAt, const std::vector<HitchStage>& stages)
        {
            if (!kEnableFrameHitchProfiler)
                return;

            double totalMs = ToMilliseconds(std::chrono::steady_clock::now() - frameStartedAt);
            double worstStageMs = 0.0;
            for (const HitchStage& stage : stages)
                worstStageMs = (std::max)(worstStageMs, stage.Milliseconds);

            if (totalMs < 24.0 && worstStageMs < 8.0)
                return;

            static auto s_LastReportTime = std::chrono::steady_clock::now() - std::chrono::seconds(2);
            auto now = std::chrono::steady_clock::now();
            if (now - s_LastReportTime < std::chrono::seconds(1))
                return;
            s_LastReportTime = now;

            // 히치는 재현 순간의 단계별 시간이 중요하다.
            // 정상 프레임에는 아무 출력도 하지 않고, 튄 프레임만 요약해 원인을 좁힌다.
            std::ostringstream stream;
            stream << "[Hitch] " << label << " total=" << std::fixed << std::setprecision(2) << totalMs << "ms";
            for (const HitchStage& stage : stages)
                stream << " | " << stage.Name << "=" << std::fixed << std::setprecision(2) << stage.Milliseconds << "ms";
            stream << '\n';

#ifdef CC_PLATFORM_WINDOWS
            OutputDebugStringA(stream.str().c_str());
#endif
            std::cout << stream.str();
        }

        void HideNativeWindow(Window* window)
        {
#ifdef CC_PLATFORM_WINDOWS
            if (window && window->GetNativeWindow())
                ShowWindow(static_cast<HWND>(window->GetNativeWindow()), SW_HIDE);
#else
            (void)window;
#endif
        }

        bool IsMouseOverNativeWindow(Window* window)
        {
#ifdef CC_PLATFORM_WINDOWS
            if (!window || !window->GetNativeWindow())
                return true;

            POINT cursor;
            GetCursorPos(&cursor);

            HWND target = static_cast<HWND>(window->GetNativeWindow());
            HWND hovered = WindowFromPoint(cursor);
            if (!hovered)
                return false;

            return hovered == target || GetAncestor(hovered, GA_ROOT) == target;
#else
            (void)window;
            return true;
#endif
        }

        bool IsInputEvent(EventType type)
        {
            return type == EventType::MouseMoved ||
                type == EventType::MouseButtonPressed ||
                type == EventType::MouseButtonReleased ||
                type == EventType::MouseScrolled ||
                type == EventType::KeyPressed ||
                type == EventType::TextInput ||
                type == EventType::FileDrop;
        }
    }

    Application::Application(const std::string& commandLineArg)
    {
        // 실행 시점의 명령줄 인수로 렌더링 API를 선택합니다.
        if (commandLineArg == "-opengl")
            RendererAPI::SetAPI(RendererAPI::API::OpenGL);
        else if (commandLineArg == "-vulkan")
            RendererAPI::SetAPI(RendererAPI::API::Vulkan);
        else
            RendererAPI::SetAPI(RendererAPI::API::DirectX11);

        // 메모리 매니저 초기화
        MemoryManager::Init();

        s_Instance = this;

        // 플랫폼별 Window 구현은 Window::Create 내부에서 선택됩니다.
        m_Window = std::unique_ptr<Window>(Window::Create());
        m_ActiveInputWindow = m_Window.get();
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

        // 추상화된 팩토리 함수로 창을 생성합니다.
        Window* newWindow = Window::Create(props);
        m_SecondaryWindows.push_back(newWindow);
        ActivateInputWindow(newWindow);

        // 메인 윈도우의 화면 좌표를 기준으로 새 창의 초기 위치를 정합니다.
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
                ForgetInputWindow(*it);
                (*it)->SetShouldClose(true);
                HideNativeWindow(*it);

                (*it)->SetRootUI(nullptr);
                return;
            }
        }
    }

    inline void Application::RequestCloseSecondaryWindow(Window* window)
    {
        if (!window || window == m_Window.get())
            return;

        for (Window* secondaryWindow : m_SecondaryWindows)
        {
            if (secondaryWindow == window)
            {
                ForgetInputWindow(secondaryWindow);
                secondaryWindow->SetShouldClose(true);
                HideNativeWindow(secondaryWindow);
                secondaryWindow->SetRootUI(nullptr);
                return;
            }
        }
    }

    void Application::ActivateInputWindow(Window* window)
    {
        if (!window)
            return;

        m_ActiveInputWindow = window;
    }

    void Application::SetModalInputWindow(Window* window)
    {
        if (!window)
            return;

        // 같은 창을 중복 등록하지 않고 맨 위 Modal로 올린다.
        m_ModalInputWindows.erase(
            std::remove(m_ModalInputWindows.begin(), m_ModalInputWindows.end(), window),
            m_ModalInputWindows.end()
        );
        m_ModalInputWindows.push_back(window);
        if (window)
            m_ActiveInputWindow = window;
    }

    void Application::ClearModalInputWindow(Window* window)
    {
        if (!window)
        {
            m_ModalInputWindows.clear();
        }
        else
        {
            m_ModalInputWindows.erase(
                std::remove(m_ModalInputWindows.begin(), m_ModalInputWindows.end(), window),
                m_ModalInputWindows.end()
            );
        }

        if (!m_ModalInputWindows.empty())
            m_ActiveInputWindow = m_ModalInputWindows.back();
        else if (!m_ActiveInputWindow)
            m_ActiveInputWindow = m_Window.get();
    }

    bool Application::IsInputEnabledForWindow(Window* window) const
    {
        if (!window)
            return false;

        return window == m_ActiveInputWindow;
    }

    void Application::ForgetInputWindow(Window* window)
    {
        if (!window)
            return;

        m_ModalInputWindows.erase(
            std::remove(m_ModalInputWindows.begin(), m_ModalInputWindows.end(), window),
            m_ModalInputWindows.end()
        );

        if (m_ActiveInputWindow == window)
        {
            if (!m_ModalInputWindows.empty())
                m_ActiveInputWindow = m_ModalInputWindows.back();
            else
                m_ActiveInputWindow = m_Window.get();
        }
    }

    void Application::OnUpdate()
    {
        // Sandbox가 레이어로 완전히 전환되기 전까지 유지되는 업데이트 경로입니다.
    }

    void Application::Run()
    {
        std::cout << "CCEngine Started! (100% Native UI Mode)" << std::endl;

        while (!m_Window->ShouldClose()) // 게임 루프
        {
            auto frameStartedAt = std::chrono::steady_clock::now();
            std::vector<HitchStage> hitchStages;

            // =========================================================
            // 1. 모든 창의 OS 메시지를 처리합니다.
            // =========================================================
            auto stageStartedAt = std::chrono::steady_clock::now();
            m_Window->OnUpdate(); // 메인 창 메시지 펌프

            for (auto it = m_SecondaryWindows.begin(); it != m_SecondaryWindows.end(); )
            {
                Window* secWin = *it;
                secWin->OnUpdate(); // 서브 창 메시지 펌프

                if (secWin->ShouldClose())
                {
                    ForgetInputWindow(secWin);
                    delete secWin;
                    it = m_SecondaryWindows.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            AddHitchStage(hitchStages, "WindowMessages", stageStartedAt);

            // =========================================================
            // 2. 엔진 로직과 레이어를 업데이트합니다.
            // =========================================================
            stageStartedAt = std::chrono::steady_clock::now();
            OnUpdate();

            for (Layer* layer : m_LayerStack)
            {
                layer->OnUpdate(0.016f); // 델타타임 (임시 60FPS)
            }
            AddHitchStage(hitchStages, "LayerUpdate", stageStartedAt);

            // =========================================================
            // 3. 메인 창과 서브 창을 같은 렌더링 경로에서 처리합니다.
            // =========================================================

            // 렌더링할 모든 창을 하나의 리스트로 묶습니다.
            std::vector<Window*> allWindows;
            allWindows.push_back(m_Window.get());
            for (Window* secWin : m_SecondaryWindows)
            {
                allWindows.push_back(secWin);
            }

            // 각 창의 백버퍼에 해당 창의 UI 트리만 그립니다.
            for (Window* win : allWindows)
            {
                auto windowRenderStartedAt = std::chrono::steady_clock::now();
                auto context = win->GetContext();
                const bool isMainWindow = (win == m_Window.get());

                if (!context) continue; // 컨텍스트가 없는 창은 렌더링하지 않습니다.

                // 현재 렌더링 대상을 이 창의 백버퍼로 전환합니다.
                stageStartedAt = std::chrono::steady_clock::now();
                context->MakeCurrent();
                context->BindBackBuffer();

                // 패널 도킹/분리로 창의 UI 트리가 바뀌면 이전 프레임의 패널 그림이
                // 백버퍼에 남을 수 있으므로, 창마다 매 프레임 먼저 지웁니다.
                context->Clear(0.08f, 0.08f, 0.09f, 1.0f);
                AddHitchStage(hitchStages, isMainWindow ? "MainContextClear" : "SecondaryContextClear", stageStartedAt);

                // 이 창이 소유한 UI 트리를 업데이트하고 렌더링합니다.
                UI::Widget* rootUI = win->GetRootUI();
                if (rootUI != nullptr)
                {
                    stageStartedAt = std::chrono::steady_clock::now();
                    rootUI->OnUpdate(0.016f);
                    AddHitchStage(hitchStages, isMainWindow ? "MainUIUpdate" : "SecondaryUIUpdate", stageStartedAt);

                    rootUI = win->GetRootUI();
                    if (rootUI == nullptr || win->ShouldClose())
                    {
                        stageStartedAt = std::chrono::steady_clock::now();
                        context->SwapBuffers(isMainWindow);
                        AddHitchStage(hitchStages, isMainWindow ? "MainPresent" : "SecondaryPresent", stageStartedAt);
                        continue;
                    }

                    // UI 업데이트 중 창이 닫히지 않은 경우에만 레이아웃을 갱신합니다.
                    stageStartedAt = std::chrono::steady_clock::now();
                    rootUI->UpdateLayout(
                        { 0.0f, 0.0f },
                        { (float)win->GetWidth(), (float)win->GetHeight() }
                    );
                    AddHitchStage(hitchStages, isMainWindow ? "MainUILayout" : "SecondaryUILayout", stageStartedAt);

                    // 현재 창 크기에 맞는 UI 투영 행렬을 설정합니다.
                    stageStartedAt = std::chrono::steady_clock::now();
                    UIRenderer::BeginUI(win->GetWidth(), win->GetHeight());

                    // 현재 창에 속한 UI만 렌더링합니다.
                    // 활성 입력 창만 hover/click 표시를 켠다. Modal 창이 있으면 그 창만 입력을 받는다.
                    UI::Widget::SetCurrentRenderWindow(win);
                    UI::Widget::SetCurrentRenderWindowMouseActive(IsInputEnabledForWindow(win) && IsMouseOverNativeWindow(win));
                    rootUI->OnRender();
                    UI::Widget::SetCurrentRenderWindowMouseActive(true);
                    UI::Widget::SetCurrentRenderWindow(nullptr);
                    UI::DockManager::DrawPreview(win);
                    AddHitchStage(hitchStages, isMainWindow ? "MainUIRender" : "SecondaryUIRender", stageStartedAt);

                    // 누적된 UI 드로우 콜을 제출합니다.
                    stageStartedAt = std::chrono::steady_clock::now();
                    UIRenderer::EndUI();
                    AddHitchStage(hitchStages, isMainWindow ? "MainUIFlush" : "SecondaryUIFlush", stageStartedAt);
                }

                // 백버퍼를 화면에 표시합니다.
                // 메인 창 하나만 v-sync 대기를 한다. 보조 창까지 대기하면
                // 멀티 윈도우가 열린 상태에서 마우스 입력 프레임이 간헐적으로 건너뛴다.
                stageStartedAt = std::chrono::steady_clock::now();
                context->SwapBuffers(isMainWindow);
                AddHitchStage(hitchStages, isMainWindow ? "MainPresent" : "SecondaryPresent", stageStartedAt);
                AddHitchStage(hitchStages, isMainWindow ? "MainWindowRenderTotal" : "SecondaryWindowRenderTotal", windowRenderStartedAt);
            }

            // 다음 프레임을 위해 렌더링 컨텍스트를 메인 창으로 되돌립니다.
            if (m_Window->GetContext())
            {
                m_Window->GetContext()->MakeCurrent();
            }

            ReportFrameHitch("ApplicationFrame", frameStartedAt, hitchStages);

        }

        std::cout << "CCEngine Shutting Down..." << std::endl;
    }


    void Application::OnEvent(Event& e)
    {
        if (IsInputEvent(e.GetEventType()) && !IsInputEnabledForWindow(m_Window.get()))
        {
            e.Handled = true;
            return;
        }

        // 창 크기 변경과 닫기 이벤트는 애플리케이션에서 먼저 처리합니다.
        if (e.GetEventType() == EventType::WindowResize)
        {
            WindowResizeEvent& resizeEvent = static_cast<WindowResizeEvent&>(e);

            if (resizeEvent.GetWidth() == 0 || resizeEvent.GetHeight() == 0) {
                m_Minimized = true;
            }
            else {
                m_Minimized = false;
                Renderer::OnWindowResize(resizeEvent.GetWidth(), resizeEvent.GetHeight());
            }
            // 리사이즈 이벤트는 UI와 카메라에도 전달되어야 하므로 여기서 소비하지 않습니다.
        }
        else if (e.GetEventType() == EventType::WindowClose)
        {
            // 창 닫기 이벤트가 오면 즉시 게임 루프 종료 플래그 설정
            m_Window->SetShouldClose(true);
            e.Handled = true;
        }

        // 나머지 입력 이벤트는 위쪽 레이어부터 전달합니다.
        for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
        {
            if (e.Handled)
                break; // 위쪽 레이어에서 처리한 이벤트는 아래로 전달하지 않습니다.

            (*it)->OnEvent(e);
        }
    }
}
