#pragma once

#include "Application.h"
#include "Events/MouseEvent.h"
#include "Renderer/UIRenderer.h"
#include "UI/Widget.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>

namespace CCEngine::UI
{
class AnimatorStateInspectorRow : public Widget
{
public:
    enum class Kind { Field, Toggle, Section, Action, Stepper };

    AnimatorStateInspectorRow(const std::string& name, std::string label, std::string value, Kind kind)
        : Widget(name), m_Label(std::move(label)), m_Value(std::move(value)), m_Kind(kind)
    {
    }

    void SetChecked(bool checked) { m_Checked = checked; }
    void SetWarning(bool warning) { m_Warning = warning; }
    void SetSelected(bool selected) { m_Selected = selected; }
    void SetOnClick(std::function<void()> callback) { m_OnClick = std::move(callback); }
    void SetOnIconClick(std::function<void()> callback) { m_OnIconClick = std::move(callback); }
    void SetOnMinus(std::function<void()> callback) { m_OnMinus = std::move(callback); }
    void SetOnPlus(std::function<void()> callback) { m_OnPlus = std::move(callback); }

    void OnRender() override
    {
        if (!m_IsVisible)
            return;

        const float x = m_CalculatedPos.x;
        const float y = m_CalculatedPos.y;
        const float w = m_CalculatedSize.x;
        const float h = m_CalculatedSize.y;
        auto [mouseX, mouseY] = Application::Get()->GetWindow().GetMousePosition();
        const bool hovered = IsInteractive() &&
            IsPointInside(mouseX, mouseY) &&
            !IsMouseBlockedByWidgetAbove(mouseX, mouseY);

        if (m_Kind == Kind::Section)
        {
            UIRenderer::DrawRectFilled(x, y + 2.0f, w, h - 4.0f, { 0.115f, 0.118f, 0.125f, 1.0f });
            UIRenderer::DrawString(m_Label, x + 8.0f, y + h * 0.5f + 7.0f, { 0.84f, 0.86f, 0.88f, 1.0f });
            return;
        }

        const float labelW = (std::min)(145.0f, w * 0.40f);
        const float fieldX = x + labelW;
        const float fieldW = (std::max)(24.0f, w - labelW);
        UIRenderer::DrawString(m_Label, x + 4.0f, y + h * 0.5f + 7.0f, { 0.72f, 0.72f, 0.74f, 1.0f });

        if (m_Kind == Kind::Toggle)
        {
            if (hovered || m_IsPressed)
                UIRenderer::DrawRectFilled(fieldX, y + 1.0f, fieldW - 4.0f, h - 2.0f, m_IsPressed ? DirectX::XMFLOAT4{ 0.16f, 0.19f, 0.23f, 1.0f } : DirectX::XMFLOAT4{ 0.14f, 0.15f, 0.17f, 1.0f });
            UIRenderer::DrawRectFilled(fieldX + 4.0f, y + 4.0f, 15.0f, 15.0f, { 0.10f, 0.10f, 0.105f, 1.0f });
            UIRenderer::DrawRect({ fieldX + 4.0f, y + 4.0f }, { 15.0f, 15.0f },
                m_Checked ? DirectX::XMFLOAT4{ 0.58f, 0.78f, 0.98f, 1.0f } : DirectX::XMFLOAT4{ 0.30f, 0.30f, 0.32f, 1.0f });
            if (m_Checked)
                UIRenderer::DrawString("v", fieldX + 8.0f, y + 18.0f, { 0.88f, 0.92f, 0.96f, 1.0f });
            return;
        }

        DirectX::XMFLOAT4 fill = m_IsPressed
            ? DirectX::XMFLOAT4{ 0.16f, 0.19f, 0.23f, 1.0f }
            : (hovered ? DirectX::XMFLOAT4{ 0.145f, 0.155f, 0.17f, 1.0f } : DirectX::XMFLOAT4{ 0.12f, 0.12f, 0.125f, 1.0f });
        if (m_Warning)
            fill = hovered ? DirectX::XMFLOAT4{ 0.28f, 0.19f, 0.09f, 1.0f } : DirectX::XMFLOAT4{ 0.22f, 0.16f, 0.08f, 1.0f };
        if (m_Kind == Kind::Action)
            fill = m_IsPressed ? DirectX::XMFLOAT4{ 0.12f, 0.20f, 0.30f, 1.0f } : (hovered ? DirectX::XMFLOAT4{ 0.22f, 0.30f, 0.40f, 1.0f } : DirectX::XMFLOAT4{ 0.18f, 0.24f, 0.32f, 1.0f });
        if (m_Selected)
            fill = hovered ? DirectX::XMFLOAT4{ 0.20f, 0.38f, 0.58f, 1.0f } : DirectX::XMFLOAT4{ 0.15f, 0.30f, 0.48f, 1.0f };

        UIRenderer::DrawRectFilled(fieldX, y + 1.0f, fieldW - 4.0f, h - 2.0f, fill);
        const DirectX::XMFLOAT4 border = m_Selected
            ? DirectX::XMFLOAT4{ 0.42f, 0.70f, 0.95f, 1.0f }
            : DirectX::XMFLOAT4{ 0.25f, 0.25f, 0.27f, 1.0f };
        UIRenderer::DrawRect({ fieldX, y + 1.0f }, { fieldW - 4.0f, h - 2.0f }, border);

        float textW = fieldW - 16.0f;
        if (m_Kind == Kind::Field)
            textW -= 24.0f;
        if (m_Kind == Kind::Stepper)
            textW -= 56.0f;
        UIRenderer::DrawString(Fit(m_Value, textW), fieldX + 8.0f, y + h * 0.5f + 7.0f, { 0.84f, 0.84f, 0.86f, 1.0f });

        if (m_Kind == Kind::Field && (m_OnClick || m_OnIconClick))
        {
            UIRenderer::DrawString("o", fieldX + fieldW - 22.0f, y + h * 0.5f + 7.0f, { 0.60f, 0.60f, 0.62f, 1.0f });
        }
        else if (m_Kind == Kind::Stepper)
        {
            const float minusX = fieldX + fieldW - 58.0f;
            const float plusX = fieldX + fieldW - 31.0f;
            UIRenderer::DrawRectFilled(minusX, y + 3.0f, 23.0f, h - 6.0f, { 0.10f, 0.105f, 0.115f, 1.0f });
            UIRenderer::DrawRectFilled(plusX, y + 3.0f, 23.0f, h - 6.0f, { 0.10f, 0.105f, 0.115f, 1.0f });
            UIRenderer::DrawString("-", minusX + 8.0f, y + h * 0.5f + 7.0f, { 0.86f, 0.86f, 0.88f, 1.0f });
            UIRenderer::DrawString("+", plusX + 7.0f, y + h * 0.5f + 7.0f, { 0.86f, 0.86f, 0.88f, 1.0f });
        }
    }

protected:
    bool OnMouseButtonPressed(MouseButtonPressedEvent& e) override
    {
        if (e.GetButton() != 0 || !IsInteractive() || !IsPointInside(e.GetX(), e.GetY()))
            return false;
        if (m_Kind == Kind::Stepper && !IsPointInsideStepperButton(e.GetX(), e.GetY()))
            return false;
        m_IsPressed = true;
        // Press와 Release가 다른 패널로 갈라지면 보이는 버튼과 실제 실행 대상이 달라진다.
        // 한 행이 클릭 수명 전체를 소유하게 해 보조 Inspector 창에서도 입력 좌표를 안정화한다.
        Widget::BeginMouseInteraction(this);
        e.Handled = true;
        return true;
    }

