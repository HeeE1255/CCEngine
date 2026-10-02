#include "Scene/PrefabSerializer.h"
#include "Animation/AnimatorControllerAsset.h"
#include "Core/AssetDatabase.h"
#include "Scene/Components.h"
#include "Renderer/MeshFactory.h"
#include "Renderer/MaterialAsset.h"
#include "Renderer/Texture.h"
#include "json.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <queue>
#include <unordered_map>

namespace CCEngine
{
    namespace
    {
        nlohmann::json Float2ToJson(const DirectX::XMFLOAT2& v)
        {
            return { v.x, v.y };
        }

        nlohmann::json Float3ToJson(const DirectX::XMFLOAT3& v)
        {
            return { v.x, v.y, v.z };
        }

        nlohmann::json Float4ToJson(const DirectX::XMFLOAT4& v)
        {
            return { v.x, v.y, v.z, v.w };
        }

        DirectX::XMFLOAT2 JsonToFloat2(const nlohmann::json& data)
        {
            return { data[0].get<float>(), data[1].get<float>() };
        }

        DirectX::XMFLOAT3 JsonToFloat3(const nlohmann::json& data)
        {
            return { data[0].get<float>(), data[1].get<float>(), data[2].get<float>() };
        }

        DirectX::XMFLOAT4 JsonToFloat4(const nlohmann::json& data)
        {
            return { data[0].get<float>(), data[1].get<float>(), data[2].get<float>(), data[3].get<float>() };
        }

        nlohmann::json AnimatorEventsToJson(const AnimatorComponent::State& state)
        {
            nlohmann::json events = nlohmann::json::array();
            for (const auto& event : state.Events)
            {
                events.push_back({
                    { "TimeSeconds", event.TimeSeconds },
                    { "FunctionName", event.FunctionName },
                    { "StringArgument", event.StringArgument }
                    });
            }
            return events;
        }

        void LoadAnimatorEvents(const nlohmann::json& stateData, AnimatorComponent::State& state)
        {
            state.Events.clear();
            if (!stateData.contains("Events") || !stateData["Events"].is_array())
                return;

            for (const auto& eventData : stateData["Events"])
            {
                AnimatorComponent::State::AnimationEvent event;
                event.TimeSeconds = eventData.value("TimeSeconds", 0.0f);
                event.FunctionName = eventData.value("FunctionName", "OnAnimationEvent");
                event.StringArgument = eventData.value("StringArgument", "");
                state.Events.push_back(event);
            }
            state.SelectedEventIndex = std::clamp(state.SelectedEventIndex, -1, static_cast<int>(state.Events.size()) - 1);
        }

        nlohmann::json AnimatorPropertyTracksToJson(const AnimatorComponent::State& state)
        {
            nlohmann::json tracks = nlohmann::json::array();
            for (const auto& track : state.PropertyTracks)
            {
                nlohmann::json keys = nlohmann::json::array();
                for (const auto& key : track.Keys)
                    keys.push_back({
                        { "TimeSeconds", key.TimeSeconds },
                        { "Value", Float4ToJson(key.Value) },
                        { "Interpolation", static_cast<int>(key.Interp) }
                        });

                tracks.push_back({
                    { "EntityPath", track.EntityPath },
                    { "ComponentName", track.ComponentName },
                    { "PropertyName", track.PropertyName },
                    { "Type", static_cast<int>(track.Type) },
                    { "Keys", keys }
                    });
            }
            return tracks;
        }

        void LoadAnimatorPropertyTracks(const nlohmann::json& stateData, AnimatorComponent::State& state)
        {
            state.PropertyTracks.clear();
            if (!stateData.contains("PropertyTracks") || !stateData["PropertyTracks"].is_array())
                return;

            for (const auto& trackData : stateData["PropertyTracks"])
            {
                AnimatorComponent::State::PropertyTrack track;
                track.EntityPath = trackData.value("EntityPath", "");
                track.ComponentName = trackData.value("ComponentName", "");
                track.PropertyName = trackData.value("PropertyName", "");
                track.Type = static_cast<AnimatorComponent::State::PropertyTrack::ValueType>(trackData.value("Type", 0));
                if (trackData.contains("Keys") && trackData["Keys"].is_array())
                {
                    for (const auto& keyData : trackData["Keys"])
                    {
                        AnimatorComponent::State::PropertyKey key;
                        key.TimeSeconds = keyData.value("TimeSeconds", 0.0f);
                        if (keyData.contains("Value") && keyData["Value"].is_array())
                            key.Value = JsonToFloat4(keyData["Value"]);
                        key.Interp = static_cast<AnimatorComponent::State::PropertyKey::Interpolation>(keyData.value("Interpolation", 1));
                        track.Keys.push_back(key);
                    }
                }
                state.PropertyTracks.push_back(track);
            }
        }

        nlohmann::json AnimatorStateToJson(const AnimatorComponent::State& state)
        {
            nlohmann::json children = nlohmann::json::array();
            for (const auto& child : state.Tree.Children)
            {
                children.push_back({
                    { "ClipIndex", child.ClipIndex },
                    { "Threshold", child.Threshold },
                    { "Position", Float2ToJson(child.Position) },
                    { "Weight", child.Weight }
                    });
            }

            return {
                { "Name", state.Name },
                { "Motion", static_cast<int>(state.Motion) },
                { "ClipIndex", state.ClipIndex },
                { "MotionGuid", state.MotionAssetGuid },
                { "MotionPath", state.MotionPath },
                { "Loop", state.Loop },
                { "Speed", state.Speed },
                { "ApplyRootMotion", state.ApplyRootMotion },
                { "BlendTree", {
                    { "Type", static_cast<int>(state.Tree.TreeType) },
                    { "ParameterX", state.Tree.ParameterX },
                    { "ParameterY", state.Tree.ParameterY },
                    { "Children", children }
                    } },
                { "ImportSettings", {
                    { "DisplayName", state.ImportSettings.DisplayName },
                    { "UseCustomRange", state.ImportSettings.UseCustomRange },
                    { "StartSeconds", state.ImportSettings.StartSeconds },
                    { "EndSeconds", state.ImportSettings.EndSeconds },
                    { "LoopPose", state.ImportSettings.LoopPose },
                    { "BakeRootTransform", state.ImportSettings.BakeRootTransform },
                    { "LockRootPositionXZ", state.ImportSettings.LockRootPositionXZ },
                    { "LockRootPositionY", state.ImportSettings.LockRootPositionY },
                    { "LockRootRotation", state.ImportSettings.LockRootRotation }
                    } },
                { "PropertyTracks", AnimatorPropertyTracksToJson(state) },
                { "GraphPosition", Float2ToJson(state.GraphPosition) },
                { "WriteDefaults", state.WriteDefaults },
                { "Events", AnimatorEventsToJson(state) }
            };
        }

