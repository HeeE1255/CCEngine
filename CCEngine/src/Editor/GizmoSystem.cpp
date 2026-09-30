#include "GizmoSystem.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "Renderer/Renderer3D.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/MeshFactory.h"
#include "Utils/MathUtils.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
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

        std::vector<Entity> BuildValidGizmoSelection(const std::vector<Entity>& selectedEntities, Entity activeEntity)
        {
            std::vector<Entity> result;
            auto addIfValid = [&result](Entity entity)
                {
                    if (!entity || !entity.HasComponent<TransformComponent>())
                        return;

                    auto existing = std::find_if(result.begin(), result.end(),
                        [entity](Entity selected) { return selected == entity; });
                    if (existing == result.end())
                        result.push_back(entity);
                };

            for (Entity entity : selectedEntities)
                addIfValid(entity);

            addIfValid(activeEntity);
            return result;
        }

        bool HasAnyCollider(Entity entity)
        {
            return entity && entity.HasComponent<TransformComponent>() &&
                (entity.HasComponent<BoxCollider3DComponent>() ||
                 entity.HasComponent<SphereCollider3DComponent>() ||
                 entity.HasComponent<CylinderCollider3DComponent>() ||
                 entity.HasComponent<MeshCollider3DComponent>());
        }

        std::vector<Entity> BuildColliderSelection(const std::vector<Entity>& selectedEntities, Entity activeEntity)
        {
            std::vector<Entity> result;
            auto add = [&result](Entity entity)
                {
                    if (!HasAnyCollider(entity))
                        return;
                    if (std::find(result.begin(), result.end(), entity) == result.end())
                        result.push_back(entity);
                };

            for (Entity entity : selectedEntities)
                add(entity);
            add(activeEntity);
            return result;
        }

        DirectX::XMFLOAT3 CalculateSelectionCenter(const std::vector<Entity>& entities, Entity activeEntity, GizmoPivotMode pivotMode)
        {
            if (pivotMode == GizmoPivotMode::Pivot || entities.size() <= 1)
                return GetWorldPosition(activeEntity);

            DirectX::XMVECTOR minPoint = DirectX::XMVectorSet(FLT_MAX, FLT_MAX, FLT_MAX, 0.0f);
            DirectX::XMVECTOR maxPoint = DirectX::XMVectorSet(-FLT_MAX, -FLT_MAX, -FLT_MAX, 0.0f);
            bool hasPoint = false;

            for (Entity entity : entities)
            {
                DirectX::XMFLOAT3 worldPosition = GetWorldPosition(entity);
                DirectX::XMVECTOR point = DirectX::XMLoadFloat3(&worldPosition);
                minPoint = DirectX::XMVectorMin(minPoint, point);
                maxPoint = DirectX::XMVectorMax(maxPoint, point);
                hasPoint = true;
            }

            DirectX::XMFLOAT3 center = GetWorldPosition(activeEntity);
            if (hasPoint)
                DirectX::XMStoreFloat3(&center, DirectX::XMVectorScale(DirectX::XMVectorAdd(minPoint, maxPoint), 0.5f));
            return center;
        }

        void StoreEulerFromQuaternion(TransformComponent& transform, DirectX::XMVECTOR quaternion)
        {
            DirectX::XMFLOAT4 qv;
            DirectX::XMStoreFloat4(&qv, quaternion);
            float sinp = 2.0f * (qv.w * qv.x - qv.y * qv.z);
            transform.Rotation.x = std::abs(sinp) >= 1.0f ? std::copysign(DirectX::XM_PI / 2.0f, sinp) : std::asin(sinp);
            transform.Rotation.y = std::atan2(2.0f * (qv.w * qv.y + qv.z * qv.x), 1.0f - 2.0f * (qv.x * qv.x + qv.y * qv.y));
            transform.Rotation.z = std::atan2(2.0f * (qv.w * qv.z + qv.x * qv.y), 1.0f - 2.0f * (qv.x * qv.x + qv.z * qv.z));
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

        float SnapFloat(float value, float step)
        {
            if (step <= 0.0001f)
                return value;

            return std::round(value / step) * step;
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

        DirectX::XMFLOAT3 TransformPoint(const DirectX::XMFLOAT3& point, DirectX::XMMATRIX matrix)
        {
            DirectX::XMFLOAT3 result;
            DirectX::XMStoreFloat3(&result, DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&point), matrix));
            return result;
        }

        float DistanceFromRay(const Math::Ray& ray, const DirectX::XMFLOAT3& point)
        {
            DirectX::XMVECTOR origin = DirectX::XMLoadFloat3(&ray.Origin);
            DirectX::XMVECTOR direction = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&ray.Direction));
            DirectX::XMVECTOR toPoint = DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&point), origin);
            float alongRay = (std::max)(0.0f, DirectX::XMVectorGetX(DirectX::XMVector3Dot(toPoint, direction)));
            DirectX::XMVECTOR closest = DirectX::XMVectorAdd(origin, DirectX::XMVectorScale(direction, alongRay));
            return DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&point), closest)));
        }

        DirectX::XMFLOAT3 GetColliderHandleLocalPosition(const BoxCollider3DComponent& collider, int handle)
        {
            DirectX::XMFLOAT3 half = { collider.Size.x * 0.5f, collider.Size.y * 0.5f, collider.Size.z * 0.5f };
            DirectX::XMFLOAT3 position = collider.Offset;
            if (handle < 6)
            {
                int axis = handle / 2;
                float sign = (handle % 2) == 0 ? -1.0f : 1.0f;
                (&position.x)[axis] += sign * (&half.x)[axis];
            }
            else
            {
                int bits = handle - 6;
                position.x += (bits & 1 ? 1.0f : -1.0f) * half.x;
                position.y += (bits & 2 ? 1.0f : -1.0f) * half.y;
                position.z += (bits & 4 ? 1.0f : -1.0f) * half.z;
            }
            return position;
        }

        DirectX::XMFLOAT3 GetSphereHandleLocalPosition(const SphereCollider3DComponent& collider, int handle)
        {
            DirectX::XMFLOAT3 position = collider.Offset;
            if (handle < 6)
            {
                int axis = handle / 2;
                float sign = (handle % 2) == 0 ? -1.0f : 1.0f;
                (&position.x)[axis] += sign * collider.Radius;
            }
            else
            {
                // Offset 핸들은 반지름 핸들과 겹치지 않도록 구 내부에 배치한다.
                int axis = handle - 6;
                float distance = (std::max)(collider.Radius * 0.45f, 0.12f);
                (&position.x)[axis] += distance;
            }
            return position;
        }

        DirectX::XMFLOAT3 GetCylinderHandleLocalPosition(const CylinderCollider3DComponent& collider, int handle)
        {
            DirectX::XMFLOAT3 position = collider.Offset;
            if (handle < 2)
            {
                position.y += (handle == 0 ? -0.5f : 0.5f) * collider.Height;
            }
            else if (handle < 6)
            {
                const int radialHandle = handle - 2;
                const int axis = radialHandle < 2 ? 0 : 2;
                const float sign = (radialHandle % 2) == 0 ? -1.0f : 1.0f;
                (&position.x)[axis] += sign * collider.Radius;
            }
            else
            {
                // Offset 핸들은 형상 핸들과 겹치지 않도록 원통 내부에 둔다.
                const int axis = handle - 6;
                const float extent = axis == 1 ? collider.Height * 0.5f : collider.Radius;
                (&position.x)[axis] += (std::max)(extent * 0.45f, 0.12f);
            }
            return position;
        }

        void ApplyBoxColliderHandleDrag(BoxCollider3DComponent& collider,
            const DirectX::XMFLOAT3& originalOffset, const DirectX::XMFLOAT3& originalSize,
            int handle, const DirectX::XMFLOAT3& localDelta)
        {
            collider.Offset = originalOffset;
            collider.Size = originalSize;
            constexpr float minimumSize = 0.01f;

            for (int axis = 0; axis < 3; ++axis)
            {
                bool changesAxis = handle < 6 ? axis == handle / 2 : true;
                if (!changesAxis)
                    continue;

                float sign = handle < 6
                    ? ((handle % 2) == 0 ? -1.0f : 1.0f)
                    : (((handle - 6) & (1 << axis)) ? 1.0f : -1.0f);
                float center = (&originalOffset.x)[axis];
                float size = (&originalSize.x)[axis];
                float opposite = center - sign * size * 0.5f;
                float moved = center + sign * size * 0.5f + (&localDelta.x)[axis];
                moved = sign > 0.0f ? (std::max)(moved, opposite + minimumSize)
                    : (std::min)(moved, opposite - minimumSize);
                (&collider.Offset.x)[axis] = (moved + opposite) * 0.5f;
                (&collider.Size.x)[axis] = std::abs(moved - opposite);
            }
        }

        void ApplySphereColliderHandleDrag(SphereCollider3DComponent& collider,
            const DirectX::XMFLOAT3& originalOffset, float originalRadius,
            int handle, float localDelta)
        {
            collider.Offset = originalOffset;
            collider.Radius = originalRadius;
            if (handle < 6)
            {
                float sign = (handle % 2) == 0 ? -1.0f : 1.0f;
                collider.Radius = (std::max)(0.01f, originalRadius + sign * localDelta);
                return;
            }

            int axis = handle - 6;
            if (axis >= 0 && axis < 3)
                (&collider.Offset.x)[axis] += localDelta;
        }

        void ApplyCylinderColliderHandleDrag(CylinderCollider3DComponent& collider,
            const DirectX::XMFLOAT3& originalOffset, float originalRadius, float originalHeight,
            int handle, float localDelta)
        {
            collider.Offset = originalOffset;
            collider.Radius = originalRadius;
            collider.Height = originalHeight;
            constexpr float minimumExtent = 0.01f;

            if (handle < 2)
            {
                // 한쪽 캡만 움직일 때 반대쪽 캡은 고정해야 크기 조절이 직관적이다.
                const float sign = handle == 0 ? -1.0f : 1.0f;
                const float opposite = originalOffset.y - sign * originalHeight * 0.5f;
                float moved = originalOffset.y + sign * originalHeight * 0.5f + localDelta;
                moved = sign > 0.0f ? (std::max)(moved, opposite + minimumExtent)
                    : (std::min)(moved, opposite - minimumExtent);
                collider.Offset.y = (moved + opposite) * 0.5f;
                collider.Height = std::abs(moved - opposite);
                return;
            }

            if (handle < 6)
            {
                const float sign = ((handle - 2) % 2) == 0 ? -1.0f : 1.0f;
                collider.Radius = (std::max)(minimumExtent, originalRadius + sign * localDelta);
                return;
            }

            const int axis = handle - 6;
            if (axis >= 0 && axis < 3)
                (&collider.Offset.x)[axis] += localDelta;
        }

        bool NearlyEqual(float a, float b)
        {
            return std::abs(a - b) <= 0.0001f;
        }
    }

    bool RunColliderGizmoRegressionChecks(std::string& message)
    {
        BoxCollider3DComponent box;
        ApplyBoxColliderHandleDrag(box, { 0.0f, 0.0f, 0.0f }, { 2.0f, 2.0f, 2.0f },
            1, { 1.0f, 0.0f, 0.0f });
        if (!NearlyEqual(box.Size.x, 3.0f) || !NearlyEqual(box.Offset.x, 0.5f))
        {
            message = "Box positive face did not keep the opposite face fixed.";
            return false;
        }

        ApplyBoxColliderHandleDrag(box, { 0.0f, 0.0f, 0.0f }, { 2.0f, 2.0f, 2.0f },
            13, { 0.5f, 0.25f, 1.0f });
        if (!NearlyEqual(box.Size.x, 2.5f) || !NearlyEqual(box.Size.y, 2.25f) ||
            !NearlyEqual(box.Size.z, 3.0f) || !NearlyEqual(box.Offset.z, 0.5f))
        {
            message = "Box corner handle did not resize all three axes.";
            return false;
        }

        ApplyBoxColliderHandleDrag(box, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f },
            1, { -2.0f, 0.0f, 0.0f });
        if (!NearlyEqual(box.Size.x, 0.01f))
        {
            message = "Box minimum size clamp failed.";
            return false;
        }

        SphereCollider3DComponent sphere;
        ApplySphereColliderHandleDrag(sphere, { 1.0f, 2.0f, 3.0f }, 0.5f, 1, 0.25f);
        if (!NearlyEqual(sphere.Radius, 0.75f) || !NearlyEqual(sphere.Offset.y, 2.0f))
        {
            message = "Sphere positive radius handle failed.";
            return false;
        }

        ApplySphereColliderHandleDrag(sphere, { 1.0f, 2.0f, 3.0f }, 0.5f, 0, 1.0f);
        if (!NearlyEqual(sphere.Radius, 0.01f))
        {
            message = "Sphere minimum radius clamp failed.";
            return false;
        }

        ApplySphereColliderHandleDrag(sphere, { 1.0f, 2.0f, 3.0f }, 0.5f, 7, -0.4f);
        if (!NearlyEqual(sphere.Offset.x, 1.0f) || !NearlyEqual(sphere.Offset.y, 1.6f) ||
            !NearlyEqual(sphere.Offset.z, 3.0f) || !NearlyEqual(sphere.Radius, 0.5f))
        {
            message = "Sphere offset handle changed the wrong axis or radius.";
            return false;
        }

        CylinderCollider3DComponent cylinder;
        ApplyCylinderColliderHandleDrag(cylinder, { 0.0f, 0.0f, 0.0f }, 0.5f, 2.0f, 1, 1.0f);
        if (!NearlyEqual(cylinder.Height, 3.0f) || !NearlyEqual(cylinder.Offset.y, 0.5f))
        {
            message = "Cylinder top handle did not keep the bottom cap fixed.";
            return false;
        }

        ApplyCylinderColliderHandleDrag(cylinder, { 0.0f, 0.0f, 0.0f }, 0.5f, 2.0f, 5, 0.25f);
        if (!NearlyEqual(cylinder.Radius, 0.75f) || !NearlyEqual(cylinder.Height, 2.0f))
        {
            message = "Cylinder radius handle changed the wrong value.";
            return false;
        }

        ApplyCylinderColliderHandleDrag(cylinder, { 1.0f, 2.0f, 3.0f }, 0.5f, 2.0f, 6, -0.4f);
        if (!NearlyEqual(cylinder.Offset.x, 0.6f) || !NearlyEqual(cylinder.Offset.y, 2.0f) ||
            !NearlyEqual(cylinder.Radius, 0.5f) || !NearlyEqual(cylinder.Height, 2.0f))
        {
            message = "Cylinder offset handle changed the shape dimensions.";
            return false;
        }

        Scene scene;
        Entity boxA = scene.CreateEntity("Box A");
        Entity boxB = scene.CreateEntity("Box B");
        Entity mixedSphere = scene.CreateEntity("Mixed Sphere");
        boxA.AddComponent<BoxCollider3DComponent>().Size = { 1.0f, 2.0f, 3.0f };
        boxB.AddComponent<BoxCollider3DComponent>().Size = { 2.0f, 3.0f, 4.0f };
        mixedSphere.AddComponent<SphereCollider3DComponent>().Radius = 0.75f;

        GizmoSystem multiEdit;
        multiEdit.CaptureColliderDragTargets({ boxA, boxB, mixedSphere }, boxA, ColliderEditShape::Box);
        if (multiEdit.m_ColliderDragTargets.size() != 2)
        {
            message = "Multi edit did not filter incompatible collider shapes.";
            return false;
        }
        multiEdit.ApplyColliderDragTargets(ColliderEditShape::Box, 1, { 0.5f, 0.0f, 0.0f }, 0.0f);
        const auto& boxAResult = boxA.GetComponent<BoxCollider3DComponent>();
        const auto& boxBResult = boxB.GetComponent<BoxCollider3DComponent>();
        if (!NearlyEqual(boxAResult.Size.x, 1.5f) || !NearlyEqual(boxBResult.Size.x, 2.5f) ||
            !NearlyEqual(boxAResult.Offset.x, 0.25f) || !NearlyEqual(boxBResult.Offset.x, 0.25f) ||
            !NearlyEqual(mixedSphere.GetComponent<SphereCollider3DComponent>().Radius, 0.75f))
        {
            message = "Multi Box edit did not preserve relative values or changed an incompatible collider.";
            return false;
        }

        Entity sphereA = scene.CreateEntity("Sphere A");
        Entity sphereB = scene.CreateEntity("Sphere B");
        sphereA.AddComponent<SphereCollider3DComponent>().Radius = 0.5f;
        sphereB.AddComponent<SphereCollider3DComponent>().Radius = 1.0f;
        multiEdit.CaptureColliderDragTargets({ sphereA, sphereB, boxA }, sphereA, ColliderEditShape::Sphere);
        multiEdit.ApplyColliderDragTargets(ColliderEditShape::Sphere, 1, {}, 0.2f);
        if (!NearlyEqual(sphereA.GetComponent<SphereCollider3DComponent>().Radius, 0.7f) ||
            !NearlyEqual(sphereB.GetComponent<SphereCollider3DComponent>().Radius, 1.2f))
        {
            message = "Multi Sphere radius edit did not apply the same delta.";
            return false;
        }

        Entity cylinderA = scene.CreateEntity("Cylinder A");
        Entity cylinderB = scene.CreateEntity("Cylinder B");
        cylinderA.AddComponent<CylinderCollider3DComponent>().Height = 1.0f;
        cylinderB.AddComponent<CylinderCollider3DComponent>().Height = 2.0f;
        multiEdit.CaptureColliderDragTargets({ cylinderA, cylinderB, sphereA }, cylinderA, ColliderEditShape::Cylinder);
        multiEdit.ApplyColliderDragTargets(ColliderEditShape::Cylinder, 1, {}, 0.4f);
        if (!NearlyEqual(cylinderA.GetComponent<CylinderCollider3DComponent>().Height, 1.4f) ||
            !NearlyEqual(cylinderB.GetComponent<CylinderCollider3DComponent>().Height, 2.4f) ||
            !NearlyEqual(cylinderA.GetComponent<CylinderCollider3DComponent>().Offset.y, 0.2f) ||
            !NearlyEqual(cylinderB.GetComponent<CylinderCollider3DComponent>().Offset.y, 0.2f))
        {
            message = "Multi Cylinder height edit did not preserve the opposite cap for every target.";
            return false;
        }

        message = "Single and multi Box, Sphere, and Cylinder collider calculations passed.";
        return true;
    }

    void GizmoSystem::SetMode(GizmoMode mode)
    {
        m_Mode = mode;
        if (mode != GizmoMode::Collider)
            ClearColliderEditTarget();
        m_IsDragging = false;
        m_ActiveAxis = -1;
        m_ActiveColliderHandle = -1;
        m_SelectedColliderHandle = -1;
        m_SelectedColliderEntity = {};
        m_HoveredColliderHandle = -1;
        m_HoveredColliderEntity = {};
        m_DragTargets.clear();
        m_ColliderDragTargets.clear();
        m_ColliderSelection.clear();
    }

    float GizmoSystem::GetColliderSnapStep(ColliderSnapValue value) const
    {
        switch (value)
        {
            case ColliderSnapValue::Offset: return m_ColliderOffsetSnapStep;
            case ColliderSnapValue::Size: return m_ColliderSizeSnapStep;
            case ColliderSnapValue::Radius: return m_ColliderRadiusSnapStep;
            case ColliderSnapValue::Height: return m_ColliderHeightSnapStep;
            default: return 0.1f;
        }
    }

    void GizmoSystem::SetColliderSnapStep(ColliderSnapValue value, float step)
    {
        // 0 간격은 나눗셈 오류뿐 아니라 드래그가 멈춘 것처럼 보이게 하므로 작은 양수로 제한한다.
        const float safeStep = (std::max)(0.001f, std::abs(step));
        switch (value)
        {
            case ColliderSnapValue::Offset: m_ColliderOffsetSnapStep = safeStep; break;
            case ColliderSnapValue::Size: m_ColliderSizeSnapStep = safeStep; break;
            case ColliderSnapValue::Radius: m_ColliderRadiusSnapStep = safeStep; break;
            case ColliderSnapValue::Height: m_ColliderHeightSnapStep = safeStep; break;
        }
    }

    void GizmoSystem::SetColliderEditTarget(Entity entity, ColliderEditShape shape)
    {
        m_ColliderEditEntity = entity;
        m_ColliderEditShape = shape;
        m_SelectedColliderHandle = -1;
        m_SelectedColliderEntity = {};
    }

    void GizmoSystem::ClearColliderEditTarget()
    {
        m_ColliderEditEntity = {};
        m_ColliderEditShape = ColliderEditShape::Auto;
        m_SelectedColliderHandle = -1;
        m_SelectedColliderEntity = {};
    }

    ColliderEditShape GizmoSystem::ResolveColliderEditShape(Entity entity) const
    {
        if (!entity)
            return ColliderEditShape::Auto;

        if (entity == m_ColliderEditEntity)
        {
            if (m_ColliderEditShape == ColliderEditShape::Box && entity.HasComponent<BoxCollider3DComponent>())
                return ColliderEditShape::Box;
            if (m_ColliderEditShape == ColliderEditShape::Sphere && entity.HasComponent<SphereCollider3DComponent>())
                return ColliderEditShape::Sphere;
            if (m_ColliderEditShape == ColliderEditShape::Cylinder && entity.HasComponent<CylinderCollider3DComponent>())
                return ColliderEditShape::Cylinder;
        }

        if (entity.HasComponent<BoxCollider3DComponent>()) return ColliderEditShape::Box;
        if (entity.HasComponent<SphereCollider3DComponent>()) return ColliderEditShape::Sphere;
        if (entity.HasComponent<CylinderCollider3DComponent>()) return ColliderEditShape::Cylinder;
        return ColliderEditShape::Auto;
    }

    bool GizmoSystem::HasColliderShape(Entity entity, ColliderEditShape shape) const
    {
        if (!entity || !entity.HasComponent<TransformComponent>())
            return false;

        switch (shape)
        {
            case ColliderEditShape::Box: return entity.HasComponent<BoxCollider3DComponent>();
            case ColliderEditShape::Sphere: return entity.HasComponent<SphereCollider3DComponent>();
            case ColliderEditShape::Cylinder: return entity.HasComponent<CylinderCollider3DComponent>();
            default: return false;
        }
    }

    void GizmoSystem::CaptureColliderDragTargets(const std::vector<Entity>& selectedEntities,
        Entity activeEntity, ColliderEditShape shape)
    {
        m_ColliderDragTargets.clear();
        std::vector<Entity> participants = BuildValidGizmoSelection(selectedEntities, activeEntity);
        for (Entity entity : participants)
        {
            if (!HasColliderShape(entity, shape))
                continue;

            ColliderDragTarget target;
            target.Target = entity;
            if (shape == ColliderEditShape::Box)
            {
                const auto& collider = entity.GetComponent<BoxCollider3DComponent>();
                target.OriginalOffset = collider.Offset;
                target.OriginalSize = collider.Size;
            }
            else if (shape == ColliderEditShape::Sphere)
            {
                const auto& collider = entity.GetComponent<SphereCollider3DComponent>();
                target.OriginalOffset = collider.Offset;
                target.OriginalRadius = collider.Radius;
            }
            else if (shape == ColliderEditShape::Cylinder)
            {
                const auto& collider = entity.GetComponent<CylinderCollider3DComponent>();
                target.OriginalOffset = collider.Offset;
                target.OriginalRadius = collider.Radius;
                target.OriginalHeight = collider.Height;
            }
            m_ColliderDragTargets.push_back(target);
        }
    }

    void GizmoSystem::ApplyColliderDragTargets(ColliderEditShape shape, int handle,
        const DirectX::XMFLOAT3& localDelta, float axisDelta)
    {
        for (ColliderDragTarget& target : m_ColliderDragTargets)
        {
            if (!target.Target)
                continue;

            if (shape == ColliderEditShape::Box && target.Target.HasComponent<BoxCollider3DComponent>())
            {
                auto& collider = target.Target.GetComponent<BoxCollider3DComponent>();
                ApplyBoxColliderHandleDrag(collider, target.OriginalOffset, target.OriginalSize, handle, localDelta);
            }
            else if (shape == ColliderEditShape::Sphere && target.Target.HasComponent<SphereCollider3DComponent>())
            {
                auto& collider = target.Target.GetComponent<SphereCollider3DComponent>();
                ApplySphereColliderHandleDrag(collider, target.OriginalOffset, target.OriginalRadius, handle, axisDelta);
            }
            else if (shape == ColliderEditShape::Cylinder && target.Target.HasComponent<CylinderCollider3DComponent>())
            {
                auto& collider = target.Target.GetComponent<CylinderCollider3DComponent>();
                ApplyCylinderColliderHandleDrag(collider, target.OriginalOffset, target.OriginalRadius,
                    target.OriginalHeight, handle, axisDelta);
            }
        }
    }

    bool GizmoSystem::IsColliderEditTarget(Entity entity, ColliderEditShape shape) const
    {
        return m_Mode == GizmoMode::Collider && ResolveColliderEditShape(entity) == shape;
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
        std::vector<Entity> singleSelection;
        if (selectedEntity)
            singleSelection.push_back(selectedEntity);
        OnRender(singleSelection, selectedEntity, viewMatrix, projMatrix);
    }

    void GizmoSystem::OnRender(const std::vector<Entity>& selectedEntities, Entity activeEntity, DirectX::XMMATRIX viewMatrix, DirectX::XMMATRIX projMatrix)
    {
        if (!activeEntity || m_Mode == GizmoMode::None) return;
        if (!activeEntity.HasComponent<TransformComponent>()) return;

        if (m_Mode == GizmoMode::Collider)
        {
            const ColliderEditShape shape = ResolveColliderEditShape(activeEntity);
            const std::vector<Entity> participants = BuildColliderSelection(selectedEntities, activeEntity);
            const bool sameShape = shape != ColliderEditShape::Auto && !participants.empty() &&
                std::all_of(participants.begin(), participants.end(),
                    [this, shape](Entity entity) { return HasColliderShape(entity, shape); });

            if (!sameShape && participants.size() > 1)
            {
                // 혼합 형상도 어떤 Collider가 참여하는지 알 수 있도록 각 외곽선은 모두 표시한다.
                // 형상별 전용 핸들은 의미가 서로 다르므로 숨기고, 중앙의 공통 Scale 기즈모만 조작에 사용한다.
                for (Entity entity : participants)
                {
                    if (entity.HasComponent<BoxCollider3DComponent>())
                        RenderBoxColliderGizmo(entity, viewMatrix, false);
                    if (entity.HasComponent<SphereCollider3DComponent>())
                        RenderSphereColliderGizmo(entity, viewMatrix, false);
                    if (entity.HasComponent<CylinderCollider3DComponent>())
                        RenderCylinderColliderGizmo(entity, viewMatrix, false);
                }

                Entity groupActive = std::find(participants.begin(), participants.end(), activeEntity) != participants.end()
                    ? activeEntity : participants.back();
                const GizmoMode savedMode = m_Mode;
                const GizmoSpace savedSpace = m_Space;
                const GizmoPivotMode savedPivot = m_PivotMode;
                m_Mode = GizmoMode::Scale;
                m_Space = GizmoSpace::World;
                m_PivotMode = GizmoPivotMode::Center;
                OnRender(participants, groupActive, viewMatrix, projMatrix);
                m_Mode = savedMode;
                m_Space = savedSpace;
                m_PivotMode = savedPivot;
                return;
            }

            // 같은 형상끼리는 어느 오브젝트의 손잡이를 잡아도 선택 전체가 같은 변화량으로 편집된다.
            for (Entity entity : participants)
            {
                switch (shape)
                {
                    case ColliderEditShape::Box: RenderBoxColliderGizmo(entity, viewMatrix, true); break;
                    case ColliderEditShape::Sphere: RenderSphereColliderGizmo(entity, viewMatrix, true); break;
                    case ColliderEditShape::Cylinder: RenderCylinderColliderGizmo(entity, viewMatrix, true); break;
                    default: break;
                }
            }
            return;
        }

        std::vector<Entity> validSelection = BuildValidGizmoSelection(selectedEntities, activeEntity);
        if (validSelection.empty())
            return;

        DirectX::XMMATRIX worldTransform = GetWorldTransform(activeEntity);
        DirectX::XMFLOAT3 worldPosition = CalculateSelectionCenter(validSelection, activeEntity, m_PivotMode);
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

        // 최종 기즈모 기준점이다.
        // Pivot 모드는 활성 오브젝트 위치, Center 모드는 선택 묶음의 중앙을 사용한다.
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

    void GizmoSystem::RenderBoxColliderGizmo(Entity entity, DirectX::XMMATRIX viewMatrix, bool showHandles)
    {
        if (!entity.HasComponent<BoxCollider3DComponent>())
            return;

        const auto& collider = entity.GetComponent<BoxCollider3DComponent>();
        DirectX::XMMATRIX world = GetWorldTransform(entity);
        DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, viewMatrix);
        DirectX::XMFLOAT3 cameraPosition;
        DirectX::XMStoreFloat3(&cameraPosition, invView.r[3]);

        DirectX::XMFLOAT3 corners[8];
        for (int i = 0; i < 8; ++i)
            corners[i] = TransformPoint(GetColliderHandleLocalPosition(collider, 6 + i), world);

        static constexpr int edges[12][2] = {
            {0,1},{2,3},{4,5},{6,7}, {0,2},{1,3},{4,6},{5,7}, {0,4},{1,5},{2,6},{3,7}
        };
        static auto cube = MeshFactory::CreateCube();
        RenderCommand::SetDepthTest(false);
        for (const auto& edge : edges)
        {
            DirectX::XMMATRIX line = BuildBoneLineTransform(corners[edge[0]], corners[edge[1]], 0.018f);
            Renderer3D::DrawMesh(line, cube, m_GizmoShader, { 0.15f, 0.8f, 0.95f, 1.0f });
        }

        if (!showHandles)
        {
            RenderCommand::SetDepthTest(true);
            return;
        }

        for (int handle = 0; handle < 14; ++handle)
        {
            DirectX::XMFLOAT3 position = TransformPoint(GetColliderHandleLocalPosition(collider, handle), world);
            DirectX::XMVECTOR delta = DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&cameraPosition), DirectX::XMLoadFloat3(&position));
            float size = (std::max)(0.035f, DirectX::XMVectorGetX(DirectX::XMVector3Length(delta)) * 0.018f);
            bool highlighted = (entity == m_SelectedColliderEntity && handle == m_ActiveColliderHandle) ||
                (entity == m_HoveredColliderEntity && handle == m_HoveredColliderHandle);
            bool selected = entity == m_SelectedColliderEntity && handle == m_SelectedColliderHandle;
            DirectX::XMFLOAT4 color = highlighted
                ? DirectX::XMFLOAT4{ 1.0f, 0.82f, 0.18f, 1.0f }
                : (selected ? DirectX::XMFLOAT4{ 1.0f, 0.55f, 0.12f, 1.0f }
                    : (handle < 6 ? DirectX::XMFLOAT4{ 0.2f, 0.75f, 1.0f, 1.0f } : DirectX::XMFLOAT4{ 0.92f, 0.92f, 0.92f, 1.0f }));
            DirectX::XMMATRIX transform = DirectX::XMMatrixScaling(size, size, size) *
                DirectX::XMMatrixTranslation(position.x, position.y, position.z);
            Renderer3D::DrawMesh(transform, cube, m_GizmoShader, color);
        }
        RenderCommand::SetDepthTest(true);
    }

    void GizmoSystem::RenderSphereColliderGizmo(Entity entity, DirectX::XMMATRIX viewMatrix, bool showHandles)
    {
        if (!entity.HasComponent<SphereCollider3DComponent>())
            return;

        const auto& collider = entity.GetComponent<SphereCollider3DComponent>();
        DirectX::XMMATRIX world = GetWorldTransform(entity);
        DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, viewMatrix);
        DirectX::XMFLOAT3 cameraPosition;
        DirectX::XMStoreFloat3(&cameraPosition, invView.r[3]);
        DirectX::XMFLOAT3 center = TransformPoint(collider.Offset, world);
        float cameraDistance = DirectX::XMVectorGetX(DirectX::XMVector3Length(
            DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&cameraPosition), DirectX::XMLoadFloat3(&center))));
        float lineThickness = (std::max)(0.006f, cameraDistance * 0.0015f);

        static auto cube = MeshFactory::CreateCube();
        constexpr int segments = 40;
        RenderCommand::SetDepthTest(false);

        // 세 개의 큰 원을 그리면 비균등 Transform 아래에서 타원체가 된 실제 모양도 바로 확인할 수 있다.
        for (int plane = 0; plane < 3; ++plane)
        {
            DirectX::XMFLOAT3 previous{};
            for (int segment = 0; segment <= segments; ++segment)
            {
                float angle = (float)segment / (float)segments * DirectX::XM_2PI;
                float c = std::cos(angle) * collider.Radius;
                float s = std::sin(angle) * collider.Radius;
                DirectX::XMFLOAT3 local = collider.Offset;
                if (plane == 0) { local.x += c; local.y += s; }
                else if (plane == 1) { local.x += c; local.z += s; }
                else { local.y += c; local.z += s; }
                DirectX::XMFLOAT3 current = TransformPoint(local, world);
                if (segment > 0)
                {
                    DirectX::XMMATRIX line = BuildBoneLineTransform(previous, current, lineThickness);
                    Renderer3D::DrawMesh(line, cube, m_GizmoShader, { 0.15f, 0.8f, 0.95f, 1.0f });
                }
                previous = current;
            }
        }

        if (!showHandles)
        {
            RenderCommand::SetDepthTest(true);
            return;
        }

        const DirectX::XMFLOAT4 axisColors[3] = {
            { 1.0f, 0.3f, 0.3f, 1.0f }, { 0.3f, 1.0f, 0.3f, 1.0f }, { 0.3f, 0.5f, 1.0f, 1.0f }
        };
        for (int handle = 0; handle < 9; ++handle)
        {
            DirectX::XMFLOAT3 position = TransformPoint(GetSphereHandleLocalPosition(collider, handle), world);
            int axis = handle < 6 ? handle / 2 : handle - 6;
            bool highlighted = (entity == m_SelectedColliderEntity && handle == m_ActiveColliderHandle) ||
                (entity == m_HoveredColliderEntity && handle == m_HoveredColliderHandle);
            bool selected = entity == m_SelectedColliderEntity && handle == m_SelectedColliderHandle;
            float size = (std::max)(0.035f, cameraDistance * (handle < 6 ? 0.018f : 0.014f));
            DirectX::XMFLOAT4 color = highlighted
                ? DirectX::XMFLOAT4{ 1.0f, 0.82f, 0.18f, 1.0f }
                : (selected ? DirectX::XMFLOAT4{ 1.0f, 0.55f, 0.12f, 1.0f } : axisColors[axis]);

            if (handle >= 6)
            {
                DirectX::XMMATRIX line = BuildBoneLineTransform(center, position, lineThickness * 0.75f);
                Renderer3D::DrawMesh(line, cube, m_GizmoShader, color);
            }
            DirectX::XMMATRIX transform = DirectX::XMMatrixScaling(size, size, size) *
                DirectX::XMMatrixTranslation(position.x, position.y, position.z);
            Renderer3D::DrawMesh(transform, cube, m_GizmoShader, color);
        }
        RenderCommand::SetDepthTest(true);
    }

    void GizmoSystem::RenderCylinderColliderGizmo(Entity entity, DirectX::XMMATRIX viewMatrix, bool showHandles)
    {
        if (!entity.HasComponent<CylinderCollider3DComponent>())
            return;

        const auto& collider = entity.GetComponent<CylinderCollider3DComponent>();
        DirectX::XMMATRIX world = GetWorldTransform(entity);
        DirectX::XMVECTOR camera = DirectX::XMMatrixInverse(nullptr, viewMatrix).r[3];
        DirectX::XMFLOAT3 cameraPosition;
        DirectX::XMStoreFloat3(&cameraPosition, camera);
        const DirectX::XMFLOAT3 center = TransformPoint(collider.Offset, world);
        const float cameraDistance = DirectX::XMVectorGetX(DirectX::XMVector3Length(
            DirectX::XMVectorSubtract(camera, DirectX::XMLoadFloat3(&center))));
        const float lineThickness = (std::max)(0.006f, cameraDistance * 0.0015f);

        static auto cube = MeshFactory::CreateCube();
        constexpr int segments = 40;
        const float halfHeight = collider.Height * 0.5f;
        RenderCommand::SetDepthTest(false);

        DirectX::XMFLOAT3 previousTop{};
        DirectX::XMFLOAT3 previousBottom{};
        for (int segment = 0; segment <= segments; ++segment)
        {
            const float angle = (float)segment / (float)segments * DirectX::XM_2PI;
            const float x = std::cos(angle) * collider.Radius;
            const float z = std::sin(angle) * collider.Radius;
            const DirectX::XMFLOAT3 top = TransformPoint(
                { collider.Offset.x + x, collider.Offset.y + halfHeight, collider.Offset.z + z }, world);
            const DirectX::XMFLOAT3 bottom = TransformPoint(
                { collider.Offset.x + x, collider.Offset.y - halfHeight, collider.Offset.z + z }, world);
            if (segment > 0)
            {
                Renderer3D::DrawMesh(BuildBoneLineTransform(previousTop, top, lineThickness), cube,
                    m_GizmoShader, { 0.15f, 0.8f, 0.95f, 1.0f });
                Renderer3D::DrawMesh(BuildBoneLineTransform(previousBottom, bottom, lineThickness), cube,
                    m_GizmoShader, { 0.15f, 0.8f, 0.95f, 1.0f });
            }
            if (segment % 10 == 0 && segment < segments)
            {
                Renderer3D::DrawMesh(BuildBoneLineTransform(top, bottom, lineThickness), cube,
                    m_GizmoShader, { 0.15f, 0.8f, 0.95f, 1.0f });
            }
            previousTop = top;
            previousBottom = bottom;
        }

        if (!showHandles)
        {
            RenderCommand::SetDepthTest(true);
            return;
        }

        const DirectX::XMFLOAT4 axisColors[3] = {
            { 1.0f, 0.3f, 0.3f, 1.0f }, { 0.3f, 1.0f, 0.3f, 1.0f }, { 0.3f, 0.5f, 1.0f, 1.0f }
        };
        for (int handle = 0; handle < 9; ++handle)
        {
            const DirectX::XMFLOAT3 position = TransformPoint(GetCylinderHandleLocalPosition(collider, handle), world);
            const int axis = handle < 2 ? 1 : (handle < 6 ? ((handle - 2) < 2 ? 0 : 2) : handle - 6);
            const bool highlighted = (entity == m_SelectedColliderEntity && handle == m_ActiveColliderHandle) ||
                (entity == m_HoveredColliderEntity && handle == m_HoveredColliderHandle);
            const bool selected = entity == m_SelectedColliderEntity && handle == m_SelectedColliderHandle;
            const float size = (std::max)(0.035f, cameraDistance * (handle < 6 ? 0.018f : 0.014f));
            const DirectX::XMFLOAT4 color = highlighted
                ? DirectX::XMFLOAT4{ 1.0f, 0.82f, 0.18f, 1.0f }
                : (selected ? DirectX::XMFLOAT4{ 1.0f, 0.55f, 0.12f, 1.0f } : axisColors[axis]);

            if (handle >= 6)
            {
                Renderer3D::DrawMesh(BuildBoneLineTransform(center, position, lineThickness * 0.75f), cube,
                    m_GizmoShader, color);
            }
            const DirectX::XMMATRIX transform = DirectX::XMMatrixScaling(size, size, size) *
                DirectX::XMMatrixTranslation(position.x, position.y, position.z);
            Renderer3D::DrawMesh(transform, cube, m_GizmoShader, color);
        }
        RenderCommand::SetDepthTest(true);
    }

    bool GizmoSystem::OnEvent(Event& e, Entity selectedEntity, DirectX::XMMATRIX viewMatrix, DirectX::XMMATRIX projMatrix, float viewportWidth, float viewportHeight, float viewportX, float viewportY)
    {
        std::vector<Entity> singleSelection;
        if (selectedEntity)
            singleSelection.push_back(selectedEntity);
        return OnEvent(e, singleSelection, selectedEntity, viewMatrix, projMatrix, viewportWidth, viewportHeight, viewportX, viewportY);
    }

    bool GizmoSystem::OnEvent(Event& e, const std::vector<Entity>& selectedEntities, Entity activeEntity, DirectX::XMMATRIX viewMatrix, DirectX::XMMATRIX projMatrix, float viewportWidth, float viewportHeight, float viewportX, float viewportY)
    {
        if (!activeEntity || m_Mode == GizmoMode::None) return false;
        if (!activeEntity.HasComponent<TransformComponent>()) return false;

        if (m_Mode == GizmoMode::Collider)
        {
            m_ColliderSelection = BuildColliderSelection(selectedEntities, activeEntity);
            const ColliderEditShape shape = ResolveColliderEditShape(activeEntity);
            const bool sameShape = shape != ColliderEditShape::Auto && !m_ColliderSelection.empty() &&
                std::all_of(m_ColliderSelection.begin(), m_ColliderSelection.end(),
                    [this, shape](Entity entity) { return HasColliderShape(entity, shape); });

            if (!sameShape && m_ColliderSelection.size() > 1)
            {
                Entity groupActive = std::find(m_ColliderSelection.begin(), m_ColliderSelection.end(), activeEntity) != m_ColliderSelection.end()
                    ? activeEntity : m_ColliderSelection.back();
                const GizmoMode savedMode = m_Mode;
                const GizmoSpace savedSpace = m_Space;
                const GizmoPivotMode savedPivot = m_PivotMode;
                m_Mode = GizmoMode::Scale;
                m_Space = GizmoSpace::World;
                m_PivotMode = GizmoPivotMode::Center;
                const bool handled = OnEvent(e, m_ColliderSelection, groupActive, viewMatrix, projMatrix,
                    viewportWidth, viewportHeight, viewportX, viewportY);
                m_Mode = savedMode;
                m_Space = savedSpace;
                m_PivotMode = savedPivot;
                return handled;
            }

            auto dispatch = [&](Entity entity)
                {
                    switch (shape)
                    {
                        case ColliderEditShape::Box:
                            return HandleBoxColliderEvent(e, entity, viewMatrix, projMatrix, viewportWidth, viewportHeight, viewportX, viewportY);
                        case ColliderEditShape::Sphere:
                            return HandleSphereColliderEvent(e, entity, viewMatrix, projMatrix, viewportWidth, viewportHeight, viewportX, viewportY);
                        case ColliderEditShape::Cylinder:
                            return HandleCylinderColliderEvent(e, entity, viewMatrix, projMatrix, viewportWidth, viewportHeight, viewportX, viewportY);
                        default:
                            return false;
                    }
                };

            // 드래그 중에는 시작 손잡이를 가진 오브젝트만 좌표 기준으로 사용한다.
            if (m_IsDragging && m_SelectedColliderEntity)
                return dispatch(m_SelectedColliderEntity);

            for (auto it = m_ColliderSelection.rbegin(); it != m_ColliderSelection.rend(); ++it)
            {
                if (HasColliderShape(*it, shape) && dispatch(*it))
                    return true;
            }
            return false;
        }

        std::vector<Entity> validSelection = BuildValidGizmoSelection(selectedEntities, activeEntity);
        if (validSelection.empty())
            return false;

        DirectX::XMMATRIX worldTransform = GetWorldTransform(activeEntity);
        DirectX::XMFLOAT3 worldPosition = CalculateSelectionCenter(validSelection, activeEntity, m_PivotMode);

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
                m_OriginalScale = activeEntity.GetComponent<TransformComponent>().Scale;
                m_OriginalQuat = activeEntity.GetComponent<TransformComponent>().QuaternionRotation;
                m_DragTargets.clear();
                m_DragTargets.reserve(validSelection.size());

                for (Entity entity : validSelection)
                {
                    auto& transform = entity.GetComponent<TransformComponent>();
                    DragTarget target;
                    target.Target = entity;
                    target.OriginalWorldPosition = GetWorldPosition(entity);
                    target.OriginalScale = transform.Scale;
                    target.OriginalQuat = transform.QuaternionRotation;
                    target.ParentWorld = GetParentWorldTransform(entity);
                    m_DragTargets.push_back(target);
                }

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
                    if (m_SnappingEnabled)
                        delta = SnapFloat(delta, m_TranslateSnapStep);

                    for (DragTarget& target : m_DragTargets)
                    {
                        if (!target.Target || !target.Target.HasComponent<TransformComponent>())
                            continue;

                        DirectX::XMVECTOR newWorldPos = DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&target.OriginalWorldPosition), DirectX::XMVectorScale(D, delta));
                        DirectX::XMVECTOR det;
                        DirectX::XMMATRIX invParentWorld = DirectX::XMMatrixInverse(&det, target.ParentWorld);
                        DirectX::XMVECTOR newLocalPos = DirectX::XMVector3TransformCoord(newWorldPos, invParentWorld);
                        DirectX::XMStoreFloat3(&target.Target.GetComponent<TransformComponent>().Translation, newLocalPos);
                    }
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
                    if (m_SnappingEnabled)
                        delta = SnapFloat(delta, m_ScaleSnapStep);

                    for (DragTarget& target : m_DragTargets)
                    {
                        if (!target.Target || !target.Target.HasComponent<TransformComponent>())
                            continue;

                        auto& transform = target.Target.GetComponent<TransformComponent>();
                        transform.Scale = target.OriginalScale;
                        if (m_ActiveAxis == 0) transform.Scale.x += delta;
                        else if (m_ActiveAxis == 1) transform.Scale.y += delta;
                        else if (m_ActiveAxis == 2) transform.Scale.z += delta;
                    }
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
                    if (m_SnappingEnabled)
                        angleDelta = SnapFloat(angleDelta, m_RotateSnapStepRadians);

                    // 짐벌 락 방어용 쿼터니언 회전 적용
                    DirectX::XMVECTOR deltaQuat = DirectX::XMQuaternionRotationAxis(D, angleDelta);
                    for (DragTarget& target : m_DragTargets)
                    {
                        if (!target.Target || !target.Target.HasComponent<TransformComponent>())
                            continue;

                        auto& transform = target.Target.GetComponent<TransformComponent>();
                        DirectX::XMVECTOR newQuat = DirectX::XMQuaternionMultiply(DirectX::XMLoadFloat4(&target.OriginalQuat), deltaQuat);
                        DirectX::XMStoreFloat4(&transform.QuaternionRotation, newQuat);
                        StoreEulerFromQuaternion(transform, newQuat);
                    }
                }
            }

            e.Handled = true; return true;
        }
        else if (e.GetEventType() == EventType::MouseButtonReleased) {
            m_IsDragging = false;
            m_ActiveAxis = -1;
            m_DragTargets.clear();
            return false;
        }

        return false;
    }

    bool GizmoSystem::HandleBoxColliderEvent(Event& e, Entity entity, DirectX::XMMATRIX viewMatrix, DirectX::XMMATRIX projMatrix,
        float viewportWidth, float viewportHeight, float viewportX, float viewportY)
    {
        if (!entity.HasComponent<BoxCollider3DComponent>())
            return false;

        auto makeRay = [&](float x, float y)
        {
            return Math::MathUtils::ScreenPosToWorldRay(x - viewportX, y - viewportY,
                viewportWidth, viewportHeight, viewMatrix, projMatrix);
        };

        auto findHandle = [&](const Math::Ray& ray)
        {
            const auto& collider = entity.GetComponent<BoxCollider3DComponent>();
            DirectX::XMMATRIX world = GetWorldTransform(entity);
            DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, viewMatrix);
            DirectX::XMVECTOR camera = invView.r[3];
            int bestHandle = -1;
            float bestRatio = FLT_MAX;
            for (int handle = 0; handle < 14; ++handle)
            {
                DirectX::XMFLOAT3 position = TransformPoint(GetColliderHandleLocalPosition(collider, handle), world);
                float distanceToCamera = DirectX::XMVectorGetX(DirectX::XMVector3Length(
                    DirectX::XMVectorSubtract(camera, DirectX::XMLoadFloat3(&position))));
                float radius = (std::max)(0.06f, distanceToCamera * 0.035f);
                float ratio = DistanceFromRay(ray, position) / radius;
                if (ratio <= 1.0f && ratio < bestRatio)
                {
                    bestRatio = ratio;
                    bestHandle = handle;
                }
            }
            return bestHandle;
        };

        if (e.GetEventType() == EventType::MouseButtonPressed)
        {
            auto& mouse = static_cast<MouseButtonPressedEvent&>(e);
            if (mouse.GetButton() != 0)
                return false;
            Math::Ray ray = makeRay(mouse.GetX(), mouse.GetY());
            int handle = findHandle(ray);
            if (handle < 0)
            {
                m_SelectedColliderHandle = -1;
                m_SelectedColliderEntity = {};
                return false;
            }

            auto& collider = entity.GetComponent<BoxCollider3DComponent>();
            m_IsDragging = true;
            m_ActiveColliderHandle = handle;
            m_SelectedColliderHandle = handle;
            m_SelectedColliderEntity = entity;
            m_HoveredColliderHandle = handle;
            m_HoveredColliderEntity = entity;
            m_OriginalColliderOffset = collider.Offset;
            m_OriginalColliderSize = collider.Size;
            CaptureColliderDragTargets(m_ColliderSelection, entity, ColliderEditShape::Box);
            DirectX::XMMATRIX world = GetWorldTransform(entity);
            DirectX::XMFLOAT3 handleWorld = TransformPoint(GetColliderHandleLocalPosition(collider, handle), world);

            if (handle < 6)
            {
                int axis = handle / 2;
                DirectX::XMVECTOR localAxis = DirectX::XMVectorZero();
                localAxis = DirectX::XMVectorSetByIndex(localAxis, 1.0f, axis);
                DirectX::XMVECTOR worldAxisRaw = DirectX::XMVector3TransformNormal(localAxis, world);
                m_ColliderAxisWorldScale = (std::max)(0.0001f, DirectX::XMVectorGetX(DirectX::XMVector3Length(worldAxisRaw)));
                DirectX::XMStoreFloat3(&m_ColliderDragAxis, DirectX::XMVector3Normalize(worldAxisRaw));
                DirectX::XMVECTOR axisVector = DirectX::XMLoadFloat3(&m_ColliderDragAxis);
                DirectX::XMVECTOR camera = DirectX::XMMatrixInverse(nullptr, viewMatrix).r[3];
                DirectX::XMVECTOR cameraToHandle = DirectX::XMVector3Normalize(
                    DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&handleWorld), camera));
                DirectX::XMVECTOR normal = DirectX::XMVector3Cross(axisVector, DirectX::XMVector3Cross(cameraToHandle, axisVector));
                if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(normal)) < 0.0001f)
                    normal = cameraToHandle;
                DirectX::XMStoreFloat3(&m_ColliderDragPlaneNormal, DirectX::XMVector3Normalize(normal));
            }
            else
            {
                DirectX::XMStoreFloat3(&m_ColliderDragPlaneNormal,
                    DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&ray.Direction)));
            }

            float t = 0.0f;
            m_ColliderDragStartHit = handleWorld;
            if (Math::MathUtils::RayPlaneIntersection(ray, DirectX::XMLoadFloat3(&handleWorld),
                DirectX::XMLoadFloat3(&m_ColliderDragPlaneNormal), t))
            {
                DirectX::XMStoreFloat3(&m_ColliderDragStartHit,
                    DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&ray.Origin),
                        DirectX::XMVectorScale(DirectX::XMLoadFloat3(&ray.Direction), t)));
            }
            e.Handled = true;
            return true;
        }

        if (e.GetEventType() == EventType::MouseMoved)
        {
            auto& mouse = static_cast<MouseMovedEvent&>(e);
            Math::Ray ray = makeRay(mouse.GetX(), mouse.GetY());
            if (!m_IsDragging)
            {
                const int handle = findHandle(ray);
                if (handle >= 0)
                {
                    m_HoveredColliderHandle = handle;
                    m_HoveredColliderEntity = entity;
                }
                else if (m_HoveredColliderEntity == entity)
                {
                    m_HoveredColliderHandle = -1;
                    m_HoveredColliderEntity = {};
                }
                return false;
            }

            float t = 0.0f;
            DirectX::XMVECTOR planePoint = DirectX::XMLoadFloat3(&m_ColliderDragStartHit);
            if (!Math::MathUtils::RayPlaneIntersection(ray, planePoint,
                DirectX::XMLoadFloat3(&m_ColliderDragPlaneNormal), t))
                return true;

            DirectX::XMVECTOR hit = DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&ray.Origin),
                DirectX::XMVectorScale(DirectX::XMLoadFloat3(&ray.Direction), t));
            DirectX::XMVECTOR worldDelta = DirectX::XMVectorSubtract(hit, planePoint);
            DirectX::XMFLOAT3 localDelta{};
            if (m_ActiveColliderHandle < 6)
            {
                int axis = m_ActiveColliderHandle / 2;
                float delta = DirectX::XMVectorGetX(DirectX::XMVector3Dot(worldDelta,
                    DirectX::XMLoadFloat3(&m_ColliderDragAxis))) / m_ColliderAxisWorldScale;
                (&localDelta.x)[axis] = delta;
            }
            else
            {
                DirectX::XMMATRIX inverseWorld = DirectX::XMMatrixInverse(nullptr, GetWorldTransform(entity));
                DirectX::XMStoreFloat3(&localDelta, DirectX::XMVector3TransformNormal(worldDelta, inverseWorld));
            }

            if (m_SnappingEnabled)
            {
                // Box 면과 모서리는 반대편을 고정하므로 이동량을 Size 간격으로 맞춘 뒤 Offset을 함께 계산한다.
                localDelta.x = SnapFloat(localDelta.x, m_ColliderSizeSnapStep);
                localDelta.y = SnapFloat(localDelta.y, m_ColliderSizeSnapStep);
                localDelta.z = SnapFloat(localDelta.z, m_ColliderSizeSnapStep);
            }

            ApplyColliderDragTargets(ColliderEditShape::Box, m_ActiveColliderHandle, localDelta, 0.0f);
            e.Handled = true;
            return true;
        }

        if (e.GetEventType() == EventType::MouseButtonReleased && m_IsDragging)
        {
            m_IsDragging = false;
            m_ActiveColliderHandle = -1;
            m_ColliderDragTargets.clear();
            e.Handled = true;
            return true;
        }
        return false;
    }

    bool GizmoSystem::HandleSphereColliderEvent(Event& e, Entity entity, DirectX::XMMATRIX viewMatrix, DirectX::XMMATRIX projMatrix,
        float viewportWidth, float viewportHeight, float viewportX, float viewportY)
    {
        if (!entity.HasComponent<SphereCollider3DComponent>())
            return false;

        auto makeRay = [&](float x, float y)
        {
            return Math::MathUtils::ScreenPosToWorldRay(x - viewportX, y - viewportY,
                viewportWidth, viewportHeight, viewMatrix, projMatrix);
        };

        auto findHandle = [&](const Math::Ray& ray)
        {
            const auto& collider = entity.GetComponent<SphereCollider3DComponent>();
            DirectX::XMMATRIX world = GetWorldTransform(entity);
            DirectX::XMVECTOR camera = DirectX::XMMatrixInverse(nullptr, viewMatrix).r[3];
            int bestHandle = -1;
            float bestRatio = FLT_MAX;
            for (int handle = 0; handle < 9; ++handle)
            {
                DirectX::XMFLOAT3 position = TransformPoint(GetSphereHandleLocalPosition(collider, handle), world);
                float cameraDistance = DirectX::XMVectorGetX(DirectX::XMVector3Length(
                    DirectX::XMVectorSubtract(camera, DirectX::XMLoadFloat3(&position))));
                float radius = (std::max)(0.06f, cameraDistance * 0.035f);
                float ratio = DistanceFromRay(ray, position) / radius;
                if (ratio <= 1.0f && ratio < bestRatio)
                {
                    bestRatio = ratio;
                    bestHandle = handle;
                }
            }
            return bestHandle;
        };

        if (e.GetEventType() == EventType::MouseButtonPressed)
        {
            auto& mouse = static_cast<MouseButtonPressedEvent&>(e);
            if (mouse.GetButton() != 0)
                return false;
            Math::Ray ray = makeRay(mouse.GetX(), mouse.GetY());
            int handle = findHandle(ray);
            if (handle < 0)
            {
                m_SelectedColliderHandle = -1;
                m_SelectedColliderEntity = {};
                return false;
            }

            auto& collider = entity.GetComponent<SphereCollider3DComponent>();
            m_IsDragging = true;
            m_ActiveColliderHandle = handle;
            m_SelectedColliderHandle = handle;
            m_SelectedColliderEntity = entity;
            m_HoveredColliderHandle = handle;
            m_HoveredColliderEntity = entity;
            m_OriginalColliderOffset = collider.Offset;
            m_OriginalColliderRadius = collider.Radius;
            CaptureColliderDragTargets(m_ColliderSelection, entity, ColliderEditShape::Sphere);

            int axis = handle < 6 ? handle / 2 : handle - 6;
            DirectX::XMVECTOR localAxis = DirectX::XMVectorZero();
            localAxis = DirectX::XMVectorSetByIndex(localAxis, 1.0f, axis);
            DirectX::XMMATRIX world = GetWorldTransform(entity);
            DirectX::XMVECTOR worldAxisRaw = DirectX::XMVector3TransformNormal(localAxis, world);
            m_ColliderAxisWorldScale = (std::max)(0.0001f,
                DirectX::XMVectorGetX(DirectX::XMVector3Length(worldAxisRaw)));
            DirectX::XMStoreFloat3(&m_ColliderDragAxis, DirectX::XMVector3Normalize(worldAxisRaw));

            DirectX::XMFLOAT3 handleWorld = TransformPoint(GetSphereHandleLocalPosition(collider, handle), world);
            DirectX::XMVECTOR axisVector = DirectX::XMLoadFloat3(&m_ColliderDragAxis);
            DirectX::XMVECTOR camera = DirectX::XMMatrixInverse(nullptr, viewMatrix).r[3];
            DirectX::XMVECTOR cameraToHandle = DirectX::XMVector3Normalize(
                DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&handleWorld), camera));
            DirectX::XMVECTOR normal = DirectX::XMVector3Cross(axisVector,
                DirectX::XMVector3Cross(cameraToHandle, axisVector));
            if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(normal)) < 0.0001f)
                normal = cameraToHandle;
            DirectX::XMStoreFloat3(&m_ColliderDragPlaneNormal, DirectX::XMVector3Normalize(normal));

            float t = 0.0f;
            m_ColliderDragStartHit = handleWorld;
            if (Math::MathUtils::RayPlaneIntersection(ray, DirectX::XMLoadFloat3(&handleWorld),
                DirectX::XMLoadFloat3(&m_ColliderDragPlaneNormal), t))
            {
                DirectX::XMStoreFloat3(&m_ColliderDragStartHit,
                    DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&ray.Origin),
                        DirectX::XMVectorScale(DirectX::XMLoadFloat3(&ray.Direction), t)));
            }
            e.Handled = true;
            return true;
        }

        if (e.GetEventType() == EventType::MouseMoved)
        {
            auto& mouse = static_cast<MouseMovedEvent&>(e);
            Math::Ray ray = makeRay(mouse.GetX(), mouse.GetY());
            if (!m_IsDragging)
            {
                const int handle = findHandle(ray);
                if (handle >= 0)
                {
                    m_HoveredColliderHandle = handle;
                    m_HoveredColliderEntity = entity;
                }
                else if (m_HoveredColliderEntity == entity)
                {
                    m_HoveredColliderHandle = -1;
                    m_HoveredColliderEntity = {};
                }
                return false;
            }

            float t = 0.0f;
            DirectX::XMVECTOR startHit = DirectX::XMLoadFloat3(&m_ColliderDragStartHit);
            if (!Math::MathUtils::RayPlaneIntersection(ray, startHit,
                DirectX::XMLoadFloat3(&m_ColliderDragPlaneNormal), t))
                return true;
            DirectX::XMVECTOR hit = DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&ray.Origin),
                DirectX::XMVectorScale(DirectX::XMLoadFloat3(&ray.Direction), t));

            // 화면상의 드래그를 월드 축에 투영한 뒤 Transform 스케일을 나눠 Collider 로컬 단위로 되돌린다.
            float localDelta = DirectX::XMVectorGetX(DirectX::XMVector3Dot(
                DirectX::XMVectorSubtract(hit, startHit), DirectX::XMLoadFloat3(&m_ColliderDragAxis))) /
                m_ColliderAxisWorldScale;
            if (m_SnappingEnabled)
            {
                const float step = m_ActiveColliderHandle < 6
                    ? m_ColliderRadiusSnapStep
                    : m_ColliderOffsetSnapStep;
                localDelta = SnapFloat(localDelta, step);
            }

            ApplyColliderDragTargets(ColliderEditShape::Sphere, m_ActiveColliderHandle, {}, localDelta);
            e.Handled = true;
            return true;
        }

        if (e.GetEventType() == EventType::MouseButtonReleased && m_IsDragging)
        {
            m_IsDragging = false;
            m_ActiveColliderHandle = -1;
            m_ColliderDragTargets.clear();
            e.Handled = true;
            return true;
        }
        return false;
    }

    bool GizmoSystem::HandleCylinderColliderEvent(Event& e, Entity entity, DirectX::XMMATRIX viewMatrix, DirectX::XMMATRIX projMatrix,
        float viewportWidth, float viewportHeight, float viewportX, float viewportY)
    {
        if (!entity.HasComponent<CylinderCollider3DComponent>())
            return false;

        auto makeRay = [&](float x, float y)
        {
            return Math::MathUtils::ScreenPosToWorldRay(x - viewportX, y - viewportY,
                viewportWidth, viewportHeight, viewMatrix, projMatrix);
        };

        auto findHandle = [&](const Math::Ray& ray)
        {
            const auto& collider = entity.GetComponent<CylinderCollider3DComponent>();
            const DirectX::XMMATRIX world = GetWorldTransform(entity);
            const DirectX::XMVECTOR camera = DirectX::XMMatrixInverse(nullptr, viewMatrix).r[3];
            int bestHandle = -1;
            float bestRatio = FLT_MAX;
            for (int handle = 0; handle < 9; ++handle)
            {
                const DirectX::XMFLOAT3 position = TransformPoint(GetCylinderHandleLocalPosition(collider, handle), world);
                const float cameraDistance = DirectX::XMVectorGetX(DirectX::XMVector3Length(
                    DirectX::XMVectorSubtract(camera, DirectX::XMLoadFloat3(&position))));
                const float radius = (std::max)(0.06f, cameraDistance * 0.035f);
                const float ratio = DistanceFromRay(ray, position) / radius;
                if (ratio <= 1.0f && ratio < bestRatio)
                {
                    bestRatio = ratio;
                    bestHandle = handle;
                }
            }
            return bestHandle;
        };

        if (e.GetEventType() == EventType::MouseButtonPressed)
        {
            auto& mouse = static_cast<MouseButtonPressedEvent&>(e);
            if (mouse.GetButton() != 0)
                return false;
            const Math::Ray ray = makeRay(mouse.GetX(), mouse.GetY());
            const int handle = findHandle(ray);
            if (handle < 0)
            {
                m_SelectedColliderHandle = -1;
                m_SelectedColliderEntity = {};
                return false;
            }

            auto& collider = entity.GetComponent<CylinderCollider3DComponent>();
            m_IsDragging = true;
            m_ActiveColliderHandle = handle;
            m_SelectedColliderHandle = handle;
            m_SelectedColliderEntity = entity;
            m_HoveredColliderHandle = handle;
            m_HoveredColliderEntity = entity;
            m_OriginalColliderOffset = collider.Offset;
            m_OriginalColliderRadius = collider.Radius;
            m_OriginalColliderHeight = collider.Height;
            CaptureColliderDragTargets(m_ColliderSelection, entity, ColliderEditShape::Cylinder);

            const int axis = handle < 2 ? 1 : (handle < 6 ? ((handle - 2) < 2 ? 0 : 2) : handle - 6);
            DirectX::XMVECTOR localAxis = DirectX::XMVectorZero();
            localAxis = DirectX::XMVectorSetByIndex(localAxis, 1.0f, axis);
            const DirectX::XMMATRIX world = GetWorldTransform(entity);
            const DirectX::XMVECTOR worldAxisRaw = DirectX::XMVector3TransformNormal(localAxis, world);
            m_ColliderAxisWorldScale = (std::max)(0.0001f,
                DirectX::XMVectorGetX(DirectX::XMVector3Length(worldAxisRaw)));
            DirectX::XMStoreFloat3(&m_ColliderDragAxis, DirectX::XMVector3Normalize(worldAxisRaw));

            const DirectX::XMFLOAT3 handleWorld = TransformPoint(GetCylinderHandleLocalPosition(collider, handle), world);
            const DirectX::XMVECTOR axisVector = DirectX::XMLoadFloat3(&m_ColliderDragAxis);
            const DirectX::XMVECTOR camera = DirectX::XMMatrixInverse(nullptr, viewMatrix).r[3];
            const DirectX::XMVECTOR cameraToHandle = DirectX::XMVector3Normalize(
                DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&handleWorld), camera));
            DirectX::XMVECTOR normal = DirectX::XMVector3Cross(axisVector,
                DirectX::XMVector3Cross(cameraToHandle, axisVector));
            if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(normal)) < 0.0001f)
                normal = cameraToHandle;
            DirectX::XMStoreFloat3(&m_ColliderDragPlaneNormal, DirectX::XMVector3Normalize(normal));

            float t = 0.0f;
            m_ColliderDragStartHit = handleWorld;
            if (Math::MathUtils::RayPlaneIntersection(ray, DirectX::XMLoadFloat3(&handleWorld),
                DirectX::XMLoadFloat3(&m_ColliderDragPlaneNormal), t))
            {
                DirectX::XMStoreFloat3(&m_ColliderDragStartHit,
                    DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&ray.Origin),
                        DirectX::XMVectorScale(DirectX::XMLoadFloat3(&ray.Direction), t)));
            }
            e.Handled = true;
            return true;
        }

        if (e.GetEventType() == EventType::MouseMoved)
        {
            auto& mouse = static_cast<MouseMovedEvent&>(e);
            const Math::Ray ray = makeRay(mouse.GetX(), mouse.GetY());
            if (!m_IsDragging)
            {
                const int handle = findHandle(ray);
                if (handle >= 0)
                {
                    m_HoveredColliderHandle = handle;
                    m_HoveredColliderEntity = entity;
                }
                else if (m_HoveredColliderEntity == entity)
                {
                    m_HoveredColliderHandle = -1;
                    m_HoveredColliderEntity = {};
                }
                return false;
            }

            float t = 0.0f;
            const DirectX::XMVECTOR startHit = DirectX::XMLoadFloat3(&m_ColliderDragStartHit);
            if (!Math::MathUtils::RayPlaneIntersection(ray, startHit,
                DirectX::XMLoadFloat3(&m_ColliderDragPlaneNormal), t))
                return true;
            const DirectX::XMVECTOR hit = DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&ray.Origin),
                DirectX::XMVectorScale(DirectX::XMLoadFloat3(&ray.Direction), t));
            float localDelta = DirectX::XMVectorGetX(DirectX::XMVector3Dot(
                DirectX::XMVectorSubtract(hit, startHit), DirectX::XMLoadFloat3(&m_ColliderDragAxis))) /
                m_ColliderAxisWorldScale;
            if (m_SnappingEnabled)
            {
                const float step = m_ActiveColliderHandle < 2
                    ? m_ColliderHeightSnapStep
                    : (m_ActiveColliderHandle < 6 ? m_ColliderRadiusSnapStep : m_ColliderOffsetSnapStep);
                localDelta = SnapFloat(localDelta, step);
            }

            ApplyColliderDragTargets(ColliderEditShape::Cylinder, m_ActiveColliderHandle, {}, localDelta);
            e.Handled = true;
            return true;
        }

        if (e.GetEventType() == EventType::MouseButtonReleased && m_IsDragging)
        {
            m_IsDragging = false;
            m_ActiveColliderHandle = -1;
            m_ColliderDragTargets.clear();
            e.Handled = true;
            return true;
        }
        return false;
    }
}
