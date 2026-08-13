#include "UI/MaterialGraphPanel.h"

#include "Core/AssetDatabase.h"
#include "Core/ConsoleLog.h"
#include "Events/KeyEvent.h"
#include "Events/MouseEvent.h"
#include "Renderer/UIRenderer.h"

#include <algorithm>
#include <cmath>

namespace CCEngine::UI
{
    namespace
    {
        constexpr float ButtonHeight = 26.0f;
        constexpr float ButtonGap = 7.0f;
        constexpr float SlotRadius = 6.0f;

        constexpr DirectX::XMFLOAT4 CanvasColor = { 0.045f, 0.047f, 0.055f, 1.0f };
        constexpr DirectX::XMFLOAT4 CanvasGridFine = { 0.105f, 0.110f, 0.125f, 0.45f };
        constexpr DirectX::XMFLOAT4 CanvasGridMajor = { 0.145f, 0.150f, 0.170f, 0.58f };
        constexpr DirectX::XMFLOAT4 PanelStroke = { 0.22f, 0.23f, 0.26f, 1.0f };
        constexpr DirectX::XMFLOAT4 NodeBody = { 0.105f, 0.110f, 0.125f, 1.0f };
        constexpr DirectX::XMFLOAT4 NodeHeader = { 0.145f, 0.150f, 0.170f, 1.0f };
        constexpr DirectX::XMFLOAT4 TextStrong = { 0.90f, 0.91f, 0.94f, 1.0f };
        constexpr DirectX::XMFLOAT4 TextMuted = { 0.58f, 0.60f, 0.66f, 1.0f };
        constexpr DirectX::XMFLOAT4 AccentBlue = { 0.38f, 0.58f, 0.86f, 1.0f };
        constexpr DirectX::XMFLOAT4 AccentCyan = { 0.28f, 0.72f, 0.82f, 1.0f };
        constexpr DirectX::XMFLOAT4 AccentAmber = { 0.86f, 0.64f, 0.30f, 1.0f };
        constexpr DirectX::XMFLOAT4 AccentGreen = { 0.42f, 0.76f, 0.48f, 1.0f };
        constexpr DirectX::XMFLOAT4 AccentPink = { 0.76f, 0.42f, 0.72f, 1.0f };

        struct ToolbarButton
        {
            const char* Label;
            float Width;
        };

        const ToolbarButton ToolbarButtons[] =
        {
            { "Save", 52.0f },
            { "Generate", 78.0f },
            { "Color", 58.0f },
            { "Texture", 72.0f },
            { "Multiply", 82.0f },
            { "Add", 48.0f },
            { "Output", 66.0f },
            { "Delete", 64.0f }
        };

        void DrawBorder(float x, float y, float w, float h, const DirectX::XMFLOAT4& color, float thickness = 1.0f)
        {
            UIRenderer::DrawRectFilled(x, y, w, thickness, color);
            UIRenderer::DrawRectFilled(x, y + h - thickness, w, thickness, color);
            UIRenderer::DrawRectFilled(x, y, thickness, h, color);
            UIRenderer::DrawRectFilled(x + w - thickness, y, thickness, h, color);
        }

        DirectX::XMFLOAT4 GetNodeAccent(VisualShaderNodeType type)
        {
            switch (type)
            {
                case VisualShaderNodeType::Color: return AccentPink;
                case VisualShaderNodeType::Texture2D: return AccentCyan;
                case VisualShaderNodeType::Multiply: return AccentBlue;
                case VisualShaderNodeType::Add: return AccentAmber;
                case VisualShaderNodeType::Output: return AccentGreen;
                default: return AccentBlue;
            }
        }

        const char* GetNodeTypeLabel(VisualShaderNodeType type)
        {
            switch (type)
            {
                case VisualShaderNodeType::Color: return "COLOR";
                case VisualShaderNodeType::Texture2D: return "TEXTURE";
                case VisualShaderNodeType::Multiply: return "MULTIPLY";
                case VisualShaderNodeType::Add: return "ADD";
                case VisualShaderNodeType::Output: return "OUTPUT";
                default: return "NODE";
            }
        }
    }

    MaterialGraphPanel::MaterialGraphPanel(const std::string& name)
        : WindowPanel(name, "Material Graph")
    {
        SetClipToBounds(true);
    }