        AnimatorComponent::State JsonToAnimatorState(const nlohmann::json& stateData)
        {
            AnimatorComponent::State state;
            state.Name = stateData.value("Name", "State");
            state.Motion = static_cast<AnimatorComponent::State::MotionType>(stateData.value("Motion", 0));
            state.ClipIndex = stateData.value("ClipIndex", 0);
            state.MotionAssetGuid = stateData.value("MotionGuid", "");
            state.MotionPath = stateData.value("MotionPath", "");
            state.Loop = stateData.value("Loop", true);
            state.Speed = stateData.value("Speed", 1.0f);
            state.ApplyRootMotion = stateData.value("ApplyRootMotion", false);
            if (stateData.contains("BlendTree") && stateData["BlendTree"].is_object())
            {
                const auto& treeData = stateData["BlendTree"];
                state.Tree.TreeType = static_cast<AnimatorComponent::State::BlendTree::Type>(treeData.value("Type", 0));
                state.Tree.ParameterX = treeData.value("ParameterX", "");
                state.Tree.ParameterY = treeData.value("ParameterY", "");
                if (treeData.contains("Children") && treeData["Children"].is_array())
                {
                    for (const auto& childData : treeData["Children"])
                    {
                        AnimatorComponent::State::BlendTreeChild child;
                        child.ClipIndex = childData.value("ClipIndex", 0);
                        child.Threshold = childData.value("Threshold", 0.0f);
                        if (childData.contains("Position"))
                            child.Position = JsonToFloat2(childData["Position"]);
                        child.Weight = childData.value("Weight", 1.0f);
                        state.Tree.Children.push_back(child);
                    }
                }
            }
            if (stateData.contains("ImportSettings") && stateData["ImportSettings"].is_object())
            {
                const auto& importData = stateData["ImportSettings"];
                state.ImportSettings.DisplayName = importData.value("DisplayName", "");
                state.ImportSettings.UseCustomRange = importData.value("UseCustomRange", false);
                state.ImportSettings.StartSeconds = importData.value("StartSeconds", 0.0f);
                state.ImportSettings.EndSeconds = importData.value("EndSeconds", 0.0f);
                state.ImportSettings.LoopPose = importData.value("LoopPose", false);
                state.ImportSettings.BakeRootTransform = importData.value("BakeRootTransform", false);
                state.ImportSettings.LockRootPositionXZ = importData.value("LockRootPositionXZ", false);
                state.ImportSettings.LockRootPositionY = importData.value("LockRootPositionY", false);
                state.ImportSettings.LockRootRotation = importData.value("LockRootRotation", false);
            }
            LoadAnimatorPropertyTracks(stateData, state);
            if (stateData.contains("GraphPosition"))
                state.GraphPosition = JsonToFloat2(stateData["GraphPosition"]);
            state.WriteDefaults = stateData.value("WriteDefaults", true);
            LoadAnimatorEvents(stateData, state);
            return state;
        }

        nlohmann::json AnimatorTransitionToJson(const AnimatorComponent::Transition& transition)
        {
            nlohmann::json transitionData = {
                { "FromStateIndex", transition.FromStateIndex },
                { "ToStateIndex", transition.ToStateIndex },
                { "HasExitTime", transition.HasExitTime },
                { "ExitTime", transition.ExitTime },
                { "BlendTime", transition.BlendTime },
                { "CanInterrupt", transition.CanInterrupt },
                { "Priority", transition.Priority },
                { "ExitTargetStateIndex", transition.ExitTargetStateIndex },
                { "Conditions", nlohmann::json::array() }
            };
            for (const auto& condition : transition.Conditions)
            {
                transitionData["Conditions"].push_back({
                    { "ParameterName", condition.ParameterName },
                    { "Mode", static_cast<int>(condition.Mode) },
                    { "FloatValue", condition.FloatValue },
                    { "BoolValue", condition.BoolValue }
                    });
            }
            return transitionData;
        }

        AnimatorComponent::Transition JsonToAnimatorTransition(const nlohmann::json& transitionData)
        {
            AnimatorComponent::Transition transition;
            transition.FromStateIndex = transitionData.value("FromStateIndex", -1);
            transition.ToStateIndex = transitionData.value("ToStateIndex", -1);
            transition.HasExitTime = transitionData.value("HasExitTime", false);
            transition.ExitTime = transitionData.value("ExitTime", 1.0f);
            transition.BlendTime = transitionData.value("BlendTime", 0.15f);
            transition.CanInterrupt = transitionData.value("CanInterrupt", true);
            transition.Priority = transitionData.value("Priority", 0);
            transition.ExitTargetStateIndex = transitionData.value("ExitTargetStateIndex", -1);
            if (transitionData.contains("Conditions") && transitionData["Conditions"].is_array())
            {
                for (const auto& conditionData : transitionData["Conditions"])
                {
                    AnimatorComponent::TransitionCondition condition;
                    condition.ParameterName = conditionData.value("ParameterName", "");
                    condition.Mode = static_cast<AnimatorComponent::TransitionCondition::CompareMode>(conditionData.value("Mode", 0));
                    condition.FloatValue = conditionData.value("FloatValue", 0.0f);
                    condition.BoolValue = conditionData.value("BoolValue", true);
                    transition.Conditions.push_back(condition);
                }
            }
            return transition;
        }

        std::shared_ptr<Mesh> CreateMeshForType(MeshComponent::MeshType type)
        {
            switch (type)
            {
                case MeshComponent::MeshType::Cube: return MeshFactory::CreateCube();
                case MeshComponent::MeshType::Sphere: return MeshFactory::CreateSphere();
                case MeshComponent::MeshType::Plane: return MeshFactory::CreatePlane();
                case MeshComponent::MeshType::Quad: return MeshFactory::CreateQuad();
                case MeshComponent::MeshType::Capsule: return MeshFactory::CreateCapsule();
                case MeshComponent::MeshType::Cylinder: return MeshFactory::CreateCylinder();
                case MeshComponent::MeshType::Torus: return MeshFactory::CreateTorus();
                default: return nullptr;
            }
        }

        std::shared_ptr<MaterialAsset> LoadMaterialForSlot(MeshComponent::MaterialSlot& slot)
        {
            std::string resolvedPath;
            if (!slot.MaterialAssetGuid.empty())
                resolvedPath = AssetDatabase::GetPathFromGuid(slot.MaterialAssetGuid).string();

            if (resolvedPath.empty())
                resolvedPath = slot.MaterialPath;

            slot.MaterialPath = resolvedPath;
            slot.Missing = !resolvedPath.empty() && !std::filesystem::exists(resolvedPath);
            if (resolvedPath.empty() || slot.Missing)
                return nullptr;

            auto material = std::make_shared<MaterialAsset>();
            if (!material->LoadFromFile(resolvedPath))
            {
                slot.Missing = true;
                return nullptr;
            }

            slot.Material = material;
            return material;
        }

        std::shared_ptr<MaterialAsset> LoadMaterialReference(MeshComponent& mesh, const nlohmann::json& meshData)
        {
            mesh.MaterialSlots.clear();

            if (meshData.contains("MaterialSlots") && meshData["MaterialSlots"].is_array())
            {
                int index = 0;
                for (const auto& slotData : meshData["MaterialSlots"])
                {
                    MeshComponent::MaterialSlot slot;
                    std::string fallbackName = std::string("Element ") + std::to_string(index);
                    slot.Name = slotData.value("Name", fallbackName);
                    slot.MaterialAssetGuid = slotData.value("MaterialGuid", "");
                    slot.MaterialPath = slotData.value("MaterialPath", "");
                    LoadMaterialForSlot(slot);
                    mesh.MaterialSlots.push_back(slot);
                    ++index;
                }

                if (!mesh.MaterialSlots.empty())
                {
                    auto& firstSlot = mesh.MaterialSlots.front();
                    mesh.Material = firstSlot.Material;
                    mesh.MaterialAssetGuid = firstSlot.MaterialAssetGuid;
                    mesh.MaterialPath = firstSlot.MaterialPath;
                    mesh.MaterialMissing = firstSlot.Missing && (!firstSlot.MaterialPath.empty() || !firstSlot.MaterialAssetGuid.empty());
                    return firstSlot.Material;
                }
            }

            std::string materialGuid = meshData.value("MaterialGuid", "");
            std::string materialPath;
            if (!materialGuid.empty())
            {
                // 프리팹 인스턴스도 재질 에셋은 GUID를 우선한다.
                // 같은 프리팹을 여러 프로젝트 위치에서 열어도 meta가 기준이 된다.
                materialPath = AssetDatabase::GetPathFromGuid(materialGuid).string();
                mesh.MaterialAssetGuid = materialGuid;
            }

            if (materialPath.empty() && meshData.contains("MaterialPath"))
            {
                materialPath = meshData["MaterialPath"].get<std::string>();
                if (mesh.MaterialAssetGuid.empty())
                    mesh.MaterialAssetGuid = AssetDatabase::GetGuidFromPath(materialPath);
            }

            mesh.MaterialPath = materialPath;
            MeshComponent::MaterialSlot slot;
            slot.Name = "Element 0";
            slot.MaterialAssetGuid = mesh.MaterialAssetGuid;
            slot.MaterialPath = mesh.MaterialPath;
            auto material = LoadMaterialForSlot(slot);
            mesh.MaterialSlots.push_back(slot);

            mesh.Material = material;
            mesh.MaterialMissing = slot.Missing && (!slot.MaterialPath.empty() || !slot.MaterialAssetGuid.empty());
            return material;
        }

