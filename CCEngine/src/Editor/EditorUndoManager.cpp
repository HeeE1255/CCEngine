#include "Editor/EditorUndoManager.h"

#include "Scene/Components.h"

#include <algorithm>

namespace CCEngine {

    void EditorUndoManager::SetCallbacks(Callbacks callbacks)
    {
        m_Callbacks = std::move(callbacks);
    }

    Scene* EditorUndoManager::GetActiveScene() const
    {
        return m_Callbacks.GetActiveScene ? m_Callbacks.GetActiveScene() : nullptr;
    }

    Entity EditorUndoManager::GetSelectedEntity() const
    {
        return m_Callbacks.GetSelectedEntity ? m_Callbacks.GetSelectedEntity() : Entity{};
    }

    void EditorUndoManager::SetSelectedEntity(Entity entity)
    {
        if (m_Callbacks.SetSelectedEntity)
            m_Callbacks.SetSelectedEntity(entity);
    }

    void EditorUndoManager::MarkHistoryChanged()
    {
        if (m_Callbacks.OnHistoryChanged)
            m_Callbacks.OnHistoryChanged();
    }

    void EditorUndoManager::BeginSceneStructureChange(const std::string& label)
    {
        Scene* scene = GetActiveScene();
        if (!scene || scene->GetState() != SceneState::Edit)
            return;

        // 구조 변경 전에 진행 중인 Transform 드래그 기록을 먼저 확정한다.
        CommitPendingTransformUndo();
        // 생성/삭제/컴포넌트 변경은 엔티티 구성이 바뀌므로 변경 전 씬을 통째로 보관한다.
        m_PendingSceneStructureBefore.reset(Scene::Copy(scene));
        m_PendingSceneStructureLabel = label;
        m_PendingSceneStructureSelectionPath = CaptureCurrentSelectionPath();
    }

    void EditorUndoManager::CommitSceneStructureChange()
    {
        Scene* scene = GetActiveScene();
        if (!scene || !m_PendingSceneStructureBefore)
            return;

        constexpr size_t maxUndoCommands = 100;
        SceneStructureCommand command;
        command.Order = m_NextUndoOrder++;
        command.Label = m_PendingSceneStructureLabel.empty() ? "Scene Change" : m_PendingSceneStructureLabel;
        command.Before = m_PendingSceneStructureBefore;
        // 변경이 끝난 뒤의 씬도 복사해 두면 Redo 때 그대로 복구할 수 있다.
        command.After.reset(Scene::Copy(scene));
        command.BeforeSelectionPath = m_PendingSceneStructureSelectionPath;
        command.AfterSelectionPath = CaptureCurrentSelectionPath();

        m_SceneUndoStack.push_back(std::move(command));
        if (m_SceneUndoStack.size() > maxUndoCommands)
            m_SceneUndoStack.erase(m_SceneUndoStack.begin());
        m_SceneRedoStack.clear();

        m_PendingSceneStructureBefore.reset();
        m_PendingSceneStructureLabel.clear();
        m_PendingSceneStructureSelectionPath.clear();

        // 씬 구조가 바뀐 직후에는 이전 Transform 감시 기준만 비운다. 기존 Undo 기록은 유지한다.
        m_HasPendingTransformUndo = false;
        m_PendingTransformUndo = {};
        m_ObservedTransformEntity = entt::null;
        m_HasLastObservedTransform = false;
        m_IsApplyingTransformUndoRedo = true;
        MarkHistoryChanged();
    }

    std::vector<std::string> EditorUndoManager::CaptureCurrentSelectionPath() const
    {
        std::vector<std::string> path;
        Entity current = GetSelectedEntity();
        while (current && current.HasComponent<TagComponent>())
        {
            // 부모에서 자식까지의 이름 경로를 만들기 위해 일단 거꾸로 쌓는다.
            path.push_back(current.GetComponent<TagComponent>().Tag);
            if (!current.HasComponent<RelationshipComponent>())
                break;

            entt::entity parent = current.GetComponent<RelationshipComponent>().Parent;
            if (parent == entt::null || !current.GetScene()->GetRegistry().valid(parent))
                break;

            current = Entity(parent, current.GetScene());
        }

        std::reverse(path.begin(), path.end());
        return path;
    }

