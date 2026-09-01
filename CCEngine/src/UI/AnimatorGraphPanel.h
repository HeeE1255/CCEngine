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
            Transition
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
        void DrawContextMenu();
        void DrawClipPicker();
        void DrawTransitionInspector(float x, float y, float w, float h);
        bool HandleToolbarClick(float mouseX, float mouseY);
        bool HandleSidebarClick(float mouseX, float mouseY);
        bool HandleContextMenuClick(float mouseX, float mouseY);
        bool HandleClipPickerClick(float mouseX, float mouseY);
        void OpenContextMenu(ContextMenuMode mode, float mouseX, float mouseY, int stateIndex = -1, int transitionIndex = -1);
        void CloseContextMenu();
        void OpenClipPicker(ContextMenuMode mode, int stateIndex, const DirectX::XMFLOAT2& graphPosition);
        void CloseClipPicker();
        void AddStateFromSelectedClip();
        void AddStateFromClipIndex(int clipIndex, const std::string& clipName, const DirectX::XMFLOAT2& graphPosition);
        void ReplaceStateClip(int stateIndex, int clipIndex, const std::string& clipName);
        void AddTransition(int fromStateIndex, int toStateIndex);
        void DeleteSelectedState();
        void SelectOnlyState(CCEngine::AnimatorComponent& animator, int stateIndex);
        void ClearStateSelection(CCEngine::AnimatorComponent& animator);
        void SelectStatesInBox(CCEngine::AnimatorComponent& animator);
        bool IsStateSelected(int stateIndex) const;
        void AddParameter(CCEngine::AnimatorComponent::Parameter::Type type);
        bool BeginParameterRename(int parameterIndex);
        bool CommitParameterRename();
        void CancelParameterRename();
        bool IsValidParameterName(const CCEngine::AnimatorComponent& animator, const std::string& name, int editingIndex) const;
        void RenameTransitionParameterReferences(CCEngine::AnimatorComponent& animator, const std::string& oldName, const std::string& newName) const;
        void ClampAnimatorSelection(CCEngine::AnimatorComponent& animator) const;
        void ResetRuntime(CCEngine::AnimatorComponent& animator) const;
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
        bool m_IsContextMenuOpen = false;
        bool m_IsClipPickerOpen = false;
        ContextMenuMode m_ContextMenuMode = ContextMenuMode::None;
        ContextMenuMode m_ClipPickerMode = ContextMenuMode::None;
        int m_ContextStateIndex = -1;
        int m_ContextSourceStateIndex = -1;
        int m_ContextTransitionIndex = -1;
        int m_SelectedTransitionIndex = -1;
        int m_ClipPickerStateIndex = -1;
        int m_EditingParameterIndex = -1;
        std::string m_ParameterEditBuffer;
        std::vector<AnimationClipInfo> m_ContextClips;
        std::vector<AnimationClipInfo> m_ClipPickerClips;
        std::string m_ClipPickerMessage;
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

        float m_TitleContentTop = 0.0f;
        float m_ToolbarHeight = 34.0f;
        float m_SidebarWidth = 260.0f;
        float m_NodeWidth = 180.0f;
        float m_NodeHeight = 62.0f;
        std::function<void()> m_OnClosed = nullptr;
    };
}
