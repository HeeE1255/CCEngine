#include "PerspectiveCamera.h"

namespace CCEngine
{
    const DirectX::XMMATRIX& PerspectiveCamera::GetViewMatrix() const
    {
        // 더티 플레그가 ture일 때만 행렬을 다시 게산
        // 더티 플레그는 카메라의 위치나 회전이 변경되었을 때 true로 설정되고, 뷰 행렬이 다시 계산된 후에는 false로 리셋
        // 매 프레임마다 불필요하게 뷰 행렬을 계산하는 것을 방지
        if (m_IsDirty)
        {
            RecalculateViewMatrix();
        }
        return m_ViewMatrix;
    }

    const DirectX::XMMATRIX& PerspectiveCamera::GetViewProjectionMatrix() const
    {
        if (m_IsDirty)
        {
            RecalculateViewMatrix();
        }
        return m_ViewProjectionMatrix;
    }

    PerspectiveCamera::PerspectiveCamera(float fov, float aspectRatio, float near, float far)
    {
        // 투영 행렬 계산: 시야각, 화면 비율, 클리핑 평면을 기반으로 3D 공간을 2D 화면에 투영하는 행렬을 계산합니다
        m_ProjectionMatrix = DirectX::XMMatrixPerspectiveFovLH(
            DirectX::XMConvertToRadians(fov), aspectRatio, near, far);

        m_ViewMatrix = DirectX::XMMatrixIdentity();
        m_ViewProjectionMatrix = m_ViewMatrix * m_ProjectionMatrix;
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

    void PerspectiveCamera::RecalculateViewMatrix() const
    {
        // 카메라의 회전과 위치를 기반으로 뷰 행렬을 계산합니다
        DirectX::XMVECTOR quat = DirectX::XMLoadFloat4(&m_RotationQuat);
        DirectX::XMMATRIX transform = DirectX::XMMatrixRotationQuaternion(quat)
            * DirectX::XMMatrixTranslation(m_Position.x, m_Position.y, m_Position.z);

        // 뷰 행렬은 카메라의 변환 행렬의 역행렬입니다. 카메라가 월드 공간에서 어떻게 배치되어 있는지를 나타내는 행렬을 뒤집어서, 월드 공간에서 카메라가 보는 방향으로 변환하는 행렬을 얻습니다
        m_ViewMatrix = DirectX::XMMatrixInverse(nullptr, transform);

        // 뷰-투영 행렬 계산: 뷰 행렬과 투영 행렬을 곱하여, 월드 공간에서 카메라가 보는 방향으로 변환한 후, 2D 화면에 투영하는 행렬을 계산합니다
        m_ViewProjectionMatrix = m_ViewMatrix * m_ProjectionMatrix;

        // 뷰 행렬이 다시 계산되었으므로 더티 플래그를 false로 리셋
        m_IsDirty = false;
    }
}