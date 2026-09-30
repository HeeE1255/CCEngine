#include "InspectorUtils.h"
#include "UI/InspectorRegistry.h"
#include "Scene/Components.h"
#include "Animation/AnimatorControllerAsset.h"
#include "Animation/AvatarAsset.h"
#include "Core/AssetDatabase.h"
#include "Scripting/ScriptMetadata.h"
#include "Utils/PlatformUtils.h"
#include "UI/Button.h"
#include "UI/InspectorPanel.h"
#include "UI/ScriptFieldWidget.h"
#include "UI/TextInput.h"
#include "Renderer/ShaderProperty.h"
#include "Renderer/UIRenderer.h"
#include <algorithm>
#include <iostream>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <type_traits>
#include <chrono>
#include <unordered_set>

namespace CCEngine {
    namespace UI {
        namespace
        {
            class InspectorFieldRow : public Widget
            {
            public:
                enum class Kind { ObjectField, Toggle, Dropdown };

                InspectorFieldRow(const std::string& name, std::string label, std::string value, Kind kind)
                    : Widget(name), m_Label(std::move(label)), m_Value(std::move(value)), m_Kind(kind)
                {
                }

                void SetValue(const std::string& value) { m_Value = value; }
                void SetChecked(bool checked) { m_Checked = checked; }
                void SetOnClick(std::function<void(InspectorFieldRow*)> callback) { m_OnClick = std::move(callback); }
                void SetOnObjectButtonClick(std::function<void(InspectorFieldRow*)> callback) { m_OnObjectButtonClick = std::move(callback); }
                void SetOnDoubleClick(std::function<void(InspectorFieldRow*)> callback) { m_OnDoubleClick = std::move(callback); }

                void OnRender() override
                {
                    if (!m_IsVisible)
                        return;

                    const float x = m_CalculatedPos.x;
                    const float y = m_CalculatedPos.y;
                    const float w = m_CalculatedSize.x;
                    const float h = m_CalculatedSize.y;
                    const float labelW = (std::min)(150.0f, w * 0.42f);
                    const float fieldX = x + labelW;
                    const float fieldW = (std::max)(24.0f, w - labelW);

                    UIRenderer::DrawString(m_Label, x + 4.0f, y + h * 0.5f + 7.0f, { 0.72f, 0.72f, 0.74f, 1.0f });

                    if (m_Kind == Kind::Toggle)
                    {
                        UIRenderer::DrawRectFilled(fieldX + 4.0f, y + 4.0f, 15.0f, 15.0f, { 0.10f, 0.10f, 0.105f, 1.0f });
                        UIRenderer::DrawRect({ fieldX + 4.0f, y + 4.0f }, { 15.0f, 15.0f }, m_Checked ? DirectX::XMFLOAT4{ 0.58f, 0.78f, 0.98f, 1.0f } : DirectX::XMFLOAT4{ 0.30f, 0.30f, 0.32f, 1.0f });
                        if (m_Checked)
                            UIRenderer::DrawString("v", fieldX + 8.0f, y + 18.0f, { 0.88f, 0.92f, 0.96f, 1.0f });
                    }
                    else
                    {
                        const DirectX::XMFLOAT4 fill = m_IsPressed
                            ? DirectX::XMFLOAT4{ 0.16f, 0.19f, 0.23f, 1.0f }
                            : DirectX::XMFLOAT4{ 0.12f, 0.12f, 0.125f, 1.0f };
                        UIRenderer::DrawRectFilled(fieldX, y + 1.0f, fieldW - 4.0f, h - 2.0f, fill);
                        UIRenderer::DrawRect({ fieldX, y + 1.0f }, { fieldW - 4.0f, h - 2.0f }, { 0.25f, 0.25f, 0.27f, 1.0f });
                        const float textW = (std::max)(12.0f, fieldW - 38.0f);
                        UIRenderer::DrawString(FitValueToWidth(m_Value, textW), fieldX + 8.0f, y + h * 0.5f + 7.0f, { 0.84f, 0.84f, 0.86f, 1.0f });
                        if (m_Kind == Kind::Dropdown)
                            UIRenderer::DrawString("v", fieldX + fieldW - 22.0f, y + h * 0.5f + 7.0f, { 0.70f, 0.70f, 0.72f, 1.0f });
                        else
                            UIRenderer::DrawString("o", fieldX + fieldW - 22.0f, y + h * 0.5f + 7.0f, { 0.60f, 0.60f, 0.62f, 1.0f });
                    }

                    Widget::OnRender();
                }

            protected:
                bool OnMouseButtonPressed(MouseButtonPressedEvent& e) override
                {
                    if (e.GetButton() != 0 || !IsPointInside(e.GetX(), e.GetY()))
                        return false;
                    m_IsPressed = true;
                    e.Handled = true;
                    return true;
                }

                bool OnMouseButtonReleased(MouseButtonReleasedEvent& e) override
                {
                    if (e.GetButton() != 0)
                        return false;
                    const bool fire = m_IsPressed && IsPointInside(e.GetX(), e.GetY());
                    m_IsPressed = false;
                    if (!fire)
                        return e.Handled;

                    const float w = m_CalculatedSize.x;
                    const float labelW = (std::min)(150.0f, w * 0.42f);
                    const float fieldX = m_CalculatedPos.x + labelW;
                    const float fieldW = (std::max)(24.0f, w - labelW);
                    const bool objectButton =
                        m_Kind == Kind::ObjectField &&
                        e.GetX() >= fieldX + fieldW - 30.0f &&
                        e.GetX() <= fieldX + fieldW;

                    if (objectButton && m_OnObjectButtonClick)
                    {
                        m_OnObjectButtonClick(this);
                        e.Handled = true;
                        return true;
                    }

                    const auto now = std::chrono::steady_clock::now();
                    const bool doubleClick =
                        m_OnDoubleClick &&
                        m_LastClickedRow == this &&
                        std::chrono::duration<float>(now - m_LastClickTime).count() <= 0.45f;
                    m_LastClickedRow = this;
                    m_LastClickTime = now;

                    if (doubleClick)
                    {
                        m_OnDoubleClick(this);
                        e.Handled = true;
                        return true;
                    }

                    if (m_OnClick)
                    {
                        m_OnClick(this);
                        e.Handled = true;
                    }
                    return e.Handled;
                }

            private:
                static std::string FitValueToWidth(const std::string& text, float availableWidth)
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
                Kind m_Kind = Kind::ObjectField;
                bool m_Checked = false;
                bool m_IsPressed = false;
                std::function<void(InspectorFieldRow*)> m_OnClick;
                std::function<void(InspectorFieldRow*)> m_OnObjectButtonClick;
                std::function<void(InspectorFieldRow*)> m_OnDoubleClick;
                inline static InspectorFieldRow* m_LastClickedRow = nullptr;
                inline static std::chrono::steady_clock::time_point m_LastClickTime =
                    std::chrono::steady_clock::now() - std::chrono::seconds(2);
            };

            Entity FindModelRoot(Entity entity)
            {
                Entity current = entity;
                while (current)
                {
                    if (current.HasComponent<ModelComponent>())
                    {
                        return current;
                    }

                    if (!current.HasComponent<RelationshipComponent>())
                    {
                        break;
                    }

                    entt::entity parentID = current.GetComponent<RelationshipComponent>().Parent;
                    if (parentID == entt::null)
                    {
                        break;
                    }

                    current = { parentID, current.GetScene() };
                }

                return {};
            }

            std::string NormalizeRootBoneKey(std::string value)
            {
                std::replace(value.begin(), value.end(), '\\', '/');
                const size_t slash = value.find_last_of('/');
                if (slash != std::string::npos)
                    value = value.substr(slash + 1);
                const size_t colon = value.find_last_of(':');
                if (colon != std::string::npos)
                    value = value.substr(colon + 1);

                std::string normalized;
                normalized.reserve(value.size());
                for (char c : value)
                {
                    if (c == '_' || c == '-' || std::isspace(static_cast<unsigned char>(c)))
                        continue;
                    normalized.push_back((char)std::tolower(static_cast<unsigned char>(c)));
                }
                return normalized;
            }

            int ScoreRootBoneCandidate(const std::string& name)
            {
                const std::string key = NormalizeRootBoneKey(name);
                if (key == "hips" || key == "pelvis")
                    return 100;
                if (key.find("hips") != std::string::npos || key.find("pelvis") != std::string::npos)
                    return 90;
                if (key == "root")
                    return 70;
                if (key.find("root") != std::string::npos)
                    return 55;
                if (key.find("armature") != std::string::npos)
                    return 35;
                return 0;
            }

            std::vector<std::string> CollectRootBoneCandidates(Entity entity)
            {
                std::vector<std::string> candidates;
                std::unordered_set<std::string> seen;
                Entity modelRoot = FindModelRoot(entity);
                if (!modelRoot || !modelRoot.HasComponent<ModelComponent>())
                    return candidates;

                auto pushCandidate = [&](const std::string& value)
                {
                    if (!value.empty() && seen.insert(value).second)
                        candidates.push_back(value);
                };

                const auto& model = modelRoot.GetComponent<ModelComponent>();
                for (const auto& [path, handle] : model.NodePathEntityMap)
                    pushCandidate(path);
                for (const auto& [name, handle] : model.NodeEntityMap)
                    pushCandidate(name);

                std::stable_sort(candidates.begin(), candidates.end(), [](const std::string& a, const std::string& b)
                {
                    const int scoreA = ScoreRootBoneCandidate(a);
                    const int scoreB = ScoreRootBoneCandidate(b);
                    if (scoreA != scoreB)
                        return scoreA > scoreB;
                    return a.size() < b.size();
                });
                return candidates;
            }

            std::string FindBestRootBoneCandidate(Entity entity)
            {
                const auto candidates = CollectRootBoneCandidates(entity);
                for (const std::string& candidate : candidates)
                {
                    if (ScoreRootBoneCandidate(candidate) > 0)
                        return candidate;
                }
                return candidates.empty() ? std::string("Hips") : candidates.front();
            }

            AnimatorComponent::State* FindInspectorActiveState(AnimatorComponent& animator)
            {
                if (!animator.Layers.empty())
                {
                    animator.ActiveLayerIndex = std::clamp(animator.ActiveLayerIndex, 0, static_cast<int>(animator.Layers.size()) - 1);
                    auto& layer = animator.Layers[animator.ActiveLayerIndex];
                    if (!layer.States.empty())
                    {
                        layer.ActiveStateIndex = std::clamp(layer.ActiveStateIndex, 0, static_cast<int>(layer.States.size()) - 1);
                        return &layer.States[layer.ActiveStateIndex];
                    }
                }

                if (!animator.States.empty())
                {
                    animator.ActiveStateIndex = std::clamp(animator.ActiveStateIndex, 0, static_cast<int>(animator.States.size()) - 1);
                    return &animator.States[animator.ActiveStateIndex];
                }
                return nullptr;
            }

            void SaveAnimatorControllerIfAssigned(AnimatorComponent& animator)
            {
                std::filesystem::path controllerPath = animator.ControllerPath;
                if (!animator.ControllerAssetGuid.empty())
                {
                    std::filesystem::path guidPath = AssetDatabase::GetPathFromGuid(animator.ControllerAssetGuid);
                    if (!guidPath.empty())
                        controllerPath = guidPath;
                }
                if (controllerPath.empty())
                    return;

                AnimatorControllerAsset::Normalize(animator);
                AnimatorControllerAsset::SaveToFile(controllerPath, animator);
            }

            std::string ResolveAnimationSourcePath(AnimatorComponent& animator, const ModelComponent& model)
            {
                if (animator.SourceAssetGuid.empty())
                    animator.SourceAssetGuid = model.AssetGuid;

                if (!animator.SourceAssetGuid.empty())
                {
                    std::filesystem::path path = AssetDatabase::GetPathFromGuid(animator.SourceAssetGuid);
                    if (!path.empty() && std::filesystem::exists(path))
                    {
                        animator.SourcePath = path.string();
                        return animator.SourcePath;
                    }
                }

                if (!animator.SourcePath.empty() && std::filesystem::exists(animator.SourcePath))
                    return animator.SourcePath;

                if (model.TargetModel)
                {
                    animator.SourcePath = model.TargetModel->GetFilePath();
                    if (animator.SourceAssetGuid.empty())
                        animator.SourceAssetGuid = AssetDatabase::GetGuidFromPath(animator.SourcePath);
                }

                return animator.SourcePath;
            }

            const char* AnimatorUpdateModeName(AnimatorComponent::UpdateMode mode)
            {
                switch (mode)
                {
                    case AnimatorComponent::UpdateMode::AnimatePhysics: return "Animate Physics";
                    case AnimatorComponent::UpdateMode::UnscaledTime: return "Unscaled Time";
                    default: return "Normal";
                }
            }

            const char* AnimatorCullingModeName(AnimatorComponent::CullingMode mode)
            {
                switch (mode)
                {
                    case AnimatorComponent::CullingMode::AlwaysAnimate: return "Always Animate";
                    case AnimatorComponent::CullingMode::CullCompletely: return "Cull Complete";
                    default: return "Cull Update";
                }
            }

            bool IsSkeletonNode(Entity entity, Entity modelRoot)
            {
                if (!entity || !modelRoot || !modelRoot.HasComponent<ModelComponent>())
                {
                    return false;
                }

                if (entity.HasComponent<MeshComponent>())
                {
                    return false;
                }

                auto& model = modelRoot.GetComponent<ModelComponent>();
                for (const auto& [path, handle] : model.NodePathEntityMap)
                {
                    if (handle == (entt::entity)entity)
                    {
                        return true;
                    }
                }

                return false;
            }

            MeshComponent::MaterialSlot& EnsurePrimaryMaterialSlot(MeshComponent& mesh)
            {
                if (mesh.MaterialSlots.empty())
                {
                    MeshComponent::MaterialSlot slot;
                    slot.Name = "Element 0";
                    slot.Material = mesh.Material;
                    slot.MaterialAssetGuid = mesh.MaterialAssetGuid;
                    slot.MaterialPath = mesh.MaterialPath;
                    mesh.MaterialSlots.push_back(slot);
                }

                return mesh.MaterialSlots.front();
            }

            void SyncPrimaryMaterialSlotToLegacyFields(MeshComponent& mesh)
            {
                auto& slot = EnsurePrimaryMaterialSlot(mesh);
                mesh.Material = slot.Material;
                mesh.MaterialAssetGuid = slot.MaterialAssetGuid;
                mesh.MaterialPath = slot.MaterialPath;
                // slot 0이 실제 렌더에 쓰이는 Material이다.
                // 그래서 Missing 상태도 legacy 필드로 내려줘야 Scene 렌더 단계에서 에러 셰이더를 고를 수 있다.
                mesh.MaterialMissing = slot.Missing && (!slot.MaterialPath.empty() || !slot.MaterialAssetGuid.empty());
            }

            bool LoadMaterialIntoSlot(MeshComponent::MaterialSlot& slot, const std::filesystem::path& path)
            {
                if (path.empty())
                    return false;

                auto material = std::make_shared<MaterialAsset>();
                if (!material->LoadFromFile(path))
                    return false;

                slot.Material = material;
                slot.MaterialPath = path.string();
                slot.MaterialAssetGuid = AssetDatabase::GetGuidFromPath(path);
                slot.Missing = false;
                return true;
            }

            std::shared_ptr<MaterialAsset> LoadMaterialAssetFromPath(const std::filesystem::path& path)
            {
                if (path.empty())
                    return nullptr;

                auto material = std::make_shared<MaterialAsset>();
                if (!material->LoadFromFile(path))
                    return nullptr;

                return material;
            }

