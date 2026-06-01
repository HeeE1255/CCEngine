#include "GizmoSystem.h"
#include "Scene/Components.h"
#include "Renderer/Renderer3D.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/MeshFactory.h"
#include "Utils/MathUtils.h"
#include <algorithm>
#include <functional>

namespace CCEngine {
    namespace
    {
        DirectX::XMMATRIX GetLocalTransform(Entity entity)
        {
            auto& tc = entity.GetComponent<TransformComponent>();
            return DirectX::XMMatrixScaling(tc.Scale.x, tc.Scale.y, tc.Scale.z) *
                DirectX::XMMatrixRotationQuaternion(DirectX::XMLoadFloat4(&tc.QuaternionRotation)) *
                DirectX::XMMatrixTranslation(tc.Translation.x, tc.Translation.y, tc.Translation.z);
        }

        DirectX::XMMATRIX GetWorldTransform(Entity entity)
        {
            DirectX::XMMATRIX transform = GetLocalTransform(entity);

            if (entity.HasComponent<RelationshipComponent>())
            {
                entt::entity parentID = entity.GetComponent<RelationshipComponent>().Parent;
                if (parentID != entt::null)
                {
                    Entity parent{ parentID, entity.GetScene() };
                    transform = transform * GetWorldTransform(parent);
                }
            }

            return transform;
        }

        DirectX::XMFLOAT3 GetWorldPosition(Entity entity)
        {
            DirectX::XMFLOAT3 position;
            DirectX::XMStoreFloat3(&position, GetWorldTransform(entity).r[3]);
            return position;
        }

        DirectX::XMMATRIX GetParentWorldTransform(Entity entity)
        {
            if (entity.HasComponent<RelationshipComponent>())
            {
                entt::entity parentID = entity.GetComponent<RelationshipComponent>().Parent;
                if (parentID != entt::null)
                {
                    return GetWorldTransform(Entity{ parentID, entity.GetScene() });
                }
            }

            return DirectX::XMMatrixIdentity();
        }

        DirectX::XMMATRIX BuildBoneLineTransform(const DirectX::XMFLOAT3& start, const DirectX::XMFLOAT3& end, float thickness)
        {
            DirectX::XMVECTOR p0 = DirectX::XMLoadFloat3(&start);
            DirectX::XMVECTOR p1 = DirectX::XMLoadFloat3(&end);
            DirectX::XMVECTOR delta = DirectX::XMVectorSubtract(p1, p0);
            float length = DirectX::XMVectorGetX(DirectX::XMVector3Length(delta));
            if (length <= 0.0001f)
            {
                return DirectX::XMMatrixIdentity();
            }

            DirectX::XMVECTOR dir = DirectX::XMVector3Normalize(delta);
            DirectX::XMVECTOR from = DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
            DirectX::XMVECTOR axis = DirectX::XMVector3Cross(from, dir);
            float axisLength = DirectX::XMVectorGetX(DirectX::XMVector3Length(axis));
            float dot = std::clamp(DirectX::XMVectorGetX(DirectX::XMVector3Dot(from, dir)), -1.0f, 1.0f);

            DirectX::XMMATRIX rotation = DirectX::XMMatrixIdentity();
            if (axisLength > 0.0001f)
            {
                axis = DirectX::XMVector3Normalize(axis);
                rotation = DirectX::XMMatrixRotationAxis(axis, std::acos(dot));
            }
            else if (dot < 0.0f)
            {
                rotation = DirectX::XMMatrixRotationZ(DirectX::XM_PI);
            }

            DirectX::XMVECTOR mid = DirectX::XMVectorScale(DirectX::XMVectorAdd(p0, p1), 0.5f);
            DirectX::XMFLOAT3 midpoint;
            DirectX::XMStoreFloat3(&midpoint, mid);

            return DirectX::XMMatrixScaling(length, thickness, thickness) *
                rotation *
                DirectX::XMMatrixTranslation(midpoint.x, midpoint.y, midpoint.z);
        }

        Entity FindModelRoot(Entity entity)
        {
            Entity current = entity;
            while (current)
            {
                if (current.HasComponent<ModelComponent>())
                {
                    return current;
                }

                if (!current.HasComponent<RelationshipComponent>())
                {
                    break;
                }

                entt::entity parentID = current.GetComponent<RelationshipComponent>().Parent;
                if (parentID == entt::null)
                {
                    break;
                }

                current = { parentID, current.GetScene() };
            }

            return {};
        }
    }

