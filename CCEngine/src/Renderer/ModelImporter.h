#pragma once
#include "Core.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Renderer/Model.h"
#include <string>
#include <memory>

namespace CCEngine {

    class CC_API ModelImporter
    {
    public:
        // 파일 경로와 씬을 넘겨주면, 모델을 파싱해서 씬에 조립하고 루트 엔티티를 반환합니다.
        static Entity ImportModel(Scene* scene, const std::string& filepath);

    private:
        // 내부에서 재귀적으로 트리를 구성하는 헬퍼 함수
        static void BuildTree(Scene* scene, const ModelNode& node, entt::entity parentHandle, bool isRootNode, DirectX::XMMATRIX correctionMat);
    };

}