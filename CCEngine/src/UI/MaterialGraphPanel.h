#pragma once

#include "UI/WindowPanel.h"
#include "Renderer/VisualShaderAsset.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace CCEngine::UI
{
    class CC_API MaterialGraphPanel : public WindowPanel
    {
    public:
        explicit MaterialGraphPanel(const std::string& name = "MaterialGraphPanel");

        bool LoadGraph(const std::filesystem::path& graphPath);
        bool SaveGraph();
        void SetGraphSavedCallback(std::function<void(const std::filesystem::path&)> callback) { m_OnGraphSaved = std::move(callback); }

        virtual void OnUpdate(float deltaTime) override;
        virtual void OnRender() override;
        virtual bool OnEvent(Event& e) override;
        virtual bool WantsMouseCapture() const override;

    protected:
        virtual bool OnMouseButtonPressed(MouseButtonPressedEvent& e) override;
        virtual bool OnMouseMoved(MouseMovedEvent& e) override;
        virtual bool OnMouseButtonReleased(MouseButtonReleasedEvent& e) override;
        virtual bool OnKeyPressed(KeyPressedEvent& e) override;

    private:
        struct NodeRect
        {
            int NodeId = -1;
            float X = 0.0f;
            float Y = 0.0f;
            float W = 0.0f;
            float H = 0.0f;
        };

        enum class SlotKind
        {
            None,
            InputA,
            InputB,
            Output
        };

        struct SlotHit
        {
            int NodeId = -1;
            SlotKind Slot = SlotKind::None;
        };

        struct GraphUndoRecord
        {
            std::string Label;
            VisualShaderAsset Before;
            VisualShaderAsset After;
            int BeforeSelectedNodeId = -1;
            int AfterSelectedNodeId = -1;
        };

        void AddNode(VisualShaderNodeType type);
        bool DeleteSelectedNode();
        VisualShaderNode* FindNode(int nodeId);
        const VisualShaderNode* FindNode(int nodeId) const;
        NodeRect GetNodeRect(const VisualShaderNode& node) const;
        DirectX::XMFLOAT2 GetSlotPosition(const VisualShaderNode& node, SlotKind slot) const;
        int GetNodeAt(float mouseX, float mouseY) const;
        SlotHit GetSlotAt(float mouseX, float mouseY) const;
        bool HandleToolbarClick(float mouseX, float mouseY);
        bool HandleNodeMenuClick(float mouseX, float mouseY);
        bool HandlePropertyPanelClick(float mouseX, float mouseY);
        bool TryConnectNodes(int sourceNodeId, int targetNodeId, SlotKind targetSlot);
        bool WouldCreateCycle(int sourceNodeId, int targetNodeId) const;
        bool DoesNodeReach(int currentNodeId, int targetNodeId) const;
        void ClearInputConnection(int nodeId, SlotKind slot);
        void OpenNodeMenu(float mouseX, float mouseY);
        void CloseNodeMenu();
        void FrameAllNodes();
        void MarkDirty();
        bool AreGraphsEqual(const VisualShaderAsset& a, const VisualShaderAsset& b) const;
        void PushUndoRecord(const std::string& label, const VisualShaderAsset& before, int beforeSelectedNodeId, const VisualShaderAsset& after, int afterSelectedNodeId);
        void ApplyGraphState(const VisualShaderAsset& graph, int selectedNodeId);
        bool UndoGraphEdit();
        bool RedoGraphEdit();
        void ClearUndoHistory();
        void DrawPropertyPanel();
        NodeRect GetPropertyPanelRect() const;
        DirectX::XMFLOAT2 GraphToScreen(const DirectX::XMFLOAT2& graphPosition) const;
        DirectX::XMFLOAT2 ScreenToGraph(float screenX, float screenY) const;
        std::string GetNodeTitle(const VisualShaderNode& node) const;
        int AllocateNodeId() const;
        int GetInputCount(const VisualShaderNode& node) const;
        void SetStatusMessage(const std::string& message, float seconds = 2.0f);

        static bool IsPointInRect(float mouseX, float mouseY, float x, float y, float w, float h);

    private:
        VisualShaderAsset m_Graph;
        std::filesystem::path m_GraphPath;
        std::function<void(const std::filesystem::path&)> m_OnGraphSaved;

        int m_SelectedNodeId = -1;
        int m_DraggingNodeId = -1;
        int m_ConnectingFromNodeId = -1;
        bool m_IsDraggingNode = false;
        bool m_IsConnecting = false;
        bool m_IsDirty = false;
        bool m_IsPanningView = false;
        float m_DragOffsetX = 0.0f;
        float m_DragOffsetY = 0.0f;
        float m_LastMouseX = 0.0f;
        float m_LastMouseY = 0.0f;
        float m_PanStartMouseX = 0.0f;
        float m_PanStartMouseY = 0.0f;
        float m_PanStartOffsetX = 0.0f;
        float m_PanStartOffsetY = 0.0f;
        float m_ViewOffsetX = 0.0f;
        float m_ViewOffsetY = 0.0f;
        float m_Zoom = 1.0f;
        VisualShaderAsset m_DragStartGraph;
        int m_DragStartSelectedNodeId = -1;
        bool m_HasDragStartGraph = false;
        bool m_IsNodeMenuOpen = false;
        float m_NodeMenuX = 0.0f;
        float m_NodeMenuY = 0.0f;
        float m_NodeSpawnX = 120.0f;
        float m_NodeSpawnY = 140.0f;
        std::string m_StatusMessage;
        float m_StatusTimer = 0.0f;
        std::vector<GraphUndoRecord> m_UndoStack;
        std::vector<GraphUndoRecord> m_RedoStack;
        size_t m_MaxGraphUndoRecords = 80;

        float m_ToolbarHeight = 34.0f;
        float m_NodeWidth = 230.0f;
        float m_NodeHeaderHeight = 24.0f;
    };
}