    void GizmoSystem::Init()
    {
        m_GizmoShader.reset(Shader::Create("assets/shaders/GizmoShader.hlsl"));
    }

    void GizmoSystem::OnRenderSkeleton(Entity selectedEntity)
    {
        if (!selectedEntity)
        {
            return;
        }

        Entity modelRoot = FindModelRoot(selectedEntity);
        if (!modelRoot || !modelRoot.HasComponent<ModelComponent>())
        {
            return;
        }

        auto& modelComponent = modelRoot.GetComponent<ModelComponent>();
        if (!modelComponent.TargetModel || modelComponent.NodePathEntityMap.empty())
        {
            return;
        }

        static auto jointMesh = MeshFactory::CreateCube();
        static auto lineMesh = MeshFactory::CreateCube();

        RenderCommand::SetDepthTest(false);

        auto isSkeletonNode = [&modelComponent](Entity entity)
            {
                if (!entity || entity.HasComponent<MeshComponent>())
                {
                    return false;
                }

                for (const auto& [path, handle] : modelComponent.NodePathEntityMap)
                {
                    if (handle == (entt::entity)entity)
                    {
                        return true;
                    }
                }
                return false;
            };

        std::function<void(const ModelNode&)> drawNode = [&](const ModelNode& node)
            {
                Entity nodeEntity;
                auto it = modelComponent.NodePathEntityMap.find(node.Path);
                if (it != modelComponent.NodePathEntityMap.end())
                {
                    nodeEntity = { it->second, selectedEntity.GetScene() };
                }

                bool isSelectedNode = nodeEntity && nodeEntity == selectedEntity;

                if (isSelectedNode)
                {
                    if (!isSkeletonNode(nodeEntity))
                    {
                        return;
                    }

                    DirectX::XMFLOAT3 nodePos = GetWorldPosition(nodeEntity);
                    DirectX::XMMATRIX jointTransform = DirectX::XMMatrixScaling(0.065f, 0.065f, 0.065f) *
                        DirectX::XMMatrixTranslation(nodePos.x, nodePos.y, nodePos.z);
                    Renderer3D::DrawMesh(jointTransform, jointMesh, m_GizmoShader, { 1.0f, 0.82f, 0.22f, 1.0f });

                    if (modelComponent.ShowBoneLinks && nodeEntity.HasComponent<RelationshipComponent>())
                    {
                        auto& rel = nodeEntity.GetComponent<RelationshipComponent>();

                        if (rel.Parent != entt::null)
                        {
                            Entity parentEntity{ rel.Parent, selectedEntity.GetScene() };
                            if (isSkeletonNode(parentEntity))
                            {
                                DirectX::XMFLOAT3 parentPos = GetWorldPosition(parentEntity);
                                DirectX::XMMATRIX lineTransform = BuildBoneLineTransform(parentPos, nodePos, 0.014f);
                                Renderer3D::DrawMesh(lineTransform, lineMesh, m_GizmoShader, { 0.15f, 0.52f, 0.74f, 1.0f });
                            }
                        }

                        for (entt::entity childID : rel.Children)
                        {
                            Entity childEntity{ childID, selectedEntity.GetScene() };
                            if (!isSkeletonNode(childEntity))
                            {
                                continue;
                            }

                            DirectX::XMFLOAT3 childPos = GetWorldPosition(childEntity);
                            DirectX::XMMATRIX lineTransform = BuildBoneLineTransform(nodePos, childPos, 0.014f);
                            Renderer3D::DrawMesh(lineTransform, lineMesh, m_GizmoShader, { 0.15f, 0.52f, 0.74f, 1.0f });
                        }
                    }
                    return;
                }

                for (const auto& child : node.Children)
                {
                    drawNode(child);
                }
            };

        for (const auto& child : modelComponent.TargetModel->GetRootNode().Children)
        {
            drawNode(child);
        }

        RenderCommand::SetDepthTest(true);
    }

