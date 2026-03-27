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
        void SetRotation(const DirectX::XMFLOAT4& rotation); // X(Pitch), Y(Yaw), Z(Roll)

        // 카메라의 위치나 회전이 변경되었을 때 뷰 행렬을 다시 계산하도록 GetViewMatrix와 GetViewProjectionMatrix에서 체크
        const DirectX::XMMATRIX& GetViewMatrix() const;
        const DirectX::XMMATRIX& GetViewProjectionMatrix() const;

        //const DirectX::XMMATRIX& GetProjectionMatrix() const { return m_ProjectionMatrix; }
        //const DirectX::XMMATRIX& GetViewMatrix() const { return m_ViewMatrix; }
        //const DirectX::XMMATRIX& GetViewProjectionMatrix() const { return m_ViewProjectionMatrix; }

    private:
        void RecalculateViewMatrix() const; // 카메라의 위치나 회전이 변경되었을 때 뷰 행렬을 다시 계산하는 함수

    private:
        DirectX::XMMATRIX m_ProjectionMatrix;
        // 뷰 행렬과 뷰-투영 행렬은 카메라의 위치나 회전이 변경될 때마다 다시 계산되어야 하므로, mutable로 선언하여 const 함수에서도 수정할 수 있도록 한다.
        mutable DirectX::XMMATRIX m_ViewMatrix;
        mutable DirectX::XMMATRIX m_ViewProjectionMatrix;

        DirectX::XMFLOAT3 m_Position = { 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT4 m_RotationQuat = { 0.0f, 0.0f, 0.0f, 1.0f };

        // 위치나 회전이 변경되어 뷰 행렬을 다시 계산해야 하는지 여부를 나타내는 플래그 (랜더링 해야하므로 처음에는 무조건 true)
        // mutable로 선언하여 const 함수에서도 수정할 수 있도록 한다.
        mutable bool m_IsDirty = true;
    };
}