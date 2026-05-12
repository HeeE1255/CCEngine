#pragma once
#include "Core.h"
#include "UI/WindowPanel.h"
#include "UI/Widget.h"
#include "Scene/Entity.h"


namespace CCEngine 
{
    namespace UI 
    {

        class CC_API InspectorPanel : public WindowPanel
        {
        public:
            InspectorPanel(const std::string& name, const std::string& title);

            // 외부(하이어라키 등)에서 선택된 엔티티를 세팅
            void SetSelectedEntity(Entity entity);
            Entity GetSelectedEntity() const { return m_SelectedEntity; }

            virtual void OnRender() override;
            virtual void UpdateLayout(const DirectX::XMFLOAT2& parentPos, const DirectX::XMFLOAT2& parentSize) override;

        private:
            Entity m_SelectedEntity;
            ScrollState m_ScrollState;
        };

    }
}