    void GizmoSystem::OnRender(Entity selectedEntity, DirectX::XMMATRIX viewMatrix, DirectX::XMMATRIX projMatrix)
    {
        if (!selectedEntity || m_Mode == GizmoMode::None) return;
        if (!selectedEntity.HasComponent<TransformComponent>()) return;

        auto& tc = selectedEntity.GetComponent<TransformComponent>();
        DirectX::XMMATRIX worldTransform = GetWorldTransform(selectedEntity);
        DirectX::XMFLOAT3 worldPosition = GetWorldPosition(selectedEntity);
        DirectX::XMVECTOR objPos = DirectX::XMLoadFloat3(&worldPosition);

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

        // 로컬 모드일 때는 오브젝트의 회전도 반영해서 기즈모가 오브젝트 축에 맞춰지도록 함
        DirectX::XMMATRIX rotationMat = DirectX::XMMatrixIdentity();
        if (m_Space == GizmoSpace::Local)
        {
            DirectX::XMVECTOR scale;
            DirectX::XMVECTOR rotation;
            DirectX::XMVECTOR translation;
            if (DirectX::XMMatrixDecompose(&scale, &rotation, &translation, worldTransform))
            {
                rotationMat = DirectX::XMMatrixRotationQuaternion(rotation);
            }
        }

        // 최종 기즈모의 기준 트랜스폼 (오브젝트 위치 + 거리 비례 스케일 + 회전)
        DirectX::XMMATRIX baseTransform = scaleMat * rotationMat * DirectX::XMMatrixTranslation(worldPosition.x, worldPosition.y, worldPosition.z);

        // =========================================================
        // ★ 2. 그림자 끄기 (Unlit) & Depth 무시
        // =========================================================
        RenderCommand::SetDepthTest(false);

        // =========================================================
        // ★ 3. 모드별 모양 렌더링
        // =========================================================
        if (m_Mode == GizmoMode::Translate)
        {
            // X축 (빨강)
            DirectX::XMMATRIX xLine = DirectX::XMMatrixScaling(1.0f, 0.02f, 0.02f) * DirectX::XMMatrixTranslation(0.5f, 0, 0) * baseTransform;
            DirectX::XMMATRIX xHead = DirectX::XMMatrixScaling(0.15f, 0.08f, 0.08f) * DirectX::XMMatrixTranslation(1.0f, 0, 0) * baseTransform;
            Renderer3D::DrawMesh(xLine, MeshFactory::CreateCube(), m_GizmoShader, { 1.0f, 0.2f, 0.2f, 1.0f });
            Renderer3D::DrawMesh(xHead, MeshFactory::CreateCube(), m_GizmoShader, { 1.0f, 0.2f, 0.2f, 1.0f });

            // Y축 (초록)
            DirectX::XMMATRIX yLine = DirectX::XMMatrixScaling(0.02f, 1.0f, 0.02f) * DirectX::XMMatrixTranslation(0, 0.5f, 0) * baseTransform;
            DirectX::XMMATRIX yHead = DirectX::XMMatrixScaling(0.08f, 0.15f, 0.08f) * DirectX::XMMatrixTranslation(0, 1.0f, 0) * baseTransform;
            Renderer3D::DrawMesh(yLine, MeshFactory::CreateCube(), m_GizmoShader, { 0.2f, 1.0f, 0.2f, 1.0f });
            Renderer3D::DrawMesh(yHead, MeshFactory::CreateCube(), m_GizmoShader, { 0.2f, 1.0f, 0.2f, 1.0f });

            // Z축 (파랑)
            DirectX::XMMATRIX zLine = DirectX::XMMatrixScaling(0.02f, 0.02f, 1.0f) * DirectX::XMMatrixTranslation(0, 0, 0.5f) * baseTransform;
            DirectX::XMMATRIX zHead = DirectX::XMMatrixScaling(0.08f, 0.08f, 0.15f) * DirectX::XMMatrixTranslation(0, 0, 1.0f) * baseTransform;
            Renderer3D::DrawMesh(zLine, MeshFactory::CreateCube(), m_GizmoShader, { 0.2f, 0.2f, 1.0f, 1.0f });
            Renderer3D::DrawMesh(zHead, MeshFactory::CreateCube(), m_GizmoShader, { 0.2f, 0.2f, 1.0f, 1.0f });
        }
        else if (m_Mode == GizmoMode::Scale)
        {
            // [크기 기즈모: 선 + 끝에 뭉뚝한 정육면체 큐브]
            DirectX::XMMATRIX xLine = DirectX::XMMatrixScaling(1.0f, 0.02f, 0.02f) * DirectX::XMMatrixTranslation(0.5f, 0, 0) * baseTransform;
            DirectX::XMMATRIX xBox = DirectX::XMMatrixScaling(0.1f, 0.1f, 0.1f) * DirectX::XMMatrixTranslation(1.0f, 0, 0) * baseTransform;
            Renderer3D::DrawMesh(xLine, MeshFactory::CreateCube(), m_GizmoShader, { 1.0f, 0.4f, 0.4f, 1.0f });
            Renderer3D::DrawMesh(xBox, MeshFactory::CreateCube(), m_GizmoShader, { 1.0f, 0.4f, 0.4f, 1.0f });

            DirectX::XMMATRIX yLine = DirectX::XMMatrixScaling(0.02f, 1.0f, 0.02f) * DirectX::XMMatrixTranslation(0, 0.5f, 0) * baseTransform;
            DirectX::XMMATRIX yBox = DirectX::XMMatrixScaling(0.1f, 0.1f, 0.1f) * DirectX::XMMatrixTranslation(0, 1.0f, 0) * baseTransform;
            Renderer3D::DrawMesh(yLine, MeshFactory::CreateCube(), m_GizmoShader, { 0.4f, 1.0f, 0.4f, 1.0f });
            Renderer3D::DrawMesh(yBox, MeshFactory::CreateCube(), m_GizmoShader, { 0.4f, 1.0f, 0.4f, 1.0f });

            DirectX::XMMATRIX zLine = DirectX::XMMatrixScaling(0.02f, 0.02f, 1.0f) * DirectX::XMMatrixTranslation(0, 0, 0.5f) * baseTransform;
            DirectX::XMMATRIX zBox = DirectX::XMMatrixScaling(0.1f, 0.1f, 0.1f) * DirectX::XMMatrixTranslation(0, 0, 1.0f) * baseTransform;
            Renderer3D::DrawMesh(zLine, MeshFactory::CreateCube(), m_GizmoShader, { 0.4f, 0.4f, 1.0f, 1.0f });
            Renderer3D::DrawMesh(zBox, MeshFactory::CreateCube(), m_GizmoShader, { 0.4f, 0.4f, 1.0f, 1.0f });
        }
        else if (m_Mode == GizmoMode::Rotate)
        {
            static auto torusMesh = MeshFactory::CreateTorus(1.0f, 0.04f, 48, 16);

            // Z축 띠 (파랑)
            DirectX::XMMATRIX zRing = baseTransform;
            Renderer3D::DrawMesh(zRing, torusMesh, m_GizmoShader, { 0.2f, 0.2f, 1.0f, 1.0f });

            // Y축 띠 (초록)
            DirectX::XMMATRIX yRing = DirectX::XMMatrixRotationX(DirectX::XM_PIDIV2) * baseTransform;
            Renderer3D::DrawMesh(yRing, torusMesh, m_GizmoShader, { 0.2f, 1.0f, 0.2f, 1.0f });

            // X축 띠 (빨강)
            DirectX::XMMATRIX xRing = DirectX::XMMatrixRotationY(DirectX::XM_PIDIV2) * baseTransform;
            Renderer3D::DrawMesh(xRing, torusMesh, m_GizmoShader, { 1.0f, 0.2f, 0.2f, 1.0f });
        }

        RenderCommand::SetDepthTest(true);
    }

