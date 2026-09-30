#pragma once
#include "Scene/Entity.h"
#include "Core.h"
#include "Events/Event.h"
#include "Events/MouseEvent.h"
#include "Renderer/Shader.h"
#include <DirectXMath.h>
#include <string>
#include <vector>

namespace CCEngine {

    // ★ 직관적인 기즈모 모드 Enum
    enum class GizmoMode 
    {
        None = 0,
        Translate, // 이동 (W)
        Rotate,    // 회전 (E)
        Scale,     // 크기 (R)
        Collider   // Collider 형상 편집
    };

    enum class GizmoSpace 
    {
        World = 0,
        Local
    };

    enum class GizmoPivotMode
    {
        Pivot = 0,
        Center
    };

    enum class ColliderEditShape
    {
        Auto = 0,
        Box,
        Sphere,
        Cylinder
    };

    enum class ColliderSnapValue
    {
        Offset = 0,
        Size,
        Radius,
        Height
    };

    CC_API bool RunColliderGizmoRegressionChecks(std::string& message);

    class CC_API GizmoSystem 
    {
        friend CC_API bool RunColliderGizmoRegressionChecks(std::string& message);

    public:
        GizmoSystem() = default;
        ~GizmoSystem() = default;

        void Init();

        // 상태 관리
        void SetMode(GizmoMode mode);
        GizmoMode GetMode() const { return m_Mode; }
        bool IsDragging() const { return m_IsDragging; }

        // 에디터 뷰포트 렌더링 전용
        //void OnRender(Entity selectedEntity);
        void OnRender(Entity selectedEntity, DirectX::XMMATRIX viewMatrix, DirectX::XMMATRIX projMatrix);
        void OnRender(const std::vector<Entity>& selectedEntities, Entity activeEntity, DirectX::XMMATRIX viewMatrix, DirectX::XMMATRIX projMatrix);
        void OnRenderSkeleton(Entity selectedEntity);

        // 마우스 레이캐스팅 및 드래그 조작을 위한 이벤트 처리
        bool OnEvent(Event& e, Entity selectedEntity,
            DirectX::XMMATRIX viewMatrix, DirectX::XMMATRIX projMatrix,
            float viewportWidth, float viewportHeight,
            float viewportX, float viewportY);
        bool OnEvent(Event& e, const std::vector<Entity>& selectedEntities, Entity activeEntity,
            DirectX::XMMATRIX viewMatrix, DirectX::XMMATRIX projMatrix,
            float viewportWidth, float viewportHeight,
            float viewportX, float viewportY);

        void SetSpace(GizmoSpace space) { m_Space = space; }
        GizmoSpace GetSpace() const { return m_Space; }
        void ToggleSpace() { m_Space = (m_Space == GizmoSpace::Local) ? GizmoSpace::World : GizmoSpace::Local; }

        void SetPivotMode(GizmoPivotMode mode) { m_PivotMode = mode; }
        GizmoPivotMode GetPivotMode() const { return m_PivotMode; }
        void TogglePivotMode() { m_PivotMode = (m_PivotMode == GizmoPivotMode::Pivot) ? GizmoPivotMode::Center : GizmoPivotMode::Pivot; }

        void SetSnappingEnabled(bool enabled) { m_SnappingEnabled = enabled; }
        bool IsSnappingEnabled() const { return m_SnappingEnabled; }
        void ToggleSnapping() { m_SnappingEnabled = !m_SnappingEnabled; }
        float GetColliderSnapStep(ColliderSnapValue value) const;
        void SetColliderSnapStep(ColliderSnapValue value, float step);
        void SetColliderEditTarget(Entity entity, ColliderEditShape shape);
        void ClearColliderEditTarget();
        bool IsColliderEditTarget(Entity entity, ColliderEditShape shape) const;

