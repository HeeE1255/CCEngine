#pragma once
#include "UI/Panel.h"
#include <DirectXMath.h>
#include <string>

namespace CCEngine {
    namespace UI {

        class InspectorItem : public Panel
        {
        public:
            InspectorItem(const std::string& name, const std::string& title);
            virtual void OnRender() override;

            virtual void UpdateLayout(const DirectX::XMFLOAT2& parentPos, const DirectX::XMFLOAT2& parentSize) override;

        private:
            std::string m_Title;
            DirectX::XMFLOAT4 m_Color = { 0.18f, 0.18f, 0.18f, 1.0f };
        };

    }
}