        void CollectPrefabEntities(Scene* scene, Entity rootEntity, std::vector<Entity>& entities)
        {
            if (!rootEntity)
                return;

            entities.push_back(rootEntity);

            if (!rootEntity.HasComponent<RelationshipComponent>())
                return;

            for (auto childID : rootEntity.GetComponent<RelationshipComponent>().Children)
            {
                if (scene->GetRegistry().valid(childID))
                    CollectPrefabEntities(scene, Entity{ childID, scene }, entities);
            }
        }

        void SerializeComponents(Entity entity, nlohmann::json& entityData, const std::unordered_map<entt::entity, int>& localIDs)
        {
            // 프리팹은 선택한 루트와 그 자식만 저장한다. 저장 대상 안에 있는 컴포넌트만 기록한다.
            if (entity.HasComponent<TagComponent>())
            {
                entityData["TagComponent"]["Tag"] = entity.GetComponent<TagComponent>().Tag;
            }

            if (entity.HasComponent<ActiveComponent>())
                entityData["ActiveComponent"]["ActiveSelf"] = entity.GetComponent<ActiveComponent>().ActiveSelf;

            if (entity.HasComponent<TransformComponent>())
            {
                auto& tc = entity.GetComponent<TransformComponent>();
                entityData["TransformComponent"]["Translation"] = Float3ToJson(tc.Translation);
                entityData["TransformComponent"]["Rotation"] = Float3ToJson(tc.Rotation);
                entityData["TransformComponent"]["Scale"] = Float3ToJson(tc.Scale);
                entityData["TransformComponent"]["QuaternionRotation"] = Float4ToJson(tc.QuaternionRotation);
            }

            if (entity.HasComponent<SpriteRendererComponent>())
            {
                auto& sprite = entity.GetComponent<SpriteRendererComponent>();
                entityData["SpriteRendererComponent"]["Color"] = Float4ToJson(sprite.Color);
            }

            if (entity.HasComponent<CameraComponent>())
            {
                auto& camera = entity.GetComponent<CameraComponent>();
                entityData["CameraComponent"]["FOV"] = camera.FOV;
                entityData["CameraComponent"]["NearClip"] = camera.NearClip;
                entityData["CameraComponent"]["FarClip"] = camera.FarClip;
                entityData["CameraComponent"]["Primary"] = camera.Primary;
            }

            if (entity.HasComponent<AudioComponent>())
            {
                auto& audio = entity.GetComponent<AudioComponent>();
                auto& data = entityData["AudioComponent"];
                data["AudioGuid"] = audio.AudioAssetGuid;
                data["AudioPath"] = audio.AudioPath;
                data["Volume"] = audio.Volume;
                data["Pitch"] = audio.Pitch;
                data["Loop"] = audio.Loop;
                data["PlayOnStart"] = audio.PlayOnStart;
                data["Enabled"] = audio.Enabled;
            }

            if (entity.HasComponent<MeshComponent>())
            {
                auto& mesh = entity.GetComponent<MeshComponent>();
                entityData["MeshComponent"]["Type"] = static_cast<int>(mesh.Type);
                entityData["MeshComponent"]["BaseColor"] = Float4ToJson(mesh.BaseColor);
                if (!mesh.AlbedoAssetGuid.empty())
                    entityData["MeshComponent"]["AlbedoGuid"] = mesh.AlbedoAssetGuid;
                if (!mesh.AlbedoPath.empty())
                {
                    entityData["MeshComponent"]["AlbedoPath"] = mesh.AlbedoPath;
                    if (mesh.AlbedoAssetGuid.empty())
                    {
                        std::string guid = AssetDatabase::GetGuidFromPath(mesh.AlbedoPath);
                        if (!guid.empty())
                            entityData["MeshComponent"]["AlbedoGuid"] = guid;
                    }
                }
                if (!mesh.MaterialAssetGuid.empty())
                    entityData["MeshComponent"]["MaterialGuid"] = mesh.MaterialAssetGuid;
                if (!mesh.MaterialPath.empty())
                {
                    entityData["MeshComponent"]["MaterialPath"] = mesh.MaterialPath;
                    if (mesh.MaterialAssetGuid.empty())
                    {
                        std::string guid = AssetDatabase::GetGuidFromPath(mesh.MaterialPath);
                        if (!guid.empty())
                            entityData["MeshComponent"]["MaterialGuid"] = guid;
                    }
                }

                if (!mesh.MaterialSlots.empty())
                {
                    nlohmann::json slots = nlohmann::json::array();
                    for (size_t slotIndex = 0; slotIndex < mesh.MaterialSlots.size(); ++slotIndex)
                    {
                        const auto& slot = mesh.MaterialSlots[slotIndex];
                        nlohmann::json slotData;
                        slotData["Name"] = slot.Name.empty() ? (std::string("Element ") + std::to_string(slotIndex)) : slot.Name;
                        if (!slot.MaterialAssetGuid.empty())
                            slotData["MaterialGuid"] = slot.MaterialAssetGuid;
                        if (!slot.MaterialPath.empty())
                            slotData["MaterialPath"] = slot.MaterialPath;
                        slots.push_back(slotData);
                    }
                    entityData["MeshComponent"]["MaterialSlots"] = slots;
                }
            }

            if (entity.HasComponent<ModelComponent>())
            {
                auto& model = entity.GetComponent<ModelComponent>();
                if (model.TargetModel)
                {
                    entityData["ModelComponent"]["Path"] = model.TargetModel->GetFilePath();
                    std::string guid = model.AssetGuid.empty() ? AssetDatabase::GetGuidFromPath(model.TargetModel->GetFilePath()) : model.AssetGuid;
                    if (!guid.empty())
                        entityData["ModelComponent"]["Guid"] = guid;
                }
                entityData["ModelComponent"]["ShowBoneLinks"] = model.ShowBoneLinks;

                // FBX 노드는 엔티티 핸들을 직접 저장하지 않고, 프리팹 안에서만 통하는 LocalID로 저장한다.
                for (const auto& [nodePath, nodeEntity] : model.NodePathEntityMap)
                {
                    auto found = localIDs.find(nodeEntity);
                    if (found != localIDs.end())
                        entityData["ModelComponent"]["NodePathLocalIDMap"][nodePath] = found->second;
                }
            }

            if (entity.HasComponent<Rigidbody2DComponent>())
            {
                auto& rb = entity.GetComponent<Rigidbody2DComponent>();
                entityData["Rigidbody2DComponent"]["Type"] = static_cast<int>(rb.Type);
                entityData["Rigidbody2DComponent"]["FixedRotation"] = rb.FixedRotation;
            }

            if (entity.HasComponent<Rigidbody3DComponent>())
            {
                const auto& rb = entity.GetComponent<Rigidbody3DComponent>();
                auto& data = entityData["Rigidbody3DComponent"];
                data["Type"] = static_cast<int>(rb.Type);
                data["Mass"] = rb.Mass;
                data["LinearDamping"] = rb.LinearDamping;
                data["AngularDamping"] = rb.AngularDamping;
                data["Friction"] = rb.Friction;
                data["Restitution"] = rb.Restitution;
                data["UseGravity"] = rb.UseGravity;
                data["FixedRotation"] = rb.FixedRotation;
                data["LinearVelocity"] = Float3ToJson(rb.LinearVelocity);
                data["AngularVelocity"] = Float3ToJson(rb.AngularVelocity);
            }

            if (entity.HasComponent<BoxCollider2DComponent>())
            {
                auto& collider = entity.GetComponent<BoxCollider2DComponent>();
                entityData["BoxCollider2DComponent"]["Offset"] = Float2ToJson(collider.Offset);
                entityData["BoxCollider2DComponent"]["Size"] = Float2ToJson(collider.Size);
                entityData["BoxCollider2DComponent"]["IsTrigger"] = collider.IsTrigger;
                entityData["BoxCollider2DComponent"]["Density"] = collider.Density;
                entityData["BoxCollider2DComponent"]["Friction"] = collider.Friction;
                entityData["BoxCollider2DComponent"]["Restitution"] = collider.Restitution;
            }

            if (entity.HasComponent<BoxCollider3DComponent>())
            {
                auto& collider = entity.GetComponent<BoxCollider3DComponent>();
                entityData["BoxCollider3DComponent"]["Offset"] = Float3ToJson(collider.Offset);
                entityData["BoxCollider3DComponent"]["Size"] = Float3ToJson(collider.Size);
                entityData["BoxCollider3DComponent"]["IsTrigger"] = collider.IsTrigger;
            }

            if (entity.HasComponent<SphereCollider3DComponent>())
            {
                auto& collider = entity.GetComponent<SphereCollider3DComponent>();
                entityData["SphereCollider3DComponent"]["Offset"] = Float3ToJson(collider.Offset);
                entityData["SphereCollider3DComponent"]["Radius"] = collider.Radius;
                entityData["SphereCollider3DComponent"]["IsTrigger"] = collider.IsTrigger;
            }

            if (entity.HasComponent<CylinderCollider3DComponent>())
            {
                auto& collider = entity.GetComponent<CylinderCollider3DComponent>();
                entityData["CylinderCollider3DComponent"]["Offset"] = Float3ToJson(collider.Offset);
                entityData["CylinderCollider3DComponent"]["Radius"] = collider.Radius;
                entityData["CylinderCollider3DComponent"]["Height"] = collider.Height;
                entityData["CylinderCollider3DComponent"]["IsTrigger"] = collider.IsTrigger;
            }

            if (entity.HasComponent<MeshCollider3DComponent>())
            {
                auto& collider = entity.GetComponent<MeshCollider3DComponent>();
                entityData["MeshCollider3DComponent"]["Offset"] = Float3ToJson(collider.Offset);
                entityData["MeshCollider3DComponent"]["Size"] = Float3ToJson(collider.Size);
                entityData["MeshCollider3DComponent"]["Convex"] = collider.Convex;
                entityData["MeshCollider3DComponent"]["IsTrigger"] = collider.IsTrigger;
            }

            if (entity.HasComponent<LightComponent>())
            {
                auto& light = entity.GetComponent<LightComponent>();
                entityData["LightComponent"]["LightColor"] = Float3ToJson(light.LightColor);
                entityData["LightComponent"]["Intensity"] = light.Intensity;
            }

            if (entity.HasComponent<AnimatorComponent>())
            {
                const auto& animator = entity.GetComponent<AnimatorComponent>();
                auto& data = entityData["AnimatorComponent"];
                data["ControllerGuid"] = animator.ControllerAssetGuid;
                data["ControllerPath"] = animator.ControllerPath;
                data["SourceGuid"] = animator.SourceAssetGuid;
                data["SourcePath"] = animator.SourcePath;
                data["SelectedClipIndex"] = animator.SelectedClipIndex;
                data["SelectedClipName"] = animator.SelectedClipName;
                data["AutoPlay"] = animator.AutoPlay;
                data["PreviewInEdit"] = animator.PreviewInEdit;
                data["Loop"] = animator.Loop;
                data["Speed"] = animator.Speed;
                data["ApplyRootMotion"] = animator.ApplyRootMotion;
                data["UpdateMode"] = static_cast<int>(animator.UpdateModeValue);
                data["CullingMode"] = static_cast<int>(animator.CullingModeValue);
                data["AvatarGuid"] = animator.AvatarGuid;
                data["AvatarPath"] = animator.AvatarPath;
                data["SourceAvatarGuid"] = animator.SourceAvatarGuid;
                data["SourceAvatarPath"] = animator.SourceAvatarPath;
                data["HumanoidRootBone"] = animator.HumanoidRootBone;
                data["RetargetToHumanoid"] = animator.RetargetToHumanoid;
                data["ActiveStateIndex"] = animator.ActiveStateIndex;
                data["EntryStateIndex"] = animator.EntryStateIndex;
                data["ActiveLayerIndex"] = animator.ActiveLayerIndex;
                data["Layers"] = nlohmann::json::array();
                for (const auto& layer : animator.Layers)
                {
                    nlohmann::json layerJson = {
                        { "Name", layer.Name },
                        { "Weight", layer.Weight },
                        { "MaskRootBone", layer.MaskRootBone },
                        { "MaskBoneNames", layer.MaskBoneNames },
                        { "Blending", static_cast<int>(layer.Blending) },
                        { "Sync", layer.Sync },
                        { "IKPass", layer.IKPass },
                        { "Visible", layer.Visible },
                        { "ActiveStateIndex", layer.ActiveStateIndex },
                        { "EntryStateIndex", layer.EntryStateIndex },
                        { "SelectedTransitionIndex", layer.SelectedTransitionIndex },
                        { "States", nlohmann::json::array() },
                        { "Transitions", nlohmann::json::array() }
                    };
                    // 프리팹도 씬과 같은 Animator Layer 포맷을 쓴다.
                    // 그래야 여러 레이어를 가진 캐릭터 프리팹을 다시 불러와도 그래프 구조가 유지된다.
                    for (const auto& state : layer.States)
                        layerJson["States"].push_back(AnimatorStateToJson(state));
                    for (const auto& transition : layer.Transitions)
                        layerJson["Transitions"].push_back(AnimatorTransitionToJson(transition));
                    data["Layers"].push_back(layerJson);
                }
                data["States"] = nlohmann::json::array();
                for (const auto& state : animator.States)
                    data["States"].push_back(AnimatorStateToJson(state));
                data["Parameters"] = nlohmann::json::array();
                for (const auto& parameter : animator.Parameters)
                {
                    data["Parameters"].push_back({
                        { "Name", parameter.Name },
                        { "Type", static_cast<int>(parameter.ParamType) },
                        { "FloatValue", parameter.FloatValue },
                        { "BoolValue", parameter.BoolValue }
                        });
                }
                data["Transitions"] = nlohmann::json::array();
                for (const auto& transition : animator.Transitions)
                    data["Transitions"].push_back(AnimatorTransitionToJson(transition));
            }

            if (entity.HasComponent<ScriptComponent>())
            {
                const auto& script = entity.GetComponent<ScriptComponent>();
                entityData["ScriptComponent"]["ClassName"] = script.ClassName;
                entityData["ScriptComponent"]["Enabled"] = script.Enabled;
                entityData["ScriptComponent"]["Fields"] = nlohmann::json::object();
                for (const auto& [name, value] : script.FieldOverrides)
                    entityData["ScriptComponent"]["Fields"][name] = value;
            }
        }

