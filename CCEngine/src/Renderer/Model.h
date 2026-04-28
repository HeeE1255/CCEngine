#pragma once
#include "Mesh.h"
#include "Core.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <string>
#include <vector>
#include <map>

namespace CCEngine
{
    struct BoneInfo
    {
        int id;
        DirectX::XMMATRIX offset; // Inverse Bind Matrix (살점을 뼈 원점으로 당겨오는 행렬)
    };

    struct ModelNode
    {
        std::string Name;
        DirectX::XMFLOAT3 Translation = { 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT4 Rotation = { 0.0f, 0.0f, 0.0f, 1.0f }; // Quaternion
        DirectX::XMFLOAT3 Scale = { 1.0f, 1.0f, 1.0f };

        std::vector<std::shared_ptr<Mesh>> Meshes; // 이 노드에 달린 메쉬들
        std::vector<ModelNode> Children;           // 자식 노드들
    };

    class CC_API Model
    {
    public:
        // 파일 경로를 받아서 Assimp로 파싱을 시작하는 생성자
        Model(const std::string& path);
        ~Model() = default;

        // 모델이 로드된 파일의 디렉토리 경로를 반환하는 함수 (텍스처 로딩에 필요)
        const std::string& GetFilePath() const { return m_FilePath; }
        // 렌더러가 부품(Mesh)들을 꺼내서 그릴 수 있도록 리스트를 반환
        const std::vector<std::shared_ptr<Mesh>>& GetMeshes() const { return m_AllMeshes; }
        const ModelNode& GetRootNode() const { return m_RootNode; }

        auto& GetBoneInfoMap() { return m_BoneInfoMap; }
        int GetBoneCount() const { return m_BoneCounter; }

    private:
        std::string m_Directory;
        std::string m_FilePath;

        std::vector<std::shared_ptr<Mesh>> m_AllMeshes; // 인스펙터 표(Table) 출력용
        ModelNode m_RootNode; // 하이어라키 트리 생성용 원본 데이터

        std::map<std::string, BoneInfo> m_BoneInfoMap; // 뼈 이름 -> 뼈 정보(인덱스, 오프셋 행렬) 매핑
        int m_BoneCounter = 0; // 뼈 개수 카운터 (새로운 뼈가 나올 때마다 1씩 증가)

        void ExtractBoneWeightForVertices(std::vector<Vertex3D>& vertices, aiMesh* mesh, const aiScene* scene);
        void SetVertexBoneData(Vertex3D& vertex, int boneID, float weight);
        DirectX::XMMATRIX ConvertAssimpMatrix(const aiMatrix4x4& from);

        // Assimp 파싱을 위한 재귀 함수들
        void LoadModel(const std::string& path);
        ModelNode ProcessNode(aiNode* node, const aiScene* scene);
        std::shared_ptr<Mesh> ProcessMesh(aiMesh* mesh, const aiScene* scene, const std::string& nodeName);
    };
}