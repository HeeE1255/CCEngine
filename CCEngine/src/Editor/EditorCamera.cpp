#include "EditorCamera.h"
#include <windows.h> // GetAsyncKeyState, GetCursorPos 용도
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string>

// [ImGui 완전 제거됨]
// #include "imgui.h"

namespace CCEngine
{
    namespace
    {
        std::string TrimUpper(std::string value)
        {
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char c) { return !std::isspace(c); }));
            value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), value.end());
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return (char)std::toupper(c); });
            return value;
        }

        int KeyNameToVirtualKey(const std::string& token)
        {
            if (token.size() == 1 && ((token[0] >= 'A' && token[0] <= 'Z') || (token[0] >= '0' && token[0] <= '9')))
                return token[0];
            if (token.size() >= 2 && token[0] == 'F')
            {
                int functionIndex = std::atoi(token.c_str() + 1);
                if (functionIndex >= 1 && functionIndex <= 24)
                    return VK_F1 + functionIndex - 1;
            }

            if (token == "SPACE") return VK_SPACE;
            if (token == "TAB") return VK_TAB;
            if (token == "ENTER") return VK_RETURN;
            if (token == "BACKSPACE") return VK_BACK;
            if (token == "INSERT") return VK_INSERT;
            if (token == "DELETE") return VK_DELETE;
            if (token == "HOME") return VK_HOME;
            if (token == "END") return VK_END;
            if (token == "PAGEUP") return VK_PRIOR;
            if (token == "PAGEDOWN") return VK_NEXT;
            if (token == "LEFT") return VK_LEFT;
            if (token == "RIGHT") return VK_RIGHT;
            if (token == "UP") return VK_UP;
            if (token == "DOWN") return VK_DOWN;
            return 0;
        }

        bool IsBindingPressed(const std::string& binding)
        {
            std::stringstream stream(binding);
            std::string token;
            bool requiresCtrl = false;
            bool requiresShift = false;
            bool requiresAlt = false;
            int mainKey = 0;

            while (std::getline(stream, token, '+'))
            {
                token = TrimUpper(token);
                if (token == "CTRL") requiresCtrl = true;
                else if (token == "SHIFT") requiresShift = true;
                else if (token == "ALT") requiresAlt = true;
                else mainKey = KeyNameToVirtualKey(token);
            }

            if (mainKey == 0)
                return false;

            if (requiresCtrl && !(GetAsyncKeyState(VK_CONTROL) & 0x8000)) return false;
            if (requiresShift && !(GetAsyncKeyState(VK_SHIFT) & 0x8000)) return false;
            if (requiresAlt && !(GetAsyncKeyState(VK_MENU) & 0x8000)) return false;
            return (GetAsyncKeyState(mainKey) & 0x8000) != 0;
        }
    }

    EditorCamera::EditorCamera(float fov, float aspectRatio, float nearClip, float farClip)
        : PerspectiveCamera(fov, aspectRatio, nearClip, farClip)
    {
        // 생성자에서 카메라의 초기 위치와 회전을 설정하는 함수
        ResetCamera();
    }

    void EditorCamera::OnUpdate(float deltaTime, const ProjectSettingsData& settings)
    {
        // 1. 네이티브 Win32 우클릭 감지
        bool isRightMouseDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;

        // 마우스 델타 계산을 위한 정적 변수 (우클릭 중일 때만 갱신)
        static bool s_IsFirstClick = true;
        static POINT s_LastMousePos = { 0, 0 };

        if (isRightMouseDown)
        {
            // 2. 현재 마우스 전역(모니터) 좌표 가져오기
            POINT currentMousePos;
            GetCursorPos(&currentMousePos);

            // 3. 처음 우클릭을 누른 순간이면 튀는(Jump) 현상을 막기 위해 초기화
            if (s_IsFirstClick)
            {
                s_LastMousePos = currentMousePos;
                s_IsFirstClick = false;
            }

            // 4. 네이티브 마우스 델타(변화량) 직접 계산!
            float deltaX = (float)(currentMousePos.x - s_LastMousePos.x);
            float deltaY = (float)(currentMousePos.y - s_LastMousePos.y);

            // 다음 프레임을 위해 현재 좌표 저장
            s_LastMousePos = currentMousePos;

            DirectX::XMVECTOR quat = DirectX::XMLoadFloat4(&m_RotationQuat);

            DirectX::XMMATRIX camRotationMatrix = DirectX::XMMatrixRotationQuaternion(quat);
            DirectX::XMVECTOR forward = DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), camRotationMatrix);
            DirectX::XMVECTOR right = DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), camRotationMatrix);
            DirectX::XMVECTOR up = DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), camRotationMatrix);

            // 5. 직접 계산한 델타값으로 회전 각도 산출 (회전 감도가 너무 빠르면 m_RotationSpeed를 조절하세요)
            float pitchAngle = deltaY * m_RotationSpeed;
            float yawAngle = deltaX * m_RotationSpeed;

            float rollAngle = 0.0f;
            float rollSpeed = 2.0f * deltaTime;
            if (GetAsyncKeyState('Z') & 0x8000) rollAngle += rollSpeed;
            if (GetAsyncKeyState('C') & 0x8000) rollAngle -= rollSpeed;

            DirectX::XMVECTOR qPitch = DirectX::XMQuaternionRotationAxis(right, pitchAngle);
            DirectX::XMVECTOR qYaw = DirectX::XMQuaternionRotationAxis(up, yawAngle);
            DirectX::XMVECTOR qRoll = DirectX::XMQuaternionRotationAxis(forward, rollAngle);

            quat = DirectX::XMQuaternionMultiply(quat, qPitch);
            quat = DirectX::XMQuaternionMultiply(quat, qYaw);
            quat = DirectX::XMQuaternionMultiply(quat, qRoll);
            quat = DirectX::XMQuaternionNormalize(quat);

            DirectX::XMStoreFloat4(&m_RotationQuat, quat);

            float moveSpeed = m_MoveSpeed * deltaTime;
            DirectX::XMVECTOR pos = DirectX::XMLoadFloat3(&m_Position);

            if (IsBindingPressed(settings.MoveForwardKey)) pos = DirectX::XMVectorAdd(pos, DirectX::XMVectorScale(forward, moveSpeed));
            if (IsBindingPressed(settings.MoveBackwardKey)) pos = DirectX::XMVectorSubtract(pos, DirectX::XMVectorScale(forward, moveSpeed));
            if (IsBindingPressed(settings.MoveRightKey)) pos = DirectX::XMVectorAdd(pos, DirectX::XMVectorScale(right, moveSpeed));
            if (IsBindingPressed(settings.MoveLeftKey)) pos = DirectX::XMVectorSubtract(pos, DirectX::XMVectorScale(right, moveSpeed));
            if (IsBindingPressed(settings.MoveUpKey)) pos = DirectX::XMVectorAdd(pos, DirectX::XMVectorScale(up, moveSpeed));
            if (IsBindingPressed(settings.MoveDownKey)) pos = DirectX::XMVectorSubtract(pos, DirectX::XMVectorScale(up, moveSpeed));

            DirectX::XMStoreFloat3(&m_Position, pos);

            // 기반 클래스(PerspectiveCamera)에 갱신된 값 전달 (더티 플래그 발동!)
            PerspectiveCamera::SetPosition(m_Position);
            PerspectiveCamera::SetRotation(m_RotationQuat);
        }
        else
        {
            // 우클릭을 떼면 다음 클릭 시 화면이 튀지 않도록 리셋
            s_IsFirstClick = true;
        }
    }

    void EditorCamera::ResetCamera()
    {
        // [위(Y:3)에서 뒤(Z:-6)로 물러난 위치를 기본값으로 설정
        m_Position = { 5.0f, 5.0f, -5.0f };

        // 아래를 비스듬히 내려다보도록 X축(Pitch)을 20도 회전시킨 쿼터니언 생성
        DirectX::XMVECTOR quat = DirectX::XMQuaternionRotationRollPitchYaw(
            DirectX::XMConvertToRadians(35.0f),
            DirectX::XMConvertToRadians(-45.0f),
            0.0f
        );
        DirectX::XMStoreFloat4(&m_RotationQuat, quat);

        PerspectiveCamera::SetPosition(m_Position);
        PerspectiveCamera::SetRotation(m_RotationQuat);
    }

    void EditorCamera::FrameSelection(const DirectX::XMFLOAT3& target, float radius)
    {
        radius = (std::max)(radius, 0.5f);

        DirectX::XMVECTOR rotation = DirectX::XMLoadFloat4(&m_RotationQuat);
        DirectX::XMMATRIX rotationMatrix = DirectX::XMMatrixRotationQuaternion(rotation);
        DirectX::XMVECTOR forward = DirectX::XMVector3Normalize(
            DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotationMatrix));

        // 프레임 선택은 카메라 회전은 유지하고 위치만 옮긴다.
        // 사용자가 보고 있던 방향을 깨지 않으면, 연속으로 여러 오브젝트를 훑을 때 시점이 덜 튄다.
        float distance = (std::max)(radius * 3.0f, 3.0f);
        DirectX::XMVECTOR targetPos = DirectX::XMLoadFloat3(&target);
        DirectX::XMVECTOR cameraPos = DirectX::XMVectorSubtract(targetPos, DirectX::XMVectorScale(forward, distance));

        DirectX::XMStoreFloat3(&m_Position, cameraPos);
        PerspectiveCamera::SetPosition(m_Position);
        PerspectiveCamera::SetRotation(m_RotationQuat);
    }
}