        void ApplyComponents(Entity entity, const nlohmann::json& entityData)
        {
            // 엔티티와 부모관계는 별도 단계에서 만든다. 여기서는 값 컴포넌트만 복원한다.
            if (entityData.contains("ActiveComponent"))
            {
                auto& active = entity.HasComponent<ActiveComponent>() ?
                    entity.GetComponent<ActiveComponent>() : entity.AddComponent<ActiveComponent>();
                active.ActiveSelf = entityData["ActiveComponent"].value("ActiveSelf", true);
            }

            if (entityData.contains("TransformComponent"))
            {
                auto& tc = entity.GetComponent<TransformComponent>();
                auto& transformData = entityData["TransformComponent"];
                tc.Translation = JsonToFloat3(transformData["Translation"]);
                tc.Rotation = JsonToFloat3(transformData["Rotation"]);
                tc.Scale = JsonToFloat3(transformData["Scale"]);
                if (transformData.contains("QuaternionRotation"))
                    tc.QuaternionRotation = JsonToFloat4(transformData["QuaternionRotation"]);
                else
                {
                    DirectX::XMStoreFloat4(
                        &tc.QuaternionRotation,
                        DirectX::XMQuaternionRotationRollPitchYaw(tc.Rotation.x, tc.Rotation.y, tc.Rotation.z));
                }
            }

            if (entityData.contains("SpriteRendererComponent"))
            {
                auto& spriteData = entityData["SpriteRendererComponent"];
                auto& sprite = entity.HasComponent<SpriteRendererComponent>() ? entity.GetComponent<SpriteRendererComponent>() : entity.AddComponent<SpriteRendererComponent>();
                sprite.Color = JsonToFloat4(spriteData["Color"]);
            }

            if (entityData.contains("CameraComponent"))
            {
                auto& cameraData = entityData["CameraComponent"];
                auto& camera = entity.HasComponent<CameraComponent>() ? entity.GetComponent<CameraComponent>() : entity.AddComponent<CameraComponent>();
                camera.FOV = cameraData["FOV"].get<float>();
                camera.NearClip = cameraData["NearClip"].get<float>();
                camera.FarClip = cameraData["FarClip"].get<float>();
                camera.Primary = cameraData["Primary"].get<bool>();
            }

            if (entityData.contains("AudioComponent"))
            {
                const auto& audioData = entityData["AudioComponent"];
                auto& audio = entity.HasComponent<AudioComponent>() ? entity.GetComponent<AudioComponent>() : entity.AddComponent<AudioComponent>();
                audio.AudioAssetGuid = audioData.value("AudioGuid", "");
                audio.AudioPath = audioData.value("AudioPath", "");
                if (!audio.AudioAssetGuid.empty())
                {
                    std::filesystem::path guidPath = AssetDatabase::GetPathFromGuid(audio.AudioAssetGuid);
                    if (!guidPath.empty())
                        audio.AudioPath = guidPath.string();
                }
                audio.Volume = std::clamp(audioData.value("Volume", 1.0f), 0.0f, 1.0f);
                audio.Pitch = (std::max)(0.01f, audioData.value("Pitch", 1.0f));
                audio.Loop = audioData.value("Loop", false);
                audio.PlayOnStart = audioData.value("PlayOnStart", false);
                audio.Enabled = audioData.value("Enabled", true);
                audio.RuntimePlaying = false;
                audio.RuntimePlayRequested = false;
                audio.RuntimeStopRequested = false;
                audio.RuntimeLastPlaySignal = 0.0f;
            }

            if (entityData.contains("MeshComponent"))
            {
                auto& meshData = entityData["MeshComponent"];
                auto& mesh = entity.HasComponent<MeshComponent>() ? entity.GetComponent<MeshComponent>() : entity.AddComponent<MeshComponent>();
                mesh.Type = static_cast<MeshComponent::MeshType>(meshData["Type"].get<int>());
                mesh.BaseColor = JsonToFloat4(meshData["BaseColor"]);
                // 기본 도형은 Type 값으로 다시 만든다. Custom Mesh는 모델 노드 복원 단계에서 다시 연결한다.
                mesh.MeshData = CreateMeshForType(mesh.Type);

                std::string textureGuid = meshData.value("AlbedoGuid", "");
                std::string texturePath;
                if (!textureGuid.empty())
                {
                    // 프리팹 안의 텍스처도 GUID를 우선한다. 인스턴스가 여러 개여도 같은 원본 에셋을 가리킨다.
                    texturePath = AssetDatabase::GetPathFromGuid(textureGuid).string();
                    mesh.AlbedoAssetGuid = textureGuid;
                }
                if (texturePath.empty() && meshData.contains("AlbedoPath"))
                {
                    texturePath = meshData["AlbedoPath"].get<std::string>();
                    if (mesh.AlbedoAssetGuid.empty())
                        mesh.AlbedoAssetGuid = AssetDatabase::GetGuidFromPath(texturePath);
                }
                mesh.AlbedoPath = texturePath;
                if (!texturePath.empty() && std::filesystem::exists(texturePath))
                    mesh.AlbedoMap.reset(Texture2D::Create(texturePath));

                mesh.Material = LoadMaterialReference(mesh, meshData);
            }

            if (entityData.contains("ModelComponent"))
            {
                auto& modelData = entityData["ModelComponent"];
                auto& modelComp = entity.HasComponent<ModelComponent>() ? entity.GetComponent<ModelComponent>() : entity.AddComponent<ModelComponent>();
                modelComp.ShowBoneLinks = modelData.value("ShowBoneLinks", false);

                std::string guid = modelData.value("Guid", "");
                std::string path;
                if (!guid.empty())
                {
                    // 프리팹도 GUID를 우선 사용한다. 같은 에셋을 옮겨도 인스턴스화가 끊기지 않게 하기 위해서다.
                    path = AssetDatabase::GetPathFromGuid(guid).string();
                    modelComp.AssetGuid = guid;
                }

                if (path.empty() && modelData.contains("Path"))
                {
                    path = modelData["Path"].get<std::string>();
                    if (modelComp.AssetGuid.empty())
                        modelComp.AssetGuid = AssetDatabase::GetGuidFromPath(path);
                }

                if (!path.empty() && std::filesystem::exists(path))
                {
                    // 프리팹에는 모델 파일 참조만 저장한다. 실제 메시와 본 정보는 인스턴스화할 때 다시 읽는다.
                    modelComp.TargetModel = std::make_shared<Model>(path);
                    if (!modelComp.TargetModel->GetBoneInfoMap().empty() && !entity.HasComponent<AnimatorComponent>())
                        entity.AddComponent<AnimatorComponent>();
                }
            }

            if (entityData.contains("Rigidbody2DComponent"))
            {
                auto& rbData = entityData["Rigidbody2DComponent"];
                auto& rb = entity.HasComponent<Rigidbody2DComponent>() ? entity.GetComponent<Rigidbody2DComponent>() : entity.AddComponent<Rigidbody2DComponent>();
                rb.Type = static_cast<Rigidbody2DComponent::BodyType>(rbData["Type"].get<int>());
                rb.FixedRotation = rbData["FixedRotation"].get<bool>();
            }

            if (entityData.contains("Rigidbody3DComponent"))
            {
                const auto& data = entityData["Rigidbody3DComponent"];
                auto& rb = entity.HasComponent<Rigidbody3DComponent>() ? entity.GetComponent<Rigidbody3DComponent>() : entity.AddComponent<Rigidbody3DComponent>();
                rb.Type = static_cast<Rigidbody3DComponent::BodyType>(data.value("Type", 0));
                rb.Mass = (std::max)(0.001f, data.value("Mass", 1.0f));
                rb.LinearDamping = (std::max)(0.0f, data.value("LinearDamping", 0.05f));
                rb.AngularDamping = (std::max)(0.0f, data.value("AngularDamping", 0.05f));
                rb.Friction = std::clamp(data.value("Friction", 0.5f), 0.0f, 1.0f);
                rb.Restitution = std::clamp(data.value("Restitution", 0.0f), 0.0f, 1.0f);
                rb.UseGravity = data.value("UseGravity", true);
                rb.FixedRotation = data.value("FixedRotation", false);
                if (data.contains("LinearVelocity")) rb.LinearVelocity = JsonToFloat3(data["LinearVelocity"]);
                if (data.contains("AngularVelocity")) rb.AngularVelocity = JsonToFloat3(data["AngularVelocity"]);
            }

            if (entityData.contains("BoxCollider2DComponent"))
            {
                auto& colliderData = entityData["BoxCollider2DComponent"];
                auto& collider = entity.HasComponent<BoxCollider2DComponent>() ? entity.GetComponent<BoxCollider2DComponent>() : entity.AddComponent<BoxCollider2DComponent>();
                collider.Offset = JsonToFloat2(colliderData["Offset"]);
                collider.Size = JsonToFloat2(colliderData["Size"]);
                collider.IsTrigger = colliderData.contains("IsTrigger") ? colliderData["IsTrigger"].get<bool>() : false;
                collider.Density = colliderData["Density"].get<float>();
                collider.Friction = colliderData["Friction"].get<float>();
                collider.Restitution = colliderData["Restitution"].get<float>();
            }

            if (entityData.contains("BoxCollider3DComponent"))
            {
                auto& colliderData = entityData["BoxCollider3DComponent"];
                auto& collider = entity.HasComponent<BoxCollider3DComponent>() ? entity.GetComponent<BoxCollider3DComponent>() : entity.AddComponent<BoxCollider3DComponent>();
                collider.Offset = JsonToFloat3(colliderData["Offset"]);
                collider.Size = JsonToFloat3(colliderData["Size"]);
                collider.IsTrigger = colliderData.value("IsTrigger", false);
            }

            if (entityData.contains("SphereCollider3DComponent"))
            {
                auto& colliderData = entityData["SphereCollider3DComponent"];
                auto& collider = entity.HasComponent<SphereCollider3DComponent>() ? entity.GetComponent<SphereCollider3DComponent>() : entity.AddComponent<SphereCollider3DComponent>();
                collider.Offset = JsonToFloat3(colliderData["Offset"]);
                collider.Radius = colliderData.value("Radius", 0.5f);
                collider.IsTrigger = colliderData.value("IsTrigger", false);
            }

            if (entityData.contains("CylinderCollider3DComponent"))
            {
                auto& colliderData = entityData["CylinderCollider3DComponent"];
                auto& collider = entity.HasComponent<CylinderCollider3DComponent>() ? entity.GetComponent<CylinderCollider3DComponent>() : entity.AddComponent<CylinderCollider3DComponent>();
                collider.Offset = JsonToFloat3(colliderData["Offset"]);
                collider.Radius = colliderData.value("Radius", 0.5f);
                collider.Height = colliderData.value("Height", 1.0f);
                collider.IsTrigger = colliderData.value("IsTrigger", false);
            }

            if (entityData.contains("MeshCollider3DComponent"))
            {
                auto& colliderData = entityData["MeshCollider3DComponent"];
                auto& collider = entity.HasComponent<MeshCollider3DComponent>() ? entity.GetComponent<MeshCollider3DComponent>() : entity.AddComponent<MeshCollider3DComponent>();
                collider.Offset = JsonToFloat3(colliderData["Offset"]);
                collider.Size = JsonToFloat3(colliderData["Size"]);
                collider.Convex = colliderData.value("Convex", false);
                collider.IsTrigger = colliderData.value("IsTrigger", false);
            }

            if (entityData.contains("LightComponent"))
            {
                auto& lightData = entityData["LightComponent"];
                auto& light = entity.HasComponent<LightComponent>() ? entity.GetComponent<LightComponent>() : entity.AddComponent<LightComponent>();
                light.LightColor = JsonToFloat3(lightData["LightColor"]);
                light.Intensity = lightData["Intensity"].get<float>();
            }

            if (entityData.contains("AnimatorComponent"))
            {
                const auto& data = entityData["AnimatorComponent"];
                auto& animator = entity.HasComponent<AnimatorComponent>() ? entity.GetComponent<AnimatorComponent>() : entity.AddComponent<AnimatorComponent>();
                animator.ControllerAssetGuid = data.value("ControllerGuid", "");
                animator.ControllerPath = data.value("ControllerPath", "");
                animator.SourceAssetGuid = data.value("SourceGuid", "");
                animator.SourcePath = data.value("SourcePath", "");
                animator.SelectedClipIndex = data.value("SelectedClipIndex", 0);
                animator.SelectedClipName = data.value("SelectedClipName", "");
                animator.AutoPlay = data.value("AutoPlay", true);
                animator.PreviewInEdit = data.value("PreviewInEdit", false);
                animator.Loop = data.value("Loop", true);
                animator.Speed = data.value("Speed", 1.0f);
                animator.ApplyRootMotion = data.value("ApplyRootMotion", false);
                animator.UpdateModeValue = static_cast<AnimatorComponent::UpdateMode>(data.value("UpdateMode", 0));
                animator.CullingModeValue = static_cast<AnimatorComponent::CullingMode>(data.value("CullingMode", 1));
                animator.AvatarGuid = data.value("AvatarGuid", "");
                animator.AvatarPath = data.value("AvatarPath", "");
                animator.SourceAvatarGuid = data.value("SourceAvatarGuid", "");
                animator.SourceAvatarPath = data.value("SourceAvatarPath", "");
                animator.HumanoidRootBone = data.value("HumanoidRootBone", "Hips");
                animator.RetargetToHumanoid = data.value("RetargetToHumanoid", false);
                animator.ActiveStateIndex = data.value("ActiveStateIndex", 0);
                animator.EntryStateIndex = data.value("EntryStateIndex", animator.ActiveStateIndex);
                animator.ActiveLayerIndex = data.value("ActiveLayerIndex", 0);
                animator.IsPlaying = false;
                animator.RuntimeClip.reset();
                animator.RuntimeClipKey.clear();
                animator.Layers.clear();
                if (data.contains("Layers") && data["Layers"].is_array())
                {
                    for (const auto& layerData : data["Layers"])
                    {
                        AnimatorComponent::Layer layer;
                        layer.Name = layerData.value("Name", "Layer");
                        layer.Weight = layerData.value("Weight", 1.0f);
                        layer.MaskRootBone = layerData.value("MaskRootBone", "");
                        layer.MaskBoneNames = layerData.value("MaskBoneNames", std::vector<std::string>{});
                        layer.Blending = static_cast<AnimatorComponent::Layer::BlendMode>(layerData.value("Blending", 0));
                        layer.Sync = layerData.value("Sync", false);
                        layer.IKPass = layerData.value("IKPass", false);
                        layer.Visible = layerData.value("Visible", true);
                        layer.ActiveStateIndex = layerData.value("ActiveStateIndex", -1);
                        layer.EntryStateIndex = layerData.value("EntryStateIndex", layer.ActiveStateIndex);
                        layer.SelectedTransitionIndex = layerData.value("SelectedTransitionIndex", -1);
                        if (layerData.contains("States") && layerData["States"].is_array())
                        {
                            for (const auto& stateData : layerData["States"])
                                layer.States.push_back(JsonToAnimatorState(stateData));
                        }
                        if (layerData.contains("Transitions") && layerData["Transitions"].is_array())
                        {
                            for (const auto& transitionData : layerData["Transitions"])
                                layer.Transitions.push_back(JsonToAnimatorTransition(transitionData));
                        }
                        animator.Layers.push_back(layer);
                    }
                }
                if (animator.Layers.empty())
                    animator.Layers.push_back({});
                animator.ActiveLayerIndex = std::clamp(animator.ActiveLayerIndex, 0, static_cast<int>(animator.Layers.size() - 1));

                animator.States.clear();
                if (data.contains("States") && data["States"].is_array())
                {
                    for (const auto& stateData : data["States"])
                        animator.States.push_back(JsonToAnimatorState(stateData));
                }
                animator.Parameters.clear();
                if (data.contains("Parameters") && data["Parameters"].is_array())
                {
                    for (const auto& parameterData : data["Parameters"])
                    {
                        AnimatorComponent::Parameter parameter;
                        parameter.Name = parameterData.value("Name", "Parameter");
                        parameter.ParamType = static_cast<AnimatorComponent::Parameter::Type>(parameterData.value("Type", 0));
                        parameter.FloatValue = parameterData.value("FloatValue", 0.0f);
                        parameter.BoolValue = parameterData.value("BoolValue", false);
                        animator.Parameters.push_back(parameter);
                    }
                }
                animator.Transitions.clear();
                if (data.contains("Transitions") && data["Transitions"].is_array())
                {
                    for (const auto& transitionData : data["Transitions"])
                        animator.Transitions.push_back(JsonToAnimatorTransition(transitionData));
                }
                if (animator.States.empty())
                {
                    animator.ActiveStateIndex = -1;
                    animator.EntryStateIndex = -1;
                    animator.Transitions.clear();
                    animator.SelectedTransitionIndex = -1;
                }
                else
                {
                    animator.ActiveStateIndex = std::clamp(animator.ActiveStateIndex, 0, static_cast<int>(animator.States.size() - 1));
                    animator.EntryStateIndex = std::clamp(animator.EntryStateIndex, 0, static_cast<int>(animator.States.size() - 1));
                    animator.Transitions.erase(
                        std::remove_if(animator.Transitions.begin(), animator.Transitions.end(), [&animator](const AnimatorComponent::Transition& transition)
                        {
                            const bool validFrom = transition.FromStateIndex == AnimatorComponent::Transition::AnyStateIndex ||
                                (transition.FromStateIndex >= 0 && transition.FromStateIndex < static_cast<int>(animator.States.size()));
                            const bool validTo = transition.ToStateIndex == AnimatorComponent::Transition::ExitStateIndex ||
                                (transition.ToStateIndex >= 0 && transition.ToStateIndex < static_cast<int>(animator.States.size()));
                            return !validFrom || !validTo;
                        }),
                        animator.Transitions.end());
                    for (auto& transition : animator.Transitions)
                    {
                        if (transition.ExitTargetStateIndex >= static_cast<int>(animator.States.size()))
                            transition.ExitTargetStateIndex = -1;
                    }
                    animator.SelectedTransitionIndex = std::clamp(animator.SelectedTransitionIndex, -1, static_cast<int>(animator.Transitions.size() - 1));
                }
                if (!animator.Layers.empty() && animator.Layers[0].States.empty() && !animator.States.empty())
                {
                    // 구버전 프리팹은 그래프가 전역 필드에만 있다.
                    // Base Layer로 옮겨 새 레이어 구조에서도 기존 프리팹이 그대로 열린다.
                    animator.Layers[0].States = animator.States;
                    animator.Layers[0].Transitions = animator.Transitions;
                    animator.Layers[0].ActiveStateIndex = animator.ActiveStateIndex;
                    animator.Layers[0].EntryStateIndex = animator.EntryStateIndex;
                    animator.Layers[0].SelectedTransitionIndex = animator.SelectedTransitionIndex;
                }
                for (auto& layer : animator.Layers)
                {
                    if (layer.States.empty())
                    {
                        layer.ActiveStateIndex = -1;
                        layer.EntryStateIndex = -1;
                        layer.SelectedTransitionIndex = -1;
                        layer.Transitions.clear();
                        continue;
                    }
                    layer.ActiveStateIndex = std::clamp(layer.ActiveStateIndex, 0, static_cast<int>(layer.States.size() - 1));
                    layer.EntryStateIndex = std::clamp(layer.EntryStateIndex, 0, static_cast<int>(layer.States.size() - 1));
                    layer.Transitions.erase(
                        std::remove_if(layer.Transitions.begin(), layer.Transitions.end(), [&layer](const AnimatorComponent::Transition& transition)
                        {
                            const bool validFrom = transition.FromStateIndex == AnimatorComponent::Transition::AnyStateIndex ||
                                (transition.FromStateIndex >= 0 && transition.FromStateIndex < static_cast<int>(layer.States.size()));
                            const bool validTo = transition.ToStateIndex == AnimatorComponent::Transition::ExitStateIndex ||
                                (transition.ToStateIndex >= 0 && transition.ToStateIndex < static_cast<int>(layer.States.size()));
                            return !validFrom || !validTo;
                        }),
                        layer.Transitions.end());
                    for (auto& transition : layer.Transitions)
                    {
                        if (transition.ExitTargetStateIndex >= static_cast<int>(layer.States.size()))
                            transition.ExitTargetStateIndex = -1;
                    }
                    layer.SelectedTransitionIndex = std::clamp(layer.SelectedTransitionIndex, -1, static_cast<int>(layer.Transitions.size() - 1));
                }
                if (!animator.Layers.empty())
                {
                    animator.States = animator.Layers[0].States;
                    animator.Transitions = animator.Layers[0].Transitions;
                    animator.ActiveStateIndex = animator.Layers[0].ActiveStateIndex;
                    animator.EntryStateIndex = animator.Layers[0].EntryStateIndex;
                    animator.SelectedTransitionIndex = animator.Layers[0].SelectedTransitionIndex;
                }

                std::filesystem::path controllerPath = animator.ControllerPath;
                if (!animator.ControllerAssetGuid.empty())
                {
                    std::filesystem::path guidPath = AssetDatabase::GetPathFromGuid(animator.ControllerAssetGuid);
                    if (!guidPath.empty())
                        controllerPath = guidPath;
                }
                if (!controllerPath.empty())
                {
                    // 프리팹도 Controller 파일을 공유할 수 있어야 한다.
                    // 저장된 복사본보다 Controller 에셋을 우선 적용하면 여러 인스턴스가 같은 상태머신을 바라본다.
                    AnimatorControllerAsset::LoadFromFile(controllerPath, animator);
                }
            }

            if (entityData.contains("ScriptComponent"))
            {
                const auto& scriptData = entityData["ScriptComponent"];
                auto& script = entity.HasComponent<ScriptComponent>() ?
                    entity.GetComponent<ScriptComponent>() : entity.AddComponent<ScriptComponent>();
                script.ClassName = scriptData.value("ClassName", "");
                script.Enabled = scriptData.value("Enabled", true);
                script.FieldOverrides.clear();
                if (scriptData.contains("Fields") && scriptData["Fields"].is_object())
                {
                    for (auto it = scriptData["Fields"].begin(); it != scriptData["Fields"].end(); ++it)
                        script.FieldOverrides[it.key()] = it.value().get<std::string>();
                }
                script.RuntimeInstanceCreated = false;
                script.RuntimeAwakeCalled = false;
                script.RuntimeEnabledCalled = false;
                script.RuntimeStartCalled = false;
            }
        }

