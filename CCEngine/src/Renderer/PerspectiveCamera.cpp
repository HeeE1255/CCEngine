#include "PerspectiveCamera.h"

namespace CCEngine
{
    PerspectiveCamera::PerspectiveCamera(float fov, float aspectRatio, float nearClip, float farClip)
        : m_FOV(fov), m_AspectRatio(aspectRatio), m_NearClip(nearClip), m_FarClip(farClip)
    {
        // 생성 시점에 초기 행렬을 한번 세팅
        RecalculateProjectionMatrix();
        m_ViewMatrix = DirectX::XMMatrixIdentity();
        m_ViewProjectionMatrix = m_ViewMatrix * m_ProjectionMatrix;

        m_IsDirty = false;
        m_IsProjectionDirty = false;
    }

    const DirectX::XMMATRIX& PerspectiveCamera::GetViewMatrix() const
    {
        if (m_IsDirty)
            RecalculateViewMatrix();

        return m_ViewMatrix;
    }

    const DirectX::XMMATRIX& PerspectiveCamera::GetProjectionMatrix() const
    {
        if (m_IsProjectionDirty)
            RecalculateProjectionMatrix();

        return m_ProjectionMatrix;
    }

    const DirectX::XMMATRIX& PerspectiveCamera::GetViewProjectionMatrix() const
    {
        // 두 행렬 중 하나라도 더티 상태라면 갱신
        if (m_IsProjectionDirty)
            RecalculateProjectionMatrix();

        if (m_IsDirty)
            RecalculateViewMatrix();

        return m_ViewProjectionMatrix;
    }

    void PerspectiveCamera::SetPosition(const DirectX::XMFLOAT3& position)
    {
        m_Position = position;
        m_IsDirty = true;
    }

    void PerspectiveCamera::SetRotation(const DirectX::XMFLOAT4& rotation)
    {
        m_RotationQuat = rotation;
        m_IsDirty = true;
    }

    void PerspectiveCamera::SetProjectionMatrix(float fovDegrees, float aspectRatio, float nearClip, float farClip)
    {
        // 최적화: 실제로 값이 달라졌을 때만 재계산을 예약
        if (m_FOV != fovDegrees || m_AspectRatio != aspectRatio || m_NearClip != nearClip || m_FarClip != farClip)
        {
            m_FOV = fovDegrees;
            m_AspectRatio = aspectRatio;
            m_NearClip = nearClip;
            m_FarClip = farClip;

            m_IsProjectionDirty = true;
            m_IsDirty = true; 
        }
    }

    void PerspectiveCamera::RecalculateViewMatrix() const
    {
        DirectX::XMVECTOR quat = DirectX::XMLoadFloat4(&m_RotationQuat);
        DirectX::XMMATRIX transform = DirectX::XMMatrixRotationQuaternion(quat)
            * DirectX::XMMatrixTranslation(m_Position.x, m_Position.y, m_Position.z);

        m_ViewMatrix = DirectX::XMMatrixInverse(nullptr, transform);
        m_ViewProjectionMatrix = m_ViewMatrix * m_ProjectionMatrix; // V * P

        m_IsDirty = false;
    }

    void PerspectiveCamera::RecalculateProjectionMatrix() const
    {
        float fovRadians = DirectX::XMConvertToRadians(m_FOV);
        m_ProjectionMatrix = DirectX::XMMatrixPerspectiveFovLH(fovRadians, m_AspectRatio, m_NearClip, m_FarClip);

        m_ViewProjectionMatrix = m_ViewMatrix * m_ProjectionMatrix;

        m_IsProjectionDirty = false;
    }
}