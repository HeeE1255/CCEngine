#pragma once

#include "Core.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"

#include <DirectXMath.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace CCEngine {

    class CC_API EditorUndoManager
    {
    public:
        // Transform Undo/Redo에서 한 순간의 Transform 상태를 저장하는 스냅샷.
        // Before/After 두 개의 스냅샷을 비교하거나 적용해서 되돌리기/다시하기를 수행한다.
        struct TransformSnapshot
        {
            DirectX::XMFLOAT3 Translation = { 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 Rotation = { 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 Scale = { 1.0f, 1.0f, 1.0f };
            DirectX::XMFLOAT3 EulerRotation = { 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT4 QuaternionRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
        };

        // 하나의 Transform Undo 작업 단위.
        struct TransformUndoCommand
        {
            // Transform 작업과 씬 구조 작업의 실제 실행 순서를 맞추는 번호다.
            size_t Order = 0;
            entt::entity Entity = entt::null;
            // 씬을 스냅샷으로 되돌리면 entt 핸들이 바뀔 수 있어서 이름 경로도 같이 저장한다.
            std::vector<std::string> EntityPath;
            TransformSnapshot Before;
            TransformSnapshot After;
        };

        // 생성/삭제/컴포넌트 추가/제거처럼 엔티티 구조가 바뀌는 작업은
        // 개별 핸들보다 씬 전체 스냅샷으로 되돌리는 편이 안전하다.
        struct SceneStructureCommand
        {
            // Transform Undo와 같은 타임라인에 섞기 위한 번호다.
            size_t Order = 0;
            std::string Label;
            // 파일 저장용이 아니라 Undo/Redo 전용 메모리 복사본이다.
            std::shared_ptr<Scene> Before;
            std::shared_ptr<Scene> After;
            // 되돌린 뒤에도 사용자가 보던 오브젝트 선택을 최대한 유지한다.
            std::vector<std::string> BeforeSelectionPath;
            std::vector<std::string> AfterSelectionPath;
        };

        struct Callbacks
        {
            std::function<Scene*()> GetActiveScene;
            std::function<void(Scene*)> ReplaceActiveScene;
            std::function<Entity()> GetSelectedEntity;
            std::function<void(Entity)> SetSelectedEntity;
            std::function<bool()> IsLeftMouseDown;
            std::function<bool()> IsGizmoDragging;
            std::function<void()> OnHistoryChanged;
        };

        void SetCallbacks(Callbacks callbacks);

        void BeginSceneStructureChange(const std::string& label);
        void CommitSceneStructureChange();
        void Undo();
        void Redo();
        void TrackTransformUndo();
        void CommitPendingTransformUndo();
        void SeekTransformHistory(size_t targetAppliedCount);
        void SeekSceneHistory(size_t targetAppliedCount);
        void ClearTransformHistory();
        void ClearSceneStructureHistory();

        const std::vector<TransformUndoCommand>& GetTransformUndoStack() const { return m_TransformUndoStack; }
        const std::vector<TransformUndoCommand>& GetTransformRedoStack() const { return m_TransformRedoStack; }
        const std::vector<SceneStructureCommand>& GetSceneUndoStack() const { return m_SceneUndoStack; }
        const std::vector<SceneStructureCommand>& GetSceneRedoStack() const { return m_SceneRedoStack; }

    private:
        Scene* GetActiveScene() const;
        Entity GetSelectedEntity() const;
        void SetSelectedEntity(Entity entity);
        void MarkHistoryChanged();

        TransformSnapshot CaptureTransform(Entity entity) const;
        void ApplyTransform(entt::entity entityHandle, const TransformSnapshot& snapshot);
        Entity ResolveTransformCommandEntity(const TransformUndoCommand& command) const;
        bool IsValidTransformEntity(entt::entity entityHandle) const;
        static bool SameTransformSnapshot(const TransformSnapshot& a, const TransformSnapshot& b);

        std::vector<std::string> CaptureCurrentSelectionPath() const;
        Entity FindEntityBySelectionPath(const std::vector<std::string>& path) const;
        void ApplySceneSnapshot(const std::shared_ptr<Scene>& snapshot, const std::vector<std::string>& selectionPath);
        void UndoTransform();
        void RedoTransform();
        void UndoSceneStructure();
        void RedoSceneStructure();

    private:
        Callbacks m_Callbacks;

        // 적용된 작업은 UndoStack, 되돌린 작업은 RedoStack에 저장한다.
        // History 패널은 이 두 스택을 합쳐서 전체 작업 타임라인처럼 보여준다.
        std::vector<TransformUndoCommand> m_TransformUndoStack;
        std::vector<TransformUndoCommand> m_TransformRedoStack;
        std::vector<SceneStructureCommand> m_SceneUndoStack;
        std::vector<SceneStructureCommand> m_SceneRedoStack;
        std::shared_ptr<Scene> m_PendingSceneStructureBefore;
        std::string m_PendingSceneStructureLabel;
        std::vector<std::string> m_PendingSceneStructureSelectionPath;

        // 드래그 중 계속 변하는 Transform을 매 프레임 작업으로 쌓지 않고,
        // 마우스를 놓을 때 하나의 작업으로 확정하기 위한 임시 기록이다.
        TransformUndoCommand m_PendingTransformUndo;
        TransformSnapshot m_LastObservedTransform;
        entt::entity m_ObservedTransformEntity = entt::null;
        bool m_HasLastObservedTransform = false;
        bool m_HasPendingTransformUndo = false;
        bool m_IsApplyingTransformUndoRedo = false;
        // 작업이 생길 때마다 증가한다. Ctrl+Z는 가장 큰 번호부터 되돌린다.
        size_t m_NextUndoOrder = 1;
    };

}