        void RebuildModelNodeMaps(const nlohmann::json& entitiesData, const std::unordered_map<int, Entity>& entityMap)
        {
            for (auto& entityData : entitiesData)
            {
                if (!entityData.contains("ModelComponent") || !entityData["ModelComponent"].contains("NodePathLocalIDMap"))
                    continue;

                int localID = entityData["LocalID"].get<int>();
                auto entityIt = entityMap.find(localID);
                if (entityIt == entityMap.end())
                    continue;

                Entity modelEntity = entityIt->second;
                if (!modelEntity.HasComponent<ModelComponent>())
                    continue;

                auto& model = modelEntity.GetComponent<ModelComponent>();
                model.NodePathEntityMap.clear();
                model.NodeEntityMap.clear();

                // 저장된 LocalID를 현재 씬의 새 엔티티로 바꾼다. 이 과정을 거쳐야 본 선택과 기즈모가 다시 맞는다.
                for (auto& [nodePath, nodeLocalIDJson] : entityData["ModelComponent"]["NodePathLocalIDMap"].items())
                {
                    int nodeLocalID = nodeLocalIDJson.get<int>();
                    auto nodeIt = entityMap.find(nodeLocalID);
                    if (nodeIt == entityMap.end())
                        continue;

                    Entity nodeEntity = nodeIt->second;
                    model.NodePathEntityMap[nodePath] = (entt::entity)nodeEntity;
                    if (nodeEntity.HasComponent<TagComponent>())
                        model.NodeEntityMap[nodeEntity.GetComponent<TagComponent>().Tag] = (entt::entity)nodeEntity;
                }
            }
        }

