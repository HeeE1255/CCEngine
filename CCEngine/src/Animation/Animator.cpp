#include "Animator.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <cmath>
#include <windows.h> // 디버그

#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Renderer/Model.h"


namespace CCEngine
{
    // =========================================================
    // 1. BoneAnimChannel (현재 시간에 맞는 프레임 찾기)
    // =========================================================
    void BoneAnimChannel::UpdateLocalTransform(float currentTime, DirectX::XMFLOAT3& outPos, DirectX::XMFLOAT4& outRot, DirectX::XMFLOAT3& outScale)
    {
        if (!PositionKeys.empty())
        {
            outPos = PositionKeys[0].second;
            for (size_t i = 0; i < PositionKeys.size() - 1; ++i)
            {
                if (currentTime < PositionKeys[i + 1].first)
                {
                    outPos = PositionKeys[i].second;
                    break;
                }
            }
        }

        if (!RotationKeys.empty())
        {
            outRot = RotationKeys[0].second;
            for (size_t i = 0; i < RotationKeys.size() - 1; ++i)
            {
                if (currentTime < RotationKeys[i + 1].first)
                {
                    outRot = RotationKeys[i].second;
                    break;
                }
            }
        }

        if (!ScaleKeys.empty())
        {
            outScale = ScaleKeys[0].second;
            for (size_t i = 0; i < ScaleKeys.size() - 1; ++i)
            {
                if (currentTime < ScaleKeys[i + 1].first)
                {
                    outScale = ScaleKeys[i].second;
                    break;
                }
            }
        }
    }

    // =========================================================
    // 2. AnimationClip (파일에서 애니메이션 추출)
    // =========================================================
    AnimationClip::AnimationClip(const std::string& path)
    {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate);