    Entity EditorUndoManager::FindEntityBySelectionPath(const std::vector<std::string>& path) const
    {
        Scene* scene = GetActiveScene();
        if (!scene || path.empty())
            return {};

        // 스냅샷 복구 후에는 기존 entt 핸들이 안전하지 않아서 이름 경로로 다시 찾는다.
        auto tagMatches = [](Entity entity, const std::string& name)
        {
            return entity && entity.HasComponent<TagComponent>() &&
                entity.GetComponent<TagComponent>().Tag == name;
        };

        Entity current;
        auto tagView = scene->GetRegistry().view<TagComponent>();
        for (auto entityID : tagView)
        {
            Entity candidate(entityID, scene);
            bool isRoot = !candidate.HasComponent<RelationshipComponent>() ||
                candidate.GetComponent<RelationshipComponent>().Parent == entt::null;
            if (isRoot && tagMatches(candidate, path.front()))
            {
                current = candidate;
                break;
            }
        }

        if (!current)
            return {};

        for (size_t pathIndex = 1; pathIndex < path.size(); ++pathIndex)
        {
            if (!current.HasComponent<RelationshipComponent>())
                return {};

            Entity next;
            for (entt::entity childID : current.GetComponent<RelationshipComponent>().Children)
            {
                if (!scene->GetRegistry().valid(childID))
                    continue;

                Entity child(childID, scene);
                if (tagMatches(child, path[pathIndex]))
                {
                    next = child;
                    break;
                }
            }

            if (!next)
                return {};

            current = next;
        }

        return current;
    }

    void EditorUndoManager::ApplySceneSnapshot(const std::shared_ptr<Scene>& snapshot, const std::vector<std::string>& selectionPath)
    {
        Scene* scene = GetActiveScene();
        if (!snapshot || !scene || scene->GetState() != SceneState::Edit || !m_Callbacks.ReplaceActiveScene)
            return;

        Scene* copiedScene = Scene::Copy(snapshot.get());
        copiedScene->SetSceneState(SceneState::Edit);
        m_Callbacks.ReplaceActiveScene(copiedScene);

        // 씬을 통째로 교체한 뒤 선택도 새 씬의 엔티티로 다시 잡는다.
        Entity restoredSelection = FindEntityBySelectionPath(selectionPath);
        SetSelectedEntity(restoredSelection);

        m_HasPendingTransformUndo = false;
        m_PendingTransformUndo = {};
        m_ObservedTransformEntity = entt::null;
        m_HasLastObservedTransform = false;
        m_IsApplyingTransformUndoRedo = true;
    }

    void EditorUndoManager::UndoSceneStructure()
    {
        if (m_SceneUndoStack.empty())
            return;

        SceneStructureCommand command = std::move(m_SceneUndoStack.back());
        m_SceneUndoStack.pop_back();
        ApplySceneSnapshot(command.Before, command.BeforeSelectionPath);
        m_SceneRedoStack.push_back(std::move(command));
        MarkHistoryChanged();
    }

    void EditorUndoManager::RedoSceneStructure()
    {
        if (m_SceneRedoStack.empty())
            return;

        SceneStructureCommand command = std::move(m_SceneRedoStack.back());
        m_SceneRedoStack.pop_back();
        ApplySceneSnapshot(command.After, command.AfterSelectionPath);
        m_SceneUndoStack.push_back(std::move(command));
        MarkHistoryChanged();
    }

    void EditorUndoManager::Undo()
    {
        CommitPendingTransformUndo();

        size_t transformOrder = m_TransformUndoStack.empty() ? 0 : m_TransformUndoStack.back().Order;
        size_t sceneOrder = m_SceneUndoStack.empty() ? 0 : m_SceneUndoStack.back().Order;
        if (transformOrder == 0 && sceneOrder == 0)
            return;

        // Transform 변경과 컴포넌트 변경이 섞여도 마지막 작업부터 정확히 되돌린다.
        if (transformOrder > sceneOrder)
            UndoTransform();
        else
            UndoSceneStructure();
    }