        void RestoreCustomModelMeshes(Scene* scene, Entity entity)
        {
            if (!entity.HasComponent<ModelComponent>())
                return;

            auto& model = entity.GetComponent<ModelComponent>();
            if (!model.TargetModel)
                return;

            std::function<void(const ModelNode&, bool)> restoreNode = [&](const ModelNode& node, bool isRootNode)
            {
                if (isRootNode)
                {
                    for (const auto& child : node.Children)
                        restoreNode(child, false);
                    return;
                }

                auto nodeIt = model.NodePathEntityMap.find(node.Path);
                if (nodeIt != model.NodePathEntityMap.end() && scene->GetRegistry().valid(nodeIt->second))
                {
                    Entity nodeEntity(nodeIt->second, scene);
                    for (size_t meshIndex = 0; meshIndex < node.Meshes.size(); ++meshIndex)
                    {
                        Entity target = nodeEntity;
                        if (node.Meshes.size() > 1)
                        {
                            // 한 FBX 노드에 메시가 여러 개 있으면 저장된 SubMesh 자식에게 다시 나눠 준다.
                            std::string subMeshName = node.Name + "_SubMesh_" + std::to_string(meshIndex);
                            if (nodeEntity.HasComponent<RelationshipComponent>())
                            {
                                for (entt::entity childHandle : nodeEntity.GetComponent<RelationshipComponent>().Children)
                                {
                                    if (!scene->GetRegistry().valid(childHandle))
                                        continue;

                                    Entity child(childHandle, scene);
                                    if (child.HasComponent<TagComponent>() && child.GetComponent<TagComponent>().Tag == subMeshName)
                                    {
                                        target = child;
                                        break;
                                    }
                                }
                            }
                        }

                        if (target.HasComponent<MeshComponent>())
                        {
                            auto& mesh = target.GetComponent<MeshComponent>();
                            mesh.Type = MeshComponent::MeshType::Custom;
                            mesh.MeshData = node.Meshes[meshIndex];
                            if (!node.Meshes[meshIndex]->TexturePath.empty() && std::filesystem::exists(node.Meshes[meshIndex]->TexturePath))
                            {
                                mesh.AlbedoPath = node.Meshes[meshIndex]->TexturePath;
                                mesh.AlbedoAssetGuid = AssetDatabase::GetGuidFromPath(mesh.AlbedoPath);
                                mesh.AlbedoMap.reset(Texture2D::Create(node.Meshes[meshIndex]->TexturePath));
                            }
                        }
                    }
                }

                for (const auto& child : node.Children)
                    restoreNode(child, false);
            };

            // Custom Mesh의 버퍼 자체는 프리팹 파일에 저장하지 않는다. 원본 모델에서 다시 찾아 연결한다.
            restoreNode(model.TargetModel->GetRootNode(), true);
        }
    }