    bool MaterialGraphPanel::LoadGraph(const std::filesystem::path& graphPath)
    {
        m_GraphPath = graphPath;
        if (!m_Graph.LoadFromFile(graphPath))
        {
            m_Graph = VisualShaderAsset::CreateDefault(graphPath.stem().string());
            if (!m_Graph.SaveToFile(graphPath))
                return false;
            m_Graph.SaveGeneratedHlsl(graphPath);
        }

        m_SelectedNodeId = -1;
        m_DraggingNodeId = -1;
        m_ConnectingFromNodeId = -1;
        m_IsDraggingNode = false;
        m_IsConnecting = false;
        m_IsDirty = false;
        SetVisible(true);
        BringToFront();
        return true;
    }

    bool MaterialGraphPanel::SaveGraph()
    {
        if (m_GraphPath.empty())
            return false;

        if (!m_Graph.SaveToFile(m_GraphPath))
        {
            ConsoleLog::Error("Visual shader save failed: " + m_GraphPath.string());
            return false;
        }

        if (!m_Graph.SaveGeneratedHlsl(m_GraphPath))
        {
            ConsoleLog::Error("Visual shader HLSL generation failed: " + m_GraphPath.string());
            return false;
        }

        // 그래프 원본과 생성 HLSL은 한 쌍이다.
        // 저장 직후 meta를 보장해야 Material이 생성 HLSL을 GUID로 안전하게 참조할 수 있다.
        AssetDatabase::EnsureMetaFile(m_GraphPath);
        AssetDatabase::EnsureMetaFile(VisualShaderAsset::GetGeneratedHlslPath(m_GraphPath));
        AssetDatabase::MarkDirty(m_GraphPath.parent_path());

        m_IsDirty = false;
        if (m_OnGraphSaved)
            m_OnGraphSaved(m_GraphPath);
        ConsoleLog::Info("Visual shader saved: " + m_GraphPath.string());
        return true;
    }

