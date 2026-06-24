#include "InspectorPanel.h"
#include "InspectorRegistry.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/UIRenderer.h"
#include "Renderer/Renderer2D.h"
#include "Application.h"
#include "UI/Button.h"
#include "UI/Panel.h"
#include "UI/TextInput.h"
#include "Scene/Components.h"
#include "Renderer/MeshFactory.h"
#include <algorithm>
#include <cctype>
#include <vector>

namespace CCEngine
{
    namespace UI
    {

        InspectorPanel::InspectorPanel(const std::string& name, const std::string& title)
            : WindowPanel(name, title)
        {
            SetClipToBounds(true);
        }


        void InspectorPanel::SetSelectedEntity(Entity entity)
        {
            if (m_SelectedEntity == entity) return;

            m_SelectedEntity = entity;
            RebuildInspector();
        }

        void InspectorPanel::RebuildInspector()
        {
            ClearChildren();
            m_AddComponentButton = nullptr;
            m_AddComponentMenu = nullptr;
            m_ComponentSearchInput = nullptr;
            m_ComponentButtons.clear();

            if (!m_SelectedEntity) return;

            InspectorRegistry::DrawAllComponents(this, m_SelectedEntity);

            m_AddComponentButton = new UI::Button("BtnAddComponent", "Add Component");
            m_AddComponentButton->SetOnClick([this]()
                {
                    if (!m_AddComponentMenu) return;
                    m_AddComponentMenu->SetVisible(!m_AddComponentMenu->IsVisible());
                    if (m_AddComponentMenu->IsVisible())
                    {
                        m_ComponentSearchInput->Clear();
                    }
                });
            AddChild(m_AddComponentButton);
            BuildAddComponentMenu();

            auto& window = CCEngine::Application::Get()->GetWindow();
            UpdateLayout({ 0.0f, 0.0f }, { (float)window.GetWidth(), (float)window.GetHeight() });
        }

        void InspectorPanel::BuildAddComponentMenu()
        {
            m_AddComponentMenu = new UI::Panel("AddComponentMenu", { 0.14f, 0.14f, 0.15f, 1.0f });
            m_AddComponentMenu->SetVisible(false);
            m_AddComponentMenu->SetAnchorMin(0.0f, 0.0f);
            m_AddComponentMenu->SetAnchorMax(0.0f, 0.0f);
            AddChild(m_AddComponentMenu);

            m_ComponentSearchInput = new UI::TextInput("ComponentSearch", "Search components...");
            m_ComponentSearchInput->SetAnchorMin(0.0f, 0.0f);
            m_ComponentSearchInput->SetAnchorMax(1.0f, 0.0f);
            m_ComponentSearchInput->SetOffsetMin(6.0f, 6.0f);
            m_ComponentSearchInput->SetOffsetMax(-6.0f, 32.0f);
            m_ComponentSearchInput->SetOnTextChanged([this](const std::string& query) { FilterAddComponentMenu(query); });
            m_AddComponentMenu->AddChild(m_ComponentSearchInput);

            auto addCandidate = [this](const std::string& name, AddComponentType type, bool alreadyExists)
                {
                    if (alreadyExists) return;
                    auto button = new UI::Button("Add" + name, name);
                    button->SetOnClick([this, type]() { AddComponent(type); });
                    m_AddComponentMenu->AddChild(button);
                    m_ComponentButtons.emplace_back(button, name);
                };

            addCandidate("Mesh Renderer", AddComponentType::Mesh, m_SelectedEntity.HasComponent<MeshComponent>());
            addCandidate("Light", AddComponentType::Light, m_SelectedEntity.HasComponent<LightComponent>());
            addCandidate("Camera", AddComponentType::Camera, m_SelectedEntity.HasComponent<CameraComponent>());
            addCandidate("Sprite Renderer", AddComponentType::SpriteRenderer, m_SelectedEntity.HasComponent<SpriteRendererComponent>());
            addCandidate("Rigidbody 2D", AddComponentType::Rigidbody2D, m_SelectedEntity.HasComponent<Rigidbody2DComponent>());
            addCandidate("Box Collider 2D", AddComponentType::BoxCollider2D, m_SelectedEntity.HasComponent<BoxCollider2DComponent>());
            FilterAddComponentMenu("");
        }