    bool PrefabSerializer::Serialize(Scene* scene, Entity rootEntity, const std::string& filepath)
    {
        if (!scene || !rootEntity)
            return false;

        std::vector<Entity> entities;
        CollectPrefabEntities(scene, rootEntity, entities);

        std::unordered_map<entt::entity, int> localIDs;
        for (size_t i = 0; i < entities.size(); ++i)
            localIDs[(entt::entity)entities[i]] = static_cast<int>(i);

        nlohmann::json prefabData;
        prefabData["Prefab"] = rootEntity.HasComponent<TagComponent>() ? rootEntity.GetComponent<TagComponent>().Tag : "Untitled Prefab";
        prefabData["Version"] = 1;
        prefabData["Root"] = 0;
        prefabData["Entities"] = nlohmann::json::array();

        for (Entity entity : entities)
        {
            nlohmann::json entityData;
            entt::entity handle = (entt::entity)entity;
            entityData["LocalID"] = localIDs[handle];
            entityData["ParentID"] = -1;

            if (entity.HasComponent<RelationshipComponent>())
            {
                entt::entity parent = entity.GetComponent<RelationshipComponent>().Parent;
                auto found = localIDs.find(parent);
                if (found != localIDs.end())
                    entityData["ParentID"] = found->second;
            }

            SerializeComponents(entity, entityData, localIDs);
            prefabData["Entities"].push_back(entityData);
        }

        std::filesystem::path outputPath(filepath);
        if (outputPath.has_parent_path())
            std::filesystem::create_directories(outputPath.parent_path());

        std::ofstream fout(filepath);
        if (!fout.is_open())
            return false;

        fout << prefabData.dump(4);
        AssetDatabase::EnsureMetaFile(filepath);
        return true;
    }