    void MaterialGraphPanel::OnRender()
    {
        WindowPanel::OnRender();
        if (!IsVisible())
            return;

        const float titleHeight = GetContentPosition().y - m_CalculatedPos.y;
        const float toolbarX = m_CalculatedPos.x + 8.0f;
        const float toolbarY = m_CalculatedPos.y + titleHeight + 5.0f;
        const float canvasX = m_CalculatedPos.x;
        const float canvasY = m_CalculatedPos.y + titleHeight + m_ToolbarHeight;
        const float canvasW = m_CalculatedSize.x;
        const float canvasH = (std::max)(0.0f, m_CalculatedSize.y - titleHeight - m_ToolbarHeight);

        UIRenderer::DrawRectFilled(canvasX, canvasY, canvasW, canvasH, CanvasColor);

        for (float x = canvasX; x < canvasX + canvasW; x += 32.0f)
            UIRenderer::DrawRectFilled(x, canvasY, 1.0f, canvasH, CanvasGridFine);
        for (float y = canvasY; y < canvasY + canvasH; y += 32.0f)
            UIRenderer::DrawRectFilled(canvasX, y, canvasW, 1.0f, CanvasGridFine);
        for (float x = canvasX; x < canvasX + canvasW; x += 128.0f)
            UIRenderer::DrawRectFilled(x, canvasY, 1.0f, canvasH, CanvasGridMajor);
        for (float y = canvasY; y < canvasY + canvasH; y += 128.0f)
            UIRenderer::DrawRectFilled(canvasX, y, canvasW, 1.0f, CanvasGridMajor);

        UIRenderer::DrawRectFilled(m_CalculatedPos.x, m_CalculatedPos.y + titleHeight, m_CalculatedSize.x, m_ToolbarHeight, { 0.080f, 0.083f, 0.095f, 0.98f });
        UIRenderer::DrawRectFilled(m_CalculatedPos.x, m_CalculatedPos.y + titleHeight + m_ToolbarHeight - 1.0f, m_CalculatedSize.x, 1.0f, PanelStroke);

        float buttonX = toolbarX;
        for (int i = 0; i < (int)(sizeof(ToolbarButtons) / sizeof(ToolbarButtons[0])); ++i)
        {
            const ToolbarButton& button = ToolbarButtons[i];
            const bool primaryAction = i < 2;
            const bool destructive = i == 7;
            DirectX::XMFLOAT4 fill = primaryAction
                ? DirectX::XMFLOAT4{ 0.18f, 0.25f, 0.34f, 1.0f }
                : DirectX::XMFLOAT4{ 0.125f, 0.130f, 0.145f, 1.0f };
            DirectX::XMFLOAT4 stroke = primaryAction ? AccentBlue : PanelStroke;
            if (destructive)
            {
                fill = { 0.22f, 0.10f, 0.12f, 1.0f };
                stroke = { 0.50f, 0.18f, 0.22f, 1.0f };
            }

            UIRenderer::DrawRectFilled(buttonX + 1.0f, toolbarY + 2.0f, button.Width, ButtonHeight, { 0.018f, 0.019f, 0.022f, 0.75f });
            UIRenderer::DrawRectFilled(buttonX, toolbarY, button.Width, ButtonHeight, fill);
            DrawBorder(buttonX, toolbarY, button.Width, ButtonHeight, stroke);
            UIRenderer::DrawString(button.Label, buttonX + 9.0f, toolbarY + 18.0f, primaryAction ? TextStrong : DirectX::XMFLOAT4{ 0.78f, 0.80f, 0.84f, 1.0f });
            buttonX += button.Width + ButtonGap;
        }

        const std::string fileLabel = m_GraphPath.empty()
            ? "No graph loaded"
            : m_GraphPath.filename().string() + (m_IsDirty ? " *" : "");
        UIRenderer::DrawRectFilled(m_CalculatedPos.x + 10.0f, canvasY + 9.0f, (std::min)(canvasW - 20.0f, 420.0f), 25.0f, { 0.035f, 0.037f, 0.044f, 0.86f });
        DrawBorder(m_CalculatedPos.x + 10.0f, canvasY + 9.0f, (std::min)(canvasW - 20.0f, 420.0f), 25.0f, { 0.14f, 0.15f, 0.17f, 1.0f });
        UIRenderer::DrawString(fileLabel, m_CalculatedPos.x + 18.0f, canvasY + 27.0f, TextMuted);

        auto drawConnection = [](DirectX::XMFLOAT2 from, DirectX::XMFLOAT2 to, DirectX::XMFLOAT4 color)
        {
            const float midX = (from.x + to.x) * 0.5f;
            DirectX::XMFLOAT4 shadow = { 0.01f, 0.012f, 0.016f, 0.85f };
            auto drawSegment = [](float x, float y, float w, float h, const DirectX::XMFLOAT4& c)
            {
                if (w < 0.0f) { x += w; w = -w; }
                if (h < 0.0f) { y += h; h = -h; }
                UIRenderer::DrawRectFilled(x, y, (std::max)(1.0f, w), (std::max)(1.0f, h), c);
            };

            drawSegment(from.x, from.y + 2.0f, midX - from.x, 2.0f, shadow);
            drawSegment(midX - 1.0f, (std::min)(from.y, to.y) + 2.0f, 2.0f, std::abs(to.y - from.y), shadow);
            drawSegment(midX, to.y + 2.0f, to.x - midX, 2.0f, shadow);
            drawSegment(from.x, from.y - 1.0f, midX - from.x, 2.0f, color);
            drawSegment(midX - 1.0f, (std::min)(from.y, to.y), 2.0f, std::abs(to.y - from.y), color);
            drawSegment(midX, to.y - 1.0f, to.x - midX, 2.0f, color);
        };

        for (const VisualShaderNode& node : m_Graph.Nodes)
        {
            for (SlotKind inputSlot : { SlotKind::InputA, SlotKind::InputB })
            {
                const int inputId = inputSlot == SlotKind::InputA ? node.InputA : node.InputB;
                if (inputId < 0)
                    continue;
                const VisualShaderNode* inputNode = FindNode(inputId);
                if (!inputNode)
                    continue;
                drawConnection(
                    GetSlotPosition(*inputNode, SlotKind::Output),
                    GetSlotPosition(node, inputSlot),
                    { 0.42f, 0.62f, 0.92f, 1.0f });
            }
        }

        if (m_IsConnecting && m_ConnectingFromNodeId >= 0)
        {
            if (const VisualShaderNode* source = FindNode(m_ConnectingFromNodeId))
            {
                drawConnection(
                    GetSlotPosition(*source, SlotKind::Output),
                    { m_LastMouseX, m_LastMouseY },
                    { 0.90f, 0.72f, 0.36f, 1.0f });
            }
        }

        for (const VisualShaderNode& node : m_Graph.Nodes)
        {
            NodeRect rect = GetNodeRect(node);
            const bool selected = node.Id == m_SelectedNodeId;
            DirectX::XMFLOAT4 accent = GetNodeAccent(node.Type);
            DirectX::XMFLOAT4 bodyColor = selected ? DirectX::XMFLOAT4{ 0.135f, 0.155f, 0.185f, 1.0f } : NodeBody;
            DirectX::XMFLOAT4 borderColor = selected ? accent : PanelStroke;

            // 노드는 그림자, 헤더, accent bar를 분리해 그린다.
            // 렌더러에 둥근 사각형이 없어도 레이어를 나누면 임시 UI보다 제품 UI에 가까운 깊이가 생긴다.
            UIRenderer::DrawRectFilled(rect.X + 4.0f, rect.Y + 5.0f, rect.W, rect.H, { 0.010f, 0.012f, 0.016f, 0.88f });
            UIRenderer::DrawRectFilled(rect.X, rect.Y, rect.W, rect.H, bodyColor);
            UIRenderer::DrawRectFilled(rect.X, rect.Y, rect.W, m_NodeHeaderHeight, NodeHeader);
            UIRenderer::DrawRectFilled(rect.X, rect.Y, 4.0f, rect.H, accent);
            DrawBorder(rect.X, rect.Y, rect.W, rect.H, borderColor, selected ? 2.0f : 1.0f);
            if (selected)
                DrawBorder(rect.X - 3.0f, rect.Y - 3.0f, rect.W + 6.0f, rect.H + 6.0f, { accent.x, accent.y, accent.z, 0.35f });

            UIRenderer::DrawString(GetNodeTitle(node), rect.X + 13.0f, rect.Y + 18.0f, TextStrong);
            UIRenderer::DrawString(GetNodeTypeLabel(node.Type), rect.X + rect.W - 104.0f, rect.Y + 18.0f, { accent.x, accent.y, accent.z, 0.88f });

            if (node.Type == VisualShaderNodeType::Color)
            {
                UIRenderer::DrawString("RGBA", rect.X + 16.0f, rect.Y + 48.0f, TextMuted);
                UIRenderer::DrawRectFilled(rect.X + 68.0f, rect.Y + 36.0f, rect.W - 86.0f, 28.0f, node.Color);
                DrawBorder(rect.X + 68.0f, rect.Y + 36.0f, rect.W - 86.0f, 28.0f, { 0.04f, 0.04f, 0.05f, 1.0f });
            }
            else if (node.Type == VisualShaderNodeType::Texture2D)
            {
                UIRenderer::DrawString("UV", rect.X + 16.0f, rect.Y + 50.0f, TextMuted);
                UIRenderer::DrawString("AlbedoTexture", rect.X + 68.0f, rect.Y + 50.0f, { 0.74f, 0.76f, 0.80f, 1.0f });
            }
            else if (node.Type == VisualShaderNodeType::Output)
            {
                UIRenderer::DrawString("Final Color", rect.X + 16.0f, rect.Y + 50.0f, { 0.74f, 0.80f, 0.74f, 1.0f });
            }
            else
            {
                UIRenderer::DrawString("A", rect.X + 16.0f, rect.Y + 50.0f, TextMuted);
                UIRenderer::DrawString("B", rect.X + 16.0f, rect.Y + 74.0f, TextMuted);
            }

            for (SlotKind slot : { SlotKind::InputA, SlotKind::InputB, SlotKind::Output })
            {
                if ((slot == SlotKind::Output && node.Type == VisualShaderNodeType::Output) ||
                    (slot != SlotKind::Output && (slot == SlotKind::InputB ? GetInputCount(node) < 2 : GetInputCount(node) < 1)))
                    continue;

                DirectX::XMFLOAT2 p = GetSlotPosition(node, slot);
                DirectX::XMFLOAT4 slotColor = slot == SlotKind::Output
                    ? accent
                    : DirectX::XMFLOAT4{ 0.50f, 0.52f, 0.58f, 1.0f };
                UIRenderer::DrawRectFilled(p.x - SlotRadius - 2.0f, p.y - SlotRadius - 2.0f, (SlotRadius + 2.0f) * 2.0f, (SlotRadius + 2.0f) * 2.0f, { 0.02f, 0.02f, 0.025f, 1.0f });
                UIRenderer::DrawRectFilled(p.x - SlotRadius, p.y - SlotRadius, SlotRadius * 2.0f, SlotRadius * 2.0f, slotColor);
                DrawBorder(p.x - SlotRadius, p.y - SlotRadius, SlotRadius * 2.0f, SlotRadius * 2.0f, { 0.82f, 0.84f, 0.88f, 0.55f });
            }
        }
    }