    bool OnMouseButtonReleased(MouseButtonReleasedEvent& e) override
    {
        if (e.GetButton() != 0)
            return false;
        const bool fire = m_IsPressed && IsPointInside(e.GetX(), e.GetY());
        m_IsPressed = false;
        Widget::EndMouseInteraction(this);
        if (!fire)
        {
            e.Handled = true;
            return true;
        }

        if (m_Kind == Kind::Stepper)
        {
            auto [minusX, plusX, buttonY, buttonH] = GetStepperButtonRects();
            if (e.GetX() >= minusX && e.GetX() <= minusX + 23.0f &&
                e.GetY() >= buttonY && e.GetY() <= buttonY + buttonH)
            {
                if (m_OnMinus) m_OnMinus();
                e.Handled = true;
                return true;
            }
            if (e.GetX() >= plusX && e.GetX() <= plusX + 23.0f &&
                e.GetY() >= buttonY && e.GetY() <= buttonY + buttonH)
            {
                if (m_OnPlus) m_OnPlus();
                e.Handled = true;
                return true;
            }
        }
        else if (m_Kind == Kind::Field && m_OnIconClick && IsPointInsideFieldIcon(e.GetX(), e.GetY()))
        {
            m_OnIconClick();
            e.Handled = true;
            return true;
        }

        if (m_OnClick)
            m_OnClick();
        e.Handled = true;
        return true;
    }

private:
    bool IsInteractive() const
    {
        if (m_Kind == Kind::Section)
            return false;
        if (m_Kind == Kind::Stepper)
            return (bool)m_OnMinus || (bool)m_OnPlus;
        if (m_Kind == Kind::Toggle || m_Kind == Kind::Action)
            return (bool)m_OnClick;
        return (bool)m_OnClick || (bool)m_OnIconClick;
    }