            bool TryRepairMaterialSlot(MeshComponent::MaterialSlot& slot)
            {
                if (slot.MaterialAssetGuid.empty())
                    return false;

                std::filesystem::path repairedPath = AssetDatabase::GetPathFromGuid(slot.MaterialAssetGuid);
                if (repairedPath.empty() || !std::filesystem::exists(repairedPath))
                {
                    if (!slot.MaterialPath.empty() || !slot.MaterialAssetGuid.empty())
                    {
                        slot.Material.reset();
                        slot.Missing = true;
                    }
                    return false;
                }

                return LoadMaterialIntoSlot(slot, repairedPath);
            }

            bool HasShaderProperty(const std::vector<ShaderPropertyDefinition>& definitions, const std::string& name, ShaderPropertyType type)
            {
                return std::any_of(definitions.begin(), definitions.end(),
                    [&](const ShaderPropertyDefinition& definition)
                    {
                        return definition.Name == name && definition.Type == type;
                    });
            }

            bool IsSurfaceBackedShaderProperty(const ShaderPropertyDefinition& definition)
            {
                if (definition.Name == "AlbedoColor" && definition.Type == ShaderPropertyType::Color)
                    return true;
                if ((definition.Name == "Roughness" || definition.Name == "Metallic") && definition.Type == ShaderPropertyType::Float)
                    return true;
                return definition.Name == "AlbedoTexture" || definition.Name == "NormalTexture";
            }

            ShaderPropertyValue& EnsureSurfaceBackedShaderProperty(MaterialAsset& material, const ShaderPropertyDefinition& definition)
            {
                const auto existing = material.ShaderProperties.find(definition.Name);
                const bool hadSavedValue = existing != material.ShaderProperties.end() && existing->second.Type != ShaderPropertyType::Unknown;
                ShaderPropertyValue& value = material.EnsureShaderPropertyValue(definition);

                // Surface UI가 보여주는 공통 값과 HLSL이 읽는 ShaderProperty 값을 같은 값으로 맞춘다.
                // 둘이 갈라지면 사용자는 위 항목을 바꿨는데 화면은 아래 항목 기준으로 바뀌는 것처럼 보게 된다.
                if (definition.Name == "AlbedoColor" && definition.Type == ShaderPropertyType::Color)
                {
                    if (hadSavedValue)
                        material.AlbedoColor = value.Color;
                    else
                        value.Color = material.AlbedoColor;
                }
                else if (definition.Name == "Roughness" && definition.Type == ShaderPropertyType::Float)
                {
                    if (hadSavedValue)
                        material.Roughness = std::clamp(value.FloatValue, 0.0f, 1.0f);
                    else
                        value.FloatValue = material.Roughness;
                }
                else if (definition.Name == "Metallic" && definition.Type == ShaderPropertyType::Float)
                {
                    if (hadSavedValue)
                        material.Metallic = std::clamp(value.FloatValue, 0.0f, 1.0f);
                    else
                        value.FloatValue = material.Metallic;
                }
                else if (definition.Name == "AlbedoTexture" && definition.Type == ShaderPropertyType::Texture2D)
                {
                    if (hadSavedValue)
                    {
                        material.AlbedoTextureGuid = value.TextureGuid;
                        material.AlbedoTexturePath = value.TexturePath;
                    }
                    else
                    {
                        value.TextureGuid = material.AlbedoTextureGuid;
                        value.TexturePath = material.AlbedoTexturePath;
                    }
                }
                else if (definition.Name == "NormalTexture" && definition.Type == ShaderPropertyType::Texture2D)
                {
                    if (hadSavedValue)
                    {
                        material.NormalTextureGuid = value.TextureGuid;
                        material.NormalTexturePath = value.TexturePath;
                    }
                    else
                    {
                        value.TextureGuid = material.NormalTextureGuid;
                        value.TexturePath = material.NormalTexturePath;
                    }
                }

                return value;
            }

            void SyncSurfaceValueToShaderProperty(MaterialAsset& material, const std::string& name)
            {
                auto it = material.ShaderProperties.find(name);
                if (it == material.ShaderProperties.end())
                    return;

                if (name == "AlbedoColor" && it->second.Type == ShaderPropertyType::Color)
                    it->second.Color = material.AlbedoColor;
                else if (name == "Roughness" && it->second.Type == ShaderPropertyType::Float)
                    it->second.FloatValue = material.Roughness;
                else if (name == "Metallic" && it->second.Type == ShaderPropertyType::Float)
                    it->second.FloatValue = material.Metallic;
                else if (name == "AlbedoTexture" && it->second.Type == ShaderPropertyType::Texture2D)
                {
                    it->second.TextureGuid = material.AlbedoTextureGuid;
                    it->second.TexturePath = material.AlbedoTexturePath;
                }
                else if (name == "NormalTexture" && it->second.Type == ShaderPropertyType::Texture2D)
                {
                    it->second.TextureGuid = material.NormalTextureGuid;
                    it->second.TexturePath = material.NormalTexturePath;
                }
            }

            void SaveMaterialFromMesh(MeshComponent& mesh)
            {
                if (mesh.Material && !mesh.MaterialPath.empty())
                    mesh.Material->SaveToFile(mesh.MaterialPath);
            }

            template<typename T>
            void AddRemoveComponentButton(UI::Widget* parent, UI::InspectorItem* item, Entity entity, const std::string& label)
            {
                // Transform을 제외한 컴포넌트들은 이 공통 함수로 제거 버튼을 붙인다.
                auto button = new UI::Button("BtnRemove" + label, "Remove Component");
                button->SetNormalColor({ 0.28f, 0.12f, 0.12f, 1.0f });
                button->SetHoverColor({ 0.42f, 0.16f, 0.16f, 1.0f });
                button->SetOnClick([parent, entity]() mutable
                    {
                        auto inspector = dynamic_cast<UI::InspectorPanel*>(parent);
                        if (!entity || !entity.HasComponent<T>())
                            return;

                        if (inspector)
                            // 실제 제거 전에 현재 씬 상태를 저장한다.
                            inspector->BeginStructureChange("Remove Component");

                        bool removedPrimaryCamera = false;
                        if constexpr (std::is_same_v<T, CameraComponent>)
                            removedPrimaryCamera = entity.GetComponent<CameraComponent>().Primary;

                        if constexpr (std::is_same_v<T, ScriptComponent>)
                        {
                            // 스크립트 제거는 registry에서 컴포넌트만 빼면 끝나지 않는다.
                            // Play 중에는 관리 객체의 OnDisable/OnDestroy까지 호출해야 런타임 상태가 남지 않는다.
                            entity.GetScene()->RemoveScriptComponent(entity);
                        }
                        else
                        {
                            entity.RemoveComponent<T>();
                        }

                        if constexpr (std::is_same_v<T, CameraComponent>)
                        {
                            if (removedPrimaryCamera)
                            {
                                // 게임 뷰 카메라를 지웠다면 남은 카메라 하나를 대신 사용한다.
                                auto view = entity.GetScene()->GetRegistry().view<CameraComponent>();
                                for (auto cameraEntity : view)
                                {
                                    view.get<CameraComponent>(cameraEntity).Primary = true;
                                    break;
                                }
                            }
                        }

                        if (inspector)
                        {
                            // 제거 후 상태를 저장하고 인스펙터 UI를 다시 그린다.
                            inspector->CommitStructureChange();
                            inspector->RequestRebuild();
                        }
                    });
                item->AddChild(button);
            }

            void AddColliderEditButton(UI::Widget* parent, UI::InspectorItem* item, Entity entity,
                UI::InspectorPanel::ColliderEditShape shape)
            {
                auto* inspector = dynamic_cast<UI::InspectorPanel*>(parent);
                auto* button = new UI::Button("BtnEditCollider", "Edit Collider");
                const bool active = inspector && inspector->IsColliderEditActive(entity, shape);
                button->SetActive(active);
                button->SetText(active ? "Editing Collider" : "Edit Collider");
                button->SetOnClick([inspector, entity, shape]() mutable
                    {
                        if (!inspector || !entity)
                            return;

                        inspector->ToggleColliderEdit(entity, shape);
                        inspector->RequestRebuild();
                    });
                item->AddChild(button);
            }

            void AddColliderSnapControls(UI::Widget* parent, UI::InspectorItem* item, Entity entity,
                UI::InspectorPanel::ColliderEditShape shape, const std::string& prefix)
            {
                auto* inspector = dynamic_cast<UI::InspectorPanel*>(parent);
                if (!inspector || !inspector->IsColliderEditActive(entity, shape))
                    return;

                auto* toggle = new UI::Button(prefix + "SnapToggle",
                    inspector->IsColliderSnapEnabled() ? "Collider Snap: On" : "Collider Snap: Off");
                toggle->SetActive(inspector->IsColliderSnapEnabled());
                toggle->SetOnClick([inspector, toggle]()
                    {
                        const bool enabled = !inspector->IsColliderSnapEnabled();
                        inspector->SetColliderSnapEnabled(enabled);
                        toggle->SetActive(enabled);
                        toggle->SetText(enabled ? "Collider Snap: On" : "Collider Snap: Off");
                    });
                item->AddChild(toggle);

                auto addStep = [inspector, item, &prefix](UI::InspectorPanel::ColliderSnapValue value,
                    const std::string& suffix, const std::string& label)
                    {
                        UI::InspectorUtils::AddDragFloat(item, prefix + suffix, label,
                            [inspector, value]() { return inspector->GetColliderSnapStep(value); },
                            [inspector, value](float step)
                            {
                                inspector->SetColliderSnapStep(value, (std::max)(0.001f, std::abs(step)));
                            });
                    };

                if (shape == UI::InspectorPanel::ColliderEditShape::Box)
                {
                    addStep(UI::InspectorPanel::ColliderSnapValue::Size, "SizeStep", "Size Step");
                }
                else if (shape == UI::InspectorPanel::ColliderEditShape::Sphere)
                {
                    addStep(UI::InspectorPanel::ColliderSnapValue::Offset, "OffsetStep", "Offset Step");
                    addStep(UI::InspectorPanel::ColliderSnapValue::Radius, "RadiusStep", "Radius Step");
                }
                else
                {
                    addStep(UI::InspectorPanel::ColliderSnapValue::Offset, "OffsetStep", "Offset Step");
                    addStep(UI::InspectorPanel::ColliderSnapValue::Radius, "RadiusStep", "Radius Step");
                    addStep(UI::InspectorPanel::ColliderSnapValue::Height, "HeightStep", "Height Step");
                }
            }

            std::string GetScriptFieldValue(const ScriptComponent& script, const ScriptFieldInfo& field)
            {
                auto it = script.FieldOverrides.find(field.Name);
                if (it != script.FieldOverrides.end())
                    return it->second;
                return field.DefaultValue;
            }

            float ToFloat(const std::string& value, float fallback = 0.0f)
            {
                try { return std::stof(value); }
                catch (...) { return fallback; }
            }

            int ToInt(const std::string& value, int fallback = 0)
            {
                try { return std::stoi(value); }
                catch (...) { return fallback; }
            }

            DirectX::XMFLOAT3 ToFloat3(const std::string& value)
            {
                DirectX::XMFLOAT3 result = { 0.0f, 0.0f, 0.0f };
                std::stringstream stream(value);
                std::string token;
                if (std::getline(stream, token, ',')) result.x = ToFloat(token);
                if (std::getline(stream, token, ',')) result.y = ToFloat(token);
                if (std::getline(stream, token, ',')) result.z = ToFloat(token);
                return result;
            }

            std::string FromFloat3(const DirectX::XMFLOAT3& value)
            {
                return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z);
            }

            void SetScriptFieldValue(Entity entity, const ScriptFieldInfo& field, const std::string& value)
            {
                if (entity && entity.HasComponent<ScriptComponent>())
                    entity.GetComponent<ScriptComponent>().FieldOverrides[field.Name] = value;
            }
        }

        void InspectorUtils::AddDragFloat(InspectorItem* item, const std::string& name, const std::string& label, std::function<float()> getter, std::function<void(float)> setter)
        {
            auto drag = new DragFloat(name, label, getter, setter);
            drag->SetAnchorMin(0.0f, 0.0f); drag->SetAnchorMax(1.0f, 0.0f);
            drag->SetOffsetMin(15.0f, 0.0f); drag->SetOffsetMax(-10.0f, 24.0f);
            item->AddChild(drag);
        }

        void InspectorUtils::AddDragFloat3(InspectorItem* item, const std::string& name, const std::string& label, std::function<DirectX::XMFLOAT3()> getter, std::function<void(DirectX::XMFLOAT3)> setter)
        {
            auto drag = new DragFloat3(name, label, getter, setter);
            drag->SetAnchorMin(0.0f, 0.0f); drag->SetAnchorMax(1.0f, 0.0f);
            drag->SetOffsetMin(15.0f, 0.0f); drag->SetOffsetMax(-10.0f, 24.0f);
            item->AddChild(drag);
        }

        void InspectorUtils::AddColor4(InspectorItem* item, const std::string& name, const std::string& label, std::function<DirectX::XMFLOAT4()> getter, std::function<void(DirectX::XMFLOAT4)> setter)
        {
            auto drag = new DragFloat4(name, label, getter, setter);
            drag->SetAnchorMin(0.0f, 0.0f); drag->SetAnchorMax(1.0f, 0.0f);
            drag->SetOffsetMin(15.0f, 0.0f); drag->SetOffsetMax(-10.0f, 24.0f);
            item->AddChild(drag);
        }

        void InspectorUtils::AddColor3(InspectorItem* item, const std::string& name, const std::string& label, std::function<DirectX::XMFLOAT3()> getter, std::function<void(DirectX::XMFLOAT3)> setter)
        {
            // XMFLOAT3를 XMFLOAT4로 포장해서 DragFloat4에 넘겨버림 (Alpha는 1.0 고정)
            auto getter4 = [getter]() -> DirectX::XMFLOAT4 {
                auto v3 = getter();
                return { v3.x, v3.y, v3.z, 1.0f };
                };
            auto setter4 = [setter](DirectX::XMFLOAT4 v4) {
                setter({ v4.x, v4.y, v4.z }); // 세팅할 때는 RGB만 쏙 빼서 넘겨줌
                };
            AddColor4(item, name, label, getter4, setter4);
        }

