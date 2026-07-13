#pragma once
#include "Core.h"
#include "UI/WindowPanel.h"
#include "UI/Widget.h"
#include "Scene/Entity.h"
#include <vector>
#include <functional>

namespace CCEngine::UI { class Button; class Panel; class TextInput; }

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
            void RequestRebuild() { m_NeedsRebuild = true; }
            void SetSceneStructureChangeCallbacks(
                std::function<void(const std::string&)> beginChange,
                std::function<void()> commitChange)
            {
                m_BeginStructureChange = std::move(beginChange);
                m_CommitStructureChange = std::move(commitChange);
            }
            void BeginStructureChange(const std::string& label);
            void CommitStructureChange();
            bool IsAlbedoTextureSlotPoint(float mouseX, float mouseY) const;

            virtual void OnRender() override;
            virtual void OnUpdate(float deltaTime) override;
            virtual void UpdateLayout(const DirectX::XMFLOAT2& parentPos, const DirectX::XMFLOAT2& parentSize) override;

        private:
            enum class AddComponentType { Mesh, Light, Camera, SpriteRenderer, Rigidbody2D, BoxCollider2D, Script };
            void RebuildInspector();
            void BuildAddComponentMenu();
            void AddComponent(AddComponentType type);
            bool CreateAndAttachScript();
            void AttachExistingScript(const std::string& className);
            std::vector<std::string> DiscoverScriptClasses() const;
            void FilterAddComponentMenu(const std::string& query);
            void ChangeComponentPage(int direction);

            Entity m_SelectedEntity;
            ScrollState m_ScrollState;
            Button* m_AddComponentButton = nullptr;
            Panel* m_AddComponentMenu = nullptr;
            TextInput* m_ComponentSearchInput = nullptr;
            std::vector<std::pair<Button*, std::string>> m_ComponentButtons;
            Button* m_PreviousComponentPage = nullptr;
            Button* m_ComponentPageLabel = nullptr;
            Button* m_NextComponentPage = nullptr;
            std::string m_ComponentFilter;
            size_t m_ComponentPage = 0;
            static constexpr size_t ComponentPageSize = 10;
            std::function<void(const std::string&)> m_BeginStructureChange;
            std::function<void()> m_CommitStructureChange;
            bool m_NeedsRebuild = false;
        };

    }
}