    std::tuple<float, float, float, float> GetStepperButtonRects() const
    {
        const float w = m_CalculatedSize.x;
        const float labelW = (std::min)(145.0f, w * 0.40f);
        const float fieldX = m_CalculatedPos.x + labelW;
        const float fieldW = (std::max)(24.0f, w - labelW);
        return { fieldX + fieldW - 58.0f, fieldX + fieldW - 31.0f, m_CalculatedPos.y + 3.0f, m_CalculatedSize.y - 6.0f };
    }

    bool IsPointInsideStepperButton(float mouseX, float mouseY) const
    {
        auto [minusX, plusX, buttonY, buttonH] = GetStepperButtonRects();
        const bool inMinus = mouseX >= minusX && mouseX <= minusX + 23.0f && mouseY >= buttonY && mouseY <= buttonY + buttonH;
        const bool inPlus = mouseX >= plusX && mouseX <= plusX + 23.0f && mouseY >= buttonY && mouseY <= buttonY + buttonH;
        return inMinus || inPlus;
    }

    std::tuple<float, float, float, float> GetFieldIconRect() const
    {
        const float w = m_CalculatedSize.x;
        const float labelW = (std::min)(145.0f, w * 0.40f);
        const float fieldX = m_CalculatedPos.x + labelW;
        const float fieldW = (std::max)(24.0f, w - labelW);
        return { fieldX + fieldW - 28.0f, m_CalculatedPos.y + 3.0f, 24.0f, m_CalculatedSize.y - 6.0f };
    }

    bool IsPointInsideFieldIcon(float mouseX, float mouseY) const
    {
        auto [iconX, iconY, iconW, iconH] = GetFieldIconRect();
        return mouseX >= iconX && mouseX <= iconX + iconW && mouseY >= iconY && mouseY <= iconY + iconH;
    }

    static std::string Fit(const std::string& text, float availableWidth)
    {
        const int maxChars = (std::max)(0, (int)(availableWidth / 8.0f));
        if ((int)text.size() <= maxChars)
            return text;
        if (maxChars <= 3)
            return text.substr(0, (size_t)(std::max)(0, maxChars));
        return text.substr(0, (size_t)maxChars - 3) + "...";
    }

    std::string m_Label;
    std::string m_Value;
    Kind m_Kind = Kind::Field;
    bool m_Checked = false;
    bool m_Warning = false;
    bool m_Selected = false;
    bool m_IsPressed = false;
    std::function<void()> m_OnClick;
    std::function<void()> m_OnIconClick;
    std::function<void()> m_OnMinus;
    std::function<void()> m_OnPlus;
};

class AnimatorTransitionBlendPreview : public Widget
{
public:
    AnimatorTransitionBlendPreview(
        const std::string& name,
        std::string sourceName,
        std::string targetName,
        bool hasExitTime,
        float exitTime,
        float blendTime)
        : Widget(name),
        m_SourceName(std::move(sourceName)),
        m_TargetName(std::move(targetName)),
        m_HasExitTime(hasExitTime),
        m_ExitTime(exitTime),
        m_BlendTime(blendTime)
    {
    }

