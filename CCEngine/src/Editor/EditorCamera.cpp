#include "EditorCamera.h"
#include <windows.h> // GetAsyncKeyState, GetCursorPos 용도

// [ImGui 완전 제거됨]
// #include "imgui.h"

namespace CCEngine
{
    EditorCamera::EditorCamera(float fov, float aspectRatio, float nearClip, float farClip)
        : PerspectiveCamera(fov, aspectRatio, nearClip, farClip)
    {
        // 생성자에서 카메라의 초기 위치와 회전을 설정하는 함수
        ResetCamera();
    }

    void EditorCamera::OnUpdate(float deltaTime)
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

            if (GetAsyncKeyState('W') & 0x8000) pos = DirectX::XMVectorAdd(pos, DirectX::XMVectorScale(forward, moveSpeed));
            if (GetAsyncKeyState('S') & 0x8000) pos = DirectX::XMVectorSubtract(pos, DirectX::XMVectorScale(forward, moveSpeed));
            if (GetAsyncKeyState('D') & 0x8000) pos = DirectX::XMVectorAdd(pos, DirectX::XMVectorScale(right, moveSpeed));
            if (GetAsyncKeyState('A') & 0x8000) pos = DirectX::XMVectorSubtract(pos, DirectX::XMVectorScale(right, moveSpeed));
            if (GetAsyncKeyState('E') & 0x8000) pos = DirectX::XMVectorAdd(pos, DirectX::XMVectorScale(up, moveSpeed));
            if (GetAsyncKeyState('Q') & 0x8000) pos = DirectX::XMVectorSubtract(pos, DirectX::XMVectorScale(up, moveSpeed));

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
}