#pragma once
#include "Scene/Entity.h"
#include "Core.h"
#include "Events/Event.h"
#include "Events/MouseEvent.h"
#include "Renderer/Shader.h"
#include <DirectXMath.h>

namespace CCEngine {

    // ★ 직관적인 기즈모 모드 Enum
    enum class GizmoMode 
    {
        None = 0,
        Translate, // 이동 (W)
        Rotate,    // 회전 (E)
        Scale      // 크기 (R)
    };

    enum class GizmoSpace 
    {
        World = 0,
        Local
    };

    class CC_API GizmoSystem 
    {
    public:
        GizmoSystem() = default;
        ~GizmoSystem() = default;

        void Init();

        // 상태 관리
        void SetMode(GizmoMode mode) { m_Mode = mode; }
        GizmoMode GetMode() const { return m_Mode; }
        bool IsDragging() const { return m_IsDragging; }

        // 에디터 뷰포트 렌더링 전용
        //void OnRender(Entity selectedEntity);
        void OnRender(Entity selectedEntity, DirectX::XMMATRIX viewMatrix, DirectX::XMMATRIX projMatrix);

        // 마우스 레이캐스팅 및 드래그 조작을 위한 이벤트 처리
        bool OnEvent(Event& e, Entity selectedEntity,
            DirectX::XMMATRIX viewMatrix, DirectX::XMMATRIX projMatrix,
            float viewportWidth, float viewportHeight,
            float viewportX, float viewportY);

        void SetSpace(GizmoSpace space) { m_Space = space; }
        GizmoSpace GetSpace() const { return m_Space; }

    private:
        std::shared_ptr<Shader> m_GizmoShader;

        GizmoMode m_Mode = GizmoMode::Translate;
        GizmoSpace m_Space = GizmoSpace::Local;

        // 향후 마우스 드래그를 위해 저장해둘 변수들
        bool m_IsDragging = false;
        int m_ActiveAxis = -1; // 0: X, 1: Y, 2: Z
        DirectX::XMFLOAT3 m_OriginalPosition;
        float m_InitialDragOffset = 0.0f;
        DirectX::XMFLOAT3 m_OriginalScale = { 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 m_OriginalQuat = { 0.0f, 0.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT3 m_InitialRotVec = { 0.0f, 0.0f, 0.0f };

        DirectX::XMFLOAT3 m_DragAxis = { 0.0f, 0.0f, 0.0f };

    };

}