    bool MaterialGraphPanel::WantsMouseCapture() const
    {
        return WindowPanel::WantsMouseCapture() || m_IsDraggingNode || m_IsConnecting;
    }

    bool MaterialGraphPanel::OnMouseButtonPressed(MouseButtonPressedEvent& e)
    {
        if (WindowPanel::OnMouseButtonPressed(e))
            return true;

        if (e.GetButton() != 0 || !IsPointInside(e.GetX(), e.GetY()))
            return false;

        BringToFront();
        m_LastMouseX = e.GetX();
        m_LastMouseY = e.GetY();

        if (HandleToolbarClick(e.GetX(), e.GetY()))
        {
            e.Handled = true;
            return true;
        }

        SlotHit slot = GetSlotAt(e.GetX(), e.GetY());
        if (slot.Slot == SlotKind::Output)
        {
            m_ConnectingFromNodeId = slot.NodeId;
            m_IsConnecting = true;
            Widget::BeginMouseInteraction(this);
            e.Handled = true;
            return true;
        }

        int nodeId = GetNodeAt(e.GetX(), e.GetY());
        m_SelectedNodeId = nodeId;
        if (VisualShaderNode* node = FindNode(nodeId))
        {
            NodeRect rect = GetNodeRect(*node);
            m_IsDraggingNode = true;
            m_DraggingNodeId = nodeId;
            m_DragOffsetX = e.GetX() - rect.X;
            m_DragOffsetY = e.GetY() - rect.Y;
            Widget::BeginMouseInteraction(this);
        }

        e.Handled = true;
        return true;
    }

