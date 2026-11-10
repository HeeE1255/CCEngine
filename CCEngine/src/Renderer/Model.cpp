#include "Model.h"
#include "Animation/Animator.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include "Core/ConsoleLog.h"
#include <assimp/GltfMaterial.h>

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

        // 모델 FBX를 읽는 순간 애니메이션 데이터도 이미 aiScene 안에 있다.
        // 이때 클립 캐시에 복사해 두면 Animator 시작 시 같은 FBX를 다시 열지 않아도 된다.
        AnimationClip::PrimeCacheFromScene(path, scene);

        m_Directory = std::filesystem::path(path).parent_path().string();
        ReadMaterials(scene);

        m_RootNode = ProcessNode(scene->mRootNode, scene);
    }

    void Model::ReadMaterials(const aiScene* scene)
    {
        m_ImportedMaterials.clear();
        m_ImportedMaterials.reserve(scene->mNumMaterials);
        std::unordered_map<const aiTexture*, std::shared_ptr<const ImportedEmbeddedTexture>> embeddedCache;
        size_t embeddedBytes = 0;
        constexpr size_t maxEmbeddedBytes = 256u * 1024u * 1024u;
        auto copyEmbedded = [&](const aiTexture* image) -> std::shared_ptr<const ImportedEmbeddedTexture>
        {
            const auto cached = embeddedCache.find(image);
            if (cached != embeddedCache.end()) return cached->second;
            auto& cachedValue = embeddedCache[image];
            const uint64_t bytes = image->mHeight == 0 ? image->mWidth :
                uint64_t(image->mWidth) * image->mHeight * 4u;
            if (!image->pcData || !bytes || bytes > maxEmbeddedBytes - embeddedBytes ||
                (image->mHeight && (image->mWidth > 16384 || image->mHeight > 16384)))
            {
                ConsoleLog::Warning("Embedded texture rejected: invalid dimensions or 256 MiB model budget exceeded: " + m_FilePath);
                return nullptr;
            }
            auto data = std::make_shared<ImportedEmbeddedTexture>();
            for (; data->SourceIndex < scene->mNumTextures; ++data->SourceIndex)
                if (scene->mTextures[data->SourceIndex] == image) break;
            if (data->SourceIndex == scene->mNumTextures) return nullptr;
            data->Width = image->mWidth;
            data->Height = image->mHeight;
            data->Bytes.resize(static_cast<size_t>(bytes));
            // Assimp Importer가 파괴되면 pcData도 사라진다. 참조를 저장하지 않고 한 번만 복사해 공유한다.
            if (!image->mHeight)
                std::copy_n(reinterpret_cast<const unsigned char*>(image->pcData), data->Bytes.size(), data->Bytes.data());
            else
                for (size_t pixel = 0; pixel < data->Bytes.size() / 4; ++pixel)
                {
                    const auto& value = image->pcData[pixel];
                    data->Bytes[pixel * 4] = value.b;
                    data->Bytes[pixel * 4 + 1] = value.g;
                    data->Bytes[pixel * 4 + 2] = value.r;
                    data->Bytes[pixel * 4 + 3] = value.a;
                }
            embeddedBytes += data->Bytes.size();
            cachedValue = data;
            return cachedValue;
        };
        const auto finiteUnit = [](float value, float fallback)
        {
            return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : fallback;
        };
        for (unsigned int index = 0; index < scene->mNumMaterials; ++index)
        {
            const aiMaterial* source = scene->mMaterials[index];
            auto material = std::make_shared<ImportedMaterialData>();
            material->SourceIndex = index;
            material->ModelPath = m_FilePath;
            aiString name;
            source->Get(AI_MATKEY_NAME, name);
            material->Name = name.length ? name.C_Str() : "Material " + std::to_string(index);
            aiColor4D color(1, 1, 1, 1);
            if (source->Get(AI_MATKEY_BASE_COLOR, color) != AI_SUCCESS)
                source->Get(AI_MATKEY_COLOR_DIFFUSE, color);
            material->BaseColor = { finiteUnit(color.r, 1), finiteUnit(color.g, 1),
                finiteUnit(color.b, 1), finiteUnit(color.a, 1) };
            float value = 0.0f;
            if (source->Get(AI_MATKEY_METALLIC_FACTOR, value) == AI_SUCCESS)
                material->Metallic = finiteUnit(value, 0.0f);
            if (source->Get(AI_MATKEY_ROUGHNESS_FACTOR, value) == AI_SUCCESS)
                material->Roughness = finiteUnit(value, 0.5f);
            if (source->Get(AI_MATKEY_OPACITY, value) == AI_SUCCESS)
                material->Opacity = finiteUnit(value, 1.0f);

            auto readTextures = [&](aiTextureType type, ImportedTextureSemantic semantic)
            {
                for (unsigned int slot = 0; slot < source->GetTextureCount(type); ++slot)
                {
                    aiString reference;
                    unsigned int uvChannel = 0;
                    aiTextureMapping mapping = aiTextureMapping_UV;
                    aiTextureMapMode address[3] = { aiTextureMapMode_Wrap, aiTextureMapMode_Wrap, aiTextureMapMode_Wrap };
                    aiTextureOp operation = aiTextureOp_Multiply;
                    ai_real blend = 1.0f;
                    if (source->GetTexture(type, slot, &reference, &mapping, &uvChannel, &blend, &operation, address) != AI_SUCCESS)
                        continue;
                    ImportedTextureReference texture;
                    texture.Semantic = semantic;
                    texture.SourceReference = reference.C_Str();
                    texture.UVChannel = uvChannel;
                    texture.SourceTextureType = static_cast<unsigned int>(type);
                    texture.SourceTextureSlot = slot;
                    texture.Mapping = static_cast<unsigned int>(mapping);
                    texture.AddressU = static_cast<unsigned int>(address[0]);
                    texture.AddressV = static_cast<unsigned int>(address[1]);
                    texture.Operation = static_cast<unsigned int>(operation);
                    texture.Blend = static_cast<float>(blend);
                    source->Get(AI_MATKEY_TEXFLAGS(type, slot), texture.Flags);
                    aiUVTransform transform;
                    source->Get(AI_MATKEY_UVTRANSFORM(type, slot), transform);
                    // Assimp 후처리 이후의 UV 규약을 보존한다. 회전은 라디안이며 중심은 (0.5, 0.5)다.
                    // 파일 원본 좌표로 재해석하면 FlipUVs가 반영된 메시와 방향이 달라질 수 있다.
                    texture.UVOffset = { transform.mTranslation.x, transform.mTranslation.y };
                    texture.UVScale = { transform.mScaling.x, transform.mScaling.y };
                    texture.UVRotation = transform.mRotation;
                    source->Get(AI_MATKEY_GLTF_MAPPINGFILTER_MIN(type, slot), texture.MinFilter);
                    source->Get(AI_MATKEY_GLTF_MAPPINGFILTER_MAG(type, slot), texture.MagFilter);
                    texture.ValidSampling = std::isfinite(texture.Blend) && std::isfinite(texture.UVOffset.x) &&
                        std::isfinite(texture.UVOffset.y) && std::isfinite(texture.UVScale.x) &&
                        std::isfinite(texture.UVScale.y) && std::isfinite(texture.UVRotation);
                    // 내장 텍스처의 *번호는 파일명이 아니다. 원본 모델과 참조를 함께 보존해
                    // 나중에 추출할 때 찾을 수 있게 하고, 일반 파일 로더에는 전달하지 않는다.
                    const auto* embedded = scene->GetEmbeddedTexture(reference.C_Str());
                    texture.Embedded = embedded != nullptr;
                    if (texture.Embedded)
                    {
                        texture.EmbeddedData = copyEmbedded(embedded);
                        texture.Exists = texture.EmbeddedData != nullptr;
                    }
                    else if (!texture.SourceReference.empty() && texture.SourceReference.front() != '*')
                    {
                        auto path = std::filesystem::path(texture.SourceReference);
                        if (path.is_relative()) path = std::filesystem::path(m_Directory) / path;
                        texture.FilePath = path.lexically_normal().string();
                        std::error_code error;
                        texture.Exists = std::filesystem::is_regular_file(path, error);
                    }
                    material->Textures.push_back(std::move(texture));
                }
            };
            readTextures(source->GetTextureCount(aiTextureType_BASE_COLOR) > 0
                ? aiTextureType_BASE_COLOR : aiTextureType_DIFFUSE, ImportedTextureSemantic::BaseColor);
            readTextures(aiTextureType_NORMALS, ImportedTextureSemantic::Normal);
            // Metallic/Roughness가 같은 이미지여도 서로 다른 의미로 보존한다.
            // 채널 패킹 규칙을 확인하기 전에는 두 값을 같은 RGB 샘플로 간주하면 안 된다.
            readTextures(aiTextureType_METALNESS, ImportedTextureSemantic::Metallic);
            readTextures(aiTextureType_DIFFUSE_ROUGHNESS, ImportedTextureSemantic::Roughness);
            m_ImportedMaterials.push_back(std::move(material));
        }
    }

    ModelNode Model::ProcessNode(aiNode* node, const aiScene* scene, const std::string& parentPath)
    {
        ModelNode resultNode;
        resultNode.Name = node->mName.C_Str();
        if (resultNode.Name.empty())
        {
            resultNode.Name = "UnnamedNode";
        }
        resultNode.Path = parentPath.empty() ? resultNode.Name : parentPath + "/" + resultNode.Name;

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
            resultNode.Children.push_back(ProcessNode(node->mChildren[i], scene, resultNode.Path));
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

        if (mesh->mMaterialIndex < m_ImportedMaterials.size())
        {
            newMesh->ImportedMaterial = m_ImportedMaterials[mesh->mMaterialIndex];
            for (const auto& texture : newMesh->ImportedMaterial->Textures)
            {
                if (texture.Semantic == ImportedTextureSemantic::BaseColor &&
                    texture.UVChannel == 0 && texture.Mapping == aiTextureMapping_UV &&
                    texture.ValidSampling && !texture.Embedded && texture.Exists)
                {
                    newMesh->TexturePath = texture.FilePath;
                    break;
                }
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
