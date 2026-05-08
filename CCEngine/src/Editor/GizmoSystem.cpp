#include "GizmoSystem.h"
#include "Scene/Components.h"
#include "Renderer/Renderer3D.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/MeshFactory.h"
#include "Utils/MathUtils.h"

namespace CCEngine {

    void GizmoSystem::OnRender(Entity selectedEntity, DirectX::XMMATRIX viewMatrix, DirectX::XMMATRIX projMatrix)
    {
        if (!selectedEntity || m_Mode == GizmoMode::None) return;
        if (!selectedEntity.HasComponent<TransformComponent>()) return;

        auto& tc = selectedEntity.GetComponent<TransformComponent>();
        DirectX::XMVECTOR objPos = DirectX::XMLoadFloat3(&tc.Translation);

        // =========================================================
        // ★ 1. 거리 비례 스케일 유지 (항상 같은 크기/굵기로 보임)
        // =========================================================
        DirectX::XMVECTOR det;
        DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(&det, viewMatrix);
        DirectX::XMVECTOR camPos = invView.r[3];

        // 카메라와 오브젝트 사이의 거리 계산
        float dist = DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMVectorSubtract(camPos, objPos)));

        // 거리에 비례해서 기즈모 크기를 키움 (0.15f는 화면에 적당히 보이게 하는 매직 넘버, 시야각(FOV)에 따라 조절)
        float gizmoScale = dist * 0.15f;
        DirectX::XMMATRIX scaleMat = DirectX::XMMatrixScaling(gizmoScale, gizmoScale, gizmoScale);

        // 최종 기즈모의 기준 트랜스폼 (오브젝트 위치 + 거리 비례 스케일)
        DirectX::XMMATRIX baseTransform = scaleMat * DirectX::XMMatrixTranslation(tc.Translation.x, tc.Translation.y, tc.Translation.z);

        // =========================================================
        // ★ 2. 그림자 끄기 (Unlit) & Depth 무시
        // =========================================================
        RenderCommand::SetDepthTest(false);
        // [TODO] Renderer3D::DrawMesh 호출 시, 라이팅 연산을 무시하고 
        // 단색(Solid Color)만 출력하는 전용 Unlit Shader를 바인딩해야 실선처럼 예쁘게 나옵니다!

        // =========================================================
        // ★ 3. 모드별 모양 렌더링
        // =========================================================
        if (m_Mode == GizmoMode::Translate)
        {
            // [이동 기즈모: 선 + 끝에 원뿔(또는 화살표 머리)]
            // (여기선 임시로 긴 큐브(선) + 짧고 두꺼운 큐브(머리)로 화살표 모양을 흉내냅니다. 나중에 Cone 메쉬로 바꾸세요!)

            // X축 (빨강)
            DirectX::XMMATRIX xLine = DirectX::XMMatrixScaling(1.0f, 0.02f, 0.02f) * DirectX::XMMatrixTranslation(0.5f, 0, 0) * baseTransform;
            DirectX::XMMATRIX xHead = DirectX::XMMatrixScaling(0.15f, 0.08f, 0.08f) * DirectX::XMMatrixTranslation(1.0f, 0, 0) * baseTransform;
            Renderer3D::DrawMesh(xLine, MeshFactory::CreateCube(), nullptr, { 1.0f, 0.2f, 0.2f, 1.0f }, -1);
            Renderer3D::DrawMesh(xHead, MeshFactory::CreateCube(), nullptr, { 1.0f, 0.2f, 0.2f, 1.0f }, -1);

            // Y축 (초록)
            DirectX::XMMATRIX yLine = DirectX::XMMatrixScaling(0.02f, 1.0f, 0.02f) * DirectX::XMMatrixTranslation(0, 0.5f, 0) * baseTransform;
            DirectX::XMMATRIX yHead = DirectX::XMMatrixScaling(0.08f, 0.15f, 0.08f) * DirectX::XMMatrixTranslation(0, 1.0f, 0) * baseTransform;
            Renderer3D::DrawMesh(yLine, MeshFactory::CreateCube(), nullptr, { 0.2f, 1.0f, 0.2f, 1.0f }, -1);
            Renderer3D::DrawMesh(yHead, MeshFactory::CreateCube(), nullptr, { 0.2f, 1.0f, 0.2f, 1.0f }, -1);

            // Z축 (파랑)
            DirectX::XMMATRIX zLine = DirectX::XMMatrixScaling(0.02f, 0.02f, 1.0f) * DirectX::XMMatrixTranslation(0, 0, 0.5f) * baseTransform;
            DirectX::XMMATRIX zHead = DirectX::XMMatrixScaling(0.08f, 0.08f, 0.15f) * DirectX::XMMatrixTranslation(0, 0, 1.0f) * baseTransform;
            Renderer3D::DrawMesh(zLine, MeshFactory::CreateCube(), nullptr, { 0.2f, 0.2f, 1.0f, 1.0f }, -1);
            Renderer3D::DrawMesh(zHead, MeshFactory::CreateCube(), nullptr, { 0.2f, 0.2f, 1.0f, 1.0f }, -1);
        }
        else if (m_Mode == GizmoMode::Scale)
        {
            // [크기 기즈모: 선 + 끝에 뭉뚝한 정육면체 큐브]
            DirectX::XMMATRIX xLine = DirectX::XMMatrixScaling(1.0f, 0.02f, 0.02f) * DirectX::XMMatrixTranslation(0.5f, 0, 0) * baseTransform;
            DirectX::XMMATRIX xBox = DirectX::XMMatrixScaling(0.1f, 0.1f, 0.1f) * DirectX::XMMatrixTranslation(1.0f, 0, 0) * baseTransform;
            Renderer3D::DrawMesh(xLine, MeshFactory::CreateCube(), nullptr, { 1.0f, 0.4f, 0.4f, 1.0f }, -1);
            Renderer3D::DrawMesh(xBox, MeshFactory::CreateCube(), nullptr, { 1.0f, 0.4f, 0.4f, 1.0f }, -1);

            DirectX::XMMATRIX yLine = DirectX::XMMatrixScaling(0.02f, 1.0f, 0.02f) * DirectX::XMMatrixTranslation(0, 0.5f, 0) * baseTransform;
            DirectX::XMMATRIX yBox = DirectX::XMMatrixScaling(0.1f, 0.1f, 0.1f) * DirectX::XMMatrixTranslation(0, 1.0f, 0) * baseTransform;
            Renderer3D::DrawMesh(yLine, MeshFactory::CreateCube(), nullptr, { 0.4f, 1.0f, 0.4f, 1.0f }, -1);
            Renderer3D::DrawMesh(yBox, MeshFactory::CreateCube(), nullptr, { 0.4f, 1.0f, 0.4f, 1.0f }, -1);

            DirectX::XMMATRIX zLine = DirectX::XMMatrixScaling(0.02f, 0.02f, 1.0f) * DirectX::XMMatrixTranslation(0, 0, 0.5f) * baseTransform;
            DirectX::XMMATRIX zBox = DirectX::XMMatrixScaling(0.1f, 0.1f, 0.1f) * DirectX::XMMatrixTranslation(0, 0, 1.0f) * baseTransform;
            Renderer3D::DrawMesh(zLine, MeshFactory::CreateCube(), nullptr, { 0.4f, 0.4f, 1.0f, 1.0f }, -1);
            Renderer3D::DrawMesh(zBox, MeshFactory::CreateCube(), nullptr, { 0.4f, 0.4f, 1.0f, 1.0f }, -1);
        }
        else if (m_Mode == GizmoMode::Rotate)
        {
            // [회전 기즈모: 둥근 띠 (Torus)]
            // (Torus 메쉬가 없다면 얇은 큐브를 납작하게 만들어서 십자선이라도 그립니다. 나중에 Torus 메쉬로 교체하세요!)
            DirectX::XMMATRIX xRing = DirectX::XMMatrixScaling(0.02f, 1.0f, 1.0f) * baseTransform; // YZ 평면 띠
            Renderer3D::DrawMesh(xRing, MeshFactory::CreateCube(), nullptr, { 1.0f, 0.2f, 0.2f, 1.0f }, -1);

            DirectX::XMMATRIX yRing = DirectX::XMMatrixScaling(1.0f, 0.02f, 1.0f) * baseTransform; // XZ 평면 띠
            Renderer3D::DrawMesh(yRing, MeshFactory::CreateCube(), nullptr, { 0.2f, 1.0f, 0.2f, 1.0f }, -1);

            DirectX::XMMATRIX zRing = DirectX::XMMatrixScaling(1.0f, 1.0f, 0.02f) * baseTransform; // XY 평면 띠
            Renderer3D::DrawMesh(zRing, MeshFactory::CreateCube(), nullptr, { 0.2f, 0.2f, 1.0f, 1.0f }, -1);
        }

        RenderCommand::SetDepthTest(true);
    }

    static float GetAxisIntersection(const Math::Ray& mouseRay, DirectX::XMFLOAT3 objPos, int axisIndex, DirectX::XMMATRIX viewMatrix)
    {
        DirectX::XMVECTOR O = DirectX::XMLoadFloat3(&objPos);
        DirectX::XMVECTOR D; // 우리가 선택한 축의 방향벡터

        if (axisIndex == 0) D = DirectX::XMVectorSet(1, 0, 0, 0);
        else if (axisIndex == 1) D = DirectX::XMVectorSet(0, 1, 0, 0);
        else D = DirectX::XMVectorSet(0, 0, 1, 0);

        // 1. 카메라 위치 가져오기 (ViewMatrix의 역행렬에서 추출)
        DirectX::XMVECTOR det;
        DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(&det, viewMatrix);
        DirectX::XMVECTOR camPos = invView.r[3];

        // 2. 가상 평면(Plane)의 노멀 벡터 생성: (축 방향)과 (카메라->오브젝트 방향)에 모두 직교하는 노멀!
        DirectX::XMVECTOR camToObj = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(O, camPos));
        DirectX::XMVECTOR planeNormal = DirectX::XMVector3Cross(D, DirectX::XMVector3Cross(camToObj, D));
        planeNormal = DirectX::XMVector3Normalize(planeNormal);

        // 3. 마우스 광선과 평면의 충돌 지점(t) 계산
        DirectX::XMVECTOR rayOrigin = DirectX::XMLoadFloat3(&mouseRay.Origin);
        DirectX::XMVECTOR rayDir = DirectX::XMLoadFloat3(&mouseRay.Direction);

        float denom = DirectX::XMVectorGetX(DirectX::XMVector3Dot(planeNormal, rayDir));
        if (abs(denom) > 0.0001f) // 평면과 평행하지 않을 때만
        {
            float t = DirectX::XMVectorGetX(DirectX::XMVector3Dot(planeNormal, DirectX::XMVectorSubtract(O, rayOrigin))) / denom;
            DirectX::XMVECTOR hitPoint = DirectX::XMVectorAdd(rayOrigin, DirectX::XMVectorScale(rayDir, t));

            // 4. 충돌 지점을 우리가 원하는 기즈모 축(D) 위로 투영(Dot)시켜서 최종 거리값 추출
            return DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMVectorSubtract(hitPoint, O), D));
        }
        return 0.0f;
    }

    bool GizmoSystem::OnEvent(Event& e, Entity selectedEntity, DirectX::XMMATRIX viewMatrix, DirectX::XMMATRIX projMatrix, float viewportWidth, float viewportHeight, float viewportX, float viewportY)
    {
        if (!selectedEntity || m_Mode == GizmoMode::None) return false;
        if (!selectedEntity.HasComponent<TransformComponent>()) return false;

        auto& tc = selectedEntity.GetComponent<TransformComponent>();
        DirectX::XMFLOAT3 pos = tc.Translation;

        // 클릭 및 드래그 시작 로직
        if (e.GetEventType() == EventType::MouseButtonPressed)
        {
            auto& me = static_cast<MouseButtonPressedEvent&>(e);
            if (me.GetButton() != 0) return false;

            float mouseX = me.GetX() - viewportX;
            float mouseY = me.GetY() - viewportY;
            Math::Ray mouseRay = Math::MathUtils::ScreenPosToWorldRay(mouseX, mouseY, viewportWidth, viewportHeight, viewMatrix, projMatrix);

            // 카메라 거리 비례 판정 두께 조절 (거리가 멀면 잡기 판정도 넓어짐)
            DirectX::XMVECTOR camPos = DirectX::XMMatrixInverse(nullptr, viewMatrix).r[3];
            float distToCam = DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMVectorSubtract(camPos, DirectX::XMLoadFloat3(&pos))));
            float dynamicThickness = distToCam * 0.1f;
            float dynamicLength = distToCam * 0.2f;

            DirectX::XMFLOAT3 axisDirs[3] = { {1,0,0}, {0,1,0}, {0,0,1} };
            float closestDist = 999.0f;
            int hitAxis = -1;

            // Translate & Scale 피킹 로직
            if (m_Mode == GizmoMode::Translate || m_Mode == GizmoMode::Scale)
            {
                for (int i = 0; i < 3; i++) {
                    Math::Ray axisRay = { pos, axisDirs[i] };
                    float tA, tR;
                    float dist = Math::MathUtils::ClosestPointBetweenTwoLines(axisRay, mouseRay, tA, tR);
                    if (dist < dynamicThickness && tA > 0.0f && tA < dynamicLength && dist < closestDist) {
                        closestDist = dist; hitAxis = i;
                    }
                }
            }
            // Rotate 피킹 로직 (평면 교차 판정)
            else if (m_Mode == GizmoMode::Rotate)
            {
                for (int i = 0; i < 3; i++) {
                    DirectX::XMVECTOR normal = DirectX::XMLoadFloat3(&axisDirs[i]);
                    float t;
                    if (Math::MathUtils::RayPlaneIntersection(mouseRay, DirectX::XMLoadFloat3(&pos), normal, t)) {
                        DirectX::XMVECTOR hitPoint = DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&mouseRay.Origin), DirectX::XMVectorScale(DirectX::XMLoadFloat3(&mouseRay.Direction), t));
                        float distFromCenter = DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMVectorSubtract(hitPoint, DirectX::XMLoadFloat3(&pos))));

                        // 원형 띠 근처를 잡았는지 판정 (반지름 근처)
                        if (abs(distFromCenter - (dynamicLength * 0.8f)) < dynamicThickness) {
                            hitAxis = i; break;
                        }
                    }
                }
            }

            if (hitAxis != -1) {
                m_IsDragging = true;
                m_ActiveAxis = hitAxis;
                m_OriginalPosition = tc.Translation; // 회전/크기에도 위치 정보는 기준점으로 쓰임

                // ★ 추가: 트랜스폼의 원래 상태 저장
                static DirectX::XMFLOAT3 s_OriginalScale;
                static DirectX::XMFLOAT3 s_OriginalRotation;
                s_OriginalScale = tc.Scale;
                s_OriginalRotation = tc.Rotation;

                DirectX::XMVECTOR D = DirectX::XMLoadFloat3(&axisDirs[m_ActiveAxis]);

                if (m_Mode == GizmoMode::Translate || m_Mode == GizmoMode::Scale)
                {
                    DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, viewMatrix);
                    DirectX::XMVECTOR camToObj = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&m_OriginalPosition), invView.r[3]));
                    DirectX::XMVECTOR planeNormal = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(D, DirectX::XMVector3Cross(camToObj, D)));

                    float t;
                    if (Math::MathUtils::RayPlaneIntersection(mouseRay, DirectX::XMLoadFloat3(&m_OriginalPosition), planeNormal, t)) {
                        DirectX::XMVECTOR hitPoint = DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&mouseRay.Origin), DirectX::XMVectorScale(DirectX::XMLoadFloat3(&mouseRay.Direction), t));
                        m_InitialDragOffset = DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMVectorSubtract(hitPoint, DirectX::XMLoadFloat3(&m_OriginalPosition)), D));
                    }
                }
                else if (m_Mode == GizmoMode::Rotate)
                {
                    // 회전 시작 벡터 저장
                    float t;
                    if (Math::MathUtils::RayPlaneIntersection(mouseRay, DirectX::XMLoadFloat3(&m_OriginalPosition), D, t)) {
                        DirectX::XMVECTOR hitPoint = DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&mouseRay.Origin), DirectX::XMVectorScale(DirectX::XMLoadFloat3(&mouseRay.Direction), t));
                        DirectX::XMVECTOR hitVec = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(hitPoint, DirectX::XMLoadFloat3(&m_OriginalPosition)));

                        // InitialDragOffset 대신 벡터 자체를 보관 (임시로 변환)
                        DirectX::XMFLOAT3 initialVec;
                        DirectX::XMStoreFloat3(&initialVec, hitVec);
                        // [주의] 실제 환경에서는 클래스 멤버로 m_InitialRotationVector (XMFLOAT3) 를 추가해서 저장하는 것이 좋습니다.
                        // 여기서는 간략화를 위해 임시 처리
                    }
                }
                e.Handled = true; return true;
            }
        }
        else if (e.GetEventType() == EventType::MouseMoved && m_IsDragging)
        {
            auto& me = static_cast<MouseMovedEvent&>(e);
            float mouseX = me.GetX() - viewportX;
            float mouseY = me.GetY() - viewportY;
            Math::Ray mouseRay = Math::MathUtils::ScreenPosToWorldRay(mouseX, mouseY, viewportWidth, viewportHeight, viewMatrix, projMatrix);
            DirectX::XMFLOAT3 axisDirs[3] = { {1,0,0}, {0,1,0}, {0,0,1} };
            DirectX::XMVECTOR D = DirectX::XMLoadFloat3(&axisDirs[m_ActiveAxis]);

            // ==========================================
            // 이동 (Translate)
            // ==========================================
            if (m_Mode == GizmoMode::Translate)
            {
                DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, viewMatrix);
                DirectX::XMVECTOR camToObj = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&m_OriginalPosition), invView.r[3]));
                DirectX::XMVECTOR planeNormal = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(D, DirectX::XMVector3Cross(camToObj, D)));

                float t;
                if (Math::MathUtils::RayPlaneIntersection(mouseRay, DirectX::XMLoadFloat3(&m_OriginalPosition), planeNormal, t)) {
                    DirectX::XMVECTOR hitPoint = DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&mouseRay.Origin), DirectX::XMVectorScale(DirectX::XMLoadFloat3(&mouseRay.Direction), t));
                    float currentPoint = DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMVectorSubtract(hitPoint, DirectX::XMLoadFloat3(&m_OriginalPosition)), D));

                    float delta = currentPoint - m_InitialDragOffset;
                    tc.Translation = m_OriginalPosition;
                    if (m_ActiveAxis == 0) tc.Translation.x += delta;
                    else if (m_ActiveAxis == 1) tc.Translation.y += delta;
                    else if (m_ActiveAxis == 2) tc.Translation.z += delta;
                }
            }
            // ==========================================
            // 크기 (Scale)
            // ==========================================
            else if (m_Mode == GizmoMode::Scale)
            {
                DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, viewMatrix);
                DirectX::XMVECTOR camToObj = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&m_OriginalPosition), invView.r[3]));
                DirectX::XMVECTOR planeNormal = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(D, DirectX::XMVector3Cross(camToObj, D)));

                float t;
                if (Math::MathUtils::RayPlaneIntersection(mouseRay, DirectX::XMLoadFloat3(&m_OriginalPosition), planeNormal, t)) {
                    DirectX::XMVECTOR hitPoint = DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&mouseRay.Origin), DirectX::XMVectorScale(DirectX::XMLoadFloat3(&mouseRay.Direction), t));
                    float currentPoint = DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMVectorSubtract(hitPoint, DirectX::XMLoadFloat3(&m_OriginalPosition)), D));

                    // 스케일은 마우스를 오른쪽으로 당기면 커지고 왼쪽이면 작아짐. (민감도 조절 가능)
                    float delta = (currentPoint - m_InitialDragOffset) * 0.5f;

                    // (이전 tc.Scale 저장 변수가 필요합니다. 임시로 tc.Scale 자체에 더함)
                    if (m_ActiveAxis == 0) tc.Scale.x += delta;
                    else if (m_ActiveAxis == 1) tc.Scale.y += delta;
                    else if (m_ActiveAxis == 2) tc.Scale.z += delta;

                    m_InitialDragOffset = currentPoint; // 스케일은 실시간 누적
                }
            }
            // ==========================================
            // 회전 (Rotate)
            // ==========================================
            else if (m_Mode == GizmoMode::Rotate)
            {
                // [간이 회전 로직] 마우스를 축 방향으로 드래그하면 회전 각도로 변환
                // 완벽한 원형 각도 계산을 하려면 벡터 Atan2 내적 수학이 필요하지만,
                // 가장 직관적인 '수직 드래그' 방식으로 우선 구현합니다.
                DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, viewMatrix);
                DirectX::XMVECTOR camToObj = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&m_OriginalPosition), invView.r[3]));
                DirectX::XMVECTOR planeNormal = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(D, DirectX::XMVector3Cross(camToObj, D)));

                float t;
                if (Math::MathUtils::RayPlaneIntersection(mouseRay, DirectX::XMLoadFloat3(&m_OriginalPosition), planeNormal, t)) {
                    DirectX::XMVECTOR hitPoint = DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&mouseRay.Origin), DirectX::XMVectorScale(DirectX::XMLoadFloat3(&mouseRay.Direction), t));
                    float currentPoint = DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMVectorSubtract(hitPoint, DirectX::XMLoadFloat3(&m_OriginalPosition)), D));

                    float angleDelta = (currentPoint - m_InitialDragOffset) * 2.0f; // 마우스 이동량 -> 라디안 각도

                    if (m_ActiveAxis == 0) tc.Rotation.x += angleDelta;
                    else if (m_ActiveAxis == 1) tc.Rotation.y += angleDelta;
                    else if (m_ActiveAxis == 2) tc.Rotation.z += angleDelta;

                    // 쿼터니언 업데이트 필수!
                    DirectX::XMStoreFloat4(&tc.QuaternionRotation, DirectX::XMQuaternionRotationRollPitchYaw(tc.Rotation.x, tc.Rotation.y, tc.Rotation.z));

                    m_InitialDragOffset = currentPoint;
                }
            }

            e.Handled = true; return true;
        }
        else if (e.GetEventType() == EventType::MouseButtonReleased) {
            m_IsDragging = false;
            m_ActiveAxis = -1;
            return false;
        }

        return false;
    }
}

