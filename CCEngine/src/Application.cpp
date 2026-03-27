#include "Application.h"
#include "Core/Memory.h"
#include "Platform/DirectX11/DX11Context.h"
#include "Renderer/Renderer.h"
#include <iostream>
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"


namespace CCEngine 
{
    Application* Application::s_Instance = nullptr;

    Application::Application()
    {
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

    void Application::OnUpdate()
    {
        // 샌드박스(게임)에서 이 함수를 덮어써서 사용
    }

    void Application::Run()
    {
        std::cout << "CCEngine Started!" << std::endl;

        /////////////////////////////////////////////////////////////////////////////////
        // ImGui 초기화
        IMGUI_CHECKVERSION();   // ImGui 버전 체크
        ImGui::CreateContext(); // ImGui 컨텍스트 생성
        ImGuiIO& io = ImGui::GetIO(); (void)io; // ImGuiIO 구조체에 대한 참조 얻기
        ImGui::StyleColorsDark(); // 다크 모드

        HWND hwnd = DX11Context::Get()->GetWindowHandle(); // DX11Context에서 윈도우 핸들
        ID3D11Device* device = DX11Context::Get()->GetDevice(); // DX11Context에서 디바이스
        ID3D11DeviceContext* context = DX11Context::Get()->GetDeviceContext(); // DX11Context에서 디바이스 컨텍스트
        
        ImGui_ImplWin32_Init(hwnd); // ImGui를 Win32 API와 연결
        ImGui_ImplDX11_Init(device, context); // ImGui를 DirectX 11과 연결
        /////////////////////////////////////////////////////////////////////////////////

        while (!m_Window->ShouldClose()/*창이 닫혔는지 확인*/) // 게임 루프
        {
            // 게임 루프의 각 프레임마다 ImGui 프레임을 시작하고, 샌드박스에서 UI를 정의한 후, 그려주는 과정
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            m_Window->OnUpdate();
            OnUpdate(); // 샌드박스(게임)에서 이 함수를 덮어써서 사용

            // 정의된 UI를 화면에 그리기
            ImGui::Render();
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

            DX11Context::Get()->SwapBuffers();
        }

        ImGui_ImplDX11_Shutdown(); // ImGui의 DirectX 11 백엔드 종료
        ImGui_ImplWin32_Shutdown();// ImGui의 Win32 백엔드 종료
        ImGui::DestroyContext();    // ImGui 컨텍스트 파괴

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