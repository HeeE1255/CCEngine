#include "UI/MaterialGraphPanel.h"

#include "Application.h"
#include "Core/AssetDatabase.h"
#include "Core/ConsoleLog.h"
#include "Events/KeyEvent.h"
#include "Events/MouseEvent.h"
#include "Renderer/UIRenderer.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <unordered_set>

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
        constexpr DirectX::XMFLOAT4 InvalidConnectionColor = { 0.92f, 0.34f, 0.30f, 1.0f };
        constexpr float NodeMenuWidth = 188.0f;
        constexpr float NodeMenuRowHeight = 25.0f;

        struct ToolbarButton
        {
            const char* Label;
            float Width;
        };

        const ToolbarButton ToolbarButtons[] =
        {
            { "Save", 52.0f },
            { "Generate", 78.0f },
            { "Undo", 56.0f },
            { "Redo", 56.0f },
            { "Color", 58.0f },
            { "Texture", 72.0f },
            { "Multiply", 82.0f },
            { "Add", 48.0f },
            { "Output", 66.0f },
            { "Delete", 64.0f }
        };

        struct NodeMenuItem
        {
            const char* Label;
            VisualShaderNodeType Type;
        };

        const NodeMenuItem NodeMenuItems[] =
        {
            { "Color", VisualShaderNodeType::Color },
            { "Texture Sample", VisualShaderNodeType::Texture2D },
            { "Multiply", VisualShaderNodeType::Multiply },
            { "Add", VisualShaderNodeType::Add },
            { "Lerp", VisualShaderNodeType::Lerp },
            { "Fresnel", VisualShaderNodeType::Fresnel },
            { "Normal", VisualShaderNodeType::Normal },
            { "Roughness", VisualShaderNodeType::Roughness },
            { "Metallic", VisualShaderNodeType::Metallic },
            { "One Minus", VisualShaderNodeType::OneMinus },
            { "Power", VisualShaderNodeType::Power },
            { "Saturate", VisualShaderNodeType::Saturate },
            { "UV", VisualShaderNodeType::UV },
            { "Time", VisualShaderNodeType::Time },
            { "Output", VisualShaderNodeType::Output }
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
                case VisualShaderNodeType::Lerp: return DirectX::XMFLOAT4{ 0.55f, 0.62f, 0.92f, 1.0f };
                case VisualShaderNodeType::Fresnel: return DirectX::XMFLOAT4{ 0.72f, 0.72f, 0.92f, 1.0f };
                case VisualShaderNodeType::Normal: return DirectX::XMFLOAT4{ 0.35f, 0.74f, 0.52f, 1.0f };
                case VisualShaderNodeType::Roughness: return DirectX::XMFLOAT4{ 0.66f, 0.66f, 0.70f, 1.0f };
                case VisualShaderNodeType::Metallic: return DirectX::XMFLOAT4{ 0.76f, 0.78f, 0.82f, 1.0f };
                case VisualShaderNodeType::OneMinus: return DirectX::XMFLOAT4{ 0.86f, 0.52f, 0.42f, 1.0f };
                case VisualShaderNodeType::Power: return DirectX::XMFLOAT4{ 0.78f, 0.56f, 0.90f, 1.0f };
                case VisualShaderNodeType::Saturate: return DirectX::XMFLOAT4{ 0.52f, 0.78f, 0.86f, 1.0f };
                case VisualShaderNodeType::UV: return DirectX::XMFLOAT4{ 0.46f, 0.68f, 0.96f, 1.0f };
                case VisualShaderNodeType::Time: return DirectX::XMFLOAT4{ 0.92f, 0.74f, 0.34f, 1.0f };
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
                case VisualShaderNodeType::Lerp: return "LERP";
                case VisualShaderNodeType::Fresnel: return "FRESNEL";
                case VisualShaderNodeType::Normal: return "NORMAL";
                case VisualShaderNodeType::Roughness: return "ROUGHNESS";
                case VisualShaderNodeType::Metallic: return "METALLIC";
                case VisualShaderNodeType::OneMinus: return "ONE MINUS";
                case VisualShaderNodeType::Power: return "POWER";
                case VisualShaderNodeType::Saturate: return "SATURATE";
                case VisualShaderNodeType::UV: return "UV";
                case VisualShaderNodeType::Time: return "TIME";
                case VisualShaderNodeType::Output: return "OUTPUT";
                default: return "NODE";
            }
        }

        float CubicBezier(float p0, float p1, float p2, float p3, float t)
        {
            const float u = 1.0f - t;
            return (u * u * u * p0) +
                (3.0f * u * u * t * p1) +
                (3.0f * u * t * t * p2) +
                (t * t * t * p3);
        }

        void DrawLineSegment(DirectX::XMFLOAT2 a, DirectX::XMFLOAT2 b, const DirectX::XMFLOAT4& color, float thickness)
        {
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            const int steps = (std::max)(1, (int)(std::sqrt(dx * dx + dy * dy) / 4.0f));
            for (int i = 0; i <= steps; ++i)
            {
                const float t = (float)i / (float)steps;
                const float x = a.x + dx * t;
                const float y = a.y + dy * t;
                UIRenderer::DrawRectFilled(x - thickness * 0.5f, y - thickness * 0.5f, thickness, thickness, color);
            }
        }

        void DrawBezierConnection(DirectX::XMFLOAT2 from, DirectX::XMFLOAT2 to, const DirectX::XMFLOAT4& color, bool preview)
        {
            const float distance = (std::max)(80.0f, std::abs(to.x - from.x) * 0.5f);
            const DirectX::XMFLOAT2 controlA = { from.x + distance, from.y };
            const DirectX::XMFLOAT2 controlB = { to.x - distance, to.y };
            const int segmentCount = preview ? 18 : 24;
            DirectX::XMFLOAT2 previous = from;

            // UI 렌더러에 곡선 primitive가 없으므로 cubic Bezier를 짧은 선분으로 샘플링한다.
            // 시작/끝 접선은 좌우 방향으로 잡아 노드 그래프에서 흔히 보는 자연스러운 S자 연결을 만든다.
            for (int i = 1; i <= segmentCount; ++i)
            {
                const float t = (float)i / (float)segmentCount;
                DirectX::XMFLOAT2 current =
                {
                    CubicBezier(from.x, controlA.x, controlB.x, to.x, t),
                    CubicBezier(from.y, controlA.y, controlB.y, to.y, t)
                };
                DrawLineSegment(previous, current, { 0.01f, 0.012f, 0.016f, 0.78f }, preview ? 5.0f : 4.0f);
                DrawLineSegment(previous, current, color, preview ? 3.0f : 2.0f);
                previous = current;
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
        m_IsPanningView = false;
        m_HasDragStartGraph = false;
        m_ViewOffsetX = 0.0f;
        m_ViewOffsetY = 0.0f;
        m_Zoom = 1.0f;
        m_IsDirty = false;
        ClearUndoHistory();
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

    void MaterialGraphPanel::OnUpdate(float deltaTime)
    {
        WindowPanel::OnUpdate(deltaTime);
        if (m_StatusTimer > 0.0f)
        {
            m_StatusTimer = (std::max)(0.0f, m_StatusTimer - deltaTime);
            if (m_StatusTimer <= 0.0f)
                m_StatusMessage.clear();
        }
    }

    void MaterialGraphPanel::OnRender()
    {
        WindowPanel::OnRender();
        if (!IsVisible())
            return;

        // Material Graph는 노드와 연결선을 자식 위젯이 아니라 직접 그린다.
        // 그래서 WindowPanel의 자식 클리핑만으로는 부족하고, 패널 전체에 직접 scissor를 걸어야 한다.
        UIRenderer::SetClipRect(m_CalculatedPos.x, m_CalculatedPos.y, m_CalculatedSize.x, m_CalculatedSize.y);

        const float titleHeight = GetContentPosition().y - m_CalculatedPos.y;
        const float toolbarX = m_CalculatedPos.x + 8.0f;
        const float toolbarY = m_CalculatedPos.y + titleHeight + 5.0f;
        const float canvasX = m_CalculatedPos.x;
        const float canvasY = m_CalculatedPos.y + titleHeight + m_ToolbarHeight;
        const float canvasW = m_CalculatedSize.x;
        const float canvasH = (std::max)(0.0f, m_CalculatedSize.y - titleHeight - m_ToolbarHeight);

        UIRenderer::DrawRectFilled(canvasX, canvasY, canvasW, canvasH, CanvasColor);

        const float fineStep = 32.0f * m_Zoom;
        const float majorStep = 128.0f * m_Zoom;
        const float fineStartX = canvasX + std::fmod(m_ViewOffsetX * m_Zoom, fineStep);
        const float fineStartY = canvasY + std::fmod(m_ViewOffsetY * m_Zoom, fineStep);
        const float majorStartX = canvasX + std::fmod(m_ViewOffsetX * m_Zoom, majorStep);
        const float majorStartY = canvasY + std::fmod(m_ViewOffsetY * m_Zoom, majorStep);

        for (float x = fineStartX; x < canvasX + canvasW; x += fineStep)
            UIRenderer::DrawRectFilled(x, canvasY, 1.0f, canvasH, CanvasGridFine);
        for (float y = fineStartY; y < canvasY + canvasH; y += fineStep)
            UIRenderer::DrawRectFilled(canvasX, y, canvasW, 1.0f, CanvasGridFine);
        for (float x = majorStartX; x < canvasX + canvasW; x += majorStep)
            UIRenderer::DrawRectFilled(x, canvasY, 1.0f, canvasH, CanvasGridMajor);
        for (float y = majorStartY; y < canvasY + canvasH; y += majorStep)
            UIRenderer::DrawRectFilled(canvasX, y, canvasW, 1.0f, CanvasGridMajor);

        UIRenderer::DrawRectFilled(m_CalculatedPos.x, m_CalculatedPos.y + titleHeight, m_CalculatedSize.x, m_ToolbarHeight, { 0.080f, 0.083f, 0.095f, 0.98f });
        UIRenderer::DrawRectFilled(m_CalculatedPos.x, m_CalculatedPos.y + titleHeight + m_ToolbarHeight - 1.0f, m_CalculatedSize.x, 1.0f, PanelStroke);

        float buttonX = toolbarX;
        for (int i = 0; i < (int)(sizeof(ToolbarButtons) / sizeof(ToolbarButtons[0])); ++i)
        {
            const ToolbarButton& button = ToolbarButtons[i];
            const bool primaryAction = i < 2;
            const bool undoButton = i == 2;
            const bool redoButton = i == 3;
            const bool disabled = (undoButton && m_UndoStack.empty()) || (redoButton && m_RedoStack.empty());
            const bool destructive = i == 9;
            DirectX::XMFLOAT4 fill = primaryAction
                ? DirectX::XMFLOAT4{ 0.18f, 0.25f, 0.34f, 1.0f }
                : DirectX::XMFLOAT4{ 0.125f, 0.130f, 0.145f, 1.0f };
            DirectX::XMFLOAT4 stroke = primaryAction ? AccentBlue : PanelStroke;
            if ((undoButton || redoButton) && !disabled)
            {
                fill = { 0.14f, 0.17f, 0.22f, 1.0f };
                stroke = AccentCyan;
            }
            if (disabled)
            {
                fill = { 0.085f, 0.088f, 0.098f, 1.0f };
                stroke = { 0.12f, 0.125f, 0.14f, 1.0f };
            }
            if (destructive)
            {
                fill = { 0.22f, 0.10f, 0.12f, 1.0f };
                stroke = { 0.50f, 0.18f, 0.22f, 1.0f };
            }

            UIRenderer::DrawRectFilled(buttonX + 1.0f, toolbarY + 2.0f, button.Width, ButtonHeight, { 0.018f, 0.019f, 0.022f, 0.75f });
            UIRenderer::DrawRectFilled(buttonX, toolbarY, button.Width, ButtonHeight, fill);
            DrawBorder(buttonX, toolbarY, button.Width, ButtonHeight, stroke);
            UIRenderer::DrawString(button.Label, buttonX + 9.0f, toolbarY + 18.0f, disabled ? TextMuted : (primaryAction ? TextStrong : DirectX::XMFLOAT4{ 0.78f, 0.80f, 0.84f, 1.0f }));
            buttonX += button.Width + ButtonGap;
        }

        const std::string fileLabel = m_GraphPath.empty()
            ? "No graph loaded"
            : m_GraphPath.filename().string() + (m_IsDirty ? " *" : "");
        UIRenderer::DrawRectFilled(m_CalculatedPos.x + 10.0f, canvasY + 9.0f, (std::min)(canvasW - 20.0f, 420.0f), 25.0f, { 0.035f, 0.037f, 0.044f, 0.86f });
        DrawBorder(m_CalculatedPos.x + 10.0f, canvasY + 9.0f, (std::min)(canvasW - 20.0f, 420.0f), 25.0f, { 0.14f, 0.15f, 0.17f, 1.0f });
        UIRenderer::DrawString(fileLabel, m_CalculatedPos.x + 18.0f, canvasY + 27.0f, TextMuted);

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
                DrawBezierConnection(
                    GetSlotPosition(*inputNode, SlotKind::Output),
                    GetSlotPosition(node, inputSlot),
                    { 0.42f, 0.62f, 0.92f, 1.0f },
                    false);
            }
        }

        if (m_IsConnecting && m_ConnectingFromNodeId >= 0)
        {
            if (const VisualShaderNode* source = FindNode(m_ConnectingFromNodeId))
            {
                SlotHit hoveredSlot = GetSlotAt(m_LastMouseX, m_LastMouseY);
                const bool canConnect = hoveredSlot.NodeId >= 0 &&
                    hoveredSlot.Slot != SlotKind::None &&
                    hoveredSlot.Slot != SlotKind::Output &&
                    hoveredSlot.NodeId != m_ConnectingFromNodeId &&
                    !WouldCreateCycle(m_ConnectingFromNodeId, hoveredSlot.NodeId);

                DrawBezierConnection(
                    GetSlotPosition(*source, SlotKind::Output),
                    { m_LastMouseX, m_LastMouseY },
                    canConnect ? DirectX::XMFLOAT4{ 0.90f, 0.72f, 0.36f, 1.0f } : InvalidConnectionColor,
                    true);
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
            else if (node.Type == VisualShaderNodeType::Fresnel)
            {
                UIRenderer::DrawString("Power", rect.X + 16.0f, rect.Y + 50.0f, TextMuted);
                UIRenderer::DrawString(std::to_string(node.Value).substr(0, 4), rect.X + 76.0f, rect.Y + 50.0f, TextStrong);
            }
            else if (node.Type == VisualShaderNodeType::Normal)
            {
                UIRenderer::DrawString("World normal", rect.X + 16.0f, rect.Y + 50.0f, TextMuted);
            }
            else if (node.Type == VisualShaderNodeType::Roughness)
            {
                UIRenderer::DrawString("SurfaceValues.x", rect.X + 16.0f, rect.Y + 50.0f, TextMuted);
            }
            else if (node.Type == VisualShaderNodeType::Metallic)
            {
                UIRenderer::DrawString("SurfaceValues.y", rect.X + 16.0f, rect.Y + 50.0f, TextMuted);
            }
            else if (node.Type == VisualShaderNodeType::OneMinus)
            {
                UIRenderer::DrawString("1 - A", rect.X + 16.0f, rect.Y + 50.0f, TextMuted);
            }
            else if (node.Type == VisualShaderNodeType::Power)
            {
                UIRenderer::DrawString("Exponent", rect.X + 16.0f, rect.Y + 50.0f, TextMuted);
                UIRenderer::DrawString(std::to_string(node.Value).substr(0, 4), rect.X + 104.0f, rect.Y + 50.0f, TextStrong);
            }
            else if (node.Type == VisualShaderNodeType::Saturate)
            {
                UIRenderer::DrawString("Clamp 0..1", rect.X + 16.0f, rect.Y + 50.0f, TextMuted);
            }
            else if (node.Type == VisualShaderNodeType::UV)
            {
                UIRenderer::DrawString("TexCoord", rect.X + 16.0f, rect.Y + 50.0f, TextMuted);
            }
            else if (node.Type == VisualShaderNodeType::Time)
            {
                UIRenderer::DrawString("SurfaceValues.z", rect.X + 16.0f, rect.Y + 50.0f, TextMuted);
            }
            else if (node.Type == VisualShaderNodeType::Lerp)
            {
                UIRenderer::DrawString("A", rect.X + 16.0f, rect.Y + 50.0f, TextMuted);
                UIRenderer::DrawString("B", rect.X + 16.0f, rect.Y + 74.0f, TextMuted);
                UIRenderer::DrawString(("Alpha " + std::to_string(node.Value)).substr(0, 10), rect.X + 66.0f, rect.Y + 74.0f, TextStrong);
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

        DrawPropertyPanel();

        if (m_IsNodeMenuOpen)
        {
            const float menuH = 28.0f + (float)(sizeof(NodeMenuItems) / sizeof(NodeMenuItems[0])) * NodeMenuRowHeight;
            UIRenderer::DrawRectFilled(m_NodeMenuX + 4.0f, m_NodeMenuY + 5.0f, NodeMenuWidth, menuH, { 0.01f, 0.012f, 0.016f, 0.85f });
            UIRenderer::DrawRectFilled(m_NodeMenuX, m_NodeMenuY, NodeMenuWidth, menuH, { 0.095f, 0.100f, 0.115f, 0.98f });
            DrawBorder(m_NodeMenuX, m_NodeMenuY, NodeMenuWidth, menuH, { 0.30f, 0.32f, 0.36f, 1.0f });
            UIRenderer::DrawString("Add Node", m_NodeMenuX + 10.0f, m_NodeMenuY + 20.0f, TextStrong);

            Window* renderWindow = Widget::GetCurrentRenderWindow();
            auto [mouseX, mouseY] = renderWindow
                ? renderWindow->GetMousePosition()
                : CCEngine::Application::Get()->GetWindow().GetMousePosition();

            for (int i = 0; i < (int)(sizeof(NodeMenuItems) / sizeof(NodeMenuItems[0])); ++i)
            {
                const float rowY = m_NodeMenuY + 28.0f + (float)i * NodeMenuRowHeight;
                const bool hovered = IsPointInRect(mouseX, mouseY, m_NodeMenuX, rowY, NodeMenuWidth, NodeMenuRowHeight);
                if (hovered)
                    UIRenderer::DrawRectFilled(m_NodeMenuX + 3.0f, rowY + 2.0f, NodeMenuWidth - 6.0f, NodeMenuRowHeight - 4.0f, { 0.20f, 0.30f, 0.42f, 1.0f });
                UIRenderer::DrawRectFilled(m_NodeMenuX + 9.0f, rowY + 7.0f, 8.0f, 8.0f, GetNodeAccent(NodeMenuItems[i].Type));
                UIRenderer::DrawString(NodeMenuItems[i].Label, m_NodeMenuX + 25.0f, rowY + 18.0f, hovered ? TextStrong : DirectX::XMFLOAT4{ 0.74f, 0.76f, 0.80f, 1.0f });
            }
        }

        if (!m_StatusMessage.empty())
        {
            const float statusW = (std::min)(canvasW - 20.0f, 520.0f);
            const float statusY = m_CalculatedPos.y + m_CalculatedSize.y - 30.0f;
            UIRenderer::DrawRectFilled(m_CalculatedPos.x + 10.0f, statusY, statusW, 22.0f, { 0.06f, 0.065f, 0.075f, 0.92f });
            DrawBorder(m_CalculatedPos.x + 10.0f, statusY, statusW, 22.0f, { 0.22f, 0.24f, 0.28f, 1.0f });
            UIRenderer::DrawString(m_StatusMessage, m_CalculatedPos.x + 18.0f, statusY + 16.0f, TextMuted);
        }

        UIRenderer::ClearClipRect();
    }

    bool MaterialGraphPanel::OnEvent(Event& e)
    {
        if (!IsVisible())
            return false;

        if (e.GetEventType() == EventType::MouseScrolled)
        {
            MouseScrolledEvent& scrollEvent = static_cast<MouseScrolledEvent&>(e);
            Window* renderWindow = Widget::GetCurrentRenderWindow();
            auto [mouseX, mouseY] = renderWindow
                ? renderWindow->GetMousePosition()
                : CCEngine::Application::Get()->GetWindow().GetMousePosition();

            if (IsPointInside(mouseX, mouseY))
            {
                const DirectX::XMFLOAT2 graphBefore = ScreenToGraph(mouseX, mouseY);
                m_Zoom = (std::clamp)(m_Zoom + scrollEvent.GetYOffset() * 0.08f, 0.80f, 1.35f);
                const DirectX::XMFLOAT2 graphAfter = ScreenToGraph(mouseX, mouseY);

                // 줌은 마우스 아래 지점을 기준으로 맞춘다.
                // 이렇게 해야 확대/축소 중 노드가 화면 밖으로 미끄러지는 느낌이 줄어든다.
                m_ViewOffsetX += graphAfter.x - graphBefore.x;
                m_ViewOffsetY += graphAfter.y - graphBefore.y;
                SetStatusMessage("Zoom " + std::to_string((int)(m_Zoom * 100.0f)) + "%", 0.7f);
                e.Handled = true;
                return true;
            }
        }

        return WindowPanel::OnEvent(e);
    }

    bool MaterialGraphPanel::WantsMouseCapture() const
    {
        return WindowPanel::WantsMouseCapture() || m_IsDraggingNode || m_IsConnecting || m_IsPanningView;
    }

    bool MaterialGraphPanel::OnMouseButtonPressed(MouseButtonPressedEvent& e)
    {
        if (WindowPanel::OnMouseButtonPressed(e))
            return true;

        if (!IsPointInside(e.GetX(), e.GetY()))
            return false;

        BringToFront();
        Widget::SetKeyboardFocus(this);
        m_LastMouseX = e.GetX();
        m_LastMouseY = e.GetY();

        if (e.GetButton() == 1)
        {
            if (SlotHit slot = GetSlotAt(e.GetX(), e.GetY()); slot.NodeId >= 0 && slot.Slot != SlotKind::None && slot.Slot != SlotKind::Output)
            {
                ClearInputConnection(slot.NodeId, slot.Slot);
                e.Handled = true;
                return true;
            }

            OpenNodeMenu(e.GetX(), e.GetY());
            e.Handled = true;
            return true;
        }

        if (e.GetButton() == 2)
        {
            m_IsPanningView = true;
            m_PanStartMouseX = e.GetX();
            m_PanStartMouseY = e.GetY();
            m_PanStartOffsetX = m_ViewOffsetX;
            m_PanStartOffsetY = m_ViewOffsetY;
            Widget::BeginMouseInteraction(this);
            e.Handled = true;
            return true;
        }

        if (e.GetButton() != 0)
            return false;

        if (m_IsNodeMenuOpen && HandleNodeMenuClick(e.GetX(), e.GetY()))
        {
            e.Handled = true;
            return true;
        }
        CloseNodeMenu();

        if (HandleToolbarClick(e.GetX(), e.GetY()))
        {
            e.Handled = true;
            return true;
        }

        if (HandlePropertyPanelClick(e.GetX(), e.GetY()))
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
            const DirectX::XMFLOAT2 graphMouse = ScreenToGraph(e.GetX(), e.GetY());
            m_IsDraggingNode = true;
            m_DraggingNodeId = nodeId;
            m_DragOffsetX = graphMouse.x - node->Position.x;
            m_DragOffsetY = graphMouse.y - node->Position.y;
            // 노드 이동은 마우스가 움직일 때마다 좌표가 계속 바뀐다.
            // 히스토리는 시작 상태만 잡아두고, 버튼을 뗄 때 최종 위치와 비교해서 한 번만 남긴다.
            m_DragStartGraph = m_Graph;
            m_DragStartSelectedNodeId = m_SelectedNodeId;
            m_HasDragStartGraph = true;
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

        if (m_IsPanningView)
        {
            m_ViewOffsetX = m_PanStartOffsetX + (e.GetX() - m_PanStartMouseX) / m_Zoom;
            m_ViewOffsetY = m_PanStartOffsetY + (e.GetY() - m_PanStartMouseY) / m_Zoom;
            e.Handled = true;
            return true;
        }

        if (m_IsDraggingNode)
        {
            if (VisualShaderNode* node = FindNode(m_DraggingNodeId))
            {
                const float titleHeight = GetContentPosition().y - m_CalculatedPos.y;
                const DirectX::XMFLOAT2 graphMouse = ScreenToGraph(e.GetX(), e.GetY());
                node->Position.x = (std::max)(8.0f, graphMouse.x - m_DragOffsetX);
                node->Position.y = (std::max)(m_ToolbarHeight + titleHeight + 8.0f, graphMouse.y - m_DragOffsetY);
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

        if (e.GetButton() == 2 && m_IsPanningView)
        {
            m_IsPanningView = false;
            Widget::EndMouseInteraction(this);
            e.Handled = true;
            return true;
        }

        if (e.GetButton() != 0)
            return false;

        if (m_IsConnecting)
        {
            SlotHit target = GetSlotAt(e.GetX(), e.GetY());
            if (target.NodeId >= 0 && target.Slot != SlotKind::None)
                TryConnectNodes(m_ConnectingFromNodeId, target.NodeId, target.Slot);

            m_IsConnecting = false;
            m_ConnectingFromNodeId = -1;
            Widget::EndMouseInteraction(this);
            e.Handled = true;
            return true;
        }

        if (m_IsDraggingNode)
        {
            if (m_HasDragStartGraph)
                PushUndoRecord("Move Node", m_DragStartGraph, m_DragStartSelectedNodeId, m_Graph, m_SelectedNodeId);
            m_HasDragStartGraph = false;
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
        if (!IsVisible() || !Widget::IsKeyboardFocusOwner(this))
            return false;

        const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        if (ctrl && e.GetKeyCode() == 'Z')
        {
            if (shift)
                RedoGraphEdit();
            else
                UndoGraphEdit();
            e.Handled = true;
            return true;
        }

        if (ctrl && e.GetKeyCode() == 'Y')
        {
            RedoGraphEdit();
            e.Handled = true;
            return true;
        }

        if (ctrl && e.GetKeyCode() == 'S')
        {
            SaveGraph();
            e.Handled = true;
            return true;
        }

        if (e.GetKeyCode() == VK_ESCAPE)
        {
            m_IsConnecting = false;
            m_ConnectingFromNodeId = -1;
            CloseNodeMenu();
            Widget::EndMouseInteraction(this);
            e.Handled = true;
            return true;
        }

        if (e.GetKeyCode() == 'F')
        {
            FrameAllNodes();
            e.Handled = true;
            return true;
        }

        if (e.GetKeyCode() == 0x2E)
        {
            if (m_SelectedNodeId >= 0)
                DeleteSelectedNode();
            else
                SetStatusMessage("No graph node selected.", 1.2f);

            // Delete가 실패해도 여기서 반드시 소비한다.
            // 그래프에서 Output 삭제를 막은 뒤 이벤트가 뒤쪽 Asset Browser까지 흘러가면 에셋 삭제 팝업이 떠버린다.
            e.Handled = true;
            return true;
        }
        return false;
    }

    void MaterialGraphPanel::AddNode(VisualShaderNodeType type)
    {
        if (type == VisualShaderNodeType::Output)
        {
            auto existing = std::find_if(m_Graph.Nodes.begin(), m_Graph.Nodes.end(), [](const VisualShaderNode& n)
            {
                return n.Type == VisualShaderNodeType::Output;
            });
            if (existing != m_Graph.Nodes.end())
            {
                ConsoleLog::Warning("Visual shader already has an Output node.");
                SetStatusMessage("Output node already exists.", 1.2f);
                return;
            }
        }

        const VisualShaderAsset beforeGraph = m_Graph;
        const int beforeSelectedNodeId = m_SelectedNodeId;

        VisualShaderNode node;
        node.Id = AllocateNodeId();
        node.Type = type;
        node.Position = {
            m_IsNodeMenuOpen ? m_NodeSpawnX : 120.0f + (float)(m_Graph.Nodes.size() % 4) * 40.0f,
            m_IsNodeMenuOpen ? m_NodeSpawnY : 110.0f + (float)(m_Graph.Nodes.size() % 5) * 36.0f
        };
        node.Color = { 0.85f, 0.85f, 0.90f, 1.0f };

        switch (type)
        {
            case VisualShaderNodeType::Color: node.Name = "Color"; break;
            case VisualShaderNodeType::Texture2D: node.Name = "Texture Sample"; break;
            case VisualShaderNodeType::Multiply: node.Name = "Multiply"; break;
            case VisualShaderNodeType::Add: node.Name = "Add"; break;
            case VisualShaderNodeType::Lerp: node.Name = "Lerp"; break;
            case VisualShaderNodeType::Fresnel: node.Name = "Fresnel"; break;
            case VisualShaderNodeType::Normal: node.Name = "Normal"; break;
            case VisualShaderNodeType::Roughness: node.Name = "Roughness"; break;
            case VisualShaderNodeType::Metallic: node.Name = "Metallic"; break;
            case VisualShaderNodeType::OneMinus: node.Name = "One Minus"; break;
            case VisualShaderNodeType::Power: node.Name = "Power"; node.Value = 2.0f; break;
            case VisualShaderNodeType::Saturate: node.Name = "Saturate"; break;
            case VisualShaderNodeType::UV: node.Name = "UV"; break;
            case VisualShaderNodeType::Time: node.Name = "Time"; break;
            case VisualShaderNodeType::Output: node.Name = "Output"; break;
        }
        if (type == VisualShaderNodeType::Fresnel)
            node.Value = 4.0f;

        m_Graph.Nodes.push_back(node);
        m_SelectedNodeId = node.Id;
        CloseNodeMenu();
        PushUndoRecord("Add " + node.Name + " Node", beforeGraph, beforeSelectedNodeId, m_Graph, m_SelectedNodeId);
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
            SetStatusMessage("Output node is required.", 1.2f);
            return false;
        }

        const VisualShaderAsset beforeGraph = m_Graph;
        const int beforeSelectedNodeId = m_SelectedNodeId;
        const int removedId = it->Id;
        const std::string removedName = GetNodeTitle(*it);
        m_Graph.Nodes.erase(it);
        for (VisualShaderNode& node : m_Graph.Nodes)
        {
            if (node.InputA == removedId)
                node.InputA = -1;
            if (node.InputB == removedId)
                node.InputB = -1;
        }
        m_SelectedNodeId = -1;
        PushUndoRecord("Delete " + removedName + " Node", beforeGraph, beforeSelectedNodeId, m_Graph, m_SelectedNodeId);
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
        DirectX::XMFLOAT2 screen = GraphToScreen(node.Position);
        return { node.Id, screen.x, screen.y, m_NodeWidth * m_Zoom, height * m_Zoom };
    }

    DirectX::XMFLOAT2 MaterialGraphPanel::GetSlotPosition(const VisualShaderNode& node, SlotKind slot) const
    {
        NodeRect rect = GetNodeRect(node);
        const float headerHeight = m_NodeHeaderHeight * m_Zoom;
        if (slot == SlotKind::Output)
            return { rect.X + rect.W, rect.Y + headerHeight + 26.0f * m_Zoom };
        if (slot == SlotKind::InputB)
            return { rect.X, rect.Y + headerHeight + 50.0f * m_Zoom };
        return { rect.X, rect.Y + headerHeight + 26.0f * m_Zoom };
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

    bool MaterialGraphPanel::HandleNodeMenuClick(float mouseX, float mouseY)
    {
        if (!m_IsNodeMenuOpen)
            return false;

        const int itemCount = (int)(sizeof(NodeMenuItems) / sizeof(NodeMenuItems[0]));
        for (int i = 0; i < itemCount; ++i)
        {
            const float rowY = m_NodeMenuY + 28.0f + (float)i * NodeMenuRowHeight;
            if (IsPointInRect(mouseX, mouseY, m_NodeMenuX, rowY, NodeMenuWidth, NodeMenuRowHeight))
            {
                AddNode(NodeMenuItems[i].Type);
                return true;
            }
        }

        CloseNodeMenu();
        return true;
    }

    bool MaterialGraphPanel::TryConnectNodes(int sourceNodeId, int targetNodeId, SlotKind targetSlot)
    {
        if (sourceNodeId < 0 || targetNodeId < 0 || sourceNodeId == targetNodeId)
        {
            SetStatusMessage("Cannot connect a node to itself.");
            return false;
        }

        if (targetSlot == SlotKind::None || targetSlot == SlotKind::Output)
        {
            SetStatusMessage("Connect from an output slot to an input slot.");
            return false;
        }

        VisualShaderNode* target = FindNode(targetNodeId);
        const VisualShaderNode* source = FindNode(sourceNodeId);
        if (!target || !source)
        {
            SetStatusMessage("Connection failed: node was not found.");
            return false;
        }

        if ((targetSlot == SlotKind::InputB && GetInputCount(*target) < 2) || GetInputCount(*target) < 1)
        {
            SetStatusMessage("This node has no compatible input slot.");
            return false;
        }

        if (WouldCreateCycle(sourceNodeId, targetNodeId))
        {
            SetStatusMessage("Connection blocked: graph cycle would be created.");
            return false;
        }

        const int currentInput = targetSlot == SlotKind::InputA ? target->InputA : target->InputB;
        if (currentInput == sourceNodeId)
        {
            SetStatusMessage("Connection already exists.", 1.2f);
            return true;
        }

        const VisualShaderAsset beforeGraph = m_Graph;
        const int beforeSelectedNodeId = m_SelectedNodeId;

        // 입력 슬롯은 하나의 값만 받을 수 있다.
        // 새 선을 놓으면 기존 선을 교체해서 Unity/Unreal류 그래프처럼 결과가 명확하게 유지된다.
        if (targetSlot == SlotKind::InputA)
            target->InputA = sourceNodeId;
        else if (targetSlot == SlotKind::InputB)
            target->InputB = sourceNodeId;

        SetStatusMessage("Connected " + GetNodeTitle(*source) + " -> " + GetNodeTitle(*target), 1.2f);
        PushUndoRecord("Connect Nodes", beforeGraph, beforeSelectedNodeId, m_Graph, m_SelectedNodeId);
        return true;
    }

    bool MaterialGraphPanel::WouldCreateCycle(int sourceNodeId, int targetNodeId) const
    {
        return DoesNodeReach(sourceNodeId, targetNodeId);
    }

    bool MaterialGraphPanel::DoesNodeReach(int currentNodeId, int targetNodeId) const
    {
        std::unordered_set<int> visited;
        std::function<bool(int)> visit = [&](int nodeId) -> bool
        {
            if (nodeId == targetNodeId)
                return true;
            if (!visited.insert(nodeId).second)
                return false;

            const VisualShaderNode* node = FindNode(nodeId);
            if (!node)
                return false;

            if (node->InputA >= 0 && visit(node->InputA))
                return true;
            if (node->InputB >= 0 && visit(node->InputB))
                return true;
            return false;
        };

        return visit(currentNodeId);
    }

    void MaterialGraphPanel::ClearInputConnection(int nodeId, SlotKind slot)
    {
        VisualShaderNode* node = FindNode(nodeId);
        if (!node)
            return;

        if (slot == SlotKind::InputA && node->InputA >= 0)
        {
            const VisualShaderAsset beforeGraph = m_Graph;
            const int beforeSelectedNodeId = m_SelectedNodeId;
            node->InputA = -1;
            PushUndoRecord("Disconnect Input A", beforeGraph, beforeSelectedNodeId, m_Graph, m_SelectedNodeId);
            SetStatusMessage("Input A disconnected.", 1.2f);
        }
        else if (slot == SlotKind::InputB && node->InputB >= 0)
        {
            const VisualShaderAsset beforeGraph = m_Graph;
            const int beforeSelectedNodeId = m_SelectedNodeId;
            node->InputB = -1;
            PushUndoRecord("Disconnect Input B", beforeGraph, beforeSelectedNodeId, m_Graph, m_SelectedNodeId);
            SetStatusMessage("Input B disconnected.", 1.2f);
        }
    }

    void MaterialGraphPanel::OpenNodeMenu(float mouseX, float mouseY)
    {
        const float titleHeight = GetContentPosition().y - m_CalculatedPos.y;
        const float menuH = 28.0f + (float)(sizeof(NodeMenuItems) / sizeof(NodeMenuItems[0])) * NodeMenuRowHeight;
        m_IsNodeMenuOpen = true;
        m_NodeMenuX = (std::min)(mouseX, m_CalculatedPos.x + m_CalculatedSize.x - NodeMenuWidth - 8.0f);
        m_NodeMenuY = (std::min)(mouseY, m_CalculatedPos.y + m_CalculatedSize.y - menuH - 8.0f);
        m_NodeMenuX = (std::max)(m_CalculatedPos.x + 8.0f, m_NodeMenuX);
        m_NodeMenuY = (std::max)(m_CalculatedPos.y + titleHeight + m_ToolbarHeight + 8.0f, m_NodeMenuY);
        DirectX::XMFLOAT2 graphMouse = ScreenToGraph(mouseX, mouseY);
        m_NodeSpawnX = (std::max)(8.0f, graphMouse.x - m_NodeWidth * 0.5f);
        m_NodeSpawnY = (std::max)(m_ToolbarHeight + titleHeight + 8.0f, graphMouse.y - 20.0f);
    }

    void MaterialGraphPanel::CloseNodeMenu()
    {
        m_IsNodeMenuOpen = false;
    }

    void MaterialGraphPanel::FrameAllNodes()
    {
        if (m_Graph.Nodes.empty())
            return;

        float minX = (std::numeric_limits<float>::max)();
        float minY = (std::numeric_limits<float>::max)();
        for (const VisualShaderNode& node : m_Graph.Nodes)
        {
            minX = (std::min)(minX, node.Position.x);
            minY = (std::min)(minY, node.Position.y);
        }

        const float titleHeight = GetContentPosition().y - m_CalculatedPos.y;
        const float targetX = 40.0f;
        const float targetY = titleHeight + m_ToolbarHeight + 55.0f;
        m_Zoom = 1.0f;
        m_ViewOffsetX = targetX - minX;
        m_ViewOffsetY = targetY - minY;
        // Frame은 그래프 내용을 바꾸는 명령이 아니라 보는 위치만 맞추는 명령이다.
        // 그래서 Undo 기록에 넣지 않아야 노드 편집 내역이 흐려지지 않는다.
        SetStatusMessage("Framed all nodes.", 1.2f);
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
                    case 2: UndoGraphEdit(); break;
                    case 3: RedoGraphEdit(); break;
                    case 4: AddNode(VisualShaderNodeType::Color); break;
                    case 5: AddNode(VisualShaderNodeType::Texture2D); break;
                    case 6: AddNode(VisualShaderNodeType::Multiply); break;
                    case 7: AddNode(VisualShaderNodeType::Add); break;
                    case 8: AddNode(VisualShaderNodeType::Output); break;
                    case 9: DeleteSelectedNode(); break;
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

    bool MaterialGraphPanel::AreGraphsEqual(const VisualShaderAsset& a, const VisualShaderAsset& b) const
    {
        if (a.Name != b.Name || a.Nodes.size() != b.Nodes.size())
            return false;

        auto almostEqual = [](float lhs, float rhs)
        {
            return std::abs(lhs - rhs) < 0.001f;
        };

        for (size_t i = 0; i < a.Nodes.size(); ++i)
        {
            const VisualShaderNode& lhs = a.Nodes[i];
            const VisualShaderNode& rhs = b.Nodes[i];
            if (lhs.Id != rhs.Id || lhs.Type != rhs.Type || lhs.Name != rhs.Name ||
                lhs.InputA != rhs.InputA || lhs.InputB != rhs.InputB)
                return false;

            if (!almostEqual(lhs.Position.x, rhs.Position.x) ||
                !almostEqual(lhs.Position.y, rhs.Position.y) ||
                !almostEqual(lhs.Color.x, rhs.Color.x) ||
                !almostEqual(lhs.Color.y, rhs.Color.y) ||
                !almostEqual(lhs.Color.z, rhs.Color.z) ||
                !almostEqual(lhs.Color.w, rhs.Color.w) ||
                !almostEqual(lhs.Value, rhs.Value))
                return false;
        }

        return true;
    }

    void MaterialGraphPanel::PushUndoRecord(const std::string& label, const VisualShaderAsset& before, int beforeSelectedNodeId, const VisualShaderAsset& after, int afterSelectedNodeId)
    {
        if (AreGraphsEqual(before, after))
            return;

        // 그래프 편집은 노드와 연결이 서로 맞물려 있다.
        // 부분 명령만 저장하면 삭제/연결 복구에서 빠지는 값이 생기기 쉬워서, 작은 그래프는 전체 스냅샷으로 안전하게 되돌린다.
        GraphUndoRecord record;
        record.Label = label;
        record.Before = before;
        record.After = after;
        record.BeforeSelectedNodeId = beforeSelectedNodeId;
        record.AfterSelectedNodeId = afterSelectedNodeId;
        m_UndoStack.push_back(record);
        if (m_UndoStack.size() > m_MaxGraphUndoRecords)
            m_UndoStack.erase(m_UndoStack.begin());

        m_RedoStack.clear();
        MarkDirty();
    }

    void MaterialGraphPanel::ApplyGraphState(const VisualShaderAsset& graph, int selectedNodeId)
    {
        m_Graph = graph;
        m_SelectedNodeId = FindNode(selectedNodeId) ? selectedNodeId : -1;
        m_DraggingNodeId = -1;
        m_ConnectingFromNodeId = -1;
        m_IsDraggingNode = false;
        m_IsConnecting = false;
        m_IsPanningView = false;
        m_HasDragStartGraph = false;
        CloseNodeMenu();
        Widget::EndMouseInteraction(this);
        MarkDirty();
    }

    bool MaterialGraphPanel::UndoGraphEdit()
    {
        if (m_UndoStack.empty())
        {
            SetStatusMessage("Nothing to undo.", 1.2f);
            return false;
        }

        GraphUndoRecord record = m_UndoStack.back();
        m_UndoStack.pop_back();
        m_RedoStack.push_back(record);
        ApplyGraphState(record.Before, record.BeforeSelectedNodeId);
        SetStatusMessage("Undo: " + record.Label, 1.4f);
        return true;
    }

    bool MaterialGraphPanel::RedoGraphEdit()
    {
        if (m_RedoStack.empty())
        {
            SetStatusMessage("Nothing to redo.", 1.2f);
            return false;
        }

        GraphUndoRecord record = m_RedoStack.back();
        m_RedoStack.pop_back();
        m_UndoStack.push_back(record);
        ApplyGraphState(record.After, record.AfterSelectedNodeId);
        SetStatusMessage("Redo: " + record.Label, 1.4f);
        return true;
    }

    void MaterialGraphPanel::ClearUndoHistory()
    {
        m_UndoStack.clear();
        m_RedoStack.clear();
    }

    MaterialGraphPanel::NodeRect MaterialGraphPanel::GetPropertyPanelRect() const
    {
        const float titleHeight = GetContentPosition().y - m_CalculatedPos.y;
        const float canvasY = m_CalculatedPos.y + titleHeight + m_ToolbarHeight;
        const float panelW = 248.0f;
        const float panelH = 176.0f;
        return { -1, m_CalculatedPos.x + m_CalculatedSize.x - panelW - 12.0f, canvasY + 46.0f, panelW, panelH };
    }

    DirectX::XMFLOAT2 MaterialGraphPanel::GraphToScreen(const DirectX::XMFLOAT2& graphPosition) const
    {
        return
        {
            m_CalculatedPos.x + (graphPosition.x + m_ViewOffsetX) * m_Zoom,
            m_CalculatedPos.y + (graphPosition.y + m_ViewOffsetY) * m_Zoom
        };
    }

    DirectX::XMFLOAT2 MaterialGraphPanel::ScreenToGraph(float screenX, float screenY) const
    {
        return
        {
            (screenX - m_CalculatedPos.x) / m_Zoom - m_ViewOffsetX,
            (screenY - m_CalculatedPos.y) / m_Zoom - m_ViewOffsetY
        };
    }

    void MaterialGraphPanel::DrawPropertyPanel()
    {
        const VisualShaderNode* node = FindNode(m_SelectedNodeId);
        if (!node || m_CalculatedSize.x < 520.0f)
            return;

        NodeRect panel = GetPropertyPanelRect();
        UIRenderer::DrawRectFilled(panel.X + 4.0f, panel.Y + 5.0f, panel.W, panel.H, { 0.010f, 0.012f, 0.016f, 0.78f });
        UIRenderer::DrawRectFilled(panel.X, panel.Y, panel.W, panel.H, { 0.090f, 0.095f, 0.110f, 0.96f });
        DrawBorder(panel.X, panel.Y, panel.W, panel.H, { 0.25f, 0.27f, 0.31f, 1.0f });
        UIRenderer::DrawString("Node Properties", panel.X + 12.0f, panel.Y + 20.0f, TextStrong);
        UIRenderer::DrawString(GetNodeTitle(*node), panel.X + 12.0f, panel.Y + 44.0f, TextMuted);

        auto drawButton = [](const char* label, float x, float y)
        {
            UIRenderer::DrawRectFilled(x, y, 28.0f, 22.0f, { 0.16f, 0.17f, 0.19f, 1.0f });
            DrawBorder(x, y, 28.0f, 22.0f, { 0.27f, 0.29f, 0.33f, 1.0f });
            UIRenderer::DrawString(label, x + 9.0f, y + 16.0f, TextStrong);
        };

        if (node->Type == VisualShaderNodeType::Color)
        {
            const char* labels[] = { "R", "G", "B", "A" };
            const float* values[] = { &node->Color.x, &node->Color.y, &node->Color.z, &node->Color.w };
            for (int i = 0; i < 4; ++i)
            {
                const float rowY = panel.Y + 62.0f + (float)i * 26.0f;
                UIRenderer::DrawString(labels[i], panel.X + 14.0f, rowY + 17.0f, TextMuted);
                UIRenderer::DrawString(std::to_string(*values[i]).substr(0, 4), panel.X + 44.0f, rowY + 17.0f, TextStrong);
                drawButton("-", panel.X + 112.0f, rowY);
                drawButton("+", panel.X + 146.0f, rowY);
            }
            UIRenderer::DrawRectFilled(panel.X + 188.0f, panel.Y + 76.0f, 42.0f, 42.0f, node->Color);
            DrawBorder(panel.X + 188.0f, panel.Y + 76.0f, 42.0f, 42.0f, { 0.03f, 0.03f, 0.04f, 1.0f });
        }
        else if (node->Type == VisualShaderNodeType::Lerp || node->Type == VisualShaderNodeType::Fresnel || node->Type == VisualShaderNodeType::Power)
        {
            const char* label = node->Type == VisualShaderNodeType::Lerp ? "Alpha" : "Power";
            UIRenderer::DrawString(label, panel.X + 14.0f, panel.Y + 82.0f, TextMuted);
            UIRenderer::DrawString(std::to_string(node->Value).substr(0, 4), panel.X + 82.0f, panel.Y + 82.0f, TextStrong);
            drawButton("-", panel.X + 128.0f, panel.Y + 64.0f);
            drawButton("+", panel.X + 164.0f, panel.Y + 64.0f);
        }
        else if (node->Type == VisualShaderNodeType::Texture2D)
        {
            UIRenderer::DrawString("Texture Slot", panel.X + 14.0f, panel.Y + 78.0f, TextMuted);
            UIRenderer::DrawString("Material AlbedoTexture", panel.X + 14.0f, panel.Y + 106.0f, TextStrong);
        }
        else
        {
            UIRenderer::DrawString("No editable values.", panel.X + 14.0f, panel.Y + 78.0f, TextMuted);
        }
    }

    bool MaterialGraphPanel::HandlePropertyPanelClick(float mouseX, float mouseY)
    {
        VisualShaderNode* node = FindNode(m_SelectedNodeId);
        if (!node || m_CalculatedSize.x < 520.0f)
            return false;

        NodeRect panel = GetPropertyPanelRect();
        if (!IsPointInRect(mouseX, mouseY, panel.X, panel.Y, panel.W, panel.H))
            return false;

        auto buttonHit = [&](float x, float y)
        {
            return IsPointInRect(mouseX, mouseY, x, y, 28.0f, 22.0f);
        };

        const VisualShaderAsset beforeGraph = m_Graph;
        const int beforeSelectedNodeId = m_SelectedNodeId;
        bool changed = false;

        if (node->Type == VisualShaderNodeType::Color)
        {
            float* channels[] = { &node->Color.x, &node->Color.y, &node->Color.z, &node->Color.w };
            for (int i = 0; i < 4; ++i)
            {
                const float rowY = panel.Y + 62.0f + (float)i * 26.0f;
                if (buttonHit(panel.X + 112.0f, rowY))
                {
                    *channels[i] = (std::clamp)(*channels[i] - 0.05f, 0.0f, 1.0f);
                    changed = true;
                }
                else if (buttonHit(panel.X + 146.0f, rowY))
                {
                    *channels[i] = (std::clamp)(*channels[i] + 0.05f, 0.0f, 1.0f);
                    changed = true;
                }
            }
        }
        else if (node->Type == VisualShaderNodeType::Lerp || node->Type == VisualShaderNodeType::Fresnel || node->Type == VisualShaderNodeType::Power)
        {
            const float step = node->Type == VisualShaderNodeType::Lerp ? 0.05f : 0.25f;
            const float minValue = node->Type == VisualShaderNodeType::Lerp ? 0.0f : 0.1f;
            const float maxValue = node->Type == VisualShaderNodeType::Lerp ? 1.0f : 12.0f;
            if (buttonHit(panel.X + 128.0f, panel.Y + 64.0f))
            {
                node->Value = (std::clamp)(node->Value - step, minValue, maxValue);
                changed = true;
            }
            else if (buttonHit(panel.X + 164.0f, panel.Y + 64.0f))
            {
                node->Value = (std::clamp)(node->Value + step, minValue, maxValue);
                changed = true;
            }
        }

        if (changed)
        {
            // 속성 패널의 작은 버튼도 그래프 편집이다.
            // 누락하면 저장 전 Ctrl+Z/Redo 흐름에서 노드 값만 되돌아가지 않는 문제가 생긴다.
            PushUndoRecord("Edit " + GetNodeTitle(*node) + " Property", beforeGraph, beforeSelectedNodeId, m_Graph, m_SelectedNodeId);
        }

        return true;
    }

    void MaterialGraphPanel::SetStatusMessage(const std::string& message, float seconds)
    {
        m_StatusMessage = message;
        m_StatusTimer = seconds;
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
            case VisualShaderNodeType::Lerp:
                return 2;
            case VisualShaderNodeType::OneMinus:
            case VisualShaderNodeType::Power:
            case VisualShaderNodeType::Saturate:
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