    void EditorUndoManager::Redo()
    {
        CommitPendingTransformUndo();

        size_t transformOrder = m_TransformRedoStack.empty() ? 0 : m_TransformRedoStack.back().Order;
        size_t sceneOrder = m_SceneRedoStack.empty() ? 0 : m_SceneRedoStack.back().Order;
        if (transformOrder == 0 && sceneOrder == 0)
            return;

        // Redo는 원래 실행 순서대로 다시 적용해야 하므로 더 작은 번호부터 복구한다.
        if (sceneOrder == 0 || (transformOrder != 0 && transformOrder < sceneOrder))
            RedoTransform();
        else
            RedoSceneStructure();
    }

    void EditorUndoManager::SeekSceneHistory(size_t targetAppliedCount)
    {
        CommitPendingTransformUndo();

        size_t totalCommands = m_SceneUndoStack.size() + m_SceneRedoStack.size();
        targetAppliedCount = (std::min)(targetAppliedCount, totalCommands);

        while (m_SceneUndoStack.size() > targetAppliedCount)
            UndoSceneStructure();

        while (m_SceneUndoStack.size() < targetAppliedCount)
            RedoSceneStructure();

        MarkHistoryChanged();
    }

    EditorUndoManager::TransformSnapshot EditorUndoManager::CaptureTransform(Entity entity) const
    {
        TransformSnapshot snapshot;
        if (!entity || !entity.HasComponent<TransformComponent>())
            return snapshot;

        const auto& transform = entity.GetComponent<TransformComponent>();
        snapshot.Translation = transform.Translation;
        snapshot.Rotation = transform.Rotation;
        snapshot.Scale = transform.Scale;
        snapshot.EulerRotation = transform.EulerRotation;
        snapshot.QuaternionRotation = transform.QuaternionRotation;
        return snapshot;
    }

    void EditorUndoManager::ApplyTransform(entt::entity entityHandle, const TransformSnapshot& snapshot)
    {
        // Undo/Redo 또는 History 점프 시 저장된 스냅샷을 실제 TransformComponent에 다시 적용한다.
        // 적용 직후 TrackTransformUndo가 이것을 새 작업으로 오해하지 않도록 관찰 상태도 같이 갱신한다.
        if (!IsValidTransformEntity(entityHandle))
            return;

        Entity entity(entityHandle, GetActiveScene());
        auto& transform = entity.GetComponent<TransformComponent>();
        transform.Translation = snapshot.Translation;
        transform.Rotation = snapshot.Rotation;
        transform.Scale = snapshot.Scale;
        transform.EulerRotation = snapshot.EulerRotation;
        transform.QuaternionRotation = snapshot.QuaternionRotation;

        m_ObservedTransformEntity = entityHandle;
        m_LastObservedTransform = snapshot;
        m_HasLastObservedTransform = true;
        m_HasPendingTransformUndo = false;
        m_IsApplyingTransformUndoRedo = true;
    }

    Entity EditorUndoManager::ResolveTransformCommandEntity(const TransformUndoCommand& command) const
    {
        // 씬 스냅샷을 되돌린 뒤에는 이름 경로가 entt 핸들보다 믿을 수 있다.
        Entity byPath = FindEntityBySelectionPath(command.EntityPath);
        if (byPath && byPath.HasComponent<TransformComponent>())
            return byPath;

        if (IsValidTransformEntity(command.Entity))
            return Entity(command.Entity, GetActiveScene());

        return {};
    }

    bool EditorUndoManager::IsValidTransformEntity(entt::entity entityHandle) const
    {
        Scene* scene = GetActiveScene();
        if (!scene || entityHandle == entt::null || !scene->GetRegistry().valid(entityHandle))
            return false;

        Entity entity(entityHandle, scene);
        return entity.HasComponent<TransformComponent>();
    }

