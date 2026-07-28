#include "InspectorPanel.h"
#include "InspectorRegistry.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Texture.h"
#include "Renderer/UIRenderer.h"
#include "Renderer/Renderer2D.h"
#include "Application.h"
#include "UI/Button.h"
#include "UI/Panel.h"
#include "UI/TextInput.h"
#include "Scene/Components.h"
#include "Renderer/MeshFactory.h"
#include "Core/AssetDatabase.h"
#include "Core/ConsoleLog.h"
#include "Scripting/ScriptCompiler.h"
#include "Scripting/ScriptMetadata.h"
#include "Utils/PlatformUtils.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <vector>

namespace CCEngine
{
    namespace UI
    {
        namespace
        {
            Widget* FindVisibleDescendantByName(Widget* widget, const std::string& name)
            {
                if (!widget || !widget->IsVisible())
                    return nullptr;

                if (widget->GetName() == name)
                    return widget;

                for (Widget* child : widget->GetChildren())
                {
                    if (Widget* found = FindVisibleDescendantByName(child, name))
                        return found;
                }

                return nullptr;
            }
        }

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
            m_PreviousComponentPage = nullptr;
            m_ComponentPageLabel = nullptr;
            m_NextComponentPage = nullptr;
            m_ComponentFilter.clear();
            m_ComponentPage = 0;

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
            m_ComponentSearchInput->SetOnTextChanged([this](const std::string& query)
                {
                    m_ComponentPage = 0;
                    FilterAddComponentMenu(query);
                });
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
            addCandidate("Box Collider 3D", AddComponentType::BoxCollider3D, m_SelectedEntity.HasComponent<BoxCollider3DComponent>());
            addCandidate("Sphere Collider 3D", AddComponentType::SphereCollider3D, m_SelectedEntity.HasComponent<SphereCollider3DComponent>());
            addCandidate("Cylinder Collider 3D", AddComponentType::CylinderCollider3D, m_SelectedEntity.HasComponent<CylinderCollider3DComponent>());
            addCandidate("Mesh Collider 3D", AddComponentType::MeshCollider3D, m_SelectedEntity.HasComponent<MeshCollider3DComponent>());
            addCandidate("New C# Script...", AddComponentType::Script, m_SelectedEntity.HasComponent<ScriptComponent>());

            if (!m_SelectedEntity.HasComponent<ScriptComponent>())
            {
                for (const std::string& className : DiscoverScriptClasses())
                {
                    auto button = new UI::Button("Attach" + className, className);
                    button->SetOnClick([this, className]() { AttachExistingScript(className); });
                    m_AddComponentMenu->AddChild(button);
                    m_ComponentButtons.emplace_back(button, "C# Script " + className);
                }
            }

            m_PreviousComponentPage = new UI::Button("PreviousComponentPage", "<");
            m_PreviousComponentPage->SetOnClick([this]() { ChangeComponentPage(-1); });
            m_AddComponentMenu->AddChild(m_PreviousComponentPage);

            m_ComponentPageLabel = new UI::Button("ComponentPageLabel", "1 / 1");
            m_AddComponentMenu->AddChild(m_ComponentPageLabel);