    void OnRender() override
    {
        if (!m_IsVisible)
            return;

        const float x = m_CalculatedPos.x;
        const float y = m_CalculatedPos.y;
        const float w = m_CalculatedSize.x;
        const float h = m_CalculatedSize.y;
        const float innerX = x + 10.0f;
        const float innerY = y + 8.0f;
        const float innerW = (std::max)(80.0f, w - 20.0f);

        UIRenderer::DrawRectFilled(x, y, w, h, { 0.060f, 0.064f, 0.072f, 1.0f });
        UIRenderer::DrawRect({ x, y }, { w, h }, { 0.22f, 0.24f, 0.28f, 1.0f });
        UIRenderer::DrawString("Blend Preview", innerX, innerY + 15.0f, { 0.84f, 0.86f, 0.90f, 1.0f });

        const float graphX = innerX + 14.0f;
        const float graphY = y + 34.0f;
        const float graphW = (std::max)(60.0f, innerW - 14.0f);
        const float graphH = 44.0f;
        const float topY = graphY + 6.0f;
        const float bottomY = graphY + graphH - 6.0f;
        const float midY = graphY + graphH * 0.5f;

        const float exitN = m_HasExitTime ? std::clamp(m_ExitTime, 0.0f, 1.0f) : 0.0f;
        const float visualBlend = std::clamp(m_BlendTime, 0.02f, 1.0f);
        float blendStart = m_HasExitTime ? exitN : 0.0f;
        float blendEnd = blendStart + visualBlend;
        if (blendEnd > 1.0f)
        {
            blendEnd = 1.0f;
            blendStart = (std::max)(0.0f, blendEnd - visualBlend);
        }

        const float blendStartX = graphX + blendStart * graphW;
        const float blendEndX = graphX + blendEnd * graphW;
        const float exitX = graphX + exitN * graphW;

        UIRenderer::DrawRectFilled(graphX, graphY, graphW, graphH, { 0.035f, 0.038f, 0.045f, 1.0f });
        UIRenderer::DrawRectFilled(blendStartX, graphY, (std::max)(2.0f, blendEndX - blendStartX), graphH, { 0.18f, 0.28f, 0.42f, 0.45f });
        UIRenderer::DrawRect({ graphX, graphY }, { graphW, graphH }, { 0.18f, 0.19f, 0.21f, 1.0f });
        DrawTimelineTicks(graphX, graphY, graphW, graphH);
        DrawPreviewLine({ graphX, midY }, { graphX + graphW, midY }, { 0.18f, 0.19f, 0.21f, 1.0f }, 1.0f);
        DrawPreviewLine({ exitX, graphY }, { exitX, graphY + graphH }, { 0.78f, 0.48f, 0.16f, 1.0f }, 1.5f);

        // 선형 블렌드는 전환 구간에서 기존 상태 가중치가 내려가고, 다음 상태 가중치가 올라간다.
        // 이 그래프는 실제 포즈 섞임을 눈으로 확인하기 위한 UI라서 Source/Target 두 선을 동시에 그린다.
        DrawPreviewLine({ graphX, topY }, { blendStartX, topY }, { 0.32f, 0.54f, 0.82f, 1.0f }, 2.0f);
        DrawPreviewLine({ blendStartX, topY }, { blendEndX, bottomY }, { 0.32f, 0.54f, 0.82f, 1.0f }, 2.0f);
        DrawPreviewLine({ blendEndX, bottomY }, { graphX + graphW, bottomY }, { 0.32f, 0.54f, 0.82f, 1.0f }, 2.0f);
        DrawPreviewLine({ graphX, bottomY }, { blendStartX, bottomY }, { 0.28f, 0.68f, 0.40f, 1.0f }, 2.0f);
        DrawPreviewLine({ blendStartX, bottomY }, { blendEndX, topY }, { 0.28f, 0.68f, 0.40f, 1.0f }, 2.0f);
        DrawPreviewLine({ blendEndX, topY }, { graphX + graphW, topY }, { 0.28f, 0.68f, 0.40f, 1.0f }, 2.0f);

        UIRenderer::DrawString("1", graphX - 6.0f, topY + 4.0f, { 0.55f, 0.57f, 0.62f, 1.0f });
        UIRenderer::DrawString("0", graphX - 6.0f, bottomY + 4.0f, { 0.55f, 0.57f, 0.62f, 1.0f });
        UIRenderer::DrawString("Exit", (std::min)(exitX + 4.0f, graphX + graphW - 38.0f), graphY + 14.0f, { 0.78f, 0.48f, 0.16f, 1.0f });
        UIRenderer::DrawString("Blend", (std::min)(blendStartX + 4.0f, graphX + graphW - 48.0f), graphY + graphH - 3.0f, { 0.70f, 0.74f, 0.80f, 1.0f });

        const float barY = y + h - 28.0f;
        UIRenderer::DrawRectFilled(innerX, barY, innerW, 8.0f, { 0.10f, 0.105f, 0.115f, 1.0f });
        UIRenderer::DrawRectFilled(innerX, barY, (std::max)(2.0f, blendEndX - innerX), 8.0f, { 0.32f, 0.54f, 0.82f, 0.85f });
        UIRenderer::DrawRectFilled(blendStartX, barY + 12.0f, (std::max)(2.0f, innerX + innerW - blendStartX), 8.0f, { 0.28f, 0.68f, 0.40f, 0.85f });

        UIRenderer::DrawString(FitLabel("Source: " + m_SourceName, innerW * 0.48f), innerX, barY + 26.0f, { 0.62f, 0.72f, 0.90f, 1.0f });
        UIRenderer::DrawString(FitLabel("Target: " + m_TargetName, innerW * 0.48f), innerX + innerW * 0.52f, barY + 26.0f, { 0.58f, 0.82f, 0.62f, 1.0f });
    }

private:
    static void DrawPreviewLine(DirectX::XMFLOAT2 a, DirectX::XMFLOAT2 b, const DirectX::XMFLOAT4& color, float thickness)
    {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const int steps = (std::max)(1, (int)(std::sqrt(dx * dx + dy * dy) / 3.0f));
        for (int i = 0; i <= steps; ++i)
        {
            const float t = (float)i / (float)steps;
            const float px = a.x + dx * t;
            const float py = a.y + dy * t;
            UIRenderer::DrawRectFilled(px - thickness * 0.5f, py - thickness * 0.5f, thickness, thickness, color);
        }
    }