        if (scene && scene->mAnimations)
        {
            aiAnimation* anim = scene->mAnimations[0]; // 0번째 애니메이션 로드
            m_Duration = anim->mDuration;
            m_TicksPerSecond = anim->mTicksPerSecond != 0.0 ? anim->mTicksPerSecond : 25.0f;

            for (unsigned int i = 0; i < anim->mNumChannels; ++i)
            {
                aiNodeAnim* channel = anim->mChannels[i];
                BoneAnimChannel boneChannel;
                boneChannel.NodeName = channel->mNodeName.C_Str();

                for (unsigned int p = 0; p < channel->mNumPositionKeys; ++p)
                {
                    aiVector3D pos = channel->mPositionKeys[p].mValue;
                    float time = channel->mPositionKeys[p].mTime;
                    boneChannel.PositionKeys.push_back({ time, {pos.x, pos.y, pos.z} });
                }

                for (unsigned int r = 0; r < channel->mNumRotationKeys; ++r)
                {
                    aiQuaternion rot = channel->mRotationKeys[r].mValue;
                    float time = channel->mRotationKeys[r].mTime;
                    boneChannel.RotationKeys.push_back({ time, {rot.x, rot.y, rot.z, rot.w} });
                }

                for (unsigned int s = 0; s < channel->mNumScalingKeys; ++s)
                {
                    aiVector3D scale = channel->mScalingKeys[s].mValue;
                    float time = channel->mScalingKeys[s].mTime;
                    boneChannel.ScaleKeys.push_back({ time, {scale.x, scale.y, scale.z} });
                }

                m_Channels[boneChannel.NodeName] = boneChannel;
            }
        }
    }

    BoneAnimChannel* AnimationClip::GetBoneChannel(const std::string& nodeName)
    {
        if (m_Channels.find(nodeName) != m_Channels.end())
        {
            return &m_Channels[nodeName];
        }
        return nullptr;
    }

    // =========================================================
    // 3. Animator (재생 및 행렬 계산)
    // =========================================================
    Animator::Animator()
    {
        // 뼈대 행렬 배열을 512개로 넉넉히 초기화 (GPU 버퍼용)
        m_FinalBoneMatrices.resize(512, DirectX::XMMatrixIdentity());
    }

    void Animator::PlayAnimation(AnimationClip* clip)
    {
        m_CurrentClip = clip;
        m_CurrentTime = 0.0f;
    }

    void Animator::Update(float deltaTime, Model* model, Scene* scene)
    {
        if (m_CurrentClip)
        {
            // 시간에 따른 프레임 진행
            m_CurrentTime += m_CurrentClip->GetTicksPerSecond() * deltaTime;
            m_CurrentTime = fmod(m_CurrentTime, m_CurrentClip->GetDuration()); // 무한 반복
        }

        CalculateBoneTransform(model->GetRootNode(), DirectX::XMMatrixIdentity(), model, scene);

    }

    DirectX::XMMATRIX Animator::GetGlobalBoneMatrix(const std::string& boneName)
    {
        if (m_GlobalBoneMatrices.find(boneName) != m_GlobalBoneMatrices.end())
        {
            return m_GlobalBoneMatrices[boneName];
        }
            
        return DirectX::XMMatrixIdentity();
    }

    bool Animator::HasBone(const std::string& boneName)
    {
        return m_GlobalBoneMatrices.find(boneName) != m_GlobalBoneMatrices.end();
    }

    int Animator::GetBoneIndex(const std::string& name, Model* model)
    {
        auto& boneMap = model->GetBoneInfoMap();
        if (boneMap.find(name) != boneMap.end())
        {
            return boneMap[name].id;
        }
            
        return -1;
    }

    DirectX::XMMATRIX Animator::GetFinalMatrix(int index)
    {
        if (index >= 0 && index < m_FinalBoneMatrices.size())
        {
            return m_FinalBoneMatrices[index];
        }
            
        return DirectX::XMMatrixIdentity();
    }

    void Animator::CalculateBoneTransform(const ModelNode& node, DirectX::XMMATRIX parentTransform, Model* model, Scene* scene)
    {
        std::string nodeName = node.Name;
        DirectX::XMMATRIX nodeTransform;

        // 1. [안전 장치] m_CurrentClip이 nullptr인지 반드시 먼저 확인!
        BoneAnimChannel* channel = m_CurrentClip ? m_CurrentClip->GetBoneChannel(nodeName) : nullptr;

        if (channel)
        {
            // 2-A. 애니메이션 재생 중: 키프레임 데이터 사용
            DirectX::XMFLOAT3 pos, scale;
            DirectX::XMFLOAT4 rot;
            channel->UpdateLocalTransform(m_CurrentTime, pos, rot, scale);

            nodeTransform = DirectX::XMMatrixScaling(scale.x, scale.y, scale.z) * DirectX::XMMatrixRotationQuaternion(DirectX::XMLoadFloat4(&rot)) * DirectX::XMMatrixTranslation(pos.x, pos.y, pos.z);
        }
        else
        {
            // 2-B. 애니메이션이 없을 때: 씬에서 엔티티 트랜스폼 훔쳐오기!
            Entity entity = scene ? scene->FindEntityByName(nodeName) : Entity{};

            if (entity)
            {
                // 에디터에서 기즈모로 조작 중인 실시간 값을 가져옴
                auto& tc = entity.GetComponent<TransformComponent>();
                nodeTransform = DirectX::XMMatrixScaling(tc.Scale.x, tc.Scale.y, tc.Scale.z) * DirectX::XMMatrixRotationQuaternion(DirectX::XMLoadFloat4(&tc.QuaternionRotation)) * DirectX::XMMatrixTranslation(tc.Translation.x, tc.Translation.y, tc.Translation.z);
            }
            else
            {
                // 엔티티도 못 찾으면 모델 원본의 기본 T-포즈 데이터 사용
                nodeTransform = DirectX::XMMatrixScaling(node.Scale.x, node.Scale.y, node.Scale.z) * DirectX::XMMatrixRotationQuaternion(DirectX::XMLoadFloat4(&node.Rotation)) * DirectX::XMMatrixTranslation(node.Translation.x, node.Translation.y, node.Translation.z);
            }
        }

        // 3. 부모 행렬과 곱해서 월드 행렬 완성
        DirectX::XMMATRIX globalTransform = nodeTransform * parentTransform;
        m_GlobalBoneMatrices[nodeName] = globalTransform;

        // 4. 이 노드가 뼈(Bone)라면, 최종 행렬 저장!
        auto& boneInfoMap = model->GetBoneInfoMap();
        if (boneInfoMap.find(nodeName) != boneInfoMap.end())
        {
            int index = boneInfoMap[nodeName].id;
            DirectX::XMMATRIX offset = boneInfoMap[nodeName].offset;

            // Final Matrix = Offset Matrix * Global Transform
            m_FinalBoneMatrices[index] = offset * globalTransform;
        }

        // 5. 자식들도 똑같이 계산
        for (const auto& child : node.Children)
        {
            CalculateBoneTransform(child, globalTransform, model, scene);
        }
    }
}