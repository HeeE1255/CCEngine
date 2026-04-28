#pragma once
#include "Core.h"
#include <DirectXMath.h>

namespace CCEngine
{
    class CC_API PerspectiveCamera
    {
    public:
        // fov: 시야각(도 단위), aspectRatio: 화면 비율, nearClip/farClip: 그리기 범위
        PerspectiveCamera(float fov, float aspectRatio, float nearClip, float farClip);

        void SetPosition(const DirectX::XMFLOAT3& position); // X, Y, Z 위치
        void SetRotation(const DirectX::XMFLOAT4& rotation); // X(Pitch), Y(Yaw), Z(Roll) 쿼터니언

        // 각각의 행렬을 요청할 때, 더티 플래그를 확인하여 최신 상태로 반환
        const DirectX::XMMATRIX& GetViewMatrix() const;
        const DirectX::XMMATRIX& GetProjectionMatrix() const;
        const DirectX::XMMATRIX& GetViewProjectionMatrix() const;

        // 카메라 투영 설정 변경
        void SetProjectionMatrix(float fovDegrees, float aspectRatio, float nearClip, float farClip);

        // (선택) 현재 카메라의 설정값을 가져오는 Getter들
        float GetFOV() const { return m_FOV; }
        float GetAspectRatio() const { return m_AspectRatio; }

    private:
        void RecalculateViewMatrix() const;
        void RecalculateProjectionMatrix() const;

    private:
        float m_FOV = 45.0f;
        float m_AspectRatio = 1.778f; // 16:9
        float m_NearClip = 0.1f;
        float m_FarClip = 100.0f;

        DirectX::XMFLOAT3 m_Position = { 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT4 m_RotationQuat = { 0.0f, 0.0f, 0.0f, 1.0f };

        // 더티 플래그와 행렬들은 const getter 내부에서 갱신되어야 하므로 mutable로 선언
        mutable bool m_IsDirty = true;             // 뷰(위치/회전) 변경 확인용
        mutable bool m_IsProjectionDirty = true;   // 프로젝션(화면/시야) 변경 확인용

        mutable DirectX::XMMATRIX m_ProjectionMatrix;
        mutable DirectX::XMMATRIX m_ViewMatrix;
        mutable DirectX::XMMATRIX m_ViewProjectionMatrix;
    };
}