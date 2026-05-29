#include "Scene/PrefabSerializer.h"
#include "Scene/Components.h"
#include "Renderer/MeshFactory.h"
#include "json.hpp"
#include <filesystem>
#include <fstream>
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

        void SerializeComponents(Entity entity, nlohmann::json& entityData)
        {
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
            }

            if (entity.HasComponent<ModelComponent>())
            {
                auto& model = entity.GetComponent<ModelComponent>();
                if (model.TargetModel)
                    entityData["ModelComponent"]["Path"] = model.TargetModel->GetFilePath();
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
            if (entityData.contains("TransformComponent"))
            {
                auto& tc = entity.GetComponent<TransformComponent>();
                auto& transformData = entityData["TransformComponent"];
                tc.Translation = JsonToFloat3(transformData["Translation"]);
                tc.Rotation = JsonToFloat3(transformData["Rotation"]);
                tc.Scale = JsonToFloat3(transformData["Scale"]);
                if (transformData.contains("QuaternionRotation"))
                    tc.QuaternionRotation = JsonToFloat4(transformData["QuaternionRotation"]);
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

                switch (mesh.Type)
                {
                    case MeshComponent::MeshType::Cube: mesh.MeshData = MeshFactory::CreateCube(); break;
                    case MeshComponent::MeshType::Plane: mesh.MeshData = MeshFactory::CreatePlane(); break;
                    case MeshComponent::MeshType::Sphere: mesh.MeshData = MeshFactory::CreateSphere(); break;
                    default: break;
                }
            }

            if (entityData.contains("ModelComponent") && entityData["ModelComponent"].contains("Path"))
            {
                std::string path = entityData["ModelComponent"]["Path"].get<std::string>();
                if (!path.empty() && std::filesystem::exists(path))
                {
                    auto model = std::make_shared<Model>(path);
                    auto& modelComp = entity.HasComponent<ModelComponent>() ? entity.GetComponent<ModelComponent>() : entity.AddComponent<ModelComponent>();
                    modelComp.TargetModel = model;
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

            SerializeComponents(entity, entityData);
            prefabData["Entities"].push_back(entityData);
        }

        std::filesystem::path outputPath(filepath);
        if (outputPath.has_parent_path())
            std::filesystem::create_directories(outputPath.parent_path());

        std::ofstream fout(filepath);
        if (!fout.is_open())
            return false;

        fout << prefabData.dump(4);
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

        if (!rootEntity && !entityMap.empty())
            rootEntity = entityMap.begin()->second;

        return rootEntity;
    }
}
