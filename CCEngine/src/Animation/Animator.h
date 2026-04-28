#pragma once
#include "Renderer/Model.h"
#include "Core.h"
#include <DirectXMath.h>
#include <map>
#include <vector>
#include <unordered_map>
#include <string>

namespace CCEngine
{
    class Scene;

    // 1. 뼈 하나의 시간별 키프레임들
    struct BoneAnimChannel
    {
        std::string NodeName;
        std::vector<std::pair<float, DirectX::XMFLOAT3>> PositionKeys;
        std::vector<std::pair<float, DirectX::XMFLOAT4>> RotationKeys;
        std::vector<std::pair<float, DirectX::XMFLOAT3>> ScaleKeys;

        void UpdateLocalTransform(float currentTime, DirectX::XMFLOAT3& outPos, DirectX::XMFLOAT4& outRot, DirectX::XMFLOAT3& outScale);
    };

    // 2. 애니메이션 클립 (예: 달리기, 걷기)
    class CC_API AnimationClip
    {
    public:
        AnimationClip(const std::string& path);
        ~AnimationClip() = default;

        float GetTicksPerSecond() const { return m_TicksPerSecond; }
        float GetDuration() const { return m_Duration; }
        BoneAnimChannel* GetBoneChannel(const std::string& nodeName);

    private:
        float m_Duration = 0.0f;
        float m_TicksPerSecond = 0.0f;
        std::map<std::string, BoneAnimChannel> m_Channels;
    };

    // 3. 애니메이션 재생기 (매 프레임 GPU로 보낼 행렬 계산)
    class CC_API Animator
    {
    public:
        Animator();
        void PlayAnimation(AnimationClip* clip);
        void Update(float deltaTime, Model* model, Scene* scene);

        const std::vector<DirectX::XMMATRIX>& GetFinalBoneMatrices() const { return m_FinalBoneMatrices; }
        DirectX::XMMATRIX GetGlobalBoneMatrix(const std::string& boneName);
        bool HasBone(const std::string& boneName);

        int GetBoneIndex(const std::string& name, Model* model);
        DirectX::XMMATRIX GetFinalMatrix(int index);

    private:
        void CalculateBoneTransform(const ModelNode& node, DirectX::XMMATRIX parentTransform, Model* model, Scene* scene);

        std::unordered_map<std::string, DirectX::XMMATRIX> m_GlobalBoneMatrices;
        std::vector<DirectX::XMMATRIX> m_FinalBoneMatrices;
        AnimationClip* m_CurrentClip = nullptr;
        float m_CurrentTime = 0.0f;
    };
}