    Entity PrefabSerializer::Deserialize(Scene* scene, const std::string& filepath)
    {
        if (!scene)
            return {};

        std::ifstream stream(filepath);
        if (!stream.is_open())
            return {};

        nlohmann::json data;
        stream >> data;

        if (!data.contains("Entities") || !data["Entities"].is_array())
            return {};

        std::unordered_map<int, Entity> entityMap;
        Entity rootEntity;

        for (auto& entityData : data["Entities"])
        {
            int localID = entityData["LocalID"].get<int>();
            std::string tag = "Prefab Entity";
            if (entityData.contains("TagComponent") && entityData["TagComponent"].contains("Tag"))
                tag = entityData["TagComponent"]["Tag"].get<std::string>();

            Entity entity = scene->CreateEntity(tag);
            entityMap[localID] = entity;

            if (data.contains("Root") && localID == data["Root"].get<int>())
                rootEntity = entity;
        }

        for (auto& entityData : data["Entities"])
        {
            int localID = entityData["LocalID"].get<int>();
            int parentID = entityData.value("ParentID", -1);
            Entity entity = entityMap[localID];

            if (parentID >= 0 && entityMap.find(parentID) != entityMap.end())
            {
                Entity parent = entityMap[parentID];
                auto& childRel = entity.HasComponent<RelationshipComponent>() ? entity.GetComponent<RelationshipComponent>() : entity.AddComponent<RelationshipComponent>();
                childRel.Parent = (entt::entity)parent;

                auto& parentRel = parent.HasComponent<RelationshipComponent>() ? parent.GetComponent<RelationshipComponent>() : parent.AddComponent<RelationshipComponent>();
                parentRel.Children.push_back((entt::entity)entity);
            }

            ApplyComponents(entity, entityData);
        }

        RebuildModelNodeMaps(data["Entities"], entityMap);

        for (auto& [localID, entity] : entityMap)
            RestoreCustomModelMeshes(scene, entity);

        if (!rootEntity && !entityMap.empty())
            rootEntity = entityMap.begin()->second;

        return rootEntity;
    }
}
