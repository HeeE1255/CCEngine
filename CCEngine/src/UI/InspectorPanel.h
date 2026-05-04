#pragma once
#include "Core.h"
#include "UI/WindowPanel.h"
#include "UI/Widget.h"
#include "Scene/Entity.h"


namespace CCEngine {
    namespace UI {

        class InspectorPanel : public WindowPanel 
        {
        public:
            InspectorPanel(const std::string& name, const std::string& title);

            // 외부(하이어라키 등)에서 선택된 엔티티를 넘겨줄 세터
            void SetSelectedEntity(Entity entity) { m_SelectedEntity = entity; }
            Entity GetSelectedEntity() const { return m_SelectedEntity; }

            virtual void OnRender() override;

        private:
            Entity m_SelectedEntity; // 현재 선택된 엔티티 보관
        };

    }
}