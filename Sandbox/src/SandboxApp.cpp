#include <Application.h>
#include <EntryPoint.h>
#include <Renderer/Renderer.h>
#include <Renderer/Buffer.h>
#include <Renderer/Shader.h>
#include <DirectXMath.h> 
#include <chrono>
#include <windows.h>
#include <filesystem>

#include "imgui.h" 
#include "Renderer/PerspectiveCamera.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/Texture.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h" // Entity 클래스는 Scene.h에서 전방 선언만 했으므로, 실제 구현이 필요한 경우 Entity.h를 포함
#include "Scene/Components.h"// TransformComponent과 SpriteRendererComponent 구조체가 정의된 헤더 파일
#include "Scene/ScriptableEntity.h" // 스크립트에서 엔티티의 컴포넌트에 접근할 수 있도록 템플릿 함수를 제공하는 ScriptableEntity 클래스가 정의된 헤더 파일
#include "Scene/SceneSerializer.h"  // 씬을 JSON으로 저장/불러오기 하는 SceneSerializer 클래스가 정의된 헤더 파일
#include "Utils/PlatformUtils.h"    // 윈도우 파일 탐색기를 띄우는 PlatformUtils 클래스가 정의된 헤더 파일 (저장기능 추가를 위해)
#include "Editor/SceneHierarchyPanel.h" // 씬 계층 구조 패널 클래스가 정의된 헤더 파일
#include "Core/MemoryMacro.h" // new 매크로 언디파인드 항상 맨 마지막에 가져오기

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class Sandbox : public CCEngine::Application 
{
private:
    CCEngine::Scene* m_ActiveScene = nullptr; // 씬 객체 포인터 (나중에 씬 매니저로 관리할 때도 이 포인터만 바꿔주면 됨)
    CCEngine::SceneHierarchyPanel m_HierarchyPanel; // 씬 계층 구조 패널

    CCEngine::PerspectiveCamera m_Camera;

    DirectX::XMFLOAT3 m_CameraPosition = { 0.0f, 0.0f, -5.0f }; 
    DirectX::XMFLOAT4 m_CameraQuat = { 0.0f, 0.0f, 0.0f, 1.0f };
    float m_ClearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
    std::chrono::time_point<std::chrono::high_resolution_clock> m_LastFrameTime;

    /////////////////////////////
    bool m_IsSPressedLastFrame = false;
    std::string m_CurrentScenePath = "";

    void SaveScene()
    {
        if (m_CurrentScenePath.empty())
        {
            SaveSceneAs();
            return;
        }

        CCEngine::SceneSerializer serializer(m_ActiveScene);
        serializer.Serialize(m_CurrentScenePath); // 그 경로에 덮어쓰기
        printf("Scene Saved to: %s\n", m_CurrentScenePath.c_str());
    }

    void SaveSceneAs()
    {
        // 유틸리티로 윈도우 파일 탐색기
        std::filesystem::path initialDirPath = std::filesystem::current_path() / "assets" / "scenes";
        std::string initialDirStr = initialDirPath.string();

        // 합성된 절대 경로를 c_str()로 변환해서 던져줍니다.
        std::string filepath = CCEngine::PlatformUtils::SaveFile("CCEngine Scene (*.ccscene)\0*.ccscene\0", initialDirStr.c_str());

        if (!filepath.empty()) // 유저가 취소를 안 누르고 경로를 골랐다면?
        {
            m_CurrentScenePath = filepath; // 경로 기억

            CCEngine::SceneSerializer serializer(m_ActiveScene);
            serializer.Serialize(m_CurrentScenePath);
            printf("Scene Saved As: %s\n", m_CurrentScenePath.c_str());
        }
    }

    void OpenScene() // load
    {
        std::filesystem::path initialDirPath = std::filesystem::current_path() / "assets" / "scenes";
        std::string initialDirStr = initialDirPath.string();

        // 윈도우 열기 창 호출!
        std::string filepath = CCEngine::PlatformUtils::OpenFile("CCEngine Scene (*.ccscene)\0*.ccscene\0", initialDirStr.c_str());

        if (!filepath.empty())
        {
            CCEngine::SceneSerializer serializer(m_ActiveScene);
            if (serializer.Deserialize(filepath)) // 성공적으로 읽어왔다면?
            {
                m_CurrentScenePath = filepath; // 내 현재 경로를 갱신! (이제 Ctrl+S 누르면 여기로 덮어써집니다)
                printf("Scene Loaded from: %s\n", m_CurrentScenePath.c_str());
            }
            else
            {
                printf("Failed to load scene!\n");
            }
        }
    }
    //////////////////////////////
    

    public:
        Sandbox()
            : m_Camera(45.0f, 1280.0f / 720.0f, 0.1f, 100.0f)
        {
            // 렌더러 초기화 (GPU 버퍼 생성, 셰이더 컴파일 등)
            CCEngine::Renderer2D::Init();

            // 씬 생성
            m_ActiveScene = new CCEngine::Scene(); 
            // 씬 계층 구조 패널에 씬 연결
            m_HierarchyPanel.SetContext(m_ActiveScene);

            m_LastFrameTime = std::chrono::high_resolution_clock::now();
        }

        ~Sandbox() 
        {
            if (m_ActiveScene != nullptr)
            {
                delete m_ActiveScene; // 씬 메모리 해제
            }

            // 렌더러 종료 (GPU 버퍼 해제, 셰이더 해제 등)
            CCEngine::Renderer2D::Shutdown();
        }

        virtual void OnUpdate() override
        {
            // 프레임 타이밍 계산: 매 프레임마다 현재 시간과 지난 프레임 시간을 비교하여 델타 타임 계산
            auto currentTime = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration<float>(currentTime - m_LastFrameTime).count();
            m_LastFrameTime = currentTime;

            // [!] 마우스 오른쪽 버튼이 눌려있는 동안에만 카메라 조작 허용 /////////////////////////////////////
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
            {
                // 현재 쿼터니언 로드
                DirectX::XMVECTOR quat = DirectX::XMLoadFloat4(&m_CameraQuat);

                // 현재 카메라가 바라보는 로컬 앞, 오른쪽, 위 방향 계산
                DirectX::XMMATRIX camRotationMatrix = DirectX::XMMatrixRotationQuaternion(quat);
                DirectX::XMVECTOR forward = DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), camRotationMatrix);
                DirectX::XMVECTOR right = DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), camRotationMatrix);
                DirectX::XMVECTOR up = DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), camRotationMatrix);

                // 마우스 이동량 가져오기
                ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
                float pitchAngle = mouseDelta.y * 0.003f; // 로컬 X축(Right) 기준 회전량
                float yawAngle = mouseDelta.x * 0.003f; // 로컬 Y축(Up) 기준 회전량

                // 키보드 입력으로 Roll 회전량 계산 (Z축 기준)
                float rollAngle = 0.0f;
                float rollSpeed = 2.0f * deltaTime; // 1초에 2라디안(약 114도) 회전
                if (GetAsyncKeyState('Z') & 0x8000/*GetAsyncKeyState 비트연산자 '키가 눌려있음'*/) rollAngle += rollSpeed; // 반시계
                if (GetAsyncKeyState('C') & 0x8000) rollAngle -= rollSpeed; // 시계

                // 현재 카메라의 Right와 Up 벡터를 축으로 하는 회전 쿼터니언 생성
                DirectX::XMVECTOR qPitch = DirectX::XMQuaternionRotationAxis(right, pitchAngle);
                DirectX::XMVECTOR qYaw = DirectX::XMQuaternionRotationAxis(up, yawAngle);
                DirectX::XMVECTOR qRoll = DirectX::XMQuaternionRotationAxis(forward, rollAngle); 

                // 기존 회전에 새로운 회전 누적 곱셈 (순서 상관없이 현재 로컬에서 계속 더해짐!)
                quat = DirectX::XMQuaternionMultiply(quat, qPitch);
                quat = DirectX::XMQuaternionMultiply(quat, qYaw);
                quat = DirectX::XMQuaternionMultiply(quat, qRoll);

                // 소수점 오차로 찌그러지는 걸 막기 위해 정규화
                quat = DirectX::XMQuaternionNormalize(quat);

                // 계산된 쿼터니언 저장
                DirectX::XMStoreFloat4(&m_CameraQuat, quat);

                // WASD 키보드 입력으로 이동 (속도 * deltaTime)
                float moveSpeed = 5.0f * deltaTime; // 초당 5유닛 이동
                DirectX::XMVECTOR pos = DirectX::XMLoadFloat3(&m_CameraPosition);

                // 윈도우 API를 이용해 어떤 키가 눌렸는지 실시간으로 확인합니다.
                if (GetAsyncKeyState('W') & 0x8000) pos = DirectX::XMVectorAdd(pos, DirectX::XMVectorScale(forward, moveSpeed));
                if (GetAsyncKeyState('S') & 0x8000) pos = DirectX::XMVectorSubtract(pos, DirectX::XMVectorScale(forward, moveSpeed));
                if (GetAsyncKeyState('D') & 0x8000) pos = DirectX::XMVectorAdd(pos, DirectX::XMVectorScale(right, moveSpeed));
                if (GetAsyncKeyState('A') & 0x8000) pos = DirectX::XMVectorSubtract(pos, DirectX::XMVectorScale(right, moveSpeed));
                if (GetAsyncKeyState('E') & 0x8000) pos = DirectX::XMVectorAdd(pos, DirectX::XMVectorScale(up, moveSpeed));
                if (GetAsyncKeyState('Q') & 0x8000) pos = DirectX::XMVectorSubtract(pos, DirectX::XMVectorScale(up, moveSpeed));

                // 계산된 최종 위치를 다시 카메라 변수에 저장!
                DirectX::XMStoreFloat3(&m_CameraPosition, pos);
            }
            ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
            
            // 카메라 업데이트
            m_Camera.SetPosition(m_CameraPosition);
            m_Camera.SetRotation(m_CameraQuat);

            // 씬 저장 및 씬 열기 //////////////////////////////////////////////////////
            bool isCtrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            bool isShiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            bool isSPressedNow = (GetAsyncKeyState('S') & 0x8000) != 0;
            
            // O(오) 키 상태 확인
            static bool s_IsOPressedLastFrame = false;
            bool isOPressedNow = (GetAsyncKeyState('O') & 0x8000) != 0;

            if (isSPressedNow && !m_IsSPressedLastFrame)
            {
                if (isCtrlPressed && isShiftPressed)
                {
                    // [Ctrl + Shift + S] 가 눌렸을 때 -> 무조건 새 창 띄우기
                    SaveSceneAs();
                }
                else if (isCtrlPressed && !isShiftPressed)
                {
                    // [Ctrl + S] 만 눌렸을 때 -> 덮어쓰기 (없으면 새 창)
                    SaveScene();
                }
            }

            if (isCtrlPressed && isOPressedNow && !s_IsOPressedLastFrame)
            {
                OpenScene();
            }
            s_IsOPressedLastFrame = isOPressedNow;

            /////////////////////////////////////////////////////////////////////////////

            m_IsSPressedLastFrame = isSPressedNow; // 상태 저장
            ///////////////////////////////////////////////////////////////////////////


            // 렌더링 준비: 화면 클리어, 텍스처 장착, 상수 버퍼 업데이트
            CCEngine::Renderer::SetClearColor(m_ClearColor[0], m_ClearColor[1], m_ClearColor[2], m_ClearColor[3]);
            CCEngine::Renderer::Clear();
            //m_Texture_0->Bind(0); // 텍스처 슬롯 0에 텍스처 장착 (셰이더에서 sampler2D로 접근할 때 0번 슬롯에서 찾게 됨)

            CCEngine::Renderer2D::BeginScene(m_Camera);

            // 씬 업데이트: 씬 안의 모든 엔티티와 컴포넌트를 업데이트하여, 최종적으로 Renderer2D에 드로우 콜을 요청
            m_ActiveScene->OnUpdate(deltaTime);

            // GPU에 데이터를 보내고 드로우 콜을 한 번만 하는 최적화된 렌더링 방식
            CCEngine::Renderer2D::EndScene(); 

            // ==========================================
            // ImGui 메인 메뉴 바
            // ==========================================
            if (ImGui::BeginMainMenuBar())
            {
                if (ImGui::BeginMenu("File"))
                {
                    if (ImGui::MenuItem("Open Scene...", "Ctrl+O"))
                    {
                        OpenScene();
                    }
                    ImGui::Separator();

                    // 단축키 힌트("Ctrl+S")는 시각적 표시일 뿐, 실제 단축키 작동은 위에서 한 C++ 코드가 처리
                    if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
                    {
                        SaveScene();
                    }

                    if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S"))
                    {
                        SaveSceneAs();
                    }

                    ImGui::Separator(); // 메뉴 사이에 예쁜 가로줄 긋기

                    if (ImGui::MenuItem("Exit", "Alt+F4"))
                    {
                        // 엔진 종료 (Win32 API 기준 가장 확실한 방법)
                        PostQuitMessage(0);
                    }

                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Window"))
                {
                    if (ImGui::MenuItem("Reset Layout"))
                    {
                        m_HierarchyPanel.ResetLayout();

                        // (만약 샌드박스에 직접 만든 '카메라 창'이 있다면 여기서 똑같이 스위치를 켜주면 됩니다!)
                    }
                    ImGui::EndMenu();
                }


                ImGui::EndMainMenuBar();
            }

            // ========================================
            // 씬 계층 구조 패널 업데이트
            // ========================================
            m_HierarchyPanel.OnImGuiRender();

            // ==========================================
            // ImGui 패널
            // ==========================================
            ImGui::Begin("CCEngine Editor");
            ImGui::Text("--- Camera ---");
            ImGui::Text("Position: %.2f, %.2f, %.2f", m_CameraPosition.x, m_CameraPosition.y, m_CameraPosition.z);
            if (ImGui::Button("Reset Camera"))
            {
                m_CameraPosition = { 0.0f, 0.0f, -5.0f };
                m_CameraQuat = { 0.0f, 0.0f, 0.0f, 1.0f };
            }
            ImGui::Separator();
            // ImGui::ColorEdit4는 RGBA 색상 편집 위젯을 생성하는 함수
            // m_ClearColor 배열의 주소를 전달하여, 사용자가 이 색상을 편집
            // 사용자가 색상을 변경하면 m_ClearColor 배열의 값이 자동으로 업데이트
            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            ImGui::End();
        }

};

CCEngine::Application* CCEngine::CreateApplication() 
{
    return new Sandbox();
}