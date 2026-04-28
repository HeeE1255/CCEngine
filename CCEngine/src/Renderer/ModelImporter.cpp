#include "ModelImporter.h"
#include "Scene/Components.h"
#include "Renderer/Texture.h"
#include <filesystem>

namespace CCEngine {

    Entity ModelImporter::ImportModel(Scene* scene, const std::string& filepath)
    {
        // 1. 모델 로드
        std::shared_ptr<Model> myModel = std::make_shared<Model>(filepath);

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
        modelRootEntity.AddComponent<ModelComponent>(myModel);

        if (!myModel->GetBoneInfoMap().empty()) 
        {
            modelRootEntity.AddComponent<AnimatorComponent>();
        }

        // 4. 트리 빌드 시작
        BuildTree(scene, myModel->GetRootNode(), (entt::entity)modelRootEntity, true, DirectX::XMMatrixIdentity());

        return modelRootEntity;
    }

    void ModelImporter::BuildTree(Scene* scene, const ModelNode& node, entt::entity parentHandle, bool isRootNode, DirectX::XMMATRIX correctionMat) 
    {
        //평탄화를 위해 최초 행렬은 아이덴티티로 시작, RootNode에서만 교정 행렬을 곱해서 내려보냄
        entt::entity currentHandle = parentHandle;

        // 1. RootNode인 경우: 엔티티를 만들지 않고, 자식들에게 넘겨줄 '교정행렬'을 준비
        if (isRootNode)
        {
            float angle = DirectX::XMConvertToRadians(-90.0f);
            DirectX::XMVECTOR q = DirectX::XMQuaternionRotationRollPitchYaw(angle, 0.0f, 0.0f);

            //XMMatrixMultiply 사용 (S * R * T 순서)
            DirectX::XMMATRIX S = DirectX::XMMatrixScaling(node.Scale.x, node.Scale.y, node.Scale.z);
            DirectX::XMMATRIX R = DirectX::XMMatrixRotationQuaternion(q);
            DirectX::XMMATRIX T = DirectX::XMMatrixTranslation(node.Translation.x, node.Translation.y, node.Translation.z);

            DirectX::XMMATRIX SR = DirectX::XMMatrixMultiply(S, R);
            DirectX::XMMATRIX rootTransform = DirectX::XMMatrixMultiply(SR, T);

            for (size_t i = 0; i < node.Children.size(); ++i)
            {
                DirectX::XMMATRIX currentCorrection = rootTransform;

                // 첫 번째 자식(Index 0, 보통 Armature)일 때만 x축 90도 회전을 추가 적용
                if (i == 0)
                {
                    float firstChildAngle = DirectX::XMConvertToRadians(90.0f);
                    DirectX::XMMATRIX firstChildRot = DirectX::XMMatrixRotationX(firstChildAngle);

                    // 자식의 로컬 회전이 부모(RootNode) 공간으로 넘어가기 전(-90도)에 적용되도록 앞에 곱함
                    currentCorrection = DirectX::XMMatrixMultiply(firstChildRot, rootTransform);
                }

                BuildTree(scene, node.Children[i], parentHandle, false, currentCorrection);
            }
            return;
        }

        // 2. 일반 노드인 경우 (RootNode의 자식 포함)
        std::string entityName = node.Name.empty() ? "UnnamedNode" : node.Name;
        Entity currentEntity = scene->CreateEntity(entityName);
        currentHandle = (entt::entity)currentEntity;

        auto& tc = currentEntity.GetComponent<TransformComponent>();

        // 노드 본연의 로컬 행렬을 계산합니다.
        DirectX::XMMATRIX localS = DirectX::XMMatrixScaling(node.Scale.x, node.Scale.y, node.Scale.z);
        DirectX::XMMATRIX localR = DirectX::XMMatrixRotationQuaternion(DirectX::XMLoadFloat4(&node.Rotation));
        DirectX::XMMATRIX localT = DirectX::XMMatrixTranslation(node.Translation.x, node.Translation.y, node.Translation.z);

        DirectX::XMMATRIX localSR = DirectX::XMMatrixMultiply(localS, localR);
        DirectX::XMMATRIX localMat = DirectX::XMMatrixMultiply(localSR, localT);

        // 부모로부터 받은 correctionMat 곱하기 (Local * Correction)
        DirectX::XMMATRIX finalMat = DirectX::XMMatrixMultiply(localMat, correctionMat);


        // 최종 행렬을 다시 Translation, Rotation, Scale로 분해해서 TransformComponent에 박습니다.
        DirectX::XMVECTOR s, r, t;
        if (DirectX::XMMatrixDecompose(&s, &r, &t, finalMat))
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
            BuildTree(scene, childNode, currentHandle, false, DirectX::XMMatrixIdentity());
        }
    }

}