    bool GizmoSystem::OnEvent(Event& e, Entity selectedEntity, DirectX::XMMATRIX viewMatrix, DirectX::XMMATRIX projMatrix, float viewportWidth, float viewportHeight, float viewportX, float viewportY)
    {
        if (!selectedEntity || m_Mode == GizmoMode::None) return false;
        if (!selectedEntity.HasComponent<TransformComponent>()) return false;

        auto& tc = selectedEntity.GetComponent<TransformComponent>();
        DirectX::XMMATRIX worldTransform = GetWorldTransform(selectedEntity);
        DirectX::XMFLOAT3 worldPosition = GetWorldPosition(selectedEntity);

        bool isLocal = (m_Space == GizmoSpace::Local) || (m_Mode == GizmoMode::Scale);
        DirectX::XMFLOAT3 localAxes[3] = { {1,0,0}, {0,1,0}, {0,0,1} };
        DirectX::XMFLOAT3 axisDirs[3];

        DirectX::XMMATRIX rotMat = DirectX::XMMatrixIdentity();
        if (isLocal) {
            DirectX::XMVECTOR scale;
            DirectX::XMVECTOR rotation;
            DirectX::XMVECTOR translation;
            if (DirectX::XMMatrixDecompose(&scale, &rotation, &translation, worldTransform))
            {
                rotMat = DirectX::XMMatrixRotationQuaternion(rotation);
            }
        }

        for (int i = 0; i < 3; i++) {
            DirectX::XMVECTOR dir = DirectX::XMVector3TransformNormal(DirectX::XMLoadFloat3(&localAxes[i]), rotMat);
            DirectX::XMStoreFloat3(&axisDirs[i], dir);
        }

        // ====================================================================
        // 1. 마우스 누름
        // ====================================================================
        if (e.GetEventType() == EventType::MouseButtonPressed)
        {
            auto& me = static_cast<MouseButtonPressedEvent&>(e);
            if (me.GetButton() != 0) return false;

            float mouseX = me.GetX() - viewportX;
            float mouseY = me.GetY() - viewportY;
            Math::Ray mouseRay = Math::MathUtils::ScreenPosToWorldRay(mouseX, mouseY, viewportWidth, viewportHeight, viewMatrix, projMatrix);

            DirectX::XMVECTOR camPos = DirectX::XMMatrixInverse(nullptr, viewMatrix).r[3];
            float distToCam = DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMVectorSubtract(camPos, DirectX::XMLoadFloat3(&worldPosition))));
            float dynamicThickness = distToCam * 0.1f;
            float dynamicLength = distToCam * 0.2f;

