#include "Model.h"
#include <iostream>

namespace CCEngine
{
    Model::Model(const std::string& path)
        : m_FilePath(path)
    {
        LoadModel(path);
    }

    void Model::LoadModel(const std::string& path)
    {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path,
            aiProcess_Triangulate |
            aiProcess_GenSmoothNormals |
            aiProcess_FlipUVs |
            aiProcess_JoinIdenticalVertices |
            aiProcess_ConvertToLeftHanded
        );

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
        {
            std::cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << std::endl;
            return;
        }

        m_Directory = path.substr(0, path.find_last_of('/'));
        if (m_Directory == path)
        {
            m_Directory = path.substr(0, path.find_last_of('\\'));
        }

        m_RootNode = ProcessNode(scene->mRootNode, scene);
    }

    ModelNode Model::ProcessNode(aiNode* node, const aiScene* scene)
    {
        ModelNode resultNode;
        resultNode.Name = node->mName.C_Str();
        if (resultNode.Name.empty())
        {
            resultNode.Name = "UnnamedNode";
        }

        // 트랜스폼 데이터 추출
        aiVector3D position, scaling;
        aiQuaternion rotation;
        node->mTransformation.Decompose(scaling, rotation, position);

        resultNode.Translation = { position.x, position.y, position.z };
        resultNode.Rotation = { rotation.x, rotation.y, rotation.z, rotation.w };
        resultNode.Scale = { scaling.x, scaling.y, scaling.z };

        for (unsigned int i = 0; i < node->mNumMeshes; i++)
        {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            auto parsedMesh = ProcessMesh(mesh, scene, resultNode.Name);

            resultNode.Meshes.push_back(parsedMesh);
            m_AllMeshes.push_back(parsedMesh);
        }

        for (unsigned int i = 0; i < node->mNumChildren; i++)
        {
            resultNode.Children.push_back(ProcessNode(node->mChildren[i], scene));
        }

        return resultNode;
    }

    std::shared_ptr<Mesh> Model::ProcessMesh(aiMesh* mesh, const aiScene* scene, const std::string& nodeName)
    {
        std::vector<Vertex3D> vertices;
        std::vector<uint32_t> indices;

        for (unsigned int i = 0; i < mesh->mNumVertices; i++)
        {
            Vertex3D vertex;

            // 초기화: 뼈 정보는 -1과 0으로 초기화
            for (int j = 0; j < 4; j++)
            {
                vertex.BoneIDs[j] = -1;
                vertex.Weights[j] = 0.0f;
            }

            vertex.Position = { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z };

            if (mesh->HasNormals())
            {
                vertex.Normal = { mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z };
            }
            else
            {
                vertex.Normal = { 0.0f, 0.0f, 0.0f };
            }

            if (mesh->mTextureCoords[0])
            {
                vertex.TexCoord = { mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y };
            }
            else
            {
                vertex.TexCoord = { 0.0f, 0.0f };
            }

            vertices.push_back(vertex);
        }

        for (unsigned int i = 0; i < mesh->mNumFaces; i++)
        {
            aiFace face = mesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; j++)
            {
                indices.push_back(face.mIndices[j]);
            }
        }

        // 뼈 가중치 추출
        ExtractBoneWeightForVertices(vertices, mesh, scene);

        auto newMesh = std::make_shared<Mesh>(vertices, indices);

        std::string meshName = mesh->mName.C_Str();
        if (meshName.empty())
        {
            meshName = nodeName;
        }
        if (meshName.empty())
        {
            meshName = "Unnamed_Mesh";
        }
        newMesh->Name = meshName;

        if (mesh->mMaterialIndex >= 0)
        {
            aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
            aiString str;
            if (material->GetTexture(aiTextureType_DIFFUSE, 0, &str) == AI_SUCCESS)
            {
                newMesh->TexturePath = m_Directory + "/" + str.C_Str();
            }
        }

        return newMesh;
    }

    void Model::SetVertexBoneData(Vertex3D& vertex, int boneID, float weight)
    {
        // 4개의 빈칸 중 하나를 찾아서 뼈 ID와 가중치를 쑤셔 넣음
        for (int i = 0; i < 4; ++i)
        {
            if (vertex.BoneIDs[i] < 0)
            {
                vertex.Weights[i] = weight;
                vertex.BoneIDs[i] = boneID;
                break;
            }
        }
    }

    void Model::ExtractBoneWeightForVertices(std::vector<Vertex3D>& vertices, aiMesh* mesh, const aiScene* scene)
    {
        for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
        {
            int boneID = -1;
            std::string boneName = mesh->mBones[boneIndex]->mName.C_Str();

            // 1. 새로운 뼈라면 Map에 등록하고 ID 발급
            if (m_BoneInfoMap.find(boneName) == m_BoneInfoMap.end())
            {
                BoneInfo newBoneInfo;
                newBoneInfo.id = m_BoneCounter;
                newBoneInfo.offset = ConvertAssimpMatrix(mesh->mBones[boneIndex]->mOffsetMatrix);

                m_BoneInfoMap[boneName] = newBoneInfo;
                boneID = m_BoneCounter;
                m_BoneCounter++;
            }
            else
            {
                // 이미 등록된 뼈라면 ID만 가져옴
                boneID = m_BoneInfoMap[boneName].id;
            }

            // 2. 이 뼈가 영향을 주는 정점(Vertex)들을 찾아서 데이터 세팅
            auto weights = mesh->mBones[boneIndex]->mWeights;
            int numWeights = mesh->mBones[boneIndex]->mNumWeights;

            for (int weightIndex = 0; weightIndex < numWeights; ++weightIndex)
            {
                int vertexId = weights[weightIndex].mVertexId;
                float weight = weights[weightIndex].mWeight;

                SetVertexBoneData(vertices[vertexId], boneID, weight);
            }
        }
    }

    DirectX::XMMATRIX Model::ConvertAssimpMatrix(const aiMatrix4x4& from)
    {
        // 1. Assimp 데이터를 그대로 XMMATRIX로 옮겨 담음
        DirectX::XMMATRIX mat = DirectX::XMMatrixSet(
            from.a1, from.a2, from.a3, from.a4,
            from.b1, from.b2, from.b3, from.b4,
            from.c1, from.c2, from.c3, from.c4,
            from.d1, from.d2, from.d3, from.d4
        );

        // 2. [핵심!!] 행과 열을 뒤집어서 DirectX가 올바르게 읽을 수 있도록 변환
        return DirectX::XMMatrixTranspose(mat);
    }
}