    bool MaterialGraphPanel::OnMouseMoved(MouseMovedEvent& e)
    {
        if (WindowPanel::OnMouseMoved(e))
            return true;

        m_LastMouseX = e.GetX();
        m_LastMouseY = e.GetY();

        if (m_IsDraggingNode)
        {
            if (VisualShaderNode* node = FindNode(m_DraggingNodeId))
            {
                const float titleHeight = GetContentPosition().y - m_CalculatedPos.y;
                node->Position.x = (std::max)(8.0f, e.GetX() - m_CalculatedPos.x - m_DragOffsetX);
                node->Position.y = (std::max)(m_ToolbarHeight + titleHeight + 8.0f, e.GetY() - m_CalculatedPos.y - m_DragOffsetY);
                MarkDirty();
            }
            e.Handled = true;
            return true;
        }

        if (m_IsConnecting)
        {
            e.Handled = true;
            return true;
        }

        return false;
    }

    bool MaterialGraphPanel::OnMouseButtonReleased(MouseButtonReleasedEvent& e)
    {
        if (WindowPanel::OnMouseButtonReleased(e))
            return true;

        if (e.GetButton() != 0)
            return false;

        if (m_IsConnecting)
        {
            SlotHit target = GetSlotAt(e.GetX(), e.GetY());
            if (target.NodeId >= 0 && target.Slot != SlotKind::None && target.Slot != SlotKind::Output && target.NodeId != m_ConnectingFromNodeId)
            {
                if (VisualShaderNode* node = FindNode(target.NodeId))
                {
                    if (target.Slot == SlotKind::InputA)
                        node->InputA = m_ConnectingFromNodeId;
                    else if (target.Slot == SlotKind::InputB)
                        node->InputB = m_ConnectingFromNodeId;
                    MarkDirty();
                }
            }

            m_IsConnecting = false;
            m_ConnectingFromNodeId = -1;
            Widget::EndMouseInteraction(this);
            e.Handled = true;
            return true;
        }

        if (m_IsDraggingNode)
        {
            m_IsDraggingNode = false;
            m_DraggingNodeId = -1;
            Widget::EndMouseInteraction(this);
            e.Handled = true;
            return true;
        }

        return false;
    }