    bool EditorUndoManager::SameTransformSnapshot(const TransformSnapshot& a, const TransformSnapshot& b)
    {
        auto same3 = [](const DirectX::XMFLOAT3& lhs, const DirectX::XMFLOAT3& rhs)
        {
            return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
        };
        auto same4 = [](const DirectX::XMFLOAT4& lhs, const DirectX::XMFLOAT4& rhs)
        {
            return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
        };

        return same3(a.Translation, b.Translation) &&
            same3(a.Rotation, b.Rotation) &&
            same3(a.Scale, b.Scale) &&
            same3(a.EulerRotation, b.EulerRotation) &&
            same4(a.QuaternionRotation, b.QuaternionRotation);
    }

    void EditorUndoManager::TrackTransformUndo()
    {
        // 매 프레임 선택된 엔티티의 Transform을 감시한다.
        // 값이 바뀌면 Pending 기록을 만들고, 드래그가 끝났을 때 하나의 Undo 작업으로 확정한다.
        if (m_IsApplyingTransformUndoRedo)
        {
            m_IsApplyingTransformUndoRedo = false;
            return;
        }

        Entity selectedEntity = GetSelectedEntity();
        if (!selectedEntity || !selectedEntity.HasComponent<TransformComponent>())
        {
            // 선택이 풀리거나 Transform 없는 엔티티로 바뀌면 이전 pending 작업을 먼저 확정한다.
            CommitPendingTransformUndo();
            m_ObservedTransformEntity = entt::null;
            m_HasLastObservedTransform = false;
            return;
        }

        entt::entity selectedHandle = (entt::entity)selectedEntity;
        TransformSnapshot current = CaptureTransform(selectedEntity);

        if (!m_HasLastObservedTransform || m_ObservedTransformEntity != selectedHandle)
        {
            // 새 엔티티를 선택한 첫 프레임은 기준점만 저장한다.
            // 이 순간 자체를 변경 작업으로 기록하면 선택만 했는데 Undo가 생기는 문제가 된다.
            CommitPendingTransformUndo();
            m_ObservedTransformEntity = selectedHandle;
            m_LastObservedTransform = current;
            m_HasLastObservedTransform = true;
            return;
        }

        if (!SameTransformSnapshot(current, m_LastObservedTransform))
        {
            // 첫 변화가 발생한 순간의 이전 상태를 Before로 저장한다.
            // 이후 드래그 중에는 After만 계속 최신 값으로 갱신해서 작업 하나로 묶는다.
            if (!m_HasPendingTransformUndo)
            {
                m_PendingTransformUndo.Order = m_NextUndoOrder++;
                m_PendingTransformUndo.Entity = selectedHandle;
                m_PendingTransformUndo.EntityPath = CaptureCurrentSelectionPath();
                m_PendingTransformUndo.Before = m_LastObservedTransform;
                m_HasPendingTransformUndo = true;
            }

            m_PendingTransformUndo.After = current;
            m_LastObservedTransform = current;
        }

        bool isLeftMouseDown = m_Callbacks.IsLeftMouseDown ? m_Callbacks.IsLeftMouseDown() : false;
        bool isGizmoDragging = m_Callbacks.IsGizmoDragging ? m_Callbacks.IsGizmoDragging() : false;
        if (m_HasPendingTransformUndo && !isLeftMouseDown && !isGizmoDragging)
        {
            // 마우스를 놓았고 기즈모도 드래그 중이 아니면 실제 Undo 스택에 확정한다.
            CommitPendingTransformUndo();
        }
    }

    void EditorUndoManager::CommitPendingTransformUndo()
    {
        // Pending 상태의 Transform 변경을 Undo 스택에 넣는다.
        // 새 작업이 추가되면 기존 Redo 경로는 더 이상 유효하지 않으므로 비운다.
        if (!m_HasPendingTransformUndo)
            return;

        if (!SameTransformSnapshot(m_PendingTransformUndo.Before, m_PendingTransformUndo.After))
        {
            constexpr size_t maxUndoCommands = 100;
            m_TransformUndoStack.push_back(m_PendingTransformUndo);
            if (m_TransformUndoStack.size() > maxUndoCommands)
                m_TransformUndoStack.erase(m_TransformUndoStack.begin());
            m_TransformRedoStack.clear();
            MarkHistoryChanged();
        }

        m_HasPendingTransformUndo = false;
        m_PendingTransformUndo = {};
    }

