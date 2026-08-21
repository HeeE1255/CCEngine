#pragma once
#include "Renderer/Model.h"
#include "Core.h"
#include <DirectXMath.h>
#include "entt.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <vector>
#include <unordered_map>
#include <string>

namespace CCEngine
{
    class Scene;

    struct AnimationClipInfo
    {
        std::string Name;
        uint32_t Index = 0;
        float DurationTicks = 0.0f;
        float TicksPerSecond = 25.0f;
        uint32_t ChannelCount = 0;
    };

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
        AnimationClip(const std::string& path, uint32_t clipIndex = 0);
        ~AnimationClip() = default;

        static std::vector<AnimationClipInfo> InspectClips(const std::string& path);
        static std::shared_ptr<AnimationClip> LoadShared(const std::string& path, uint32_t clipIndex);

        const std::string& GetName() const { return m_Name; }
        const std::string& GetSourcePath() const { return m_SourcePath; }
        uint32_t GetClipIndex() const { return m_ClipIndex; }
        float GetTicksPerSecond() const { return m_TicksPerSecond; }
        float GetDuration() const { return m_Duration; }
        float GetDurationSeconds() const { return m_TicksPerSecond > 0.0f ? m_Duration / m_TicksPerSecond : 0.0f; }
        BoneAnimChannel* GetBoneChannel(const std::string& nodeName);

    private:
        std::string m_SourcePath;
        std::string m_Name;
        uint32_t m_ClipIndex = 0;
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
        void PlayAnimation(const std::shared_ptr<AnimationClip>& clip, bool restart = true);
        void StopAnimation();
        void SetLoop(bool loop) { m_Loop = loop; }
        void SetSpeed(float speed) { m_Speed = speed; }
        bool IsPlaying() const { return m_Playing; }
        AnimationClip* GetCurrentClip() const { return m_CurrentClip.get(); }
        void Update(float deltaTime, Model* model, Scene* scene);
        void Update(float deltaTime, Model* model, Scene* scene, const std::unordered_map<std::string, entt::entity>* nodeEntityMap);

        const std::vector<DirectX::XMMATRIX>& GetFinalBoneMatrices() const { return m_FinalBoneMatrices; }
        DirectX::XMMATRIX GetGlobalBoneMatrix(const std::string& boneName);
        bool HasBone(const std::string& boneName);

        int GetBoneIndex(const std::string& name, Model* model);
        DirectX::XMMATRIX GetFinalMatrix(int index);

    private:
        struct TransformSnapshot
        {
            DirectX::XMFLOAT3 Translation = { 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT4 Rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
            DirectX::XMFLOAT3 Scale = { 1.0f, 1.0f, 1.0f };
        };

        void CalculateBoneTransform(const ModelNode& node, DirectX::XMMATRIX parentTransform, Model* model, Scene* scene, const std::unordered_map<std::string, entt::entity>* nodeEntityMap);
        bool StaticPoseChanged(Scene* scene, const std::unordered_map<std::string, entt::entity>* nodeEntityMap);
        bool SameSnapshot(const TransformSnapshot& left, const TransformSnapshot& right) const;

        std::unordered_map<std::string, DirectX::XMMATRIX> m_GlobalBoneMatrices;
        std::unordered_map<entt::entity, TransformSnapshot> m_LastStaticPose;
        std::vector<DirectX::XMMATRIX> m_FinalBoneMatrices;
        std::shared_ptr<AnimationClip> m_CurrentClip;
        AnimationClip* m_LegacyCurrentClip = nullptr;
        float m_CurrentTime = 0.0f;
        float m_Speed = 1.0f;
        bool m_Loop = true;
        bool m_Playing = false;
        bool m_StaticPoseInitialized = false;
    };
}