            m_NextComponentPage = new UI::Button("NextComponentPage", ">");
            m_NextComponentPage->SetOnClick([this]() { ChangeComponentPage(1); });
            m_AddComponentMenu->AddChild(m_NextComponentPage);
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
                    case AddComponentType::BoxCollider3D: return "Box Collider 3D";
                    case AddComponentType::SphereCollider3D: return "Sphere Collider 3D";
                    case AddComponentType::CylinderCollider3D: return "Cylinder Collider 3D";
                    case AddComponentType::MeshCollider3D: return "Mesh Collider 3D";
                    case AddComponentType::Script: return "C# Script";
                    default: return "Component";
                }
            };

            if (type == AddComponentType::Script)
            {
                m_AddComponentMenu->SetVisible(false);
                if (CreateAndAttachScript())
                {
                    CommitStructureChange();
                    RequestRebuild();
                }
                return;
            }

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
                case AddComponentType::BoxCollider3D: m_SelectedEntity.AddComponent<BoxCollider3DComponent>(); break;
                case AddComponentType::SphereCollider3D: m_SelectedEntity.AddComponent<SphereCollider3DComponent>(); break;
                case AddComponentType::CylinderCollider3D: m_SelectedEntity.AddComponent<CylinderCollider3DComponent>(); break;
                case AddComponentType::MeshCollider3D: m_SelectedEntity.AddComponent<MeshCollider3DComponent>(); break;
                case AddComponentType::Script: break;
            }

            m_AddComponentMenu->SetVisible(false);
            CommitStructureChange();
            RequestRebuild();
        }

        bool InspectorPanel::CreateAndAttachScript()
        {
            if (!m_SelectedEntity || m_SelectedEntity.GetScene()->GetState() != SceneState::Edit)
            {
                ConsoleLog::Warning("Stop Play Mode before creating a C# script.");
                return false;
            }

            const auto scriptsDirectory = std::filesystem::current_path() / "assets" / "Scripts" / "Game";
            std::error_code ec;
            std::filesystem::create_directories(scriptsDirectory, ec);
            if (ec)
            {
                ConsoleLog::Error("Failed to create the script directory: " + scriptsDirectory.string());
                return false;
            }

            const std::string initialDirectory = scriptsDirectory.string();
            std::string filepath = PlatformUtils::SaveFile(
                "C# Script (*.cs)\0*.cs\0",
                initialDirectory.c_str());
            if (filepath.empty())
                return false;

            std::filesystem::path scriptPath = std::filesystem::absolute(filepath).lexically_normal();
            std::filesystem::path assetsRoot = std::filesystem::absolute(
                std::filesystem::current_path() / "assets").lexically_normal();
            std::filesystem::path relative = std::filesystem::relative(scriptPath, assetsRoot, ec);
            if (ec || relative.empty() || relative.begin()->string() == "..")
            {
                ConsoleLog::Error("C# scripts must be saved inside the project assets folder.");
                return false;
            }

            std::string className = scriptPath.stem().string();
            className.erase(
                std::remove_if(className.begin(), className.end(),
                    [](unsigned char c) { return !std::isalnum(c) && c != '_'; }),
                className.end());
            if (className.empty())
                className = "NewScript";
            if (std::isdigit(static_cast<unsigned char>(className.front())))
                className.insert(className.begin(), '_');

            std::ofstream output(scriptPath, std::ios::trunc);
            if (!output.is_open())
            {
                ConsoleLog::Error("Failed to create C# script: " + scriptPath.string());
                return false;
            }

            output <<
                "using CCEngine;\n\n"
                "namespace Game\n"
                "{\n"
                "    public sealed class " << className << " : GameScript\n"
                "    {\n"
                "        // [Range(0.0f, 10.0f)]처럼 붙이면 인스펙터 표시 방식이 바뀝니다.\n"
                "        public float Speed = 1.0f;\n\n"
                "        protected override void Awake()\n"
                "        {\n"
                "            // Play가 시작되어 스크립트 인스턴스가 준비될 때 한 번 호출됩니다.\n"
                "        }\n\n"
                "        protected override void Start()\n"
                "        {\n"
                "            // 모든 Awake와 OnEnable이 끝난 뒤, 첫 Update 전에 한 번 호출됩니다.\n"
                "        }\n\n"
                "        protected override void Update(float deltaTime)\n"
                "        {\n"
                "            // Play 중 매 프레임 호출되며 deltaTime은 이전 프레임부터 흐른 시간입니다.\n"
                "        }\n\n"
                "        protected override void OnDestroy()\n"
                "        {\n"
                "            // Play가 끝나거나 스크립트 인스턴스가 제거될 때 한 번 호출됩니다.\n"
                "        }\n"
                "    }\n"
                "}\n";
            output.close();

            BeginStructureChange("Add C# Script");
            m_SelectedEntity.GetScene()->AddScriptComponent(m_SelectedEntity, "Game." + className, true);

            AssetDatabase::EnsureMetaFile(scriptPath);
            AssetDatabase::MarkDirty(assetsRoot);
            ScriptCompiler::RequestCompile();
            ConsoleLog::Info("C# script created and attached: " + scriptPath.string());
            return true;
        }

        void InspectorPanel::AttachExistingScript(const std::string& className)
        {
            if (!m_SelectedEntity || m_SelectedEntity.HasComponent<ScriptComponent>())
                return;

            Scene* scene = m_SelectedEntity.GetScene();
            const bool editMode = scene && scene->GetState() == SceneState::Edit;

            if (editMode)
                BeginStructureChange("Add C# Script");

            // 기존 스크립트 부착은 Play 중에도 허용한다.
            // 런타임 씬에 붙이면 즉시 Awake/OnEnable 준비를 하고, Stop하면 에디터 원본 씬에는 남지 않는다.
            scene->AddScriptComponent(m_SelectedEntity, className, true);

            if (editMode)
                CommitStructureChange();

            m_AddComponentMenu->SetVisible(false);
            ScriptCompiler::RequestCompile();
            ConsoleLog::Info("C# script attached: " + className);
            RequestRebuild();
        }

        std::vector<std::string> InspectorPanel::DiscoverScriptClasses() const
        {
            // 메뉴에는 컴파일된 DLL에서 검증된 스크립트만 보여준다.
            // 소스 파싱은 주석, partial class, 네임스페이스 형식에 따라 쉽게 틀어진다.
            ScriptMetadata::Refresh();
            return ScriptMetadata::GetClassNames();
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

        bool InspectorPanel::IsAlbedoTextureSlotPoint(float mouseX, float mouseY) const
        {
            Entity selected = m_SelectedEntity;
            if (!selected || !selected.HasComponent<MeshComponent>())
                return false;

            // Mesh Renderer 안의 텍스처 슬롯만 드롭 대상으로 본다.
            // 인스펙터 빈 공간까지 허용하면 다른 항목 위에서 놓았을 때 의도치 않게 재질이 바뀐다.
            Widget* slot = FindVisibleDescendantByName(const_cast<InspectorPanel*>(this), "BtnChangeTexture");
            return slot && slot->IsPointInside(mouseX, mouseY);
        }

        void InspectorPanel::FilterAddComponentMenu(const std::string& query)
        {
            m_ComponentFilter = query;
            std::string lowerQuery = query;
            std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(),
                [](unsigned char c) { return (char)std::tolower(c); });

            std::vector<Button*> matches;
            for (auto& [button, name] : m_ComponentButtons)
            {
                std::string lowerName = name;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                    [](unsigned char c) { return (char)std::tolower(c); });
                bool visible = lowerQuery.empty() || lowerName.find(lowerQuery) != std::string::npos;
                button->SetVisible(false);
                if (visible)
                    matches.push_back(button);
            }

            const size_t pageCount = (std::max<size_t>)(1, (matches.size() + ComponentPageSize - 1) / ComponentPageSize);
            m_ComponentPage = (std::min)(m_ComponentPage, pageCount - 1);
            const size_t first = m_ComponentPage * ComponentPageSize;
            const size_t last = (std::min)(first + ComponentPageSize, matches.size());

            float currentY = 38.0f;
            for (size_t index = first; index < last; ++index)
            {
                Button* button = matches[index];
                button->SetVisible(true);
                button->SetAnchorMin(0.0f, 0.0f);
                button->SetAnchorMax(1.0f, 0.0f);
                button->SetOffsetMin(6.0f, currentY);
                button->SetOffsetMax(-6.0f, currentY + 26.0f);
                currentY += 28.0f;
            }

            const bool showPager = pageCount > 1;
            if (m_PreviousComponentPage && m_ComponentPageLabel && m_NextComponentPage)
            {
                m_PreviousComponentPage->SetVisible(showPager);
                m_ComponentPageLabel->SetVisible(showPager);
                m_NextComponentPage->SetVisible(showPager);
                if (showPager)
                {
                    m_PreviousComponentPage->SetAnchorMin(0.0f, 0.0f);
                    m_PreviousComponentPage->SetAnchorMax(0.0f, 0.0f);
                    m_PreviousComponentPage->SetOffsetMin(6.0f, currentY);
                    m_PreviousComponentPage->SetOffsetMax(42.0f, currentY + 26.0f);

                    m_ComponentPageLabel->SetAnchorMin(0.0f, 0.0f);
                    m_ComponentPageLabel->SetAnchorMax(1.0f, 0.0f);
                    m_ComponentPageLabel->SetOffsetMin(46.0f, currentY);
                    m_ComponentPageLabel->SetOffsetMax(-46.0f, currentY + 26.0f);
                    m_ComponentPageLabel->SetText(
                        std::to_string(m_ComponentPage + 1) + " / " + std::to_string(pageCount));

                    m_NextComponentPage->SetAnchorMin(1.0f, 0.0f);
                    m_NextComponentPage->SetAnchorMax(1.0f, 0.0f);
                    m_NextComponentPage->SetOffsetMin(-42.0f, currentY);
                    m_NextComponentPage->SetOffsetMax(-6.0f, currentY + 26.0f);
                    currentY += 30.0f;
                }
            }
            if (m_AddComponentMenu)
                m_AddComponentMenu->SetSize(m_AddComponentMenu->GetSize().x, currentY + 6.0f);
        }

        void InspectorPanel::ChangeComponentPage(int direction)
        {
            if (direction < 0 && m_ComponentPage > 0)
                --m_ComponentPage;
            else if (direction > 0)
                ++m_ComponentPage;
            FilterAddComponentMenu(m_ComponentFilter);
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
