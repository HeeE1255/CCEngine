#pragma once
#include "Renderer/PerspectiveCamera.h"
#include <DirectXMath.h>

namespace CCEngine
{
    class CC_API EditorCamera : public PerspectiveCamera
    {
    public:
        EditorCamera() = default;
        EditorCamera(float fov, float aspectRatio, float nearClip, float farClip);

        // 매 프레임 입력(키보드/마우스)을 처리하는 함수
        void OnUpdate(float deltaTime);

        // UI에 띄워주기 위한 Getter
        const DirectX::XMFLOAT3& GetPosition() const { return m_Position; }
        const DirectX::XMFLOAT4& GetRotation() const { return m_RotationQuat; }

        void ResetCamera();

    private:
        DirectX::XMFLOAT3 m_Position = { 0.0f, 0.0f, -5.0f };
        DirectX::XMFLOAT4 m_RotationQuat = { 0.0f, 0.0f, 0.0f, 1.0f };

        float m_MoveSpeed = 5.0f;
        float m_RotationSpeed = 0.003f;
    };
}