        void InspectorUtils::InitStandardComponents()
        {
            UI::InspectorRegistry::RegisterComponent<ActiveComponent>(
                [](UI::Widget* parent, CCEngine::Entity entity, ActiveComponent& active)
                {
                    auto item = new UI::InspectorItem("GameObjectActiveItem", "GameObject");
                    item->SetAnchorMin(0.0f, 0.0f);
                    item->SetAnchorMax(1.0f, 0.0f);
                    parent->AddChild(item);

                    bool activeSelf = active.ActiveSelf;
                    bool activeInHierarchy = entity.GetScene() ? entity.GetScene()->IsEntityActiveInHierarchy(entity) : activeSelf;
                    auto btnActive = new UI::Button("BtnGameObjectActive", activeSelf ? "Active: On" : "Active: Off");
                    btnActive->SetActive(activeSelf);
                    btnActive->SetOnClick([entity, btnActive]() mutable
                        {
                            if (!entity || !entity.GetScene())
                                return;

                            bool nextActive = !entity.GetScene()->IsEntityActiveSelf(entity);
                            // 인스펙터 토글은 사용자가 직접 설정한 ActiveSelf만 바꾼다.
                            // 부모 때문에 꺼진 상태까지 저장값에 섞으면 프리팹/씬 복원 때 원인을 추적하기 어렵다.
                            entity.GetScene()->SetEntityActiveSelf(entity, nextActive);
                            btnActive->SetActive(nextActive);
                            btnActive->SetText(nextActive ? "Active: On" : "Active: Off");
                        });
                    item->AddChild(btnActive);

                    if (activeSelf && !activeInHierarchy)
                    {
                        auto inheritedOff = new UI::Button("BtnInactiveByParent", "Inactive by Parent");
                        inheritedOff->SetNormalColor({ 0.16f, 0.16f, 0.16f, 1.0f });
                        inheritedOff->SetHoverColor({ 0.16f, 0.16f, 0.16f, 1.0f });
                        item->AddChild(inheritedOff);
                    }
                });

            // ==========================================================
            // 1. Transform 컴포넌트 (가장 기본이므로 가장 먼저 등록!)
            // ==========================================================
            UI::InspectorRegistry::RegisterComponent<TransformComponent>(
                [](UI::Widget* parent, CCEngine::Entity entity, TransformComponent& transform)
                {
                    auto item = new UI::InspectorItem("TransformItem", "Transform");
                    item->SetAnchorMin(0.0f, 0.0f); item->SetAnchorMax(1.0f, 0.0f);
                    parent->AddChild(item);

                    UI::InspectorUtils::AddDragFloat3(item, "Position", "Position",
                        [entity]() mutable { return entity.GetComponent<TransformComponent>().Translation; },
                        [entity](DirectX::XMFLOAT3 v) mutable { entity.GetComponent<TransformComponent>().Translation = v; });

                    UI::InspectorUtils::AddDragFloat3(item, "Rotation", "Rotation",
                        [entity]() mutable {
                            auto& rot = entity.GetComponent<TransformComponent>().Rotation;
                            auto norm = [](float d) { float r = fmod(DirectX::XMConvertToDegrees(d), 360.f); return r < 0 ? r + 360.f : r; };
                            return DirectX::XMFLOAT3{ norm(rot.x), norm(rot.y), norm(rot.z) };
                        },
                        [entity](DirectX::XMFLOAT3 v) mutable {
                            auto& tc = entity.GetComponent<TransformComponent>();
                            tc.Rotation = { DirectX::XMConvertToRadians(v.x), DirectX::XMConvertToRadians(v.y), DirectX::XMConvertToRadians(v.z) };
                            DirectX::XMStoreFloat4(&tc.QuaternionRotation, DirectX::XMQuaternionRotationRollPitchYaw(tc.Rotation.x, tc.Rotation.y, tc.Rotation.z));
                        });

                    UI::InspectorUtils::AddDragFloat3(item, "Scale", "Scale",
                        [entity]() mutable { return entity.GetComponent<TransformComponent>().Scale; },
                        [entity](DirectX::XMFLOAT3 v) mutable { entity.GetComponent<TransformComponent>().Scale = v; });

                    Entity modelRoot = FindModelRoot(entity);
                    if (IsSkeletonNode(entity, modelRoot))
                    {
                        auto boneItem = new UI::InspectorItem("SkinnedBoneItem", "Skinned Mesh Bone");
                        boneItem->SetAnchorMin(0.0f, 0.0f); boneItem->SetAnchorMax(1.0f, 0.0f);
                        parent->AddChild(boneItem);

                        auto btnBoneLines = new UI::Button("BtnToggleBoneLines", "Bone Lines: Off");
                        btnBoneLines->SetAnchorMin(0.0f, 0.0f); btnBoneLines->SetAnchorMax(1.0f, 0.0f);
                        btnBoneLines->SetOffsetMin(15.0f, 0.0f); btnBoneLines->SetOffsetMax(-10.0f, 28.0f);
                        btnBoneLines->SetOnClick([modelRoot, btnBoneLines]() mutable
                            {
                                if (!modelRoot || !modelRoot.HasComponent<ModelComponent>())
                                {
                                    return;
                                }

                                auto& model = modelRoot.GetComponent<ModelComponent>();
                                model.ShowBoneLinks = !model.ShowBoneLinks;
                                btnBoneLines->SetText(model.ShowBoneLinks ? "Bone Lines: On" : "Bone Lines: Off");
                            });

                        if (modelRoot && modelRoot.HasComponent<ModelComponent>() && modelRoot.GetComponent<ModelComponent>().ShowBoneLinks)
                        {
                            btnBoneLines->SetText("Bone Lines: On");
                        }

                        boneItem->AddChild(btnBoneLines);
                    }
                });

            // ==========================================================
            // 2. Mesh 컴포넌트
            // ==========================================================
            UI::InspectorRegistry::RegisterComponent<MeshComponent>(
                [](UI::Widget* parent, CCEngine::Entity entity, MeshComponent& mesh)
                {
                    auto item = new UI::InspectorItem("MeshItem", "Mesh Renderer");
                    item->SetAnchorMin(0.0f, 0.0f); item->SetAnchorMax(1.0f, 0.0f);
                    parent->AddChild(item);

                    UI::InspectorUtils::AddColor4(item, "AlbedoColor", "Albedo Color",
                        [entity]() mutable { return entity.GetComponent<MeshComponent>().BaseColor; },
                        [entity](DirectX::XMFLOAT4 v) mutable { entity.GetComponent<MeshComponent>().BaseColor = v; });

                    auto inspector = dynamic_cast<UI::InspectorPanel*>(parent);
                    auto& primarySlot = EnsurePrimaryMaterialSlot(mesh);
                    TryRepairMaterialSlot(primarySlot);
                    SyncPrimaryMaterialSlotToLegacyFields(mesh);

                    std::filesystem::path currentMaterialPath(primarySlot.MaterialPath);
                    std::string materialButtonText = currentMaterialPath.empty()
                        ? "Element 0: (none)"
                        : "Element 0: " + currentMaterialPath.filename().string();

                    auto btnMaterial = new UI::Button("BtnChangeMaterial", materialButtonText);
                    btnMaterial->SetAnchorMin(0.0f, 0.0f); btnMaterial->SetAnchorMax(1.0f, 0.0f);
                    btnMaterial->SetOffsetMin(15.0f, 0.0f); btnMaterial->SetOffsetMax(-10.0f, 28.0f);
                    btnMaterial->SetOnClick([entity, btnMaterial, inspector]() mutable
                        {
                            std::string filepath = PlatformUtils::OpenFile("CC Material (*.ccmat)\0*.ccmat\0");
                            if (!filepath.empty())
                            {
                                auto material = LoadMaterialAssetFromPath(filepath);
                                if (!material)
                                    return;

                                if (inspector)
                                    inspector->BeginStructureChange("Change Material Slot");

                                auto& mesh = entity.GetComponent<MeshComponent>();
                                auto& slot = EnsurePrimaryMaterialSlot(mesh);
                                slot.Material = material;
                                slot.MaterialPath = filepath;
                                slot.MaterialAssetGuid = AssetDatabase::GetGuidFromPath(filepath);
                                slot.Missing = false;
                                // Material 역시 저장 시에는 포인터가 아니라 GUID/경로를 남긴다.
                                // 그래야 파일명을 바꾸거나 위치를 옮겨도 meta 기준으로 다시 연결된다.
                                SyncPrimaryMaterialSlotToLegacyFields(mesh);
                                btnMaterial->SetText("Element 0: " + std::filesystem::path(filepath).filename().string());

                                if (inspector)
                                {
                                    inspector->CommitStructureChange();
                                    inspector->RequestRebuild();
                                }
                            }
                        });
                    item->AddChild(btnMaterial);

                    if (!primarySlot.MaterialPath.empty() && (!primarySlot.Material || primarySlot.Missing))
                    {
                        auto missingButton = new UI::Button("MaterialSlotMissing", "Missing Material: " + currentMaterialPath.filename().string());
                        missingButton->SetNormalColor({ 0.32f, 0.12f, 0.12f, 1.0f });
                        missingButton->SetHoverColor({ 0.44f, 0.16f, 0.16f, 1.0f });
                        item->AddChild(missingButton);

                        auto repairButton = new UI::Button("MaterialSlotLocate", "Locate/Fix Material...");
                        repairButton->SetOnClick([entity, inspector]() mutable
                            {
                                std::string filepath = PlatformUtils::OpenFile("CC Material (*.ccmat)\0*.ccmat\0");
                                if (filepath.empty())
                                    return;

                                auto material = LoadMaterialAssetFromPath(filepath);
                                if (!material)
                                    return;

                                if (inspector)
                                    inspector->BeginStructureChange("Fix Missing Material");
                                auto& mesh = entity.GetComponent<MeshComponent>();
                                auto& slot = EnsurePrimaryMaterialSlot(mesh);
                                slot.Material = material;
                                slot.MaterialPath = filepath;
                                slot.MaterialAssetGuid = AssetDatabase::GetGuidFromPath(filepath);
                                slot.Missing = false;
                                SyncPrimaryMaterialSlotToLegacyFields(mesh);
                                if (inspector)
                                {
                                    inspector->CommitStructureChange();
                                    inspector->RequestRebuild();
                                }
                            });
                        item->AddChild(repairButton);

                        auto clearButton = new UI::Button("MaterialSlotClear", "Clear Missing Material");
                        clearButton->SetOnClick([entity, inspector]() mutable
                            {
                                auto& mesh = entity.GetComponent<MeshComponent>();
                                auto& slot = EnsurePrimaryMaterialSlot(mesh);
                                if (inspector)
                                    inspector->BeginStructureChange("Clear Missing Material");
                                slot = MeshComponent::MaterialSlot{};
                                slot.Name = "Element 0";
                                SyncPrimaryMaterialSlotToLegacyFields(mesh);
                                if (inspector)
                                {
                                    inspector->CommitStructureChange();
                                    inspector->RequestRebuild();
                                }
                            });
                        item->AddChild(clearButton);
                    }

                    if (mesh.Material && !mesh.MaterialMissing)
                    {
                        std::vector<ShaderPropertyDefinition> materialDefinitions;
                        const bool hasCustomShader = !mesh.Material->ShaderPath.empty();
                        if (hasCustomShader)
                        {
                            materialDefinitions = ShaderPropertyParser::LoadFromShaderFile(mesh.Material->ShaderPath);
                            for (const ShaderPropertyDefinition& definition : materialDefinitions)
                            {
                                if (IsSurfaceBackedShaderProperty(definition))
                                    EnsureSurfaceBackedShaderProperty(*mesh.Material, definition);
                            }
                        }

                        const std::string materialName = mesh.MaterialPath.empty()
                            ? mesh.Material->Name
                            : std::filesystem::path(mesh.MaterialPath).filename().string();
                        auto materialHeader = new UI::Button("AppliedMaterialHeader", "Material: " + materialName);
                        materialHeader->SetNormalColor({ 0.13f, 0.13f, 0.14f, 1.0f });
                        materialHeader->SetHoverColor({ 0.13f, 0.13f, 0.14f, 1.0f });
                        item->AddChild(materialHeader);

                        const std::string shaderName = mesh.Material->ShaderPath.empty()
                            ? "Built-in/" + mesh.Material->ShaderName
                            : std::filesystem::path(mesh.Material->ShaderPath).filename().string();
                        auto shaderHeader = new UI::Button("AppliedMaterialShaderHeader", "Shader: " + shaderName);
                        shaderHeader->SetNormalColor({ 0.13f, 0.13f, 0.14f, 1.0f });
                        shaderHeader->SetHoverColor({ 0.13f, 0.13f, 0.14f, 1.0f });
                        item->AddChild(shaderHeader);

                        const bool showAlbedoColor = !hasCustomShader || HasShaderProperty(materialDefinitions, "AlbedoColor", ShaderPropertyType::Color);
                        const bool showAlbedoTexture = !hasCustomShader || HasShaderProperty(materialDefinitions, "AlbedoTexture", ShaderPropertyType::Texture2D);
                        const bool showNormalTexture = HasShaderProperty(materialDefinitions, "NormalTexture", ShaderPropertyType::Texture2D);
                        const bool showRoughness = !hasCustomShader || HasShaderProperty(materialDefinitions, "Roughness", ShaderPropertyType::Float);
                        const bool showMetallic = !hasCustomShader || HasShaderProperty(materialDefinitions, "Metallic", ShaderPropertyType::Float);

                        if (showAlbedoColor)
                        {
                            UI::InspectorUtils::AddColor4(item, "MaterialAlbedoColor", "Albedo",
                                [entity]() mutable
                                {
                                    auto& mesh = entity.GetComponent<MeshComponent>();
                                    return mesh.Material ? mesh.Material->AlbedoColor : mesh.BaseColor;
                                },
                                [entity](DirectX::XMFLOAT4 v) mutable
                                {
                                    auto& mesh = entity.GetComponent<MeshComponent>();
                                    if (!mesh.Material)
                                        return;

                                    mesh.Material->AlbedoColor = v;
                                    SyncSurfaceValueToShaderProperty(*mesh.Material, "AlbedoColor");
                                    SaveMaterialFromMesh(mesh);
                                });
                        }

                        if (showAlbedoTexture)
                        {
                            auto materialAlbedoButton = new UI::Button("MaterialInstanceAlbedoTexture",
                                mesh.Material->AlbedoTexturePath.empty()
                                ? "Albedo Texture: (none)"
                                : "Albedo Texture: " + std::filesystem::path(mesh.Material->AlbedoTexturePath).filename().string());
                            materialAlbedoButton->SetOnClick([entity, materialAlbedoButton]() mutable
                                {
                                    std::string filepath = PlatformUtils::OpenFile("Texture (*.png;*.jpg;*.jpeg;*.tga)\0*.png;*.jpg;*.jpeg;*.tga\0");
                                    if (filepath.empty())
                                        return;

                                    auto& mesh = entity.GetComponent<MeshComponent>();
                                    if (!mesh.Material)
                                        return;

                                    mesh.Material->AlbedoTexturePath = filepath;
                                    mesh.Material->AlbedoTextureGuid = AssetDatabase::GetGuidFromPath(filepath);
                                    mesh.Material->AlbedoTexture.reset(Texture2D::Create(filepath));
                                    SyncSurfaceValueToShaderProperty(*mesh.Material, "AlbedoTexture");
                                    SaveMaterialFromMesh(mesh);
                                    materialAlbedoButton->SetText("Albedo Texture: " + std::filesystem::path(filepath).filename().string());
                                });
                            item->AddChild(materialAlbedoButton);
                        }

                        if (showNormalTexture)
                        {
                            auto materialNormalButton = new UI::Button("MaterialInstanceNormalTexture",
                                mesh.Material->NormalTexturePath.empty()
                                ? "Normal Texture: (none)"
                                : "Normal Texture: " + std::filesystem::path(mesh.Material->NormalTexturePath).filename().string());
                            materialNormalButton->SetOnClick([entity, materialNormalButton]() mutable
                                {
                                    std::string filepath = PlatformUtils::OpenFile("Texture (*.png;*.jpg;*.jpeg;*.tga)\0*.png;*.jpg;*.jpeg;*.tga\0");
                                    if (filepath.empty())
                                        return;

                                    auto& mesh = entity.GetComponent<MeshComponent>();
                                    if (!mesh.Material)
                                        return;

                                    mesh.Material->NormalTexturePath = filepath;
                                    mesh.Material->NormalTextureGuid = AssetDatabase::GetGuidFromPath(filepath);
                                    SyncSurfaceValueToShaderProperty(*mesh.Material, "NormalTexture");
                                    SaveMaterialFromMesh(mesh);
                                    materialNormalButton->SetText("Normal Texture: " + std::filesystem::path(filepath).filename().string());
                                });
                            item->AddChild(materialNormalButton);
                        }

                        if (showRoughness)
                        {
                            UI::InspectorUtils::AddDragFloat(item, "MaterialRoughness", "Roughness",
                                [entity]() mutable
                                {
                                    auto& mesh = entity.GetComponent<MeshComponent>();
                                    return mesh.Material ? mesh.Material->Roughness : 0.5f;
                                },
                                [entity](float v) mutable
                                {
                                    auto& mesh = entity.GetComponent<MeshComponent>();
                                    if (!mesh.Material)
                                        return;

                                    mesh.Material->Roughness = std::clamp(v, 0.0f, 1.0f);
                                    SyncSurfaceValueToShaderProperty(*mesh.Material, "Roughness");
                                    SaveMaterialFromMesh(mesh);
                                });
                        }

                        if (showMetallic)
                        {
                            UI::InspectorUtils::AddDragFloat(item, "MaterialMetallic", "Metallic",
                                [entity]() mutable
                                {
                                    auto& mesh = entity.GetComponent<MeshComponent>();
                                    return mesh.Material ? mesh.Material->Metallic : 0.0f;
                                },
                                [entity](float v) mutable
                                {
                                    auto& mesh = entity.GetComponent<MeshComponent>();
                                    if (!mesh.Material)
                                        return;

                                    // Metallic은 금속성 비율이다. 값이 높을수록 색이 확산광보다 반사광 쪽에 더 많이 반영된다.
                                    mesh.Material->Metallic = std::clamp(v, 0.0f, 1.0f);
                                    SyncSurfaceValueToShaderProperty(*mesh.Material, "Metallic");
                                    SaveMaterialFromMesh(mesh);
                                });
                        }

                        for (const ShaderPropertyDefinition& definition : materialDefinitions)
                        {
                            if (IsSurfaceBackedShaderProperty(definition))
                                continue;

                            ShaderPropertyValue& value = mesh.Material->EnsureShaderPropertyValue(definition);
                            const std::string widgetName = "MaterialInstanceProperty_" + definition.Name;
                            if (definition.Type == ShaderPropertyType::Color)
                            {
                                UI::InspectorUtils::AddColor4(item, widgetName, definition.DisplayName,
                                    [entity, propertyName = definition.Name]() mutable
                                    {
                                        auto& mesh = entity.GetComponent<MeshComponent>();
                                        return mesh.Material ? mesh.Material->ShaderProperties[propertyName].Color : DirectX::XMFLOAT4{ 1, 1, 1, 1 };
                                    },
                                    [entity, propertyName = definition.Name](DirectX::XMFLOAT4 v) mutable
                                    {
                                        auto& mesh = entity.GetComponent<MeshComponent>();
                                        if (!mesh.Material)
                                            return;
                                        mesh.Material->ShaderProperties[propertyName].Color = v;
                                        SaveMaterialFromMesh(mesh);
                                    });
                            }
                            else if (definition.Type == ShaderPropertyType::Float)
                            {
                                UI::InspectorUtils::AddDragFloat(item, widgetName, definition.DisplayName,
                                    [entity, propertyName = definition.Name]() mutable
                                    {
                                        auto& mesh = entity.GetComponent<MeshComponent>();
                                        return mesh.Material ? mesh.Material->ShaderProperties[propertyName].FloatValue : 0.0f;
                                    },
                                    [entity, propertyName = definition.Name, definition](float v) mutable
                                    {
                                        auto& mesh = entity.GetComponent<MeshComponent>();
                                        if (!mesh.Material)
                                            return;
                                        if (definition.HasRange)
                                            v = std::clamp(v, definition.Min, definition.Max);
                                        mesh.Material->ShaderProperties[propertyName].FloatValue = v;
                                        SaveMaterialFromMesh(mesh);
                                    });
                            }
                            else if (definition.Type == ShaderPropertyType::Toggle)
                            {
                                auto toggleButton = new UI::Button(widgetName, definition.DisplayName + ": " + (value.BoolValue ? "On" : "Off"));
                                toggleButton->SetOnClick([entity, toggleButton, propertyName = definition.Name, label = definition.DisplayName]() mutable
                                    {
                                        auto& mesh = entity.GetComponent<MeshComponent>();
                                        if (!mesh.Material)
                                            return;
                                        ShaderPropertyValue& toggleValue = mesh.Material->ShaderProperties[propertyName];
                                        toggleValue.BoolValue = !toggleValue.BoolValue;
                                        toggleButton->SetText(label + ": " + (toggleValue.BoolValue ? "On" : "Off"));
                                        SaveMaterialFromMesh(mesh);
                                    });
                                item->AddChild(toggleButton);
                            }
                            else if (definition.Type == ShaderPropertyType::Texture2D)
                            {
                                auto textureButton = new UI::Button(widgetName,
                                    value.TexturePath.empty()
                                    ? definition.DisplayName + ": (none)"
                                    : definition.DisplayName + ": " + std::filesystem::path(value.TexturePath).filename().string());
                                textureButton->SetOnClick([entity, textureButton, propertyName = definition.Name, label = definition.DisplayName]() mutable
                                    {
                                        std::string filepath = PlatformUtils::OpenFile("Texture (*.png;*.jpg;*.jpeg;*.tga)\0*.png;*.jpg;*.jpeg;*.tga\0");
                                        if (filepath.empty())
                                            return;

                                        auto& mesh = entity.GetComponent<MeshComponent>();
                                        if (!mesh.Material)
                                            return;

                                        ShaderPropertyValue& textureValue = mesh.Material->ShaderProperties[propertyName];
                                        textureValue.TexturePath = filepath;
                                        textureValue.TextureGuid = AssetDatabase::GetGuidFromPath(filepath);
                                        SaveMaterialFromMesh(mesh);
                                        textureButton->SetText(label + ": " + std::filesystem::path(filepath).filename().string());
                                    });
                                item->AddChild(textureButton);
                            }
                        }
                    }

                    std::filesystem::path currentTexturePath(mesh.AlbedoPath);
                    std::string textureButtonText = currentTexturePath.empty()
                        ? "Albedo Texture: (none)"
                        : "Albedo Texture: " + currentTexturePath.filename().string();

                    auto btnTexture = new UI::Button("BtnChangeTexture", textureButtonText);
                    btnTexture->SetAnchorMin(0.0f, 0.0f); btnTexture->SetAnchorMax(1.0f, 0.0f);
                    btnTexture->SetOffsetMin(15.0f, 0.0f); btnTexture->SetOffsetMax(-10.0f, 28.0f);
                    btnTexture->SetOnClick([entity, btnTexture]() mutable
                        {
                            std::string filepath = PlatformUtils::OpenFile("PNG Image (*.png)\0*.png\0JPG Image (*.jpg)\0*.jpg\0");
                            if (!filepath.empty())
                            {
                                std::shared_ptr<Texture2D> newTex = std::shared_ptr<Texture2D>(Texture2D::Create(filepath));
                                auto& mesh = entity.GetComponent<MeshComponent>();
                                mesh.AlbedoMap = newTex;
                                // 인스펙터에서 직접 넣은 텍스처도 포인터만 들고 있으면 저장 후 잃어버린다.
                                // 파일 참조는 GUID와 경로를 같이 남겨 구버전 파일도 읽을 수 있게 한다.
                                mesh.AlbedoPath = filepath;
                                mesh.AlbedoAssetGuid = AssetDatabase::GetGuidFromPath(filepath);
                                btnTexture->SetText("Albedo Texture: " + std::filesystem::path(filepath).filename().string());
                                std::cout << "Texture Applied to Mesh: " << filepath << std::endl;
                            }
                        });
                    item->AddChild(btnTexture);
                    AddRemoveComponentButton<MeshComponent>(parent, item, entity, "Mesh");
                });

            // ==========================================================
            // ★ 3. Light 컴포넌트 
            // ==========================================================
            UI::InspectorRegistry::RegisterComponent<LightComponent>(
                [](UI::Widget* parent, CCEngine::Entity entity, LightComponent& light)
                {
                    auto item = new UI::InspectorItem("LightItem", "Light");
                    item->SetAnchorMin(0.0f, 0.0f); item->SetAnchorMax(1.0f, 0.0f);
                    parent->AddChild(item);

                    // 색상 (DragFloat4의 Color Block UI를 재사용)
                    UI::InspectorUtils::AddColor3(item, "LightColor", "Color",
                        [entity]() mutable { return entity.GetComponent<LightComponent>().LightColor; },
                        [entity](DirectX::XMFLOAT3 v) mutable { entity.GetComponent<LightComponent>().LightColor = v; });

                    // 밝기 (단일 Float 슬라이더)
                    UI::InspectorUtils::AddDragFloat(item, "Intensity", "Intensity",
                        [entity]() mutable { return entity.GetComponent<LightComponent>().Intensity; },
                        [entity](float v) mutable { entity.GetComponent<LightComponent>().Intensity = v; });
                    AddRemoveComponentButton<LightComponent>(parent, item, entity, "Light");
                });

            // ==========================================================
            // 4. Camera 컴포넌트
            // ==========================================================
            UI::InspectorRegistry::RegisterComponent<CameraComponent>(
                [](UI::Widget* parent, CCEngine::Entity entity, CameraComponent& camera)
                {
                    auto item = new UI::InspectorItem("CameraItem", "Camera");
                    item->SetAnchorMin(0.0f, 0.0f); item->SetAnchorMax(1.0f, 0.0f);
                    parent->AddChild(item);

                    UI::InspectorUtils::AddDragFloat(item, "FOV", "FOV",
                        [entity]() mutable { return entity.GetComponent<CameraComponent>().FOV; },
                        [entity](float v) mutable { entity.GetComponent<CameraComponent>().FOV = v; });
                    UI::InspectorUtils::AddDragFloat(item, "NearClip", "Near Clip",
                        [entity]() mutable { return entity.GetComponent<CameraComponent>().NearClip; },
                        [entity](float v) mutable { entity.GetComponent<CameraComponent>().NearClip = v; });
                    UI::InspectorUtils::AddDragFloat(item, "FarClip", "Far Clip",
                        [entity]() mutable { return entity.GetComponent<CameraComponent>().FarClip; },
                        [entity](float v) mutable { entity.GetComponent<CameraComponent>().FarClip = v; });

                    auto btnPrimary = new UI::Button("BtnPrimaryCamera", "Game View Camera: Off");
                    btnPrimary->SetAnchorMin(0.0f, 0.0f); btnPrimary->SetAnchorMax(1.0f, 0.0f);
                    btnPrimary->SetOffsetMin(15.0f, 0.0f); btnPrimary->SetOffsetMax(-10.0f, 28.0f);
                    btnPrimary->SetActive(camera.Primary);
                    btnPrimary->SetText(camera.Primary ? "Game View Camera: On" : "Game View Camera: Off");
                    btnPrimary->SetOnClick([entity, btnPrimary]() mutable
                        {
                            if (!entity || !entity.HasComponent<CameraComponent>())
                                return;

                            auto& selectedCamera = entity.GetComponent<CameraComponent>();
                            if (!selectedCamera.Primary)
                            {
                                // 게임 뷰 카메라는 씬에 하나만 존재하도록 다른 카메라를 모두 해제한다.
                                auto view = entity.GetScene()->GetRegistry().view<CameraComponent>();
                                for (auto cameraEntity : view)
                                    view.get<CameraComponent>(cameraEntity).Primary = false;
                                selectedCamera.Primary = true;
                            }

                            btnPrimary->SetActive(selectedCamera.Primary);
                            btnPrimary->SetText(selectedCamera.Primary ? "Game View Camera: On" : "Game View Camera: Off");
                        });
                    item->AddChild(btnPrimary);
                    AddRemoveComponentButton<CameraComponent>(parent, item, entity, "Camera");
                });

            UI::InspectorRegistry::RegisterComponent<SpriteRendererComponent>(
                [](UI::Widget* parent, CCEngine::Entity entity, SpriteRendererComponent& sprite)
                {
                    auto item = new UI::InspectorItem("SpriteRendererItem", "Sprite Renderer");
                    item->SetAnchorMin(0.0f, 0.0f); item->SetAnchorMax(1.0f, 0.0f);
                    parent->AddChild(item);
                    UI::InspectorUtils::AddColor4(item, "SpriteColor", "Color",
                        [entity]() mutable { return entity.GetComponent<SpriteRendererComponent>().Color; },
                        [entity](DirectX::XMFLOAT4 v) mutable { entity.GetComponent<SpriteRendererComponent>().Color = v; });
                    AddRemoveComponentButton<SpriteRendererComponent>(parent, item, entity, "SpriteRenderer");
                });

            UI::InspectorRegistry::RegisterComponent<AudioComponent>(
                [](UI::Widget* parent, CCEngine::Entity entity, AudioComponent& audio)
                {
                    auto item = new UI::InspectorItem("AudioItem", "Audio");
                    item->SetAnchorMin(0.0f, 0.0f); item->SetAnchorMax(1.0f, 0.0f);
                    parent->AddChild(item);

                    std::filesystem::path audioPath(audio.AudioPath);
                    auto clipButton = new UI::Button("BtnAudioClip", audioPath.empty() ? "Clip: None" : "Clip: " + audioPath.filename().string());
                    clipButton->SetAnchorMin(0.0f, 0.0f); clipButton->SetAnchorMax(1.0f, 0.0f);
                    clipButton->SetOffsetMin(15.0f, 0.0f); clipButton->SetOffsetMax(-10.0f, 28.0f);
                    clipButton->SetOnClick([entity, clipButton]() mutable
                        {
                            if (!entity || !entity.HasComponent<AudioComponent>())
                                return;

                            std::string filepath = PlatformUtils::OpenFile("Audio (*.wav;*.mp3;*.ogg)\0*.wav;*.mp3;*.ogg\0");
                            if (filepath.empty())
                                return;

                            auto& audio = entity.GetComponent<AudioComponent>();
                            audio.AudioPath = filepath;
                            audio.AudioAssetGuid = AssetDatabase::GetGuidFromPath(filepath);
                            clipButton->SetText("Clip: " + std::filesystem::path(filepath).filename().string());
                        });
                    item->AddChild(clipButton);

                    UI::InspectorUtils::AddDragFloat(item, "AudioVolume", "Volume",
                        [entity]() mutable { return entity.GetComponent<AudioComponent>().Volume; },
                        [entity](float v) mutable { entity.GetComponent<AudioComponent>().Volume = std::clamp(v, 0.0f, 1.0f); });
                    UI::InspectorUtils::AddDragFloat(item, "AudioPitch", "Pitch",
                        [entity]() mutable { return entity.GetComponent<AudioComponent>().Pitch; },
                        [entity](float v) mutable { entity.GetComponent<AudioComponent>().Pitch = (std::max)(0.01f, v); });

                    auto makeToggle = [item, entity](const std::string& name, const std::string& label, bool AudioComponent::* field)
                    {
                        auto button = new UI::Button(name, "");
                        button->SetAnchorMin(0.0f, 0.0f); button->SetAnchorMax(1.0f, 0.0f);
                        button->SetOffsetMin(15.0f, 0.0f); button->SetOffsetMax(-10.0f, 26.0f);
                        auto refresh = [entity, button, label, field]() mutable
                        {
                            if (!entity || !entity.HasComponent<AudioComponent>())
                                return;
                            const bool enabled = entity.GetComponent<AudioComponent>().*field;
                            button->SetActive(enabled);
                            button->SetText(label + (enabled ? ": On" : ": Off"));
                        };
                        button->SetOnClick([entity, button, label, field]() mutable
                            {
                                if (!entity || !entity.HasComponent<AudioComponent>())
                                    return;
                                auto& audio = entity.GetComponent<AudioComponent>();
                                audio.*field = !(audio.*field);
                                const bool enabled = audio.*field;
                                button->SetActive(enabled);
                                button->SetText(label + (enabled ? ": On" : ": Off"));
                            });
                        refresh();
                        item->AddChild(button);
                    };
                    makeToggle("BtnAudioEnabled", "Enabled", &AudioComponent::Enabled);
                    makeToggle("BtnAudioLoop", "Loop", &AudioComponent::Loop);
                    makeToggle("BtnAudioPlayOnStart", "Play On Start", &AudioComponent::PlayOnStart);

                    auto triggerButton = new UI::Button("BtnAudioPlayTrigger", "Play Trigger");
                    triggerButton->SetAnchorMin(0.0f, 0.0f); triggerButton->SetAnchorMax(1.0f, 0.0f);
                    triggerButton->SetOffsetMin(15.0f, 0.0f); triggerButton->SetOffsetMax(-10.0f, 26.0f);
                    triggerButton->SetOnClick([entity]() mutable
                        {
                            if (!entity || !entity.HasComponent<AudioComponent>())
                                return;
                            // 버튼은 실제 사운드를 직접 재생하지 않고 런타임 요청만 남긴다.
                            // 후속 Audio Backend는 이 플래그를 한 번 소비한 뒤 false로 되돌리면 된다.
                            entity.GetComponent<AudioComponent>().RuntimePlayRequested = true;
                        });
                    item->AddChild(triggerButton);

                    AddRemoveComponentButton<AudioComponent>(parent, item, entity, "Audio");
                });

            UI::InspectorRegistry::RegisterComponent<AnimatorComponent>(
                [](UI::Widget* parent, CCEngine::Entity entity, AnimatorComponent& animator)
                {
                    auto item = new UI::InspectorItem("AnimatorItem", "Animator");
                    item->SetAnchorMin(0.0f, 0.0f); item->SetAnchorMax(1.0f, 0.0f);
                    parent->AddChild(item);

                    std::filesystem::path controllerPath = animator.ControllerPath;
                    if (!animator.ControllerAssetGuid.empty())
                    {
                        std::filesystem::path guidPath = AssetDatabase::GetPathFromGuid(animator.ControllerAssetGuid);
                        if (!guidPath.empty())
                            controllerPath = guidPath;
                    }
                    {
                    const std::string controllerLabel = controllerPath.empty()
                        ? "None (Runtime Animator Controller)"
                        : controllerPath.filename().string();
                    std::filesystem::path avatarPath = animator.AvatarPath;
                    if (!animator.AvatarGuid.empty())
                    {
                        std::filesystem::path guidPath = AssetDatabase::GetPathFromGuid(animator.AvatarGuid);
                        if (!guidPath.empty())
                            avatarPath = guidPath;
                    }

                    auto controllerRow = new InspectorFieldRow("AnimatorControllerAsset", "Controller", controllerLabel, InspectorFieldRow::Kind::ObjectField);
                    controllerRow->SetAnchorMin(0.0f, 0.0f); controllerRow->SetAnchorMax(1.0f, 0.0f);
                    controllerRow->SetOffsetMin(15.0f, 0.0f); controllerRow->SetOffsetMax(-10.0f, 22.0f);
                    auto pickAnimatorController = [entity](InspectorFieldRow*) mutable
                        {
                            if (!entity || !entity.HasComponent<AnimatorComponent>())
                                return;
                            // Inspector는 EditorLayer의 창 구조를 직접 알지 않는다.
                            // 요청 플래그만 남기면 EditorLayer가 Material/Clip과 같은 공용 Asset Picker를 연다.
                            entity.GetComponent<AnimatorComponent>().EditorPickControllerRequested = true;
                        };
                    // 슬롯 본문은 선택창을 열지 않는다.
                    // 더블클릭은 Graph 열기, 오른쪽 o 버튼은 Controller 선택으로 역할을 분리해야
                    // 더블클릭 때 첫 클릭이 파일 선택창을 먼저 띄우는 문제가 생기지 않는다.
                    controllerRow->SetOnObjectButtonClick(pickAnimatorController);
                    controllerRow->SetOnDoubleClick([entity](InspectorFieldRow*) mutable
                        {
                            if (entity && entity.HasComponent<AnimatorComponent>())
                            {
                                const auto& anim = entity.GetComponent<AnimatorComponent>();
                                if (!anim.ControllerPath.empty() || !anim.ControllerAssetGuid.empty())
                                    entity.GetComponent<AnimatorComponent>().EditorOpenGraphRequested = true;
                            }
                        });
                    item->AddChild(controllerRow);

                    auto avatarRow = new InspectorFieldRow("AnimatorAvatarAsset", "Avatar",
                        avatarPath.empty() ? "None" : avatarPath.filename().string(),
                        InspectorFieldRow::Kind::ObjectField);
                    avatarRow->SetAnchorMin(0.0f, 0.0f); avatarRow->SetAnchorMax(1.0f, 0.0f);
                    avatarRow->SetOffsetMin(15.0f, 0.0f); avatarRow->SetOffsetMax(-10.0f, 22.0f);
                    auto pickAvatar = [entity](InspectorFieldRow*) mutable
                        {
                            if (!entity || !entity.HasComponent<AnimatorComponent>())
                                return;
                            entity.GetComponent<AnimatorComponent>().EditorPickAvatarRequested = true;
                        };
                    avatarRow->SetOnObjectButtonClick(pickAvatar);
                    avatarRow->SetOnClick([entity, avatarPath](InspectorFieldRow*) mutable
                        {
                            if (!entity || !entity.HasComponent<AnimatorComponent>() || !avatarPath.empty())
                                return;
                            entity.GetComponent<AnimatorComponent>().EditorPickAvatarRequested = true;
                        });
                    avatarRow->SetOnDoubleClick([entity](InspectorFieldRow*) mutable
                        {
                            if (!entity || !entity.HasComponent<AnimatorComponent>())
                                return;
                            auto& anim = entity.GetComponent<AnimatorComponent>();
                            if (anim.AvatarPath.empty() && anim.AvatarGuid.empty())
                                anim.EditorPickAvatarRequested = true;
                            else
                                anim.EditorOpenAvatarRequested = true;
                        });
                    item->AddChild(avatarRow);

                    auto applyRootRow = new InspectorFieldRow("AnimatorApplyRootMotion", "Apply Root Motion", "", InspectorFieldRow::Kind::Toggle);
                    applyRootRow->SetAnchorMin(0.0f, 0.0f); applyRootRow->SetAnchorMax(1.0f, 0.0f);
                    applyRootRow->SetOffsetMin(15.0f, 0.0f); applyRootRow->SetOffsetMax(-10.0f, 22.0f);
                    applyRootRow->SetChecked(animator.ApplyRootMotion);
                    applyRootRow->SetOnClick([entity](InspectorFieldRow* row) mutable
                        {
                            if (!entity || !entity.HasComponent<AnimatorComponent>())
                                return;

                            auto& anim = entity.GetComponent<AnimatorComponent>();
                            anim.ApplyRootMotion = !anim.ApplyRootMotion;
                            if (auto* state = FindInspectorActiveState(anim))
                                state->ApplyRootMotion = anim.ApplyRootMotion;
                            SaveAnimatorControllerIfAssigned(anim);
                            row->SetChecked(anim.ApplyRootMotion);
                        });
                    item->AddChild(applyRootRow);

                    auto updateModeRow = new InspectorFieldRow("AnimatorUpdateMode", "Update Mode", AnimatorUpdateModeName(animator.UpdateModeValue), InspectorFieldRow::Kind::Dropdown);
                    updateModeRow->SetAnchorMin(0.0f, 0.0f); updateModeRow->SetAnchorMax(1.0f, 0.0f);
                    updateModeRow->SetOffsetMin(15.0f, 0.0f); updateModeRow->SetOffsetMax(-10.0f, 22.0f);
                    updateModeRow->SetOnClick([entity](InspectorFieldRow* row) mutable
                        {
                            if (!entity || !entity.HasComponent<AnimatorComponent>())
                                return;

                            auto& anim = entity.GetComponent<AnimatorComponent>();
                            int next = (static_cast<int>(anim.UpdateModeValue) + 1) % 3;
                            anim.UpdateModeValue = static_cast<AnimatorComponent::UpdateMode>(next);
                            row->SetValue(AnimatorUpdateModeName(anim.UpdateModeValue));
                        });
                    item->AddChild(updateModeRow);

                    auto cullingModeRow = new InspectorFieldRow("AnimatorCullingMode", "Culling Mode", AnimatorCullingModeName(animator.CullingModeValue), InspectorFieldRow::Kind::Dropdown);
                    cullingModeRow->SetAnchorMin(0.0f, 0.0f); cullingModeRow->SetAnchorMax(1.0f, 0.0f);
                    cullingModeRow->SetOffsetMin(15.0f, 0.0f); cullingModeRow->SetOffsetMax(-10.0f, 22.0f);
                    cullingModeRow->SetOnClick([entity](InspectorFieldRow* row) mutable
                        {
                            if (!entity || !entity.HasComponent<AnimatorComponent>())
                                return;

                            auto& anim = entity.GetComponent<AnimatorComponent>();
                            int next = (static_cast<int>(anim.CullingModeValue) + 1) % 3;
                            anim.CullingModeValue = static_cast<AnimatorComponent::CullingMode>(next);
                            row->SetValue(AnimatorCullingModeName(anim.CullingModeValue));
                        });
                    item->AddChild(cullingModeRow);

                    // Unity처럼 Animator 컴포넌트는 실행에 필요한 참조와 정책만 노출한다.
                    // 상세 그래프 편집은 Asset Browser에서 Controller 에셋을 더블클릭해 연다.
                    AddRemoveComponentButton<AnimatorComponent>(parent, item, entity, "Animator");
                    return;
                    }

                    Entity controllerModelRoot = FindModelRoot(entity);
                    std::string sourceLabel = "Animation Source: (none)";
                    if (controllerModelRoot && controllerModelRoot.HasComponent<ModelComponent>())
                    {
                        const auto& model = controllerModelRoot.GetComponent<ModelComponent>();
                        std::filesystem::path sourcePath = animator.SourcePath;
                        if (!animator.SourceAssetGuid.empty())
                        {
                            std::filesystem::path guidPath = AssetDatabase::GetPathFromGuid(animator.SourceAssetGuid);
                            if (!guidPath.empty())
                                sourcePath = guidPath;
                        }
                        if (sourcePath.empty() && !model.AssetGuid.empty())
                            sourcePath = AssetDatabase::GetPathFromGuid(model.AssetGuid);
                        if (!sourcePath.empty())
                            sourceLabel = "Animation Source: " + sourcePath.filename().string();
                    }
                    auto sourceButton = new UI::Button("AnimatorSourceAsset", sourceLabel);
                    sourceButton->SetAnchorMin(0.0f, 0.0f); sourceButton->SetAnchorMax(1.0f, 0.0f);
                    sourceButton->SetOffsetMin(15.0f, 0.0f); sourceButton->SetOffsetMax(-10.0f, 28.0f);
                    item->AddChild(sourceButton);

                    std::filesystem::path sourceAvatarPath = animator.SourceAvatarPath;
                    if (!animator.SourceAvatarGuid.empty())
                    {
                        std::filesystem::path guidPath = AssetDatabase::GetPathFromGuid(animator.SourceAvatarGuid);
                        if (!guidPath.empty())
                            sourceAvatarPath = guidPath;
                    }

                    auto sourceAvatarButton = new UI::Button("AnimatorSourceAvatarAsset",
                        sourceAvatarPath.empty()
                            ? "Source Avatar: Auto / None"
                            : "Source Avatar: " + sourceAvatarPath.filename().string());
                    sourceAvatarButton->SetAnchorMin(0.0f, 0.0f); sourceAvatarButton->SetAnchorMax(1.0f, 0.0f);
                    sourceAvatarButton->SetOffsetMin(15.0f, 0.0f); sourceAvatarButton->SetOffsetMax(-10.0f, 28.0f);
                    sourceAvatarButton->SetOnClick([entity, sourceAvatarButton]() mutable
                        {
                            if (!entity || !entity.HasComponent<AnimatorComponent>())
                                return;

                            std::string filepath = PlatformUtils::OpenFile("CC Avatar (*.ccavatar)\0*.ccavatar\0");
                            if (filepath.empty() || AssetDatabase::GetAssetKind(filepath) != AssetKind::Avatar)
                                return;

                            auto& anim = entity.GetComponent<AnimatorComponent>();
                            anim.SourceAvatarPath = filepath;
                            anim.SourceAvatarGuid = AssetDatabase::GetGuidFromPath(filepath);
                            sourceAvatarButton->SetText("Source Avatar: " + std::filesystem::path(filepath).filename().string());
                        });
                    item->AddChild(sourceAvatarButton);

                    std::filesystem::path avatarPath = animator.AvatarPath;
                    if (!animator.AvatarGuid.empty())
                    {
                        std::filesystem::path guidPath = AssetDatabase::GetPathFromGuid(animator.AvatarGuid);
                        if (!guidPath.empty())
                            avatarPath = guidPath;
                    }

                    auto avatarButton = new UI::Button("AnimatorAvatarAsset",
                        avatarPath.empty()
                            ? "Target Avatar: None (assign .ccavatar)"
                            : "Target Avatar: " + avatarPath.filename().string());
                    avatarButton->SetAnchorMin(0.0f, 0.0f); avatarButton->SetAnchorMax(1.0f, 0.0f);
                    avatarButton->SetOffsetMin(15.0f, 0.0f); avatarButton->SetOffsetMax(-10.0f, 28.0f);
                    avatarButton->SetOnClick([entity, avatarButton]() mutable
                        {
                            if (!entity || !entity.HasComponent<AnimatorComponent>())
                                return;

                            std::string filepath = PlatformUtils::OpenFile("CC Avatar (*.ccavatar)\0*.ccavatar\0");
                            if (filepath.empty() || AssetDatabase::GetAssetKind(filepath) != AssetKind::Avatar)
                                return;

                            auto& anim = entity.GetComponent<AnimatorComponent>();
                            anim.AvatarPath = filepath;
                            anim.AvatarGuid = AssetDatabase::GetGuidFromPath(filepath);
                            avatarButton->SetText("Target Avatar: " + std::filesystem::path(filepath).filename().string());
                        });
                    item->AddChild(avatarButton);

                    auto retargetButton = new UI::Button("AnimatorRetargetToggle", animator.RetargetToHumanoid ? "Retarget: On" : "Retarget: Off");
                    retargetButton->SetAnchorMin(0.0f, 0.0f); retargetButton->SetAnchorMax(1.0f, 0.0f);
                    retargetButton->SetOffsetMin(15.0f, 0.0f); retargetButton->SetOffsetMax(-10.0f, 28.0f);
                    retargetButton->SetActive(animator.RetargetToHumanoid);
                    retargetButton->SetOnClick([entity, retargetButton]() mutable
                        {
                            if (!entity || !entity.HasComponent<AnimatorComponent>())
                                return;

                            auto& anim = entity.GetComponent<AnimatorComponent>();
                            anim.RetargetToHumanoid = !anim.RetargetToHumanoid;
                            retargetButton->SetActive(anim.RetargetToHumanoid);
                            retargetButton->SetText(anim.RetargetToHumanoid ? "Retarget: On" : "Retarget: Off");
                        });
                    item->AddChild(retargetButton);

                    auto rootInput = new UI::TextInput("AnimatorRootBoneInput", "Humanoid Root Bone");
                    rootInput->SetAnchorMin(0.0f, 0.0f); rootInput->SetAnchorMax(1.0f, 0.0f);
                    rootInput->SetOffsetMin(15.0f, 0.0f); rootInput->SetOffsetMax(-10.0f, 28.0f);
                    rootInput->SetText(animator.HumanoidRootBone, false);
                    rootInput->SetOnTextChanged([entity](const std::string& text) mutable
                        {
                            if (entity && entity.HasComponent<AnimatorComponent>())
                                entity.GetComponent<AnimatorComponent>().HumanoidRootBone = text.empty() ? "Hips" : text;
                        });
                    item->AddChild(rootInput);

                    auto autoRootButton = new UI::Button("AnimatorAutoRootBone", "Auto Root Bone");
                    autoRootButton->SetAnchorMin(0.0f, 0.0f); autoRootButton->SetAnchorMax(1.0f, 0.0f);
                    autoRootButton->SetOffsetMin(15.0f, 0.0f); autoRootButton->SetOffsetMax(-10.0f, 28.0f);
                    autoRootButton->SetOnClick([entity, rootInput, autoRootButton]() mutable
                        {
                            if (!entity || !entity.HasComponent<AnimatorComponent>())
                                return;

                            // 후보 점수 계산은 Hips/Pelvis/Root 같은 관례 이름을 찾기 위한 보조 단계다.
                            // 저장할 때는 원본 문자열을 그대로 써야 FBX 애니메이션 채널 이름과 정확히 맞는다.
                            const std::string detected = FindBestRootBoneCandidate(entity);
                            auto& anim = entity.GetComponent<AnimatorComponent>();
                            anim.HumanoidRootBone = detected.empty() ? "Hips" : detected;
                            rootInput->SetText(anim.HumanoidRootBone, false);
                            autoRootButton->SetText("Auto Root Bone");
                            SaveAnimatorControllerIfAssigned(anim);
                        });
                    item->AddChild(autoRootButton);

                    if (auto* activeState = FindInspectorActiveState(animator))
                    {
                        auto importSummary = new UI::Button("AnimatorRootImportSummary", "Active Root Import");
                        importSummary->SetAnchorMin(0.0f, 0.0f); importSummary->SetAnchorMax(1.0f, 0.0f);
                        importSummary->SetOffsetMin(15.0f, 0.0f); importSummary->SetOffsetMax(-10.0f, 28.0f);
                        item->AddChild(importSummary);

                        auto addImportToggle = [&](const std::string& id, const std::string& label, bool active, auto mutator)
                        {
                            auto button = new UI::Button(id, label + (active ? ": On" : ": Off"));
                            button->SetAnchorMin(0.0f, 0.0f); button->SetAnchorMax(1.0f, 0.0f);
                            button->SetOffsetMin(15.0f, 0.0f); button->SetOffsetMax(-10.0f, 28.0f);
                            button->SetActive(active);
                            button->SetOnClick([entity, button, label, mutator]() mutable
                                {
                                    if (!entity || !entity.HasComponent<AnimatorComponent>())
                                        return;

                                    auto& anim = entity.GetComponent<AnimatorComponent>();
                                    auto* state = FindInspectorActiveState(anim);
                                    if (!state)
                                        return;

                                    const bool enabled = mutator(anim, *state);
                                    SaveAnimatorControllerIfAssigned(anim);
                                    button->SetActive(enabled);
                                    button->SetText(label + (enabled ? ": On" : ": Off"));
                                });
                            item->AddChild(button);
                        };

                        addImportToggle("AnimatorApplyRootMotionImport", "Apply Root Motion", activeState->ApplyRootMotion,
                            [](AnimatorComponent& anim, AnimatorComponent::State& state)
                            {
                                state.ApplyRootMotion = !state.ApplyRootMotion;
                                anim.ApplyRootMotion = state.ApplyRootMotion;
                                return state.ApplyRootMotion;
                            });
                        addImportToggle("AnimatorBakeRootTransformImport", "Bake Root Transform", activeState->ImportSettings.BakeRootTransform,
                            [](AnimatorComponent&, AnimatorComponent::State& state)
                            {
                                state.ImportSettings.BakeRootTransform = !state.ImportSettings.BakeRootTransform;
                                return state.ImportSettings.BakeRootTransform;
                            });
                        addImportToggle("AnimatorLockRootXZImport", "Lock Root XZ", activeState->ImportSettings.LockRootPositionXZ,
                            [](AnimatorComponent&, AnimatorComponent::State& state)
                            {
                                state.ImportSettings.LockRootPositionXZ = !state.ImportSettings.LockRootPositionXZ;
                                return state.ImportSettings.LockRootPositionXZ;
                            });
                        addImportToggle("AnimatorLockRootYImport", "Lock Root Y", activeState->ImportSettings.LockRootPositionY,
                            [](AnimatorComponent&, AnimatorComponent::State& state)
                            {
                                state.ImportSettings.LockRootPositionY = !state.ImportSettings.LockRootPositionY;
                                return state.ImportSettings.LockRootPositionY;
                            });
                        addImportToggle("AnimatorLockRootRotationImport", "Lock Root Rotation", activeState->ImportSettings.LockRootRotation,
                            [](AnimatorComponent&, AnimatorComponent::State& state)
                            {
                                state.ImportSettings.LockRootRotation = !state.ImportSettings.LockRootRotation;
                                return state.ImportSettings.LockRootRotation;
                            });
                    }

                    if (!avatarPath.empty() && controllerModelRoot && controllerModelRoot.HasComponent<ModelComponent>())
                    {
                        AvatarAsset avatar;
                        if (AvatarAsset::LoadFromFile(avatarPath, avatar))
                        {
                            const AvatarValidationResult validation = AvatarAsset::ValidateAgainstModel(avatar, controllerModelRoot.GetComponent<ModelComponent>());
                            auto validationButton = new UI::Button("AnimatorAvatarValidation",
                                validation.Valid
                                    ? "Avatar Valid: " + std::to_string(validation.RequiredMapped) + " required bones"
                                    : "Avatar Missing: " + std::to_string(validation.RequiredMissing) + " required bones");
                            validationButton->SetAnchorMin(0.0f, 0.0f); validationButton->SetAnchorMax(1.0f, 0.0f);
                            validationButton->SetOffsetMin(15.0f, 0.0f); validationButton->SetOffsetMax(-10.0f, 28.0f);
                            validationButton->SetActive(validation.Valid);
                            item->AddChild(validationButton);

                            int shownIssues = 0;
                            for (const AvatarValidationIssue& issue : validation.Issues)
                            {
                                if (!issue.Error || shownIssues >= 3)
                                    continue;

                                // 검증 결과는 너무 길게 뿌리지 않는다.
                                // 인스펙터에서는 첫 문제 몇 개만 보여주고, 실제 수정은 Avatar 에셋 전용 UI에서 확장한다.
                                auto issueButton = new UI::Button("AnimatorAvatarIssue" + std::to_string(shownIssues),
                                    "Missing: " + issue.HumanBone + " -> " + issue.SourceBone);
                                issueButton->SetAnchorMin(0.0f, 0.0f); issueButton->SetAnchorMax(1.0f, 0.0f);
                                issueButton->SetOffsetMin(15.0f, 0.0f); issueButton->SetOffsetMax(-10.0f, 28.0f);
                                item->AddChild(issueButton);
                                ++shownIssues;
                            }
                        }
                    }

                    auto assignControllerButton = new UI::Button("AnimatorAssignController", "Assign Controller...");
                    assignControllerButton->SetAnchorMin(0.0f, 0.0f); assignControllerButton->SetAnchorMax(1.0f, 0.0f);
                    assignControllerButton->SetOffsetMin(15.0f, 0.0f); assignControllerButton->SetOffsetMax(-10.0f, 28.0f);
                    assignControllerButton->SetOnClick([entity]() mutable
                        {
                            if (!entity || !entity.HasComponent<AnimatorComponent>())
                                return;

                            std::string filepath = PlatformUtils::OpenFile("CC Animator Controller (*.ccanimcontroller)\0*.ccanimcontroller\0");
                            if (filepath.empty() || AssetDatabase::GetAssetKind(filepath) != AssetKind::AnimatorController)
                                return;

                            auto& anim = entity.GetComponent<AnimatorComponent>();
                            anim.ControllerPath = filepath;
                            anim.ControllerAssetGuid = AssetDatabase::GetGuidFromPath(filepath);
                            AnimatorControllerAsset::LoadFromFile(filepath, anim);
                        });
                    item->AddChild(assignControllerButton);

                    auto openGraphButton = new UI::Button("AnimatorOpenGraph", "Open Animator Graph");
                    openGraphButton->SetAnchorMin(0.0f, 0.0f); openGraphButton->SetAnchorMax(1.0f, 0.0f);
                    openGraphButton->SetOffsetMin(15.0f, 0.0f); openGraphButton->SetOffsetMax(-10.0f, 28.0f);
                    openGraphButton->SetOnClick([entity]() mutable
                        {
                            if (entity && entity.HasComponent<AnimatorComponent>())
                                entity.GetComponent<AnimatorComponent>().EditorOpenGraphRequested = true;
                        });
                    item->AddChild(openGraphButton);

                    const std::string stateSummary = animator.States.empty()
                        ? "States: 0"
                        : "States: " + std::to_string(animator.States.size()) + " / Entry: " +
                            animator.States[std::clamp(animator.EntryStateIndex, 0, static_cast<int>(animator.States.size() - 1))].Name;
                    auto summaryButton = new UI::Button("AnimatorSummary", stateSummary);
                    summaryButton->SetAnchorMin(0.0f, 0.0f); summaryButton->SetAnchorMax(1.0f, 0.0f);
                    summaryButton->SetOffsetMin(15.0f, 0.0f); summaryButton->SetOffsetMax(-10.0f, 28.0f);
                    item->AddChild(summaryButton);

                    auto autoPlayButtonSimple = new UI::Button("AnimatorAutoPlaySimple", animator.AutoPlay ? "Auto Play: On" : "Auto Play: Off");
                    autoPlayButtonSimple->SetAnchorMin(0.0f, 0.0f); autoPlayButtonSimple->SetAnchorMax(1.0f, 0.0f);
                    autoPlayButtonSimple->SetOffsetMin(15.0f, 0.0f); autoPlayButtonSimple->SetOffsetMax(-10.0f, 28.0f);
                    autoPlayButtonSimple->SetActive(animator.AutoPlay);
                    autoPlayButtonSimple->SetOnClick([entity, autoPlayButtonSimple]() mutable
                        {
                            if (!entity || !entity.HasComponent<AnimatorComponent>())
                                return;
                            auto& anim = entity.GetComponent<AnimatorComponent>();
                            anim.AutoPlay = !anim.AutoPlay;
                            autoPlayButtonSimple->SetActive(anim.AutoPlay);
                            autoPlayButtonSimple->SetText(anim.AutoPlay ? "Auto Play: On" : "Auto Play: Off");
                        });
                    item->AddChild(autoPlayButtonSimple);

                    // Animator Component는 실행 옵션과 컨트롤러 참조만 보여준다.
                    // 상태/파라미터/노드 위치 편집은 별도 Animator Graph 창에서 다뤄 인스펙터가 비대해지는 것을 막는다.
                    AddRemoveComponentButton<AnimatorComponent>(parent, item, entity, "Animator");
                    return;

                    Entity modelRoot = FindModelRoot(entity);
                    if (!modelRoot || !modelRoot.HasComponent<ModelComponent>())
                    {
                        auto missing = new UI::Button("AnimatorMissingModel", "Model required");
                        missing->SetAnchorMin(0.0f, 0.0f); missing->SetAnchorMax(1.0f, 0.0f);
                        missing->SetOffsetMin(15.0f, 0.0f); missing->SetOffsetMax(-10.0f, 28.0f);
                        item->AddChild(missing);
                        AddRemoveComponentButton<AnimatorComponent>(parent, item, entity, "Animator");
                        return;
                    }

                    auto& model = modelRoot.GetComponent<ModelComponent>();
                    std::string sourcePath = ResolveAnimationSourcePath(animator, model);
                    std::vector<AnimationClipInfo> clips;
                    if (!sourcePath.empty() && std::filesystem::exists(sourcePath))
                        clips = AnimationClip::InspectClips(sourcePath);

                    if (clips.empty())
                    {
                        auto noClips = new UI::Button("AnimatorNoClips", "No animation clips");
                        noClips->SetAnchorMin(0.0f, 0.0f); noClips->SetAnchorMax(1.0f, 0.0f);
                        noClips->SetOffsetMin(15.0f, 0.0f); noClips->SetOffsetMax(-10.0f, 28.0f);
                        item->AddChild(noClips);
                        AddRemoveComponentButton<AnimatorComponent>(parent, item, entity, "Animator");
                        return;
                    }

                    animator.SelectedClipIndex = std::clamp(animator.SelectedClipIndex, 0, static_cast<int>(clips.size() - 1));
                    animator.SelectedClipName = clips[animator.SelectedClipIndex].Name;

                    auto clipButton = new UI::Button("AnimatorClip", "Source Clip: " + animator.SelectedClipName);
                    clipButton->SetAnchorMin(0.0f, 0.0f); clipButton->SetAnchorMax(1.0f, 0.0f);
                    clipButton->SetOffsetMin(15.0f, 0.0f); clipButton->SetOffsetMax(-10.0f, 28.0f);
                    clipButton->SetOnClick([entity, clipButton, clips]() mutable
                        {
                            if (!entity || !entity.HasComponent<AnimatorComponent>())
                                return;

                            auto& anim = entity.GetComponent<AnimatorComponent>();
                            anim.SelectedClipIndex = (anim.SelectedClipIndex + 1) % static_cast<int>(clips.size());
                            anim.SelectedClipName = clips[anim.SelectedClipIndex].Name;
                            anim.RuntimeClip.reset();
                            anim.RuntimeClipKey.clear();
                            // 클립을 바꾸면 기존 재생 위치가 다른 클립 시간에 남지 않도록 멈춘다.
                            anim.AnimPlayer.StopAnimation();
                            anim.IsPlaying = false;
                            clipButton->SetText("Source Clip: " + anim.SelectedClipName);
                        });
                    item->AddChild(clipButton);

                    auto addStateButton = new UI::Button("AnimatorAddState", "Add State From Source Clip");
                    addStateButton->SetAnchorMin(0.0f, 0.0f); addStateButton->SetAnchorMax(1.0f, 0.0f);
                    addStateButton->SetOffsetMin(15.0f, 0.0f); addStateButton->SetOffsetMax(-10.0f, 28.0f);
                    addStateButton->SetOnClick([entity, clips]() mutable
                        {
                            if (!entity || !entity.HasComponent<AnimatorComponent>())
                                return;

                            auto& anim = entity.GetComponent<AnimatorComponent>();
                            anim.SelectedClipIndex = std::clamp(anim.SelectedClipIndex, 0, static_cast<int>(clips.size() - 1));

                            AnimatorComponent::State state;
                            state.Name = clips[anim.SelectedClipIndex].Name.empty()
                                ? ("State " + std::to_string(anim.States.size() + 1))
                                : clips[anim.SelectedClipIndex].Name;
                            state.ClipIndex = anim.SelectedClipIndex;
                            state.Loop = true;
                            state.Speed = 1.0f;
                            state.WriteDefaults = true;
                            anim.States.push_back(state);

                            // 엔진이 기본 상태를 몰래 만들지 않고, 사용자가 처음 추가한 상태를 Entry로 삼는다.
                            // 이후에는 Entry State 버튼으로 시작 상태를 직접 바꿀 수 있다.
                            if (anim.ActiveStateIndex < 0)
                                anim.ActiveStateIndex = 0;
                            if (anim.EntryStateIndex < 0)
                                anim.EntryStateIndex = 0;

                            anim.RuntimeClip.reset();
                            anim.RuntimeClipKey.clear();
                            anim.AnimPlayer.StopAnimation();
                            anim.IsPlaying = false;
                        });
                    item->AddChild(addStateButton);

                    if (animator.States.empty())
                    {
                        auto emptyState = new UI::Button("AnimatorNoStates", "State Machine: empty. Add states manually.");
                        emptyState->SetAnchorMin(0.0f, 0.0f); emptyState->SetAnchorMax(1.0f, 0.0f);
                        emptyState->SetOffsetMin(15.0f, 0.0f); emptyState->SetOffsetMax(-10.0f, 28.0f);
                        item->AddChild(emptyState);
                        AddRemoveComponentButton<AnimatorComponent>(parent, item, entity, "Animator");
                        return;
                    }

                    animator.ActiveStateIndex = std::clamp(animator.ActiveStateIndex, 0, static_cast<int>(animator.States.size() - 1));
                    animator.EntryStateIndex = std::clamp(animator.EntryStateIndex, 0, static_cast<int>(animator.States.size() - 1));
                    auto& activeState = animator.States[animator.ActiveStateIndex];
                    activeState.ClipIndex = std::clamp(activeState.ClipIndex, 0, static_cast<int>(clips.size() - 1));

                    auto activeStateButton = new UI::Button("AnimatorActiveState", "Active State: " + activeState.Name);
                    activeStateButton->SetAnchorMin(0.0f, 0.0f); activeStateButton->SetAnchorMax(1.0f, 0.0f);
                    activeStateButton->SetOffsetMin(15.0f, 0.0f); activeStateButton->SetOffsetMax(-10.0f, 28.0f);
                    activeStateButton->SetOnClick([entity, activeStateButton]() mutable
                        {
                            if (!entity || !entity.HasComponent<AnimatorComponent>())
                                return;
                            auto& anim = entity.GetComponent<AnimatorComponent>();
                            if (anim.States.empty())
                                return;

                            anim.ActiveStateIndex = (anim.ActiveStateIndex + 1) % static_cast<int>(anim.States.size());
                            auto& state = anim.States[anim.ActiveStateIndex];
                            anim.SelectedClipIndex = state.ClipIndex;
                            anim.Loop = state.Loop;
                            anim.Speed = state.Speed;
                            anim.RuntimeClip.reset();
                            anim.RuntimeClipKey.clear();
                            anim.AnimPlayer.StopAnimation();
                            anim.IsPlaying = false;
                            activeStateButton->SetText("Active State: " + state.Name);
                        });
                    item->AddChild(activeStateButton);

                    auto entryButton = new UI::Button("AnimatorEntryState", "Entry State: " + animator.States[animator.EntryStateIndex].Name);
                    entryButton->SetAnchorMin(0.0f, 0.0f); entryButton->SetAnchorMax(1.0f, 0.0f);
                    entryButton->SetOffsetMin(15.0f, 0.0f); entryButton->SetOffsetMax(-10.0f, 28.0f);
                    entryButton->SetOnClick([entity, entryButton]() mutable
                        {
                            if (!entity || !entity.HasComponent<AnimatorComponent>())
                                return;
                            auto& anim = entity.GetComponent<AnimatorComponent>();
                            if (anim.States.empty())
                                return;

                            anim.EntryStateIndex = anim.ActiveStateIndex;
                            entryButton->SetText("Entry State: " + anim.States[anim.EntryStateIndex].Name);
                        });
                    item->AddChild(entryButton);

                    auto stateName = new UI::TextInput("AnimatorStateName", "State Name");
                    stateName->SetText(activeState.Name, false);
                    stateName->SetAnchorMin(0.0f, 0.0f); stateName->SetAnchorMax(1.0f, 0.0f);
                    stateName->SetOffsetMin(15.0f, 0.0f); stateName->SetOffsetMax(-10.0f, 28.0f);
                    stateName->SetOnTextChanged([entity](const std::string& text) mutable
                        {
                            if (!entity || !entity.HasComponent<AnimatorComponent>())
                                return;
                            auto& anim = entity.GetComponent<AnimatorComponent>();
                            if (anim.States.empty())
                                return;

                            int index = std::clamp(anim.ActiveStateIndex, 0, static_cast<int>(anim.States.size() - 1));
                            anim.States[index].Name = text.empty() ? "State" : text;
                        });
                    item->AddChild(stateName);

                    auto stateClipButton = new UI::Button("AnimatorStateClip", "State Clip: " + clips[activeState.ClipIndex].Name);
                    stateClipButton->SetAnchorMin(0.0f, 0.0f); stateClipButton->SetAnchorMax(1.0f, 0.0f);
                    stateClipButton->SetOffsetMin(15.0f, 0.0f); stateClipButton->SetOffsetMax(-10.0f, 28.0f);
                    stateClipButton->SetOnClick([entity, stateClipButton, clips]() mutable
                        {
                            if (!entity || !entity.HasComponent<AnimatorComponent>())
                                return;
                            auto& anim = entity.GetComponent<AnimatorComponent>();
                            if (anim.States.empty())
                                return;

                            int stateIndex = std::clamp(anim.ActiveStateIndex, 0, static_cast<int>(anim.States.size() - 1));
                            auto& state = anim.States[stateIndex];
                            state.ClipIndex = (state.ClipIndex + 1) % static_cast<int>(clips.size());
                            anim.SelectedClipIndex = state.ClipIndex;
                            anim.RuntimeClip.reset();
                            anim.RuntimeClipKey.clear();
                            anim.AnimPlayer.StopAnimation();
                            anim.IsPlaying = false;
                            stateClipButton->SetText("State Clip: " + clips[state.ClipIndex].Name);
                        });
                    item->AddChild(stateClipButton);

                    auto stateLoopButton = new UI::Button("AnimatorStateLoop", activeState.Loop ? "State Loop: On" : "State Loop: Off");
                    stateLoopButton->SetAnchorMin(0.0f, 0.0f); stateLoopButton->SetAnchorMax(1.0f, 0.0f);
                    stateLoopButton->SetOffsetMin(15.0f, 0.0f); stateLoopButton->SetOffsetMax(-10.0f, 28.0f);
                    stateLoopButton->SetOnClick([entity, stateLoopButton]() mutable
                        {
                            if (!entity || !entity.HasComponent<AnimatorComponent>())
                                return;
                            auto& anim = entity.GetComponent<AnimatorComponent>();
                            if (anim.States.empty())
                                return;

                            int stateIndex = std::clamp(anim.ActiveStateIndex, 0, static_cast<int>(anim.States.size() - 1));
                            auto& state = anim.States[stateIndex];
                            state.Loop = !state.Loop;
                            anim.Loop = state.Loop;
                            stateLoopButton->SetText(state.Loop ? "State Loop: On" : "State Loop: Off");
                        });
                    item->AddChild(stateLoopButton);

                    auto writeDefaultsButton = new UI::Button("AnimatorStateWriteDefaults", activeState.WriteDefaults ? "Write Defaults: On" : "Write Defaults: Off");
                    writeDefaultsButton->SetAnchorMin(0.0f, 0.0f); writeDefaultsButton->SetAnchorMax(1.0f, 0.0f);
                    writeDefaultsButton->SetOffsetMin(15.0f, 0.0f); writeDefaultsButton->SetOffsetMax(-10.0f, 28.0f);
                    writeDefaultsButton->SetOnClick([entity, writeDefaultsButton]() mutable
                        {
                            if (!entity || !entity.HasComponent<AnimatorComponent>())
                                return;
                            auto& anim = entity.GetComponent<AnimatorComponent>();
                            if (anim.States.empty())
                                return;

                            int stateIndex = std::clamp(anim.ActiveStateIndex, 0, static_cast<int>(anim.States.size() - 1));
                            auto& state = anim.States[stateIndex];
                            state.WriteDefaults = !state.WriteDefaults;
                            // 이 값은 상태가 클립에 없는 본을 기본 포즈로 되돌릴지, 직전 포즈로 둘지 정한다.
                            // 상체/손 애니메이션처럼 일부 본만 움직이는 클립에서는 Off가 필요하다.
                            anim.AnimPlayer.SetWriteDefaults(state.WriteDefaults);
                            writeDefaultsButton->SetText(state.WriteDefaults ? "Write Defaults: On" : "Write Defaults: Off");
                        });
                    item->AddChild(writeDefaultsButton);

                    UI::InspectorUtils::AddDragFloat(item, "AnimatorStateSpeed", "State Speed",
                        [entity]() mutable
                        {
                            auto& anim = entity.GetComponent<AnimatorComponent>();
                            if (anim.States.empty())
                                return 1.0f;
                            int index = std::clamp(anim.ActiveStateIndex, 0, static_cast<int>(anim.States.size() - 1));
                            return anim.States[index].Speed;
                        },
                        [entity](float v) mutable
                        {
                            auto& anim = entity.GetComponent<AnimatorComponent>();
                            if (anim.States.empty())
                                return;
                            int index = std::clamp(anim.ActiveStateIndex, 0, static_cast<int>(anim.States.size() - 1));
                            anim.States[index].Speed = std::max(0.0f, v);
                            anim.Speed = anim.States[index].Speed;
                        });

                    auto playButton = new UI::Button("AnimatorPreview", animator.IsPlaying ? "Preview Active State: Playing" : "Preview Active State: Stopped");
                    playButton->SetAnchorMin(0.0f, 0.0f); playButton->SetAnchorMax(1.0f, 0.0f);
                    playButton->SetOffsetMin(15.0f, 0.0f); playButton->SetOffsetMax(-10.0f, 28.0f);
                    playButton->SetOnClick([entity, playButton]() mutable
                        {
                            if (!entity || !entity.HasComponent<AnimatorComponent>())
                                return;

                            auto& anim = entity.GetComponent<AnimatorComponent>();
                            anim.PreviewInEdit = true;
                            anim.IsPlaying = !anim.IsPlaying;
                            if (!anim.IsPlaying)
                                anim.AnimPlayer.StopAnimation();
                            playButton->SetText(anim.IsPlaying ? "Preview Active State: Playing" : "Preview Active State: Stopped");
                        });
                    item->AddChild(playButton);

                    auto autoPlayButton = new UI::Button("AnimatorAutoPlay", animator.AutoPlay ? "Auto Play: On" : "Auto Play: Off");
                    autoPlayButton->SetAnchorMin(0.0f, 0.0f); autoPlayButton->SetAnchorMax(1.0f, 0.0f);
                    autoPlayButton->SetOffsetMin(15.0f, 0.0f); autoPlayButton->SetOffsetMax(-10.0f, 28.0f);
                    autoPlayButton->SetOnClick([entity, autoPlayButton]() mutable
                        {
                            if (!entity || !entity.HasComponent<AnimatorComponent>())
                                return;
                            auto& anim = entity.GetComponent<AnimatorComponent>();
                            anim.AutoPlay = !anim.AutoPlay;
                            autoPlayButton->SetText(anim.AutoPlay ? "Auto Play: On" : "Auto Play: Off");
                        });
                    item->AddChild(autoPlayButton);

                    auto deleteStateButton = new UI::Button("AnimatorDeleteState", "Delete Active State");
                    deleteStateButton->SetAnchorMin(0.0f, 0.0f); deleteStateButton->SetAnchorMax(1.0f, 0.0f);
                    deleteStateButton->SetOffsetMin(15.0f, 0.0f); deleteStateButton->SetOffsetMax(-10.0f, 28.0f);
                    deleteStateButton->SetNormalColor({ 0.34f, 0.08f, 0.10f, 1.0f });
                    deleteStateButton->SetHoverColor({ 0.45f, 0.11f, 0.13f, 1.0f });
                    deleteStateButton->SetOnClick([entity]() mutable
                        {
                            if (!entity || !entity.HasComponent<AnimatorComponent>())
                                return;
                            auto& anim = entity.GetComponent<AnimatorComponent>();
                            if (anim.States.empty())
                                return;

                            int index = std::clamp(anim.ActiveStateIndex, 0, static_cast<int>(anim.States.size() - 1));
                            anim.States.erase(anim.States.begin() + index);
                            if (anim.States.empty())
                            {
                                anim.ActiveStateIndex = -1;
                                anim.EntryStateIndex = -1;
                            }
                            else
                            {
                                anim.ActiveStateIndex = std::clamp(index, 0, static_cast<int>(anim.States.size() - 1));
                                anim.EntryStateIndex = std::clamp(anim.EntryStateIndex, 0, static_cast<int>(anim.States.size() - 1));
                            }
                            anim.RuntimeClip.reset();
                            anim.RuntimeClipKey.clear();
                            anim.AnimPlayer.StopAnimation();
                            anim.IsPlaying = false;
                        });
                    item->AddChild(deleteStateButton);

                    AddRemoveComponentButton<AnimatorComponent>(parent, item, entity, "Animator");
                });

            UI::InspectorRegistry::RegisterComponent<Rigidbody2DComponent>(
                [](UI::Widget* parent, CCEngine::Entity entity, Rigidbody2DComponent& rigidbody)
                {
                    auto item = new UI::InspectorItem("Rigidbody2DItem", "Rigidbody 2D");
                    item->SetAnchorMin(0.0f, 0.0f); item->SetAnchorMax(1.0f, 0.0f);
                    parent->AddChild(item);

                    auto bodyTypeText = [](Rigidbody2DComponent::BodyType type)
                        {
                            switch (type)
                            {
                                case Rigidbody2DComponent::BodyType::Dynamic: return "Body Type: Dynamic";
                                case Rigidbody2DComponent::BodyType::Kinematic: return "Body Type: Kinematic";
                                default: return "Body Type: Static";
                            }
                        };
                    auto btnBodyType = new UI::Button("BtnBodyType", bodyTypeText(rigidbody.Type));
                    btnBodyType->SetOnClick([entity, btnBodyType, bodyTypeText]() mutable
                        {
                            auto& rb = entity.GetComponent<Rigidbody2DComponent>();
                            rb.Type = static_cast<Rigidbody2DComponent::BodyType>(((int)rb.Type + 1) % 3);
                            btnBodyType->SetText(bodyTypeText(rb.Type));
                        });
                    item->AddChild(btnBodyType);

                    auto btnFixedRotation = new UI::Button("BtnFixedRotation",
                        rigidbody.FixedRotation ? "Fixed Rotation: On" : "Fixed Rotation: Off");
                    btnFixedRotation->SetActive(rigidbody.FixedRotation);
                    btnFixedRotation->SetOnClick([entity, btnFixedRotation]() mutable
                        {
                            auto& rb = entity.GetComponent<Rigidbody2DComponent>();
                            rb.FixedRotation = !rb.FixedRotation;
                            btnFixedRotation->SetActive(rb.FixedRotation);
                            btnFixedRotation->SetText(rb.FixedRotation ? "Fixed Rotation: On" : "Fixed Rotation: Off");
                        });
                    item->AddChild(btnFixedRotation);
                    AddRemoveComponentButton<Rigidbody2DComponent>(parent, item, entity, "Rigidbody2D");
                });

            UI::InspectorRegistry::RegisterComponent<BoxCollider2DComponent>(
                [](UI::Widget* parent, CCEngine::Entity entity, BoxCollider2DComponent& collider)
                {
                    auto item = new UI::InspectorItem("BoxCollider2DItem", "Box Collider 2D");
                    item->SetAnchorMin(0.0f, 0.0f); item->SetAnchorMax(1.0f, 0.0f);
                    parent->AddChild(item);
                    UI::InspectorUtils::AddDragFloat(item, "Density", "Density",
                        [entity]() mutable { return entity.GetComponent<BoxCollider2DComponent>().Density; },
                        [entity](float v) mutable { entity.GetComponent<BoxCollider2DComponent>().Density = v; });
                    UI::InspectorUtils::AddDragFloat(item, "Friction", "Friction",
                        [entity]() mutable { return entity.GetComponent<BoxCollider2DComponent>().Friction; },
                        [entity](float v) mutable { entity.GetComponent<BoxCollider2DComponent>().Friction = v; });
                    UI::InspectorUtils::AddDragFloat(item, "Restitution", "Restitution",
                        [entity]() mutable { return entity.GetComponent<BoxCollider2DComponent>().Restitution; },
                        [entity](float v) mutable { entity.GetComponent<BoxCollider2DComponent>().Restitution = v; });

                    auto btnTrigger = new UI::Button("BoxCollider2DTrigger", collider.IsTrigger ? "Is Trigger: On" : "Is Trigger: Off");
                    btnTrigger->SetActive(collider.IsTrigger);
                    btnTrigger->SetOnClick([entity, btnTrigger]() mutable
                        {
                            auto& current = entity.GetComponent<BoxCollider2DComponent>();
                            current.IsTrigger = !current.IsTrigger;
                            btnTrigger->SetActive(current.IsTrigger);
                            btnTrigger->SetText(current.IsTrigger ? "Is Trigger: On" : "Is Trigger: Off");
                        });
                    item->AddChild(btnTrigger);

                    AddRemoveComponentButton<BoxCollider2DComponent>(parent, item, entity, "BoxCollider2D");
                });

            UI::InspectorRegistry::RegisterComponent<BoxCollider3DComponent>(
                [](UI::Widget* parent, CCEngine::Entity entity, BoxCollider3DComponent& collider)
                {
                    auto item = new UI::InspectorItem("BoxCollider3DItem", "Box Collider 3D");
                    item->SetAnchorMin(0.0f, 0.0f); item->SetAnchorMax(1.0f, 0.0f);
                    parent->AddChild(item);

                    // 3D 충돌체는 Transform과 별도로 로컬 Offset/Size를 가진다.
                    // 이렇게 분리해야 메시 크기는 그대로 두고 충돌 범위만 보정할 수 있다.
                    UI::InspectorUtils::AddDragFloat3(item, "BoxCollider3DOffset", "Offset",
                        [entity]() mutable { return entity.GetComponent<BoxCollider3DComponent>().Offset; },
                        [entity](DirectX::XMFLOAT3 v) mutable { entity.GetComponent<BoxCollider3DComponent>().Offset = v; });
                    UI::InspectorUtils::AddDragFloat3(item, "BoxCollider3DSize", "Size",
                        [entity]() mutable { return entity.GetComponent<BoxCollider3DComponent>().Size; },
                        [entity](DirectX::XMFLOAT3 v) mutable { entity.GetComponent<BoxCollider3DComponent>().Size = v; });

                    AddColliderEditButton(parent, item, entity, UI::InspectorPanel::ColliderEditShape::Box);
                    AddColliderSnapControls(parent, item, entity, UI::InspectorPanel::ColliderEditShape::Box, "BoxCollider3D");

                    auto btnTrigger = new UI::Button("BoxCollider3DTrigger", collider.IsTrigger ? "Is Trigger: On" : "Is Trigger: Off");
                    btnTrigger->SetActive(collider.IsTrigger);
                    btnTrigger->SetOnClick([entity, btnTrigger]() mutable
                        {
                            auto& current = entity.GetComponent<BoxCollider3DComponent>();
                            current.IsTrigger = !current.IsTrigger;
                            btnTrigger->SetActive(current.IsTrigger);
                            btnTrigger->SetText(current.IsTrigger ? "Is Trigger: On" : "Is Trigger: Off");
                        });
                    item->AddChild(btnTrigger);
                    AddRemoveComponentButton<BoxCollider3DComponent>(parent, item, entity, "BoxCollider3D");
                });

            UI::InspectorRegistry::RegisterComponent<SphereCollider3DComponent>(
                [](UI::Widget* parent, CCEngine::Entity entity, SphereCollider3DComponent& collider)
                {
                    auto item = new UI::InspectorItem("SphereCollider3DItem", "Sphere Collider 3D");
                    item->SetAnchorMin(0.0f, 0.0f); item->SetAnchorMax(1.0f, 0.0f);
                    parent->AddChild(item);

                    UI::InspectorUtils::AddDragFloat3(item, "SphereCollider3DOffset", "Offset",
                        [entity]() mutable { return entity.GetComponent<SphereCollider3DComponent>().Offset; },
                        [entity](DirectX::XMFLOAT3 v) mutable { entity.GetComponent<SphereCollider3DComponent>().Offset = v; });
                    UI::InspectorUtils::AddDragFloat(item, "SphereCollider3DRadius", "Radius",
                        [entity]() mutable { return entity.GetComponent<SphereCollider3DComponent>().Radius; },
                        [entity](float v) mutable { entity.GetComponent<SphereCollider3DComponent>().Radius = std::max(0.01f, v); });

                    AddColliderEditButton(parent, item, entity, UI::InspectorPanel::ColliderEditShape::Sphere);
                    AddColliderSnapControls(parent, item, entity, UI::InspectorPanel::ColliderEditShape::Sphere, "SphereCollider3D");

                    auto btnTrigger = new UI::Button("SphereCollider3DTrigger", collider.IsTrigger ? "Is Trigger: On" : "Is Trigger: Off");
                    btnTrigger->SetActive(collider.IsTrigger);
                    btnTrigger->SetOnClick([entity, btnTrigger]() mutable
                        {
                            auto& current = entity.GetComponent<SphereCollider3DComponent>();
                            current.IsTrigger = !current.IsTrigger;
                            btnTrigger->SetActive(current.IsTrigger);
                            btnTrigger->SetText(current.IsTrigger ? "Is Trigger: On" : "Is Trigger: Off");
                        });
                    item->AddChild(btnTrigger);
                    AddRemoveComponentButton<SphereCollider3DComponent>(parent, item, entity, "SphereCollider3D");
                });

            UI::InspectorRegistry::RegisterComponent<CylinderCollider3DComponent>(
                [](UI::Widget* parent, CCEngine::Entity entity, CylinderCollider3DComponent& collider)
                {
                    auto item = new UI::InspectorItem("CylinderCollider3DItem", "Cylinder Collider 3D");
                    item->SetAnchorMin(0.0f, 0.0f); item->SetAnchorMax(1.0f, 0.0f);
                    parent->AddChild(item);

                    UI::InspectorUtils::AddDragFloat3(item, "CylinderCollider3DOffset", "Offset",
                        [entity]() mutable { return entity.GetComponent<CylinderCollider3DComponent>().Offset; },
                        [entity](DirectX::XMFLOAT3 v) mutable { entity.GetComponent<CylinderCollider3DComponent>().Offset = v; });
                    UI::InspectorUtils::AddDragFloat(item, "CylinderCollider3DRadius", "Radius",
                        [entity]() mutable { return entity.GetComponent<CylinderCollider3DComponent>().Radius; },
                        [entity](float v) mutable { entity.GetComponent<CylinderCollider3DComponent>().Radius = std::max(0.01f, v); });
                    UI::InspectorUtils::AddDragFloat(item, "CylinderCollider3DHeight", "Height",
                        [entity]() mutable { return entity.GetComponent<CylinderCollider3DComponent>().Height; },
                        [entity](float v) mutable { entity.GetComponent<CylinderCollider3DComponent>().Height = std::max(0.01f, v); });

                    AddColliderEditButton(parent, item, entity, UI::InspectorPanel::ColliderEditShape::Cylinder);
                    AddColliderSnapControls(parent, item, entity, UI::InspectorPanel::ColliderEditShape::Cylinder, "CylinderCollider3D");

                    auto btnTrigger = new UI::Button("CylinderCollider3DTrigger", collider.IsTrigger ? "Is Trigger: On" : "Is Trigger: Off");
                    btnTrigger->SetActive(collider.IsTrigger);
                    btnTrigger->SetOnClick([entity, btnTrigger]() mutable
                        {
                            auto& current = entity.GetComponent<CylinderCollider3DComponent>();
                            current.IsTrigger = !current.IsTrigger;
                            btnTrigger->SetActive(current.IsTrigger);
                            btnTrigger->SetText(current.IsTrigger ? "Is Trigger: On" : "Is Trigger: Off");
                        });
                    item->AddChild(btnTrigger);
                    AddRemoveComponentButton<CylinderCollider3DComponent>(parent, item, entity, "CylinderCollider3D");
                });

            UI::InspectorRegistry::RegisterComponent<MeshCollider3DComponent>(
                [](UI::Widget* parent, CCEngine::Entity entity, MeshCollider3DComponent& collider)
                {
                    auto item = new UI::InspectorItem("MeshCollider3DItem", "Mesh Collider 3D");
                    item->SetAnchorMin(0.0f, 0.0f); item->SetAnchorMax(1.0f, 0.0f);
                    parent->AddChild(item);

                    UI::InspectorUtils::AddDragFloat3(item, "MeshCollider3DOffset", "Offset",
                        [entity]() mutable { return entity.GetComponent<MeshCollider3DComponent>().Offset; },
                        [entity](DirectX::XMFLOAT3 v) mutable { entity.GetComponent<MeshCollider3DComponent>().Offset = v; });
                    UI::InspectorUtils::AddDragFloat3(item, "MeshCollider3DSize", "Bounds Size",
                        [entity]() mutable { return entity.GetComponent<MeshCollider3DComponent>().Size; },
                        [entity](DirectX::XMFLOAT3 v) mutable { entity.GetComponent<MeshCollider3DComponent>().Size = v; });

                    auto btnConvex = new UI::Button("MeshCollider3DConvex", collider.Convex ? "Convex: On" : "Convex: Off");
                    btnConvex->SetActive(collider.Convex);
                    btnConvex->SetOnClick([entity, btnConvex]() mutable
                        {
                            auto& current = entity.GetComponent<MeshCollider3DComponent>();
                            current.Convex = !current.Convex;
                            btnConvex->SetActive(current.Convex);
                            btnConvex->SetText(current.Convex ? "Convex: On" : "Convex: Off");
                        });
                    item->AddChild(btnConvex);

                    auto btnTrigger = new UI::Button("MeshCollider3DTrigger", collider.IsTrigger ? "Is Trigger: On" : "Is Trigger: Off");
                    btnTrigger->SetActive(collider.IsTrigger);
                    btnTrigger->SetOnClick([entity, btnTrigger]() mutable
                        {
                            auto& current = entity.GetComponent<MeshCollider3DComponent>();
                            current.IsTrigger = !current.IsTrigger;
                            btnTrigger->SetActive(current.IsTrigger);
                            btnTrigger->SetText(current.IsTrigger ? "Is Trigger: On" : "Is Trigger: Off");
                        });
                    item->AddChild(btnTrigger);
                    AddRemoveComponentButton<MeshCollider3DComponent>(parent, item, entity, "MeshCollider3D");
                });

            UI::InspectorRegistry::RegisterComponent<ScriptComponent>(
                [](UI::Widget* parent, CCEngine::Entity entity, ScriptComponent& script)
                {
                    auto item = new UI::InspectorItem("ScriptItem", "C# Script");
                    item->SetAnchorMin(0.0f, 0.0f);
                    item->SetAnchorMax(1.0f, 0.0f);
                    parent->AddChild(item);

                    auto className = new UI::TextInput("ScriptClassName", "Namespace.ClassName");
                    className->SetText(script.ClassName, false);
                    className->SetOnTextChanged([entity](const std::string& value) mutable
                        {
                            if (entity && entity.HasComponent<ScriptComponent>())
                                entity.GetComponent<ScriptComponent>().ClassName = value;
                        });
                    item->AddChild(className);

                    auto enabled = new UI::Button("ScriptEnabled", script.Enabled ? "Enabled" : "Disabled");
                    enabled->SetOnClick([entity, enabled]() mutable
                        {
                            if (!entity || !entity.HasComponent<ScriptComponent>())
                                return;
                            auto& component = entity.GetComponent<ScriptComponent>();
                            component.Enabled = !component.Enabled;
                            enabled->SetText(component.Enabled ? "Enabled" : "Disabled");
                        });
                    item->AddChild(enabled);

                    const ScriptClassInfo* classInfo = ScriptMetadata::FindClass(script.ClassName);
                    if (classInfo)
                    {
                        for (const ScriptFieldInfo& field : classInfo->Fields)
                        {
                            const std::string widgetName = "ScriptField_" + field.Name;
                            switch (field.Type)
                            {
                            case ScriptFieldType::Int:
                            case ScriptFieldType::String:
                            case ScriptFieldType::Float:
                            {
                                auto fieldWidget = new UI::ScriptFieldWidget(widgetName, field,
                                    [entity, field]() mutable
                                    {
                                        if (!entity || !entity.HasComponent<ScriptComponent>())
                                            return std::string{};
                                        return GetScriptFieldValue(entity.GetComponent<ScriptComponent>(), field);
                                    },
                                    [entity, field](const std::string& value) mutable
                                    {
                                        SetScriptFieldValue(entity, field, value);
                                    });
                                item->AddChild(fieldWidget);
                                break;
                            }
                            case ScriptFieldType::Bool:
                            {
                                bool currentValue = GetScriptFieldValue(script, field) == "true" ||
                                    GetScriptFieldValue(script, field) == "True" ||
                                    GetScriptFieldValue(script, field) == "1";
                                auto toggle = new UI::Button(widgetName, field.Name + ": " + (currentValue ? "On" : "Off"));
                                toggle->SetActive(currentValue);
                                toggle->SetOnClick([entity, field, toggle]() mutable
                                    {
                                        if (field.Display == ScriptFieldDisplay::ReadOnly || field.ReadOnly)
                                            return;
                                        if (!entity || !entity.HasComponent<ScriptComponent>())
                                            return;
                                        auto& component = entity.GetComponent<ScriptComponent>();
                                        bool oldValue = GetScriptFieldValue(component, field) == "true" ||
                                            GetScriptFieldValue(component, field) == "True" ||
                                            GetScriptFieldValue(component, field) == "1";
                                        bool newValue = !oldValue;
                                        component.FieldOverrides[field.Name] = newValue ? "true" : "false";
                                        toggle->SetText(field.Name + ": " + (newValue ? "On" : "Off"));
                                        toggle->SetActive(newValue);
                                    });
                                item->AddChild(toggle);
                                break;
                            }
                            case ScriptFieldType::Vector3:
                                UI::InspectorUtils::AddDragFloat3(item, widgetName, field.Name,
                                    [entity, field]() mutable
                                    {
                                        if (!entity || !entity.HasComponent<ScriptComponent>())
                                            return DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
                                        return ToFloat3(GetScriptFieldValue(entity.GetComponent<ScriptComponent>(), field));
                                    },
                                    [entity, field](DirectX::XMFLOAT3 value) mutable
                                    {
                                        if (entity && entity.HasComponent<ScriptComponent>())
                                            entity.GetComponent<ScriptComponent>().FieldOverrides[field.Name] = FromFloat3(value);
                                    });
                                break;
                            default:
                                break;
                            }
                        }
                    }
                    else
                    {
                        auto missing = new UI::Button("ScriptMetadataMissing", "Build scripts to show fields");
                        item->AddChild(missing);
                    }

                    AddRemoveComponentButton<ScriptComponent>(parent, item, entity, "Script");
                });
        }

    }
}