    static float PickTickStep(float graphWidth)
    {
        const float desiredPixels = 72.0f;
        const float desiredTickCount = std::clamp(graphWidth / desiredPixels, 2.0f, 10.0f);
        const float rawStep = 1.0f / desiredTickCount;
        const float candidates[] = { 0.5f, 0.25f, 0.2f, 0.1f, 0.05f };
        for (float candidate : candidates)
        {
            if (candidate <= rawStep)
                return candidate;
        }
        return 0.05f;
    }

    static std::string FormatTickLabel(float value)
    {
        std::ostringstream stream;
        const bool whole = std::abs(value - std::round(value)) < 0.001f;
        stream << std::fixed << std::setprecision(whole ? 0 : 2) << value;
        std::string text = stream.str();
        while (text.size() > 1 && text.back() == '0')
            text.pop_back();
        if (!text.empty() && text.back() == '.')
            text.pop_back();
        return text;
    }

    static void DrawTimelineTicks(float graphX, float graphY, float graphW, float graphH)
    {
        const float step = PickTickStep(graphW);
        const int tickCount = (int)std::round(1.0f / step);
        for (int i = 0; i <= tickCount; ++i)
        {
            const float normalizedTime = std::clamp(i * step, 0.0f, 1.0f);
            const float tickX = graphX + normalizedTime * graphW;
            const bool major = i == 0 || i == tickCount || std::abs(std::fmod(normalizedTime, 0.5f)) < 0.001f;
            const float tickH = major ? graphH : graphH * 0.42f;
            const DirectX::XMFLOAT4 tickColor = major
                ? DirectX::XMFLOAT4{ 0.24f, 0.25f, 0.28f, 1.0f }
                : DirectX::XMFLOAT4{ 0.16f, 0.17f, 0.19f, 1.0f };

            // 인스팩터 폭이 넓어지면 더 촘촘한 눈금을 보여준다.
            // 좁을 때는 자동으로 큰 단위만 남겨 숫자끼리 겹치지 않게 한다.
            DrawPreviewLine({ tickX, graphY }, { tickX, graphY + tickH }, tickColor, 1.0f);
            UIRenderer::DrawString(FormatTickLabel(normalizedTime), tickX - 7.0f, graphY + graphH + 14.0f, { 0.50f, 0.52f, 0.57f, 1.0f });
        }
    }

    static std::string FitLabel(const std::string& text, float width)
    {
        const int maxChars = (std::max)(0, (int)(width / 8.0f));
        if ((int)text.size() <= maxChars)
            return text;
        if (maxChars <= 3)
            return text.substr(0, (size_t)(std::max)(0, maxChars));
        return text.substr(0, (size_t)maxChars - 3) + "...";
    }

    std::string m_SourceName;
    std::string m_TargetName;
    bool m_HasExitTime = false;
    float m_ExitTime = 0.0f;
    float m_BlendTime = 0.0f;
};

}
