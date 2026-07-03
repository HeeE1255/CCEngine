#include "Scene/SceneSerializer.h"
#include "Core/AssetDatabase.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Renderer/MeshFactory.h"
#include "Renderer/Texture.h"

#include "json.hpp"
#include <fstream>
#include <filesystem>
#include <functional>
#include <unordered_map>

namespace CCEngine
{
    namespace
    {
        nlohmann::json Float2ToJson(const DirectX::XMFLOAT2& value)
        {
            return { value.x, value.y };
        }

        nlohmann::json Float3ToJson(const DirectX::XMFLOAT3& value)
        {
            return { value.x, value.y, value.z };
        }

        nlohmann::json Float4ToJson(const DirectX::XMFLOAT4& value)
        {
            return { value.x, value.y, value.z, value.w };
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

        void SerializeEntityComponents(Entity entity, nlohmann::json& entityData)
        {
            // 엔티티가 가진 컴포넌트만 기록한다. 없는 컴포넌트는 저장하지 않아야 로드할 때 불필요하게 붙지 않는다.
            if (entity.HasComponent<TagComponent>())
                entityData["TagComponent"]["Tag"] = entity.GetComponent<TagComponent>().Tag;

            if (entity.HasComponent<TransformComponent>())
            {
                auto& transform = entity.GetComponent<TransformComponent>();
                entityData["TransformComponent"]["Translation"] = Float3ToJson(transform.Translation);
                entityData["TransformComponent"]["Rotation"] = Float3ToJson(transform.Rotation);
                entityData["TransformComponent"]["Scale"] = Float3ToJson(transform.Scale);
                entityData["TransformComponent"]["QuaternionRotation"] = Float4ToJson(transform.QuaternionRotation);
            }

            if (entity.HasComponent<SpriteRendererComponent>())
                entityData["SpriteRendererComponent"]["Color"] = Float4ToJson(entity.GetComponent<SpriteRendererComponent>().Color);

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

                // FBX 노드와 엔티티의 연결 정보다. 로드 후 새 엔티티 ID로 다시 매핑해야 구조가 유지된다.
                for (const auto& [nodePath, nodeEntity] : model.NodePathEntityMap)
                    entityData["ModelComponent"]["NodePathEntityMap"][nodePath] = static_cast<uint32_t>(nodeEntity);
            }

            if (entity.HasComponent<LightComponent>())
            {
                auto& light = entity.GetComponent<LightComponent>();
                entityData["LightComponent"]["LightColor"] = Float3ToJson(light.LightColor);
                entityData["LightComponent"]["Intensity"] = light.Intensity;
            }

            if (entity.HasComponent<CameraComponent>())
            {
                auto& camera = entity.GetComponent<CameraComponent>();
                entityData["CameraComponent"]["FOV"] = camera.FOV;
                entityData["CameraComponent"]["NearClip"] = camera.NearClip;
                entityData["CameraComponent"]["FarClip"] = camera.FarClip;
                entityData["CameraComponent"]["Primary"] = camera.Primary;
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
        }

        void ApplyEntityComponents(Entity entity, const nlohmann::json& entityData)
        {
            // 엔티티는 먼저 모두 만든 뒤 컴포넌트를 붙인다. 그래야 부모나 모델 노드가 서로를 참조할 수 있다.
            if (entityData.contains("TransformComponent"))
            {
                auto& tc = entity.GetComponent<TransformComponent>();
                auto& transformData = entityData["TransformComponent"];
                tc.Translation = JsonToFloat3(transformData["Translation"]);
                tc.Rotation = JsonToFloat3(transformData["Rotation"]);
                tc.Scale = JsonToFloat3(transformData["Scale"]);
                if (transformData.contains("QuaternionRotation"))
                {
                    tc.QuaternionRotation = JsonToFloat4(transformData["QuaternionRotation"]);
                }
                else
                {
                    DirectX::XMStoreFloat4(
                        &tc.QuaternionRotation,
                        DirectX::XMQuaternionRotationRollPitchYaw(tc.Rotation.x, tc.Rotation.y, tc.Rotation.z));
                }
            }

            if (entityData.contains("SpriteRendererComponent"))
            {
                auto& src = entityData["SpriteRendererComponent"];
                auto& sprite = entity.HasComponent<SpriteRendererComponent>() ? entity.GetComponent<SpriteRendererComponent>() : entity.AddComponent<SpriteRendererComponent>();
                sprite.Color = JsonToFloat4(src["Color"]);
            }

            if (entityData.contains("MeshComponent"))
            {
                auto& meshData = entityData["MeshComponent"];
                auto& mesh = entity.HasComponent<MeshComponent>() ? entity.GetComponent<MeshComponent>() : entity.AddComponent<MeshComponent>();
                mesh.Type = static_cast<MeshComponent::MeshType>(meshData["Type"].get<int>());
                mesh.BaseColor = JsonToFloat4(meshData["BaseColor"]);
                // 저장된 MeshType 숫자로 기본 메시를 다시 만든다. GPU 버퍼 포인터 자체는 파일에 저장하지 않는다.
                mesh.MeshData = CreateMeshForType(mesh.Type);

                std::string textureGuid = meshData.value("AlbedoGuid", "");
                std::string texturePath;
                if (!textureGuid.empty())
                {
                    // 텍스처도 GUID를 먼저 본다. 파일명이 바뀌어도 meta가 남아 있으면 다시 찾을 수 있다.
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
                auto& model = entity.HasComponent<ModelComponent>() ? entity.GetComponent<ModelComponent>() : entity.AddComponent<ModelComponent>();
                model.ShowBoneLinks = modelData.value("ShowBoneLinks", false);

                std::string guid = modelData.value("Guid", "");
                std::string path;
                if (!guid.empty())
                {
                    // 새 저장 포맷은 GUID로 먼저 찾는다. 경로가 바뀐 에셋도 meta가 있으면 복원된다.
                    path = AssetDatabase::GetPathFromGuid(guid).string();
                    model.AssetGuid = guid;
                }

                if (path.empty() && modelData.contains("Path"))
                {
                    path = modelData["Path"].get<std::string>();
                    if (model.AssetGuid.empty())
                        model.AssetGuid = AssetDatabase::GetGuidFromPath(path);
                }

                if (!path.empty() && std::filesystem::exists(path))
                {
                    // 모델 파일은 참조만 저장하고, 실제 메시와 본 데이터는 로드 시점에 다시 읽어 온다.
                    model.TargetModel = std::make_shared<Model>(path);
                    if (!model.TargetModel->GetBoneInfoMap().empty() && !entity.HasComponent<AnimatorComponent>())
                        entity.AddComponent<AnimatorComponent>();
                }
            }

            if (entityData.contains("LightComponent"))
            {
                auto& lightData = entityData["LightComponent"];
                auto& light = entity.HasComponent<LightComponent>() ? entity.GetComponent<LightComponent>() : entity.AddComponent<LightComponent>();
                light.LightColor = JsonToFloat3(lightData["LightColor"]);
                light.Intensity = lightData["Intensity"].get<float>();
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
        }

        void SerializeRelationship(Entity entity, nlohmann::json& entityData)
        {
            if (!entity.HasComponent<RelationshipComponent>())
                return;

            auto& rel = entity.GetComponent<RelationshipComponent>();
            // 자식 목록은 저장하지 않는다. 각 자식의 Parent만 저장하고 로드할 때 부모의 Children을 다시 채운다.
            if (rel.Parent != entt::null)
                entityData["RelationshipComponent"]["Parent"] = static_cast<uint32_t>(rel.Parent);
        }

        void RestoreRelationships(Scene* scene, const nlohmann::json& entitiesData, const std::unordered_map<uint32_t, Entity>& entityMap, entt::entity appendRoot = entt::null)
        {
            // 저장된 entt ID는 현재 실행에서 그대로 쓸 수 없다. old ID -> 새 Entity 맵을 통해 부모를 다시 찾는다.
            for (auto& entityData : entitiesData)
            {
                uint32_t oldID = entityData["Entity"].get<uint32_t>();
                auto entityIt = entityMap.find(oldID);
                if (entityIt == entityMap.end())
                    continue;

                Entity entity = entityIt->second;
                entt::entity parentHandle = appendRoot;

                if (entityData.contains("RelationshipComponent") && entityData["RelationshipComponent"].contains("Parent"))
                {
                    uint32_t oldParentID = entityData["RelationshipComponent"]["Parent"].get<uint32_t>();
                    auto parentIt = entityMap.find(oldParentID);
                    if (parentIt != entityMap.end())
                        parentHandle = (entt::entity)parentIt->second;
                }

                if (parentHandle == entt::null)
                    continue;

                auto& childRel = entity.HasComponent<RelationshipComponent>() ? entity.GetComponent<RelationshipComponent>() : entity.AddComponent<RelationshipComponent>();
                childRel.Parent = parentHandle;

                Entity parent(parentHandle, scene);
                auto& parentRel = parent.HasComponent<RelationshipComponent>() ? parent.GetComponent<RelationshipComponent>() : parent.AddComponent<RelationshipComponent>();
                parentRel.Children.push_back((entt::entity)entity);
            }
        }

        void RebuildModelNodeMaps(const nlohmann::json& entitiesData, const std::unordered_map<uint32_t, Entity>& entityMap)
        {
            for (auto& entityData : entitiesData)
            {
                if (!entityData.contains("ModelComponent") || !entityData["ModelComponent"].contains("NodePathEntityMap"))
                    continue;

                uint32_t oldID = entityData["Entity"].get<uint32_t>();
                auto entityIt = entityMap.find(oldID);
                if (entityIt == entityMap.end())
                    continue;

                Entity modelEntity = entityIt->second;
                if (!modelEntity.HasComponent<ModelComponent>())
                    continue;

                auto& model = modelEntity.GetComponent<ModelComponent>();
                model.NodePathEntityMap.clear();
                model.NodeEntityMap.clear();

                // 저장된 FBX 노드 맵도 old ID 기준이다. 새로 생성된 엔티티로 바꿔야 본 선택과 기즈모가 맞는다.
                for (auto& [nodePath, oldNodeIDJson] : entityData["ModelComponent"]["NodePathEntityMap"].items())
                {
                    uint32_t oldNodeID = oldNodeIDJson.get<uint32_t>();
                    auto nodeIt = entityMap.find(oldNodeID);
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
                            // 한 노드에 메시가 여러 개 있으면 하위 SubMesh 엔티티에 나눠 붙인다.
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

            // FBX의 Custom Mesh는 파일에 버퍼를 저장하지 않고, 원본 모델에서 다시 찾아 연결한다.
            restoreNode(model.TargetModel->GetRootNode(), true);
        }
    }

    SceneSerializer::SceneSerializer(Scene* scene)
        : m_Scene(scene)
    {
    }

    bool SceneSerializer::Serialize(const std::string& filepath)
    {
        nlohmann::json sceneData;
        sceneData["Scene"] = "Untitled Scene";
        sceneData["Entities"] = nlohmann::json::array();

        m_Scene->m_Registry.view<TransformComponent>().each([&](auto entityID, auto& transform)
            {
                Entity entity = { entityID, m_Scene };

                nlohmann::json entityData;
                entityData["Entity"] = static_cast<uint32_t>(entityID);

                // 씬 파일에는 컴포넌트 값만 저장한다. 런타임용 포인터나 물리 ID는 다시 만든다.
                SerializeEntityComponents(entity, entityData);
                SerializeRelationship(entity, entityData);

                sceneData["Entities"].push_back(entityData);
            });

        std::ofstream fout(filepath);
        if (!fout.is_open())
            return false;

        fout << sceneData.dump(4);
        AssetDatabase::EnsureMetaFile(filepath);
        return true;
    }

    bool SceneSerializer::Deserialize(const std::string& filepath)
    {
        std::ifstream stream(filepath);
        if (!stream.is_open())
            return false;

        nlohmann::json data;
        stream >> data;

        if (!data.contains("Entities"))
            return false;

        m_Scene->m_Registry.clear();

        std::unordered_map<uint32_t, Entity> entityMap;

        // 1단계: 파일에 있던 모든 엔티티를 먼저 만든다. 이 단계에서는 관계를 아직 연결하지 않는다.
        for (auto& entityData : data["Entities"])
        {
            std::string name = "Untitled Entity";
            if (entityData.contains("TagComponent"))
                name = entityData["TagComponent"]["Tag"];

            Entity entity = m_Scene->CreateEntity(name);
            uint32_t oldID = entityData["Entity"].get<uint32_t>();
            entityMap[oldID] = entity;
        }

        // 2단계: 컴포넌트 값을 복원한다. 이때 ModelComponent는 원본 모델 파일을 다시 로드한다.
        for (auto& entityData : data["Entities"])
        {
            uint32_t oldID = entityData["Entity"].get<uint32_t>();
            auto it = entityMap.find(oldID);
            if (it == entityMap.end())
                continue;

            ApplyEntityComponents(it->second, entityData);
        }

        // 3단계: 새 엔티티 ID 기준으로 부모 관계와 FBX 노드 맵을 다시 연결한다.
        RestoreRelationships(m_Scene, data["Entities"], entityMap);
        RebuildModelNodeMaps(data["Entities"], entityMap);

        // 4단계: FBX에서 온 Custom Mesh를 원본 모델 노드에서 찾아 다시 꽂는다.
        for (auto& [oldID, entity] : entityMap)
            RestoreCustomModelMeshes(m_Scene, entity);

        return true;
    }

    Entity SceneSerializer::DeserializeAppend(const std::string& filepath)
    {
        std::ifstream stream(filepath);
        if (!stream.is_open())
            return {};

        nlohmann::json data;
        stream >> data;
        if (!data.contains("Entities"))
            return {};

        std::filesystem::path scenePath(filepath);
        Entity sceneRoot = m_Scene->CreateEntity("Scene: " + scenePath.stem().string());
        sceneRoot.AddComponent<RelationshipComponent>();

        std::unordered_map<uint32_t, Entity> entityMap;

        // 씬을 추가 로드할 때는 파일 안의 최상위 엔티티들을 새 Scene Root 밑으로 묶는다.
        for (auto& entityData : data["Entities"])
        {
            std::string name = "Untitled Entity";
            if (entityData.contains("TagComponent"))
                name = entityData["TagComponent"]["Tag"];

            Entity entity = m_Scene->CreateEntity(name);
            uint32_t oldID = entityData["Entity"].get<uint32_t>();
            entityMap[oldID] = entity;
        }

        for (auto& entityData : data["Entities"])
        {
            uint32_t oldID = entityData["Entity"].get<uint32_t>();
            auto it = entityMap.find(oldID);
            if (it == entityMap.end())
                continue;

            ApplyEntityComponents(it->second, entityData);
        }

        RestoreRelationships(m_Scene, data["Entities"], entityMap, (entt::entity)sceneRoot);
        RebuildModelNodeMaps(data["Entities"], entityMap);

        for (auto& [oldID, entity] : entityMap)
            RestoreCustomModelMeshes(m_Scene, entity);

        return sceneRoot;
    }
}
