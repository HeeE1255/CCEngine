#pragma once

#include "Animation/Animator.h"
#include "UI/WindowPanel.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"

#include <functional>
#include <string>
#include <vector>

namespace CCEngine::UI
{
    class CC_API AnimatorGraphPanel : public WindowPanel
    {
    public:
        explicit AnimatorGraphPanel(const std::string& name = "AnimatorGraphPanel");

        void SetTarget(Entity entity);
        void SetOnClosed(std::function<void()> callback) { m_OnClosed = std::move(callback); }
        void SetOnStateSelected(std::function<void(Entity, int, int)> callback) { m_OnStateSelected = std::move(callback); }
        bool TryAcceptAssetDrop(const std::string& filepath, const std::string& assetType, float mouseX, float mouseY);

        virtual void OnRender() override;
        virtual bool OnEvent(Event& e) override;
        virtual bool WantsMouseCapture() const override;

    protected:
        virtual bool OnMouseButtonPressed(MouseButtonPressedEvent& e) override;
        virtual bool OnMouseMoved(MouseMovedEvent& e) override;
        virtual bool OnMouseButtonReleased(MouseButtonReleasedEvent& e) override;
        virtual bool OnKeyPressed(KeyPressedEvent& e) override;
        virtual bool OnTextInput(TextInputEvent& e) override;

    private:
        struct StateNodeRect
        {
            int StateIndex = -1;
            float X = 0.0f;
            float Y = 0.0f;
            float W = 0.0f;
            float H = 0.0f;
        };

        enum class SidebarPage
        {
            Layers,
            Parameters
        };

        enum class ContextMenuMode
        {
            None,
            AddState,
            ReplaceState,
            Transition,
            BlendTreeAddChild,
            BlendTreeReplaceChild
        };

        enum class GraphViewMode
        {
            StateMachine,
            BlendTree
        };

        enum class BlendTreeValueField
        {
            None,
            DirectWeight,
            Threshold,
            PositionX,
            PositionY
        };

        enum class PropertyEditField
        {
            None,
            KeyTime,
            ValueX,
            ValueY,
            ValueZ,
            ValueW,
            TargetPath
        };

        enum class PropertyTrackPreset
        {
            TransformPosition,
            TransformRotation,
            TransformScale,
            ActiveSelf,
            MaterialColor,
            LightIntensity,
            LightColor,
            CameraFOV,
            ScriptEnabled,
            AudioVolume,
            AudioPitch,
            AudioPlayTrigger
        };

