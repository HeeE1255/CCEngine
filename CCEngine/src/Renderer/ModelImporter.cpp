#include "ModelImporter.h"
#include "Core/AssetDatabase.h"
#include "Scene/Components.h"
#include "Renderer/Texture.h"
#include <filesystem>
#include <unordered_map>

namespace CCEngine {
    namespace
    {
        std::shared_ptr<Model> GetOrLoadModel(const std::string& filepath)
        {
            static std::unordered_map<std::string, std::weak_ptr<Model>> s_ModelCache;

            std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(std::filesystem::path(filepath));
            std::string cacheKey = canonicalPath.string();

            auto it = s_ModelCache.find(cacheKey);
            if (it != s_ModelCache.end())
            {
                if (auto cachedModel = it->second.lock())
                {
                    return cachedModel;
                }
            }

            auto model = std::make_shared<Model>(filepath);
            s_ModelCache[cacheKey] = model;
            return model;
        }
    }

    Entity ModelImporter::ImportModel(Scene* scene, const std::string& filepath)
    {
        // 1. 모델 로드
        std::shared_ptr<Model> myModel = GetOrLoadModel(filepath);

        // 2. 파일명에서 확장자를 제외한 이름을 추출해서 최상위 엔티티 이름으로 사용 (예: "FBX_MAYO")
        std::filesystem::path path(filepath);
        std::string modelName = path.stem().string();

        // 3. 모델 루트 엔티티 생성
        Entity modelRootEntity = scene->CreateEntity(modelName);
        auto& rootTc = modelRootEntity.GetComponent<TransformComponent>();

        rootTc.Translation = { 0.0f, 0.0f, 0.0f };
        rootTc.Rotation = { 0.0f, 0.0f, 0.0f };
        rootTc.QuaternionRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
        rootTc.Scale = { 1.0f, 1.0f, 1.0f };

        modelRootEntity.AddComponent<RelationshipComponent>();
        auto& modelComponent = modelRootEntity.AddComponent<ModelComponent>(myModel);
        // 임포트된 모델 루트에 원본 에셋 GUID를 남겨 저장/프리팹에서 같은 파일을 다시 찾는다.
        modelComponent.AssetGuid = AssetDatabase::GetGuidFromPath(filepath);

        if (!myModel->GetBoneInfoMap().empty()) 
        {
            modelRootEntity.AddComponent<AnimatorComponent>();
        }

        // 4. 트리 빌드 시작
        BuildTree(scene, myModel->GetRootNode(), (entt::entity)modelRootEntity, true, modelComponent);

        return modelRootEntity;
    }

    void ModelImporter::BuildTree(Scene* scene, const ModelNode& node, entt::entity parentHandle, bool isRootNode, ModelComponent& modelComponent) 
    {
        entt::entity currentHandle = parentHandle;

        // 1. Assimp RootNode 자체는 파일명 루트 엔티티가 대신하고,
        //    Armature 같은 자식 노드의 원본 보정(-90도 등)은 하이어라키에 그대로 유지합니다.
        if (isRootNode)
        {
            for (const auto& childNode : node.Children)
            {
                BuildTree(scene, childNode, parentHandle, false, modelComponent);
            }
            return;
        }

        // 2. 일반 노드인 경우 (RootNode의 자식 포함)
        std::string entityName = node.Name.empty() ? "UnnamedNode" : node.Name;
        Entity currentEntity = scene->CreateEntity(entityName);
        currentHandle = (entt::entity)currentEntity;
        modelComponent.NodeEntityMap[entityName] = currentHandle;
        modelComponent.NodePathEntityMap[node.Path] = currentHandle;

        auto& tc = currentEntity.GetComponent<TransformComponent>();

        // 노드 본연의 로컬 행렬을 계산합니다.
        DirectX::XMMATRIX localS = DirectX::XMMatrixScaling(node.Scale.x, node.Scale.y, node.Scale.z);
        DirectX::XMMATRIX localR = DirectX::XMMatrixRotationQuaternion(DirectX::XMLoadFloat4(&node.Rotation));
        DirectX::XMMATRIX localT = DirectX::XMMatrixTranslation(node.Translation.x, node.Translation.y, node.Translation.z);

        DirectX::XMMATRIX localSR = DirectX::XMMatrixMultiply(localS, localR);
        DirectX::XMMATRIX localMat = DirectX::XMMatrixMultiply(localSR, localT);

        // Unity처럼 임포트 보정은 하이어라키 트랜스폼에 남기고, 노드 로컬 트랜스폼을 그대로 씁니다.
        DirectX::XMVECTOR s, r, t;
        if (DirectX::XMMatrixDecompose(&s, &r, &t, localMat))
        {
            DirectX::XMStoreFloat3(&tc.Scale, s);
            DirectX::XMStoreFloat4(&tc.QuaternionRotation, r);
            DirectX::XMStoreFloat3(&tc.Translation, t);
        }

        currentEntity.AddComponent<RelationshipComponent>();
        currentEntity.GetComponent<RelationshipComponent>().Parent = parentHandle;
        scene->GetRegistry().get<RelationshipComponent>(parentHandle).Children.push_back(currentHandle);

        // 3. 메쉬 세팅 (기존 로직 동일)
        for (size_t i = 0; i < node.Meshes.size(); ++i)
        {
            entt::entity targetHandle = currentHandle;
            if (node.Meshes.size() > 1) {
                Entity targetEntity = scene->CreateEntity(entityName + "_SubMesh_" + std::to_string(i));
                targetHandle = (entt::entity)targetEntity;
                targetEntity.AddComponent<RelationshipComponent>();
                targetEntity.GetComponent<RelationshipComponent>().Parent = currentHandle;
                scene->GetRegistry().get<RelationshipComponent>(currentHandle).Children.push_back(targetHandle);
            }
            Entity targetEnt{ targetHandle, scene };
            auto& meshComp = targetEnt.AddComponent<MeshComponent>(MeshComponent::MeshType::Custom);
            meshComp.MeshData = node.Meshes[i];
            if (!node.Meshes[i]->TexturePath.empty() && std::filesystem::exists(node.Meshes[i]->TexturePath))
                meshComp.AlbedoMap.reset(Texture2D::Create(node.Meshes[i]->TexturePath));
        }

        // 4. 자식 노드 재귀 호출 (상속 끝났으니 다음 레벨은 Identity를 넘김)
        for (const auto& childNode : node.Children)
        {
            BuildTree(scene, childNode, currentHandle, false, modelComponent);
        }
    }

}
