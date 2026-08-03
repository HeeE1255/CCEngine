#include "InspectorUtils.h"
#include "UI/InspectorRegistry.h"
#include "Scene/Components.h"
#include "Core/AssetDatabase.h"
#include "Scripting/ScriptMetadata.h"
#include "Utils/PlatformUtils.h"
#include "UI/Button.h"
#include "UI/InspectorPanel.h"
#include "UI/ScriptFieldWidget.h"
#include "UI/TextInput.h"
#include <algorithm>
#include <iostream>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <type_traits>

namespace CCEngine {
    namespace UI {
        namespace
        {
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

                    std::filesystem::path currentMaterialPath(mesh.MaterialPath);
                    std::string materialButtonText = currentMaterialPath.empty()
                        ? "Material: (none)"
                        : "Material: " + currentMaterialPath.filename().string();

                    auto btnMaterial = new UI::Button("BtnChangeMaterial", materialButtonText);
                    btnMaterial->SetAnchorMin(0.0f, 0.0f); btnMaterial->SetAnchorMax(1.0f, 0.0f);
                    btnMaterial->SetOffsetMin(15.0f, 0.0f); btnMaterial->SetOffsetMax(-10.0f, 28.0f);
                    btnMaterial->SetOnClick([entity, btnMaterial]() mutable
                        {
                            std::string filepath = PlatformUtils::OpenFile("CC Material (*.ccmat)\0*.ccmat\0");
                            if (!filepath.empty())
                            {
                                auto material = std::make_shared<MaterialAsset>();
                                if (!material->LoadFromFile(filepath))
                                    return;

                                auto& mesh = entity.GetComponent<MeshComponent>();
                                mesh.Material = material;
                                // Material 역시 저장 시에는 포인터가 아니라 GUID/경로를 남긴다.
                                // 그래야 파일명을 바꾸거나 위치를 옮겨도 meta 기준으로 다시 연결된다.
                                mesh.MaterialPath = filepath;
                                mesh.MaterialAssetGuid = AssetDatabase::GetGuidFromPath(filepath);
                                btnMaterial->SetText("Material: " + std::filesystem::path(filepath).filename().string());
                            }
                        });
                    item->AddChild(btnMaterial);

                    if (mesh.Material)
                    {
                        UI::InspectorUtils::AddColor4(item, "MaterialAlbedoColor", "Mat Albedo",
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
                                if (!mesh.MaterialPath.empty())
                                    mesh.Material->SaveToFile(mesh.MaterialPath);
                            });

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
                                if (!mesh.MaterialPath.empty())
                                    mesh.Material->SaveToFile(mesh.MaterialPath);
                            });

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

                                // 현재 기본 셰이더는 Metallic/Roughness를 아직 조명 계산에 쓰지 않는다.
                                // 그래도 파일 포맷과 UI 값을 먼저 고정해 두면 PBR 셰이더로 넘어갈 때 저장 구조를 다시 바꾸지 않아도 된다.
                                mesh.Material->Metallic = std::clamp(v, 0.0f, 1.0f);
                                if (!mesh.MaterialPath.empty())
                                    mesh.Material->SaveToFile(mesh.MaterialPath);
                            });
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