    void EditorUndoManager::UndoTransform()
    {
        // 마지막으로 적용된 작업을 Before 상태로 되돌리고 Redo 스택으로 이동한다.
        // 삭제된 엔티티처럼 더 이상 유효하지 않은 기록은 건너뛴다.
        CommitPendingTransformUndo();

        while (!m_TransformUndoStack.empty())
        {
            TransformUndoCommand command = m_TransformUndoStack.back();
            m_TransformUndoStack.pop_back();

            Entity target = ResolveTransformCommandEntity(command);
            if (!target)
                continue;

            ApplyTransform((entt::entity)target, command.Before);
            SetSelectedEntity(target);
            m_TransformRedoStack.push_back(command);
            MarkHistoryChanged();
            return;
        }
    }

    void EditorUndoManager::RedoTransform()
    {
        // 되돌린 작업을 After 상태로 다시 적용하고 Undo 스택으로 이동한다.
        CommitPendingTransformUndo();

        while (!m_TransformRedoStack.empty())
        {
            TransformUndoCommand command = m_TransformRedoStack.back();
            m_TransformRedoStack.pop_back();

            Entity target = ResolveTransformCommandEntity(command);
            if (!target)
                continue;

            ApplyTransform((entt::entity)target, command.After);
            SetSelectedEntity(target);
            m_TransformUndoStack.push_back(command);
            MarkHistoryChanged();
            return;
        }
    }

    void EditorUndoManager::SeekTransformHistory(size_t targetAppliedCount)
    {
        // History 패널에서 특정 시점을 클릭했을 때 호출된다.
        // 현재 적용된 작업 개수와 목표 개수를 비교해서 필요한 만큼 Undo/Redo를 반복한다.
        CommitPendingTransformUndo();

        size_t totalCommands = m_TransformUndoStack.size() + m_TransformRedoStack.size();
        targetAppliedCount = (std::min)(targetAppliedCount, totalCommands);

        while (m_TransformUndoStack.size() > targetAppliedCount)
        {
            // 현재 위치가 목표보다 뒤에 있으면 Undo 방향으로 이동한다.
            if (m_TransformUndoStack.empty())
                break;

            TransformUndoCommand command = m_TransformUndoStack.back();
            m_TransformUndoStack.pop_back();

            Entity target = ResolveTransformCommandEntity(command);
            if (target)
            {
                ApplyTransform((entt::entity)target, command.Before);
                SetSelectedEntity(target);
            }

            m_TransformRedoStack.push_back(command);
        }

        while (m_TransformUndoStack.size() < targetAppliedCount)
        {
            // 현재 위치가 목표보다 앞에 있으면 Redo 방향으로 이동한다.
            if (m_TransformRedoStack.empty())
                break;

            TransformUndoCommand command = m_TransformRedoStack.back();
            m_TransformRedoStack.pop_back();

            Entity target = ResolveTransformCommandEntity(command);
            if (target)
            {
                ApplyTransform((entt::entity)target, command.After);
                SetSelectedEntity(target);
            }

            m_TransformUndoStack.push_back(command);
        }

        MarkHistoryChanged();
    }

    void EditorUndoManager::ClearTransformHistory()
    {
        // 씬 로드/플레이모드 전환처럼 엔티티 핸들이 바뀔 수 있는 순간에는
        // 오래된 히스토리가 잘못된 엔티티를 가리키지 않도록 모두 초기화한다.
        m_TransformUndoStack.clear();
        m_TransformRedoStack.clear();
        m_HasPendingTransformUndo = false;
        m_PendingTransformUndo = {};
        m_ObservedTransformEntity = entt::null;
        m_HasLastObservedTransform = false;
        m_IsApplyingTransformUndoRedo = false;
        MarkHistoryChanged();
    }

    void EditorUndoManager::ClearSceneStructureHistory()
    {
        m_SceneUndoStack.clear();
        m_SceneRedoStack.clear();
        m_PendingSceneStructureBefore.reset();
        m_PendingSceneStructureLabel.clear();
        m_PendingSceneStructureSelectionPath.clear();
        MarkHistoryChanged();
    }

}
