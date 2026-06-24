#pragma once

#include "UI/WindowPanel.h"

namespace CCEngine::UI
{
    class CC_API ConsolePanel : public WindowPanel
    {
    public:
        ConsolePanel(const std::string& name = "ConsolePanel");

        virtual void UpdateLayout(const DirectX::XMFLOAT2& parentPos, const DirectX::XMFLOAT2& parentSize) override;
        virtual void OnRender() override;
        virtual bool OnEvent(Event& e) override;

    private:
        ScrollState m_ScrollState;
        float m_ContentTop = 28.0f;
        float m_RowHeight = 22.0f;
    };
}