            float closestDist = 999.0f;
            int hitAxis = -1;

            if (m_Mode == GizmoMode::Translate || m_Mode == GizmoMode::Scale)
            {
                for (int i = 0; i < 3; i++)
                {
                    Math::Ray axisRay = { worldPosition, axisDirs[i] };
                    float tA, tR;
                    float dist = Math::MathUtils::ClosestPointBetweenTwoLines(axisRay, mouseRay, tA, tR);
                    if (dist < dynamicThickness && tA > 0.0f && tA < dynamicLength && dist < closestDist)
                    {
                        closestDist = dist; hitAxis = i;
                    }
                }
            }
            else if (m_Mode == GizmoMode::Rotate)
            {
                // 원형 기즈모의 실제 반지름 (OnRender의 gizmoScale * 1.0f(Torus 기본 반지름) 에 맞춤)
                float ringRadius = distToCam * 0.15f;
                // 판정 기본 두께 (Torus의 굵기 비율에 맞게 조정)
                float baseThickness = ringRadius * 0.15f;

                for (int i = 0; i < 3; i++)
                {
                    DirectX::XMVECTOR normal = DirectX::XMLoadFloat3(&axisDirs[i]);

                    // 1. 카메라 시선과 기즈모 평면 사이의 각도(내적) 계산
                    // 1에 가까울수록 정면에서 동그랗게 보는 것, 0에 가까울수록 측면에서 얇은 선처럼 보는 것
                    DirectX::XMVECTOR camDir = DirectX::XMLoadFloat3(&mouseRay.Direction);
                    float dotProduct = DirectX::XMVectorGetX(DirectX::XMVector3Dot(camDir, normal));

                    // ★ 핵심 1: 사각지대 보정 (Angle Compensation)
                    // 띠가 화면에서 얇게 보일수록 판정 두께를 동적으로 최대 10배까지 늘려줌!
                    float angleCompensation = 1.0f / (std::max(abs(dotProduct), 0.1f));
                    float compensatedThickness = baseThickness * angleCompensation;

                    float t;
                    if (Math::MathUtils::RayPlaneIntersection(mouseRay, DirectX::XMLoadFloat3(&worldPosition), normal, t))
                    {
                        DirectX::XMVECTOR hitPoint = DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&mouseRay.Origin), DirectX::XMVectorScale(camDir, t));
                        float distFromCenter = DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMVectorSubtract(hitPoint, DirectX::XMLoadFloat3(&worldPosition))));

                        // 보정된 두께를 사용하여 반지름 근처를 클릭했는지 넉넉하게 확인
                        if (abs(distFromCenter - ringRadius) < compensatedThickness)
                        {
                            // ★ 핵심 2: 깊이(Z) 판정 추가 (break 삭제)
                            // 띠들이 겹쳐 있을 때 무조건 카메라에서 '가장 가까운 띠'를 잡도록 함!
                            if (t < closestDist)
                            {
                                closestDist = t;
                                hitAxis = i;
                            }
                        }
                    }
                }
            }

            if (hitAxis != -1)
            {
                m_IsDragging = true;
                m_ActiveAxis = hitAxis;
                m_OriginalPosition = worldPosition;
                m_OriginalScale = tc.Scale;
                m_OriginalQuat = tc.QuaternionRotation;

                // 회전 시 축이 실시간으로 비틀리는 현상을 막기 위해 클릭 시점의 축을 영구 박제!
                m_DragAxis = axisDirs[m_ActiveAxis];
                DirectX::XMVECTOR D = DirectX::XMLoadFloat3(&m_DragAxis);

                if (m_Mode == GizmoMode::Translate || m_Mode == GizmoMode::Scale)
                {
                    DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, viewMatrix);
                    DirectX::XMVECTOR camToObj = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&m_OriginalPosition), invView.r[3]));
                    DirectX::XMVECTOR planeNormal = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(D, DirectX::XMVector3Cross(camToObj, D)));

                    float t;
                    if (Math::MathUtils::RayPlaneIntersection(mouseRay, DirectX::XMLoadFloat3(&m_OriginalPosition), planeNormal, t))
                    {
                        DirectX::XMVECTOR hitPoint = DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&mouseRay.Origin), DirectX::XMVectorScale(DirectX::XMLoadFloat3(&mouseRay.Direction), t));
                        m_InitialDragOffset = DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMVectorSubtract(hitPoint, DirectX::XMLoadFloat3(&m_OriginalPosition)), D));
                    }
                }
                else if (m_Mode == GizmoMode::Rotate)
                {
                    float t;
                    if (Math::MathUtils::RayPlaneIntersection(mouseRay, DirectX::XMLoadFloat3(&m_OriginalPosition), D, t)) {
          
                        DirectX::XMVECTOR hitPoint = DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&mouseRay.Origin), DirectX::XMVectorScale(DirectX::XMLoadFloat3(&mouseRay.Direction), t));
                        DirectX::XMVECTOR hitVec = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(hitPoint, DirectX::XMLoadFloat3(&m_OriginalPosition)));
                        DirectX::XMStoreFloat3(&m_InitialRotVec, hitVec);
                    }
                }
                e.Handled = true; return true;
            }
        }
        // ====================================================================
        // 2. 마우스 이동
        // ====================================================================
        else if (e.GetEventType() == EventType::MouseMoved && m_IsDragging)
        {
            auto& me = static_cast<MouseMovedEvent&>(e);
            float mouseX = me.GetX() - viewportX;
            float mouseY = me.GetY() - viewportY;
            Math::Ray mouseRay = Math::MathUtils::ScreenPosToWorldRay(mouseX, mouseY, viewportWidth, viewportHeight, viewMatrix, projMatrix);

            // ★ 중요: 드래그 중에는 무조건 박제된 축을 사용하여 기준점이 흔들리는 것을 방지!
            DirectX::XMVECTOR D = DirectX::XMLoadFloat3(&m_DragAxis);

            if (m_Mode == GizmoMode::Translate)
            {
                DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, viewMatrix);
                DirectX::XMVECTOR camToObj = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&m_OriginalPosition), invView.r[3]));
                DirectX::XMVECTOR planeNormal = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(D, DirectX::XMVector3Cross(camToObj, D)));

                float t;
                if (Math::MathUtils::RayPlaneIntersection(mouseRay, DirectX::XMLoadFloat3(&m_OriginalPosition), planeNormal, t))
                {
                    DirectX::XMVECTOR hitPoint = DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&mouseRay.Origin), DirectX::XMVectorScale(DirectX::XMLoadFloat3(&mouseRay.Direction), t));
                    float currentPoint = DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMVectorSubtract(hitPoint, DirectX::XMLoadFloat3(&m_OriginalPosition)), D));

                    float delta = currentPoint - m_InitialDragOffset;
                    DirectX::XMVECTOR newWorldPos = DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&m_OriginalPosition), DirectX::XMVectorScale(D, delta));
                    DirectX::XMMATRIX parentWorld = GetParentWorldTransform(selectedEntity);
                    DirectX::XMVECTOR det;
                    DirectX::XMMATRIX invParentWorld = DirectX::XMMatrixInverse(&det, parentWorld);
                    DirectX::XMVECTOR newLocalPos = DirectX::XMVector3TransformCoord(newWorldPos, invParentWorld);
                    DirectX::XMStoreFloat3(&tc.Translation, newLocalPos);
                }
            }
            else if (m_Mode == GizmoMode::Scale)
            {
                DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, viewMatrix);
                DirectX::XMVECTOR camToObj = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&m_OriginalPosition), invView.r[3]));
                DirectX::XMVECTOR planeNormal = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(D, DirectX::XMVector3Cross(camToObj, D)));

                float t;
                if (Math::MathUtils::RayPlaneIntersection(mouseRay, DirectX::XMLoadFloat3(&m_OriginalPosition), planeNormal, t))
                {
                    DirectX::XMVECTOR hitPoint = DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&mouseRay.Origin), DirectX::XMVectorScale(DirectX::XMLoadFloat3(&mouseRay.Direction), t));
                    float currentPoint = DirectX::XMVectorGetX(DirectX::XMVector3Dot(DirectX::XMVectorSubtract(hitPoint, DirectX::XMLoadFloat3(&m_OriginalPosition)), D));

                    float delta = currentPoint - m_InitialDragOffset;

                    tc.Scale = m_OriginalScale;
                    if (m_ActiveAxis == 0) tc.Scale.x += delta;
                    else if (m_ActiveAxis == 1) tc.Scale.y += delta;
                    else if (m_ActiveAxis == 2) tc.Scale.z += delta;
                }
            }
            else if (m_Mode == GizmoMode::Rotate)
            {
               
                float t;
                if (Math::MathUtils::RayPlaneIntersection(mouseRay, DirectX::XMLoadFloat3(&m_OriginalPosition), D, t))
                {
                    DirectX::XMVECTOR hitPoint = DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&mouseRay.Origin), DirectX::XMVectorScale(DirectX::XMLoadFloat3(&mouseRay.Direction), t));
                    DirectX::XMVECTOR currentVec = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(hitPoint, DirectX::XMLoadFloat3(&m_OriginalPosition)));

                    // 시작 벡터와 현재 마우스 벡터 사이의 각도(Radian)를 완벽한 원형 수학(Atan2)으로 추출!
                    DirectX::XMVECTOR initVec = DirectX::XMLoadFloat3(&m_InitialRotVec);
                    DirectX::XMVECTOR crossVec = DirectX::XMVector3Cross(initVec, currentVec);
                    float sinAngle = DirectX::XMVectorGetX(DirectX::XMVector3Dot(crossVec, D));
                    float cosAngle = DirectX::XMVectorGetX(DirectX::XMVector3Dot(initVec, currentVec));
                    float angleDelta = std::atan2(sinAngle, cosAngle);

                    // 짐벌 락 방어용 쿼터니언 회전 적용
                    DirectX::XMVECTOR deltaQuat = DirectX::XMQuaternionRotationAxis(D, angleDelta);
                    DirectX::XMVECTOR newQuat = DirectX::XMQuaternionMultiply(DirectX::XMLoadFloat4(&m_OriginalQuat), deltaQuat);
                    DirectX::XMStoreFloat4(&tc.QuaternionRotation, newQuat);

                    // 엔진 인스펙터 출력을 위한 오일러 변환
                    DirectX::XMFLOAT4 qv;
                    DirectX::XMStoreFloat4(&qv, newQuat);
                    float sinp = 2.0f * (qv.w * qv.x - qv.y * qv.z);
                    tc.Rotation.x = std::abs(sinp) >= 1.0f ? std::copysign(DirectX::XM_PI / 2.0f, sinp) : std::asin(sinp);
                    tc.Rotation.y = std::atan2(2.0f * (qv.w * qv.y + qv.z * qv.x), 1.0f - 2.0f * (qv.x * qv.x + qv.y * qv.y));
                    tc.Rotation.z = std::atan2(2.0f * (qv.w * qv.z + qv.x * qv.y), 1.0f - 2.0f * (qv.x * qv.x + qv.z * qv.z));
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
