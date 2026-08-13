#pragma once

#include "UI/WindowPanel.h"
#include "Renderer/VisualShaderAsset.h"

#include <filesystem>
#include <functional>

namespace CCEngine::UI
{
    class CC_API MaterialGraphPanel : public WindowPanel
    {
    public:
        explicit MaterialGraphPanel(const std::string& name = "MaterialGraphPanel");

        bool LoadGraph(const std::filesystem::path& graphPath);
        bool SaveGraph();
        void SetGraphSavedCallback(std::function<void(const std::filesystem::path&)> callback) { m_OnGraphSaved = std::move(callback); }

        virtual void OnRender() override;
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

        void AddNode(VisualShaderNodeType type);
        bool DeleteSelectedNode();
        VisualShaderNode* FindNode(int nodeId);
        const VisualShaderNode* FindNode(int nodeId) const;
        NodeRect GetNodeRect(const VisualShaderNode& node) const;
        DirectX::XMFLOAT2 GetSlotPosition(const VisualShaderNode& node, SlotKind slot) const;
        int GetNodeAt(float mouseX, float mouseY) const;
        SlotHit GetSlotAt(float mouseX, float mouseY) const;
        bool HandleToolbarClick(float mouseX, float mouseY);
        void MarkDirty();
        std::string GetNodeTitle(const VisualShaderNode& node) const;
        int AllocateNodeId() const;
        int GetInputCount(const VisualShaderNode& node) const;

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
        float m_DragOffsetX = 0.0f;
        float m_DragOffsetY = 0.0f;
        float m_LastMouseX = 0.0f;
        float m_LastMouseY = 0.0f;

        float m_ToolbarHeight = 34.0f;
        float m_NodeWidth = 230.0f;
        float m_NodeHeaderHeight = 24.0f;
    };
}