    bool MaterialGraphPanel::OnKeyPressed(KeyPressedEvent& e)
    {
        if (e.GetKeyCode() == 0x2E && DeleteSelectedNode())
        {
            e.Handled = true;
            return true;
        }
        return false;
    }

    void MaterialGraphPanel::AddNode(VisualShaderNodeType type)
    {
        VisualShaderNode node;
        node.Id = AllocateNodeId();
        node.Type = type;
        node.Position = { 120.0f + (float)(m_Graph.Nodes.size() % 4) * 40.0f, 110.0f + (float)(m_Graph.Nodes.size() % 5) * 36.0f };
        node.Color = { 0.85f, 0.85f, 0.90f, 1.0f };

        switch (type)
        {
            case VisualShaderNodeType::Color: node.Name = "Color"; break;
            case VisualShaderNodeType::Texture2D: node.Name = "Texture Sample"; break;
            case VisualShaderNodeType::Multiply: node.Name = "Multiply"; break;
            case VisualShaderNodeType::Add: node.Name = "Add"; break;
            case VisualShaderNodeType::Output: node.Name = "Output"; break;
        }

        if (type == VisualShaderNodeType::Output)
        {
            auto existing = std::find_if(m_Graph.Nodes.begin(), m_Graph.Nodes.end(), [](const VisualShaderNode& n)
            {
                return n.Type == VisualShaderNodeType::Output;
            });
            if (existing != m_Graph.Nodes.end())
            {
                ConsoleLog::Warning("Visual shader already has an Output node.");
                return;
            }
        }

        m_Graph.Nodes.push_back(node);
        m_SelectedNodeId = node.Id;
        MarkDirty();
    }

    bool MaterialGraphPanel::DeleteSelectedNode()
    {
        if (m_SelectedNodeId < 0)
            return false;

        auto it = std::find_if(m_Graph.Nodes.begin(), m_Graph.Nodes.end(), [this](const VisualShaderNode& node)
        {
            return node.Id == m_SelectedNodeId;
        });
        if (it == m_Graph.Nodes.end())
            return false;

        if (it->Type == VisualShaderNodeType::Output)
        {
            ConsoleLog::Warning("Output node is required.");
            return false;
        }

        const int removedId = it->Id;
        m_Graph.Nodes.erase(it);
        for (VisualShaderNode& node : m_Graph.Nodes)
        {
            if (node.InputA == removedId)
                node.InputA = -1;
            if (node.InputB == removedId)
                node.InputB = -1;
        }
        m_SelectedNodeId = -1;
        MarkDirty();
        return true;
    }

    VisualShaderNode* MaterialGraphPanel::FindNode(int nodeId)
    {
        auto it = std::find_if(m_Graph.Nodes.begin(), m_Graph.Nodes.end(), [nodeId](const VisualShaderNode& node)
        {
            return node.Id == nodeId;
        });
        return it == m_Graph.Nodes.end() ? nullptr : &(*it);
    }

    const VisualShaderNode* MaterialGraphPanel::FindNode(int nodeId) const
    {
        auto it = std::find_if(m_Graph.Nodes.begin(), m_Graph.Nodes.end(), [nodeId](const VisualShaderNode& node)
        {
            return node.Id == nodeId;
        });
        return it == m_Graph.Nodes.end() ? nullptr : &(*it);
    }

    MaterialGraphPanel::NodeRect MaterialGraphPanel::GetNodeRect(const VisualShaderNode& node) const
    {
        const float height = node.Type == VisualShaderNodeType::Color ? 88.0f : (GetInputCount(node) >= 2 ? 94.0f : 72.0f);
        return { node.Id, m_CalculatedPos.x + node.Position.x, m_CalculatedPos.y + node.Position.y, m_NodeWidth, height };
    }

