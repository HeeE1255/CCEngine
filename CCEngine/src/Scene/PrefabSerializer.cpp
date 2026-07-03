#include "Scene/PrefabSerializer.h"
#include "Core/AssetDatabase.h"
#include "Scene/Components.h"
#include "Renderer/MeshFactory.h"
#include "Renderer/Texture.h"
#include "json.hpp"
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

            if (entity.HasComponent<BoxCollider2DComponent>())
            {
                auto& collider = entity.GetComponent<BoxCollider2DComponent>();
                entityData["BoxCollider2DComponent"]["Offset"] = Float2ToJson(collider.Offset);
                entityData["BoxCollider2DComponent"]["Size"] = Float2ToJson(collider.Size);
                entityData["BoxCollider2DComponent"]["Density"] = collider.Density;
                entityData["BoxCollider2DComponent"]["Friction"] = collider.Friction;
                entityData["BoxCollider2DComponent"]["Restitution"] = collider.Restitution;
            }

            if (entity.HasComponent<LightComponent>())
            {
                auto& light = entity.GetComponent<LightComponent>();
                entityData["LightComponent"]["LightColor"] = Float3ToJson(light.LightColor);
                entityData["LightComponent"]["Intensity"] = light.Intensity;
            }

            if (entity.HasComponent<AnimatorComponent>())
            {
                entityData["AnimatorComponent"] = nlohmann::json::object();
            }
        }

        void ApplyComponents(Entity entity, const nlohmann::json& entityData)
        {
            // 엔티티와 부모관계는 별도 단계에서 만든다. 여기서는 값 컴포넌트만 복원한다.
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

            if (entityData.contains("BoxCollider2DComponent"))
            {
                auto& colliderData = entityData["BoxCollider2DComponent"];
                auto& collider = entity.HasComponent<BoxCollider2DComponent>() ? entity.GetComponent<BoxCollider2DComponent>() : entity.AddComponent<BoxCollider2DComponent>();
                collider.Offset = JsonToFloat2(colliderData["Offset"]);
                collider.Size = JsonToFloat2(colliderData["Size"]);
                collider.Density = colliderData["Density"].get<float>();
                collider.Friction = colliderData["Friction"].get<float>();
                collider.Restitution = colliderData["Restitution"].get<float>();
            }

            if (entityData.contains("LightComponent"))
            {
                auto& lightData = entityData["LightComponent"];
                auto& light = entity.HasComponent<LightComponent>() ? entity.GetComponent<LightComponent>() : entity.AddComponent<LightComponent>();
                light.LightColor = JsonToFloat3(lightData["LightColor"]);
                light.Intensity = lightData["Intensity"].get<float>();
            }

            if (entityData.contains("AnimatorComponent") && !entity.HasComponent<AnimatorComponent>())
            {
                entity.AddComponent<AnimatorComponent>();
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