        void InspectorPanel::AddComponent(AddComponentType type)
        {
            if (!m_SelectedEntity) return;

            auto componentName = [](AddComponentType componentType)
            {
                switch (componentType)
                {
                    case AddComponentType::Mesh: return "Mesh Renderer";
                    case AddComponentType::Light: return "Light";
                    case AddComponentType::Camera: return "Camera";
                    case AddComponentType::SpriteRenderer: return "Sprite Renderer";
                    case AddComponentType::Rigidbody2D: return "Rigidbody 2D";
                    case AddComponentType::BoxCollider2D: return "Box Collider 2D";
                    default: return "Component";
                }
            };

            // 컴포넌트 추가도 Undo/Redo 대상이라 변경 전후를 에디터 레이어에 알려준다.
            BeginStructureChange(std::string("Add ") + componentName(type));
            switch (type)
            {
                case AddComponentType::Mesh:
                {
                    auto& mesh = m_SelectedEntity.AddComponent<MeshComponent>(MeshComponent::MeshType::Cube);
                    mesh.MeshData = MeshFactory::CreateCube();
                    break;
                }
                case AddComponentType::Light: m_SelectedEntity.AddComponent<LightComponent>(); break;
                case AddComponentType::Camera:
                {
                    auto& camera = m_SelectedEntity.AddComponent<CameraComponent>();
                    auto view = m_SelectedEntity.GetScene()->GetRegistry().view<CameraComponent>();
                    bool hasOtherPrimary = false;
                    for (auto entity : view)
                    {
                        if (entity != (entt::entity)m_SelectedEntity && view.get<CameraComponent>(entity).Primary)
                        {
                            hasOtherPrimary = true;
                            break;
                        }
                    }
                    camera.Primary = !hasOtherPrimary;
                    break;
                }
                case AddComponentType::SpriteRenderer: m_SelectedEntity.AddComponent<SpriteRendererComponent>(); break;
                case AddComponentType::Rigidbody2D: m_SelectedEntity.AddComponent<Rigidbody2DComponent>(); break;
                case AddComponentType::BoxCollider2D: m_SelectedEntity.AddComponent<BoxCollider2DComponent>(); break;
            }

            m_AddComponentMenu->SetVisible(false);
            CommitStructureChange();
            RequestRebuild();
        }

        void InspectorPanel::BeginStructureChange(const std::string& label)
        {
            // 인스펙터는 Undo 스택을 직접 들고 있지 않고, 변경 시작만 에디터 레이어에 전달한다.
            if (m_BeginStructureChange)
                m_BeginStructureChange(label);
        }

        void InspectorPanel::CommitStructureChange()
        {
            // 실제 컴포넌트 변경이 끝난 뒤 호출해야 Before/After 스냅샷이 나뉜다.
            if (m_CommitStructureChange)
                m_CommitStructureChange();
        }

        void InspectorPanel::FilterAddComponentMenu(const std::string& query)
        {
            std::string lowerQuery = query;
            std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(),
                [](unsigned char c) { return (char)std::tolower(c); });