    DirectX::XMFLOAT2 MaterialGraphPanel::GetSlotPosition(const VisualShaderNode& node, SlotKind slot) const
    {
        NodeRect rect = GetNodeRect(node);
        if (slot == SlotKind::Output)
            return { rect.X + rect.W, rect.Y + m_NodeHeaderHeight + 26.0f };
        if (slot == SlotKind::InputB)
            return { rect.X, rect.Y + m_NodeHeaderHeight + 50.0f };
        return { rect.X, rect.Y + m_NodeHeaderHeight + 26.0f };
    }

    int MaterialGraphPanel::GetNodeAt(float mouseX, float mouseY) const
    {
        for (auto it = m_Graph.Nodes.rbegin(); it != m_Graph.Nodes.rend(); ++it)
        {
            NodeRect rect = GetNodeRect(*it);
            if (IsPointInRect(mouseX, mouseY, rect.X, rect.Y, rect.W, rect.H))
                return it->Id;
        }
        return -1;
    }

    MaterialGraphPanel::SlotHit MaterialGraphPanel::GetSlotAt(float mouseX, float mouseY) const
    {
        for (const VisualShaderNode& node : m_Graph.Nodes)
        {
            for (SlotKind slot : { SlotKind::InputA, SlotKind::InputB, SlotKind::Output })
            {
                if ((slot == SlotKind::Output && node.Type == VisualShaderNodeType::Output) ||
                    (slot != SlotKind::Output && (slot == SlotKind::InputB ? GetInputCount(node) < 2 : GetInputCount(node) < 1)))
                    continue;

                DirectX::XMFLOAT2 p = GetSlotPosition(node, slot);
                if (std::abs(mouseX - p.x) <= SlotRadius + 4.0f && std::abs(mouseY - p.y) <= SlotRadius + 4.0f)
                    return { node.Id, slot };
            }
        }
        return {};
    }

    bool MaterialGraphPanel::HandleToolbarClick(float mouseX, float mouseY)
    {
        const float titleHeight = GetContentPosition().y - m_CalculatedPos.y;
        float buttonX = m_CalculatedPos.x + 8.0f;
        const float buttonY = m_CalculatedPos.y + titleHeight + 5.0f;

        for (int i = 0; i < (int)(sizeof(ToolbarButtons) / sizeof(ToolbarButtons[0])); ++i)
        {
            const ToolbarButton& button = ToolbarButtons[i];
            if (IsPointInRect(mouseX, mouseY, buttonX, buttonY, button.Width, ButtonHeight))
            {
                switch (i)
                {
                    case 0: SaveGraph(); break;
                    case 1: SaveGraph(); break;
                    case 2: AddNode(VisualShaderNodeType::Color); break;
                    case 3: AddNode(VisualShaderNodeType::Texture2D); break;
                    case 4: AddNode(VisualShaderNodeType::Multiply); break;
                    case 5: AddNode(VisualShaderNodeType::Add); break;
                    case 6: AddNode(VisualShaderNodeType::Output); break;
                    case 7: DeleteSelectedNode(); break;
                }
                return true;
            }
            buttonX += button.Width + ButtonGap;
        }

        return false;
    }

    void MaterialGraphPanel::MarkDirty()
    {
        m_IsDirty = true;
    }

    std::string MaterialGraphPanel::GetNodeTitle(const VisualShaderNode& node) const
    {
        return node.Name.empty() ? "Node" : node.Name;
    }

    int MaterialGraphPanel::AllocateNodeId() const
    {
        int nextId = 1;
        for (const VisualShaderNode& node : m_Graph.Nodes)
            nextId = (std::max)(nextId, node.Id + 1);
        return nextId;
    }

    int MaterialGraphPanel::GetInputCount(const VisualShaderNode& node) const
    {
        switch (node.Type)
        {
            case VisualShaderNodeType::Multiply:
            case VisualShaderNodeType::Add:
                return 2;
            case VisualShaderNodeType::Output:
                return 1;
            default:
                return 0;
        }
    }

    bool MaterialGraphPanel::IsPointInRect(float mouseX, float mouseY, float x, float y, float w, float h)
    {
        return mouseX >= x && mouseX <= x + w && mouseY >= y && mouseY <= y + h;
    }
}
