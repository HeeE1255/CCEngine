#pragma once
#include "Core.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Renderer/Model.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <utility>
#include <filesystem>
#include <vector>

namespace CCEngine {
    struct ModelComponent;
    struct MaterialAsset;

    class CC_API ModelImporter
    {
    public:
        // 파일 경로와 씬을 넘겨주면, 모델을 파싱해서 씬에 조립하고 루트 엔티티를 반환합니다.
        static Entity ImportModel(Scene* scene, const std::string& filepath);
        static void ClearCache();
        static bool ReimportMaterials(const std::string& filepath, bool adoptLegacyBaseline = false,
            std::vector<std::string>* refreshedMaterials = nullptr);
        static bool BindMaterial(const std::string& filepath, unsigned int slot, const std::string& expectedName, const std::string& materialPath);
        // 삭제 승인 목록이 아닌 보수적인 정리 검토 보고서다. 동적/미저장 참조는 별도 확인이 필요하다.
        static std::vector<std::string> AuditExtractedImages(const std::filesystem::path& modelPath,
            const std::filesystem::path& assetsRoot);

    private:
        using ImportedMaterialMap = std::unordered_map<unsigned int,
            std::pair<std::string, std::shared_ptr<MaterialAsset>>>;
        static ImportedMaterialMap PrepareMaterials(const Model& model, bool reimport = false, bool adoptLegacyBaseline = false,
            int explicitSlot = -1, const std::string& explicitPath = {});
        // 내부에서 재귀적으로 트리를 구성하는 헬퍼 함수
        static void BuildTree(Scene* scene, const ModelNode& node, entt::entity parentHandle, bool isRootNode, ModelComponent& modelComponent, const ImportedMaterialMap& materials);
    };

}