            float currentY = 38.0f;
            for (auto& [button, name] : m_ComponentButtons)
            {
                std::string lowerName = name;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                    [](unsigned char c) { return (char)std::tolower(c); });
                bool visible = lowerQuery.empty() || lowerName.find(lowerQuery) != std::string::npos;
                button->SetVisible(visible);
                if (!visible) continue;
                button->SetAnchorMin(0.0f, 0.0f);
                button->SetAnchorMax(1.0f, 0.0f);
                button->SetOffsetMin(6.0f, currentY);
                button->SetOffsetMax(-6.0f, currentY + 26.0f);
                currentY += 28.0f;
            }
            if (m_AddComponentMenu)
                m_AddComponentMenu->SetSize(m_AddComponentMenu->GetSize().x, currentY + 6.0f);
        }

        void InspectorPanel::OnUpdate(float deltaTime)
        {
            WindowPanel::OnUpdate(deltaTime);
            if (m_NeedsRebuild)
            {
                m_NeedsRebuild = false;
                RebuildInspector();
            }
        }

        void InspectorPanel::OnRender()
        {
            if (!m_IsVisible) return;

            float rightPadding = (m_ScrollState.GetMaxScroll() > 0) ? 22.0f : 0.0f;
            SetClipPadding(0.0f, 40.0f, rightPadding, 0.0f);

            UI::WindowPanel::OnRender();

            // 스크롤바 그리기
            if (m_ScrollState.GetMaxScroll() > 0)
            {
                float thumbH = m_ScrollState.GetThumbHeight();
                float thumbY = m_ScrollState.GetThumbY(m_CalculatedPos.y + 40.0f);
                float thumbX = m_CalculatedPos.x + m_CalculatedSize.x - 20.0f;

                UIRenderer::DrawRect({ thumbX, m_CalculatedPos.y + 40.0f }, { 8.0f, m_ScrollState.ViewportHeight }, { 0.1f, 0.1f, 0.1f, 0.5f });
                UIRenderer::DrawRect({ thumbX, thumbY }, { 8.0f, thumbH }, { 0.4f, 0.4f, 0.4f, 1.0f });
            }
        }

        void InspectorPanel::UpdateLayout(const DirectX::XMFLOAT2& parentPos, const DirectX::XMFLOAT2& parentSize)
        {
            WindowPanel::UpdateLayout(parentPos, parentSize);

            if (!m_IsVisible || !m_SelectedEntity) return;

            float startY = 40.0f;
            float currentY = startY - m_ScrollState.ScrollY;

            for (auto child : m_Children)
            {
                if (!child->IsVisible()) continue;
                if (child == m_AddComponentMenu) continue;

                if (child == m_AddComponentButton)
                {
                    child->SetAnchorMin(0.0f, 0.0f);
                    child->SetAnchorMax(1.0f, 0.0f);
                    child->SetOffsetMin(12.0f, currentY);
                    child->SetOffsetMax(-12.0f, currentY + 30.0f);
                    child->UpdateLayout(m_CalculatedPos, m_CalculatedSize);
                    currentY += 34.0f;
                    continue;
                }

                child->SetOffsetMin(0.0f, currentY);
                child->SetOffsetMax(0.0f, currentY);
                child->UpdateLayout(m_CalculatedPos, m_CalculatedSize);

                currentY += child->GetCalculatedSize().y + 4.0f;
            }

            m_ScrollState.ContentHeight = (currentY + m_ScrollState.ScrollY) - startY;
            m_ScrollState.ViewportHeight = m_CalculatedSize.y - startY;

            if (m_AddComponentMenu && m_AddComponentButton)
            {
                auto buttonPos = m_AddComponentButton->GetCalculatedPosition();
                float menuWidth = (std::max)(180.0f, m_CalculatedSize.x - 24.0f);
                float menuHeight = m_AddComponentMenu->GetSize().y;
                float localX = 12.0f;
                float localY = buttonPos.y - m_CalculatedPos.y + 34.0f;
                if (localY + menuHeight > m_CalculatedSize.y)
                    localY = (std::max)(40.0f, buttonPos.y - m_CalculatedPos.y - menuHeight);
                m_AddComponentMenu->SetOffsetMin(localX, localY);
                m_AddComponentMenu->SetOffsetMax(localX + menuWidth, localY + menuHeight);
                m_AddComponentMenu->UpdateLayout(m_CalculatedPos, m_CalculatedSize);
            }
        }

    }
}
