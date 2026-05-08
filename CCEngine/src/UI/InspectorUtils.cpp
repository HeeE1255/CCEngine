#include "InspectorUtils.h"
#include "UI/InspectorRegistry.h"
#include "Scene/Components.h"
#include "Utils/PlatformUtils.h"
#include "UI/Button.h"
#include <iostream>
#include <cmath>

namespace CCEngine {
    namespace UI {

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

                    // 2. 텍스처 로드 버튼 (기존 코드 유지)
                    auto btnTexture = new UI::Button("BtnChangeTexture", "Load Diffuse Texture...");
                    btnTexture->SetAnchorMin(0.0f, 0.0f); btnTexture->SetAnchorMax(1.0f, 0.0f);
                    btnTexture->SetOffsetMin(15.0f, 0.0f); btnTexture->SetOffsetMax(-10.0f, 28.0f);
                    btnTexture->SetOnClick([entity]() mutable
                        {
                            std::string filepath = PlatformUtils::OpenFile("PNG Image (*.png)\0*.png\0JPG Image (*.jpg)\0*.jpg\0");
                            if (!filepath.empty())
                            {
                                std::shared_ptr<Texture2D> newTex = std::shared_ptr<Texture2D>(Texture2D::Create(filepath));
                                entity.GetComponent<MeshComponent>().AlbedoMap = newTex;
                                std::cout << "Texture Applied to Mesh: " << filepath << std::endl;
                            }
                        });
                    item->AddChild(btnTexture);
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
                });
        }

    }
}