        CCEngine::AnimatorComponent* GetAnimator() const;
        StateNodeRect GetStateRect(int stateIndex) const;
        int GetStateAt(float mouseX, float mouseY) const;
        int GetTransitionAt(float mouseX, float mouseY) const;
        void DrawGrid(float x, float y, float w, float h) const;
        void DrawNodeConnection(DirectX::XMFLOAT2 from, DirectX::XMFLOAT2 to, const DirectX::XMFLOAT4& color) const;
        void DrawTransitionArrow(int transitionIndex, const CCEngine::AnimatorComponent::Transition& transition, bool selected) const;
        void DrawToolbar(float x, float y, float w);
        void DrawSidebar(float x, float y, float w, float h);
        void DrawGraph(float x, float y, float w, float h);
        void DrawBlendTreeEditor(float x, float y, float w, float h);
        void DrawTimeline(float x, float y, float w, float h);
        void DrawContextMenu();
        void DrawClipPicker();
        void DrawTransitionInspector(float x, float y, float w, float h);
        bool HandleToolbarClick(float mouseX, float mouseY);
        bool HandleSidebarClick(float mouseX, float mouseY);
        bool HandleBlendTreeClick(float mouseX, float mouseY);
        bool HandleTimelineClick(float mouseX, float mouseY);
        bool HandleContextMenuClick(float mouseX, float mouseY);
        bool HandleClipPickerClick(float mouseX, float mouseY);
        void OpenContextMenu(ContextMenuMode mode, float mouseX, float mouseY, int stateIndex = -1, int transitionIndex = -1);
        void CloseContextMenu();
        void BeginTransitionCreation(int sourceStateIndex);
        void CancelTransitionCreation();
        void OpenClipPicker(ContextMenuMode mode, int stateIndex, const DirectX::XMFLOAT2& graphPosition);
        void CloseClipPicker();
        void AddEmptyState(const DirectX::XMFLOAT2& graphPosition);
        void AddStateFromSelectedClip();
        void AddStateFromClipIndex(int clipIndex, const std::string& clipName, const DirectX::XMFLOAT2& graphPosition);
        void ReplaceStateClip(int stateIndex, int clipIndex, const std::string& clipName);
        bool EnsureSelectedPropertyClip(CCEngine::AnimatorComponent& animator);
        bool AddPropertyTrack(PropertyTrackPreset preset);
        bool AddPropertyKeyAtTimeline();
        bool DeleteSelectedPropertyKey();
        bool CopySelectedPropertyKeys();
        bool PastePropertyKeysAtTimeline();
        bool MoveSelectedPropertyKeys(float deltaSeconds);
        bool CycleSelectedPropertyInterpolation();
        bool BeginPropertyEdit(PropertyEditField field);
        bool CommitPropertyEdit();
        void CancelPropertyEdit();
        DirectX::XMFLOAT4 CapturePropertyValue(const CCEngine::AnimatorComponent::State::PropertyTrack& track) const;
        void ApplyPropertyTrackPreview(const CCEngine::AnimatorComponent::State::PropertyTrack& track, float timeSeconds);
        void ApplyPropertyClipPreview(const CCEngine::AnimatorComponent::State& state, float timeSeconds);
        bool EnsureSelectedBlendTree(CCEngine::AnimatorComponent& animator);
        void AddBlendTreeChild(int clipIndex, const std::string& clipName);
        void ReplaceBlendTreeChild(int stateIndex, int childIndex, int clipIndex, const std::string& clipName);
        void AutoLayoutBlendTreeChildren(CCEngine::AnimatorComponent::State& state) const;
        void CycleBlendTreeParameter(CCEngine::AnimatorComponent& animator, bool parameterX);
        void BeginBlendTreeValueEdit(BlendTreeValueField field, int childIndex, float currentValue);
        bool CommitBlendTreeValueEdit();
        void CancelBlendTreeValueEdit();
        bool ApplyBlendTreeValueEdit(CCEngine::AnimatorComponent& animator, BlendTreeValueField field, int childIndex, float value);
        bool TryPickClipFromDroppedAsset(const std::string& filepath, int& outClipIndex, std::string& outClipName) const;
        float GetBlendTreeMaxScroll(const CCEngine::AnimatorComponent::State& state, float canvasH) const;
        std::string GetClipDisplayName(const CCEngine::AnimatorComponent& animator, int clipIndex) const;
        void AddTransition(int fromStateIndex, int toStateIndex);
        void DeleteSelectedState();
        void SelectOnlyState(CCEngine::AnimatorComponent& animator, int stateIndex);
        void ClearStateSelection(CCEngine::AnimatorComponent& animator);
        void NotifyStateSelected(const CCEngine::AnimatorComponent& animator, int stateIndex) const;
        void SelectStatesInBox(CCEngine::AnimatorComponent& animator);
        bool IsStateSelected(int stateIndex) const;
        void AddParameter(CCEngine::AnimatorComponent::Parameter::Type type);
        bool BeginParameterRename(int parameterIndex);
        bool CommitParameterRename();
        void CancelParameterRename();
        bool IsValidParameterName(const CCEngine::AnimatorComponent& animator, const std::string& name, int editingIndex) const;
        bool BeginStateRename(int stateIndex);
        bool CommitStateRename();
        void CancelStateRename();
        bool IsValidStateName(const CCEngine::AnimatorComponent::Layer& layer, const std::string& name, int editingIndex) const;
        void RenameTransitionParameterReferences(CCEngine::AnimatorComponent& animator, const std::string& oldName, const std::string& newName) const;
        void ClampAnimatorSelection(CCEngine::AnimatorComponent& animator) const;
        void ResetRuntime(CCEngine::AnimatorComponent& animator) const;
        void PropagateSharedControllerEdit(CCEngine::AnimatorComponent& editedAnimator) const;
        void CommitGraphEdit(CCEngine::AnimatorComponent& animator);
        void CaptureCommittedAnimator(const CCEngine::AnimatorComponent& animator);
        bool UndoGraphEdit(CCEngine::AnimatorComponent& animator);
        bool RedoGraphEdit(CCEngine::AnimatorComponent& animator);
        float GetSelectedStateRawDurationSeconds(const CCEngine::AnimatorComponent& animator, const CCEngine::AnimatorComponent::State& state);
        float GetSelectedStateDurationSeconds(const CCEngine::AnimatorComponent& animator, const CCEngine::AnimatorComponent::State& state);
        void ApplyTimelinePreview(CCEngine::AnimatorComponent& animator, CCEngine::AnimatorComponent::State& state, float timeSeconds);
        std::vector<AnimationClipInfo> InspectSourceClips(const CCEngine::AnimatorComponent& animator) const;
        DirectX::XMFLOAT2 GraphToScreen(const DirectX::XMFLOAT2& graphPosition) const;
        DirectX::XMFLOAT2 ScreenToGraph(float screenX, float screenY) const;
        static bool IsPointInRect(float mouseX, float mouseY, float x, float y, float w, float h);