    private:
        void RenderBoxColliderGizmo(Entity entity, DirectX::XMMATRIX viewMatrix, bool showHandles = true);
        bool HandleBoxColliderEvent(Event& e, Entity entity,
            DirectX::XMMATRIX viewMatrix, DirectX::XMMATRIX projMatrix,
            float viewportWidth, float viewportHeight, float viewportX, float viewportY);
        void RenderSphereColliderGizmo(Entity entity, DirectX::XMMATRIX viewMatrix, bool showHandles = true);
        bool HandleSphereColliderEvent(Event& e, Entity entity,
            DirectX::XMMATRIX viewMatrix, DirectX::XMMATRIX projMatrix,
            float viewportWidth, float viewportHeight, float viewportX, float viewportY);
        void RenderCylinderColliderGizmo(Entity entity, DirectX::XMMATRIX viewMatrix, bool showHandles = true);
        bool HandleCylinderColliderEvent(Event& e, Entity entity,
            DirectX::XMMATRIX viewMatrix, DirectX::XMMATRIX projMatrix,
            float viewportWidth, float viewportHeight, float viewportX, float viewportY);
        ColliderEditShape ResolveColliderEditShape(Entity entity) const;
        bool HasColliderShape(Entity entity, ColliderEditShape shape) const;
        void CaptureColliderDragTargets(const std::vector<Entity>& selectedEntities, Entity activeEntity,
            ColliderEditShape shape);
        void ApplyColliderDragTargets(ColliderEditShape shape, int handle,
            const DirectX::XMFLOAT3& localDelta, float axisDelta);

        std::shared_ptr<Shader> m_GizmoShader;

        GizmoMode m_Mode = GizmoMode::Translate;
        GizmoSpace m_Space = GizmoSpace::Local;
        GizmoPivotMode m_PivotMode = GizmoPivotMode::Pivot;
        bool m_SnappingEnabled = false;
        float m_TranslateSnapStep = 0.5f;
        float m_RotateSnapStepRadians = DirectX::XMConvertToRadians(15.0f);
        float m_ScaleSnapStep = 0.25f;
        float m_ColliderOffsetSnapStep = 0.1f;
        float m_ColliderSizeSnapStep = 0.1f;
        float m_ColliderRadiusSnapStep = 0.1f;
        float m_ColliderHeightSnapStep = 0.1f;

        // 향후 마우스 드래그를 위해 저장해둘 변수들
        bool m_IsDragging = false;
        int m_ActiveAxis = -1; // 0: X, 1: Y, 2: Z
        DirectX::XMFLOAT3 m_OriginalPosition;
        float m_InitialDragOffset = 0.0f;
        DirectX::XMFLOAT3 m_OriginalScale = { 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 m_OriginalQuat = { 0.0f, 0.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT3 m_InitialRotVec = { 0.0f, 0.0f, 0.0f };

        DirectX::XMFLOAT3 m_DragAxis = { 0.0f, 0.0f, 0.0f };

        struct DragTarget
        {
            Entity Target;
            DirectX::XMFLOAT3 OriginalWorldPosition = { 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 OriginalScale = { 1.0f, 1.0f, 1.0f };
            DirectX::XMFLOAT4 OriginalQuat = { 0.0f, 0.0f, 0.0f, 1.0f };
            DirectX::XMMATRIX ParentWorld = DirectX::XMMatrixIdentity();
        };
        std::vector<DragTarget> m_DragTargets;

        struct ColliderDragTarget
        {
            Entity Target;
            DirectX::XMFLOAT3 OriginalOffset = { 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 OriginalSize = { 1.0f, 1.0f, 1.0f };
            float OriginalRadius = 0.5f;
            float OriginalHeight = 1.0f;
        };
        std::vector<ColliderDragTarget> m_ColliderDragTargets;
        std::vector<Entity> m_ColliderSelection;

        int m_HoveredColliderHandle = -1;
        Entity m_HoveredColliderEntity;
        int m_ActiveColliderHandle = -1;
        int m_SelectedColliderHandle = -1;
        Entity m_SelectedColliderEntity;
        Entity m_ColliderEditEntity;
        ColliderEditShape m_ColliderEditShape = ColliderEditShape::Auto;
        DirectX::XMFLOAT3 m_OriginalColliderOffset = { 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 m_OriginalColliderSize = { 1.0f, 1.0f, 1.0f };
        float m_OriginalColliderRadius = 0.5f;
        float m_OriginalColliderHeight = 1.0f;
        DirectX::XMFLOAT3 m_ColliderDragAxis = { 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 m_ColliderDragPlaneNormal = { 0.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT3 m_ColliderDragStartHit = { 0.0f, 0.0f, 0.0f };
        float m_ColliderAxisWorldScale = 1.0f;

    };

}