    private:
        Entity m_TargetEntity;
        SidebarPage m_SidebarPage = SidebarPage::Layers;
        int m_SelectedStateIndex = -1;
        int m_DraggingStateIndex = -1;
        bool m_IsDraggingState = false;
        bool m_IsPanningGraph = false;
        bool m_IsBoxSelecting = false;
        bool m_IsScrubbingTimeline = false;
        bool m_IsDraggingBlendChild = false;
        bool m_IsCreatingTransition = false;
        bool m_IsContextMenuOpen = false;
        bool m_IsClipPickerOpen = false;
        GraphViewMode m_GraphViewMode = GraphViewMode::StateMachine;
        ContextMenuMode m_ContextMenuMode = ContextMenuMode::None;
        ContextMenuMode m_ClipPickerMode = ContextMenuMode::None;
        int m_ContextStateIndex = -1;
        int m_ContextSourceStateIndex = -1;
        int m_ContextTransitionIndex = -1;
        int m_SelectedTransitionIndex = -1;
        int m_TransitionSourceStateIndex = -1;
        int m_SelectedPropertyTrackIndex = -1;
        int m_SelectedPropertyKeyIndex = -1;
        int m_ClipPickerStateIndex = -1;
        int m_SelectedBlendChildIndex = -1;
        int m_DraggingBlendChildIndex = -1;
        int m_ClipPickerBlendChildIndex = -1;
        int m_EditingStateNameIndex = -1;
        int m_EditingParameterIndex = -1;
        int m_EditingBlendChildIndex = -1;
        BlendTreeValueField m_EditingBlendField = BlendTreeValueField::None;
        PropertyEditField m_EditingPropertyField = PropertyEditField::None;
        std::string m_StateNameEditBuffer;
        std::string m_StateEditMessage;
        std::string m_ParameterEditBuffer;
        std::string m_ParameterEditMessage;
        std::string m_BlendValueEditBuffer;
        std::string m_PropertyEditBuffer;
        std::string m_BlendTreeMessage;
        std::vector<CCEngine::AnimatorComponent::State::PropertyKey> m_CopiedPropertyKeys;
        std::vector<AnimationClipInfo> m_ContextClips;
        std::vector<AnimationClipInfo> m_ClipPickerClips;
        std::string m_ClipPickerMessage;
        std::string m_TimelineClipKey;
        float m_TimelineClipDurationSeconds = 1.0f;
        float m_BlendTreeChildScrollY = 0.0f;
        float m_DragOffsetX = 0.0f;
        float m_DragOffsetY = 0.0f;
        float m_ContextMenuX = 0.0f;
        float m_ContextMenuY = 0.0f;
        DirectX::XMFLOAT2 m_ContextGraphPosition = { 0.0f, 0.0f };
        DirectX::XMFLOAT2 m_ClipPickerGraphPosition = { 0.0f, 0.0f };
        DirectX::XMFLOAT2 m_BoxSelectStart = { 0.0f, 0.0f };
        DirectX::XMFLOAT2 m_BoxSelectEnd = { 0.0f, 0.0f };
        std::vector<int> m_SelectedStateIndices;
        float m_LastMouseX = 0.0f;
        float m_LastMouseY = 0.0f;
        float m_PanStartMouseX = 0.0f;
        float m_PanStartMouseY = 0.0f;
        float m_PanStartOffsetX = 0.0f;
        float m_PanStartOffsetY = 0.0f;
        float m_ViewOffsetX = 0.0f;
        float m_ViewOffsetY = 0.0f;
        float m_Zoom = 1.0f;
        float m_TimelineX = 0.0f;
        float m_TimelineY = 0.0f;
        float m_TimelineW = 0.0f;
        float m_TimelineH = 0.0f;
        CCEngine::AnimatorComponent m_LastCommittedAnimator;
        std::vector<CCEngine::AnimatorComponent> m_UndoStack;
        std::vector<CCEngine::AnimatorComponent> m_RedoStack;
        bool m_HasCommittedAnimator = false;

        float m_TitleContentTop = 0.0f;
        float m_ToolbarHeight = 34.0f;
        float m_SidebarWidth = 260.0f;
        float m_NodeWidth = 218.0f;
        float m_NodeHeight = 82.0f;
        std::function<void()> m_OnClosed = nullptr;
        std::function<void(Entity, int, int)> m_OnStateSelected = nullptr;
    };
}
