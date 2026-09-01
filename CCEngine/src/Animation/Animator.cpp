#include "Animator.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <windows.h> // 디버그

#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Renderer/Model.h"


namespace CCEngine
{
    namespace
    {
        struct LocalTransformParts
        {
            DirectX::XMFLOAT3 Translation = { 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT4 Rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
            DirectX::XMFLOAT3 Scale = { 1.0f, 1.0f, 1.0f };
        };

        DirectX::XMMATRIX ComposeLocalTransform(const LocalTransformParts& parts)
        {
            return DirectX::XMMatrixScaling(parts.Scale.x, parts.Scale.y, parts.Scale.z) *
                DirectX::XMMatrixRotationQuaternion(DirectX::XMLoadFloat4(&parts.Rotation)) *
                DirectX::XMMatrixTranslation(parts.Translation.x, parts.Translation.y, parts.Translation.z);
        }

        LocalTransformParts DecomposeLocalTransform(DirectX::XMMATRIX matrix)
        {
            LocalTransformParts parts;
            DirectX::XMVECTOR scale;
            DirectX::XMVECTOR rotation;
            DirectX::XMVECTOR translation;
            if (DirectX::XMMatrixDecompose(&scale, &rotation, &translation, matrix))
            {
                DirectX::XMStoreFloat3(&parts.Scale, scale);
                DirectX::XMStoreFloat4(&parts.Rotation, rotation);
                DirectX::XMStoreFloat3(&parts.Translation, translation);
            }
            return parts;
        }

        template<typename TValue>
        float GetKeyAlpha(const std::vector<std::pair<float, TValue>>& keys, float currentTime, size_t& outIndex)
        {
            outIndex = 0;
            if (keys.size() <= 1)
                return 0.0f;

            for (size_t i = 0; i + 1 < keys.size(); ++i)
            {
                if (currentTime < keys[i + 1].first)
                {
                    outIndex = i;
                    const float span = keys[i + 1].first - keys[i].first;
                    return span > 0.0f ? (currentTime - keys[i].first) / span : 0.0f;
                }
            }

            outIndex = keys.size() - 2;
            return 1.0f;
        }

        std::string MakeClipCacheKey(const std::string& path, uint32_t clipIndex)
        {
            std::error_code ec;
            auto writeTime = std::filesystem::last_write_time(path, ec);
            std::ostringstream stream;
            stream << path << "#" << clipIndex;
            if (!ec)
                stream << "#" << writeTime.time_since_epoch().count();
            return stream.str();
        }

        std::mutex& ClipCacheMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        std::unordered_map<std::string, std::shared_ptr<AnimationClip>>& ClipCache()
        {
            static std::unordered_map<std::string, std::shared_ptr<AnimationClip>> cache;
            return cache;
        }

        std::mutex& ClipInfoCacheMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        std::unordered_map<std::string, std::vector<AnimationClipInfo>>& ClipInfoCache()
        {
            static std::unordered_map<std::string, std::vector<AnimationClipInfo>> cache;
            return cache;
        }

        AnimationClipInfo MakeClipInfo(const aiAnimation* anim, uint32_t index)
        {
            AnimationClipInfo info;
            info.Index = index;
            info.Name = (anim && anim->mName.length > 0) ? anim->mName.C_Str() : ("Clip " + std::to_string(index));
            info.DurationTicks = anim ? static_cast<float>(anim->mDuration) : 0.0f;
            info.TicksPerSecond = (anim && anim->mTicksPerSecond != 0.0) ? static_cast<float>(anim->mTicksPerSecond) : 25.0f;
            info.ChannelCount = anim ? anim->mNumChannels : 0;
            return info;
        }

        void StoreClipInfosInCache(const std::string& path, const std::vector<AnimationClipInfo>& clips)
        {
            std::lock_guard<std::mutex> lock(ClipInfoCacheMutex());
            ClipInfoCache()[MakeClipCacheKey(path, 0)] = clips;
        }

        bool TryGetClipInfosFromCache(const std::string& path, std::vector<AnimationClipInfo>& outClips)
        {
            std::lock_guard<std::mutex> lock(ClipInfoCacheMutex());
            auto found = ClipInfoCache().find(MakeClipCacheKey(path, 0));
            if (found == ClipInfoCache().end())
                return false;

            outClips = found->second;
            return true;
        }

        void StoreClipInCache(const std::string& key, const std::shared_ptr<AnimationClip>& clip)
        {
            auto& cache = ClipCache();
            cache[key] = clip;
            cache[MakeClipCacheKey(clip->GetSourcePath(), clip->GetClipIndex())] = clip;

            constexpr size_t MaxCachedAnimationClips = 32;
            if (cache.size() <= MaxCachedAnimationClips)
                return;

            for (auto it = cache.begin(); it != cache.end(); )
            {
                // 캐시만 들고 있는 클립부터 비워서 오래 연 프로젝트의 메모리 증가를 막는다.
                if (it->second.use_count() <= 1)
                {
                    it = cache.erase(it);
                    if (cache.size() <= MaxCachedAnimationClips)
                        break;
                }
                else
                {
                    ++it;
                }
            }
        }
    }

    // =========================================================
    // 1. BoneAnimChannel (현재 시간에 맞는 프레임 찾기)
    // =========================================================
    void BoneAnimChannel::UpdateLocalTransform(float currentTime, DirectX::XMFLOAT3& outPos, DirectX::XMFLOAT4& outRot, DirectX::XMFLOAT3& outScale)
    {
        if (!PositionKeys.empty())
        {
            size_t keyIndex = 0;
            float alpha = GetKeyAlpha(PositionKeys, currentTime, keyIndex);
            auto a = DirectX::XMLoadFloat3(&PositionKeys[keyIndex].second);
            auto b = DirectX::XMLoadFloat3(&PositionKeys[(std::min)(keyIndex + 1, PositionKeys.size() - 1)].second);
            DirectX::XMStoreFloat3(&outPos, DirectX::XMVectorLerp(a, b, alpha));
        }

        if (!RotationKeys.empty())
        {
            size_t keyIndex = 0;
            float alpha = GetKeyAlpha(RotationKeys, currentTime, keyIndex);
            auto a = DirectX::XMLoadFloat4(&RotationKeys[keyIndex].second);
            auto b = DirectX::XMLoadFloat4(&RotationKeys[(std::min)(keyIndex + 1, RotationKeys.size() - 1)].second);
            DirectX::XMStoreFloat4(&outRot, DirectX::XMQuaternionSlerp(a, b, alpha));
        }

        if (!ScaleKeys.empty())
        {
            size_t keyIndex = 0;
            float alpha = GetKeyAlpha(ScaleKeys, currentTime, keyIndex);
            auto a = DirectX::XMLoadFloat3(&ScaleKeys[keyIndex].second);
            auto b = DirectX::XMLoadFloat3(&ScaleKeys[(std::min)(keyIndex + 1, ScaleKeys.size() - 1)].second);
            DirectX::XMStoreFloat3(&outScale, DirectX::XMVectorLerp(a, b, alpha));
        }
    }

    // =========================================================
    // 2. AnimationClip (파일에서 애니메이션 추출)
    // =========================================================
    AnimationClip::AnimationClip(const std::string& path, uint32_t clipIndex)
        : m_SourcePath(path), m_ClipIndex(clipIndex)
    {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path,
            aiProcess_Triangulate |
            aiProcess_GenSmoothNormals |
            aiProcess_FlipUVs |
            aiProcess_JoinIdenticalVertices |
            aiProcess_ConvertToLeftHanded
        );

        if (scene && scene->mAnimations && scene->mNumAnimations > 0)
        {
            uint32_t lastIndex = scene->mNumAnimations - 1;
            uint32_t safeIndex = clipIndex < lastIndex ? clipIndex : lastIndex;
            LoadFromAssimpAnimation(scene->mAnimations[safeIndex], safeIndex);
        }
    }

    void AnimationClip::LoadFromAssimpAnimation(const aiAnimation* anim, uint32_t safeIndex)
    {
        if (!anim)
            return;

        m_ClipIndex = safeIndex;
        m_Name = anim->mName.length > 0 ? anim->mName.C_Str() : ("Clip " + std::to_string(safeIndex));
        m_Duration = static_cast<float>(anim->mDuration);
        m_TicksPerSecond = anim->mTicksPerSecond != 0.0 ? static_cast<float>(anim->mTicksPerSecond) : 25.0f;
        m_Channels.clear();

        for (unsigned int i = 0; i < anim->mNumChannels; ++i)
        {
            aiNodeAnim* channel = anim->mChannels[i];
            BoneAnimChannel boneChannel;
            boneChannel.NodeName = channel->mNodeName.C_Str();

            for (unsigned int p = 0; p < channel->mNumPositionKeys; ++p)
            {
                aiVector3D pos = channel->mPositionKeys[p].mValue;
                float time = static_cast<float>(channel->mPositionKeys[p].mTime);
                boneChannel.PositionKeys.push_back({ time, {pos.x, pos.y, pos.z} });
            }

            for (unsigned int r = 0; r < channel->mNumRotationKeys; ++r)
            {
                aiQuaternion rot = channel->mRotationKeys[r].mValue;
                float time = static_cast<float>(channel->mRotationKeys[r].mTime);
                boneChannel.RotationKeys.push_back({ time, {rot.x, rot.y, rot.z, rot.w} });
            }

            for (unsigned int s = 0; s < channel->mNumScalingKeys; ++s)
            {
                aiVector3D scale = channel->mScalingKeys[s].mValue;
                float time = static_cast<float>(channel->mScalingKeys[s].mTime);
                boneChannel.ScaleKeys.push_back({ time, {scale.x, scale.y, scale.z} });
            }

            m_Channels[boneChannel.NodeName] = boneChannel;
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
        m_CurrentClip.reset();
        m_LegacyCurrentClip = clip;
        m_CurrentTime = 0.0f;
        m_Playing = clip != nullptr;
        m_StaticPoseInitialized = false;
    }

    void Animator::PlayAnimation(const std::shared_ptr<AnimationClip>& clip, bool restart)
    {
        if (restart || m_CurrentClip != clip)
            m_CurrentTime = 0.0f;

        m_CurrentClip = clip;
        m_LegacyCurrentClip = nullptr;
        m_Playing = clip != nullptr;
        m_StaticPoseInitialized = false;
    }

    void Animator::StopAnimation()
    {
        m_CurrentClip.reset();
        m_LegacyCurrentClip = nullptr;
        m_CurrentTime = 0.0f;
        m_Playing = false;
        m_StaticPoseInitialized = false;
    }

    void Animator::SetWriteDefaults(bool writeDefaults)
    {
        if (m_WriteDefaults == writeDefaults)
            return;

        m_WriteDefaults = writeDefaults;
        if (m_WriteDefaults)
        {
            // Write Defaults를 다시 켜면 이전 상태에서 남은 포즈가 다음 평가에 섞이면 안 된다.
            m_LastLocalNodeTransforms.clear();
        }
    }

    void Animator::Update(float deltaTime, Model* model, Scene* scene)
    {
        Update(deltaTime, model, scene, nullptr);
    }

    void Animator::Update(float deltaTime, Model* model, Scene* scene, const std::unordered_map<std::string, entt::entity>* nodeEntityMap)
    {
        AnimationClip* currentClip = m_CurrentClip ? m_CurrentClip.get() : m_LegacyCurrentClip;
        if (currentClip && m_Playing)
        {
            // 클립 시간은 초가 아니라 FBX 내부 tick 단위다.
            // deltaTime(초)에 tick/sec와 speed를 곱해야 원본 애니메이션 속도를 그대로 따라간다.
            m_CurrentTime += currentClip->GetTicksPerSecond() * deltaTime * (std::max)(0.0f, m_Speed);
            if (currentClip->GetDuration() > 0.0f)
            {
                if (m_Loop)
                {
                    m_CurrentTime = fmod(m_CurrentTime, currentClip->GetDuration());
                }
                else if (m_CurrentTime >= currentClip->GetDuration())
                {
                    m_CurrentTime = currentClip->GetDuration();
                    m_Playing = false;
                }
            }
        }
        else if (!StaticPoseChanged(scene, nodeEntityMap))
        {
            return;
        }

        CalculateBoneTransform(model->GetRootNode(), DirectX::XMMatrixIdentity(), model, scene, nodeEntityMap);

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

    void Animator::CalculateBoneTransform(const ModelNode& node, DirectX::XMMATRIX parentTransform, Model* model, Scene* scene, const std::unordered_map<std::string, entt::entity>* nodeEntityMap)
    {
        std::string nodeName = node.Name;
        DirectX::XMMATRIX nodeTransform;

        // 현재 클립이 없으면 원본 노드 또는 씬 엔티티 트랜스폼을 사용합니다.
        AnimationClip* currentClip = m_CurrentClip ? m_CurrentClip.get() : m_LegacyCurrentClip;
        BoneAnimChannel* channel = (currentClip && m_Playing) ? currentClip->GetBoneChannel(nodeName) : nullptr;

        LocalTransformParts basePose;
        basePose.Translation = node.Translation;
        basePose.Rotation = node.Rotation;
        basePose.Scale = node.Scale;

        Entity entity{};
        if (scene && nodeEntityMap)
        {
            auto it = !node.Path.empty() ? nodeEntityMap->find(node.Path) : nodeEntityMap->end();
            if (it == nodeEntityMap->end())
            {
                it = nodeEntityMap->find(nodeName);
            }

            if (it != nodeEntityMap->end())
            {
                entity = { it->second, scene };
            }
        }
        else
        {
            entity = scene ? scene->FindEntityByName(nodeName) : Entity{};
        }

        if (entity)
        {
            auto& tc = entity.GetComponent<TransformComponent>();
            basePose.Translation = tc.Translation;
            basePose.Rotation = tc.QuaternionRotation;
            basePose.Scale = tc.Scale;
        }

        const bool preservePreviousPose = !m_WriteDefaults && currentClip && m_Playing;
        auto previousLocal = m_LastLocalNodeTransforms.find(nodeName);
        if (preservePreviousPose && previousLocal != m_LastLocalNodeTransforms.end())
        {
            // Write Defaults Off는 "클립이 쓰지 않는 값은 건드리지 않는다"는 뜻이다.
            // 그래서 채널이 없는 본이나 Position/Rotation/Scale 일부가 빠진 본은 직전 로컬 포즈를 시작값으로 삼는다.
            basePose = DecomposeLocalTransform(previousLocal->second);
        }

        if (channel)
        {
            // 애니메이션 재생 중에는 키프레임 데이터를 사용합니다.
            DirectX::XMFLOAT3 pos = basePose.Translation;
            DirectX::XMFLOAT4 rot = basePose.Rotation;
            DirectX::XMFLOAT3 scale = basePose.Scale;
            channel->UpdateLocalTransform(m_CurrentTime, pos, rot, scale);

            nodeTransform = DirectX::XMMatrixScaling(scale.x, scale.y, scale.z) * DirectX::XMMatrixRotationQuaternion(DirectX::XMLoadFloat4(&rot)) * DirectX::XMMatrixTranslation(pos.x, pos.y, pos.z);
        }
        else
        {
            // 채널이 없을 때 ON은 기본 포즈, OFF는 위에서 가져온 직전 포즈를 사용한다.
            nodeTransform = ComposeLocalTransform(basePose);
        }

        m_LastLocalNodeTransforms[nodeName] = nodeTransform;

        // 부모 행렬과 결합해 노드의 월드 행렬을 계산합니다.
        DirectX::XMMATRIX globalTransform = nodeTransform * parentTransform;
        m_GlobalBoneMatrices[nodeName] = globalTransform;

        // 뼈 노드라면 스키닝에 사용할 최종 행렬을 저장합니다.
        auto& boneInfoMap = model->GetBoneInfoMap();
        if (boneInfoMap.find(nodeName) != boneInfoMap.end())
        {
            int index = boneInfoMap[nodeName].id;
            DirectX::XMMATRIX offset = boneInfoMap[nodeName].offset;

            // 최종 행렬은 오프셋 행렬과 월드 행렬의 곱입니다.
            m_FinalBoneMatrices[index] = offset * globalTransform;
        }

        // 자식 노드도 같은 규칙으로 재귀 계산합니다.
        for (const auto& child : node.Children)
        {
            CalculateBoneTransform(child, globalTransform, model, scene, nodeEntityMap);
        }
    }

    std::vector<AnimationClipInfo> AnimationClip::InspectClips(const std::string& path)
    {
        std::vector<AnimationClipInfo> clips;
        if (TryGetClipInfosFromCache(path, clips))
            return clips;

        {
            std::lock_guard<std::mutex> lock(ClipCacheMutex());
            auto& cache = ClipCache();
            for (const auto& [key, clip] : cache)
            {
                if (!clip || clip->GetSourcePath() != path)
                    continue;

                AnimationClipInfo info;
                info.Index = clip->GetClipIndex();
                info.Name = clip->GetName().empty() ? ("Clip " + std::to_string(info.Index)) : clip->GetName();
                info.DurationTicks = clip->GetDuration();
                info.TicksPerSecond = clip->GetTicksPerSecond() != 0.0f ? clip->GetTicksPerSecond() : 25.0f;
                clips.push_back(info);
            }
        }

        if (!clips.empty())
        {
            std::sort(clips.begin(), clips.end(), [](const AnimationClipInfo& left, const AnimationClipInfo& right)
                {
                    return left.Index < right.Index;
                });
            StoreClipInfosInCache(path, clips);
            return clips;
        }

        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path,
            aiProcess_Triangulate |
            aiProcess_GenSmoothNormals |
            aiProcess_FlipUVs |
            aiProcess_JoinIdenticalVertices |
            aiProcess_ConvertToLeftHanded
        );
        if (scene && scene->mAnimations && scene->mNumAnimations > 0)
        {
            for (uint32_t i = 0; i < scene->mNumAnimations; ++i)
            {
                aiAnimation* anim = scene->mAnimations[i];
                clips.push_back(MakeClipInfo(anim, i));
            }
        }

        StoreClipInfosInCache(path, clips);
        return clips;
    }

    std::shared_ptr<AnimationClip> AnimationClip::LoadShared(const std::string& path, uint32_t clipIndex)
    {
        const std::string cacheKey = MakeClipCacheKey(path, clipIndex);
        {
            std::lock_guard<std::mutex> lock(ClipCacheMutex());
            auto found = ClipCache().find(cacheKey);
            if (found != ClipCache().end())
            {
                return found->second;
            }
        }

        // Assimp 파일 읽기는 큰 임시 메모리를 쓰는 작업이다.
        // 캐시 mutex를 잡은 채 로드하면 다른 클립 요청까지 같이 멈추므로, 실제 로드는 잠금 밖에서 처리한다.
        auto clip = std::make_shared<AnimationClip>(path, clipIndex);
        {
            std::lock_guard<std::mutex> lock(ClipCacheMutex());
            auto found = ClipCache().find(cacheKey);
            if (found != ClipCache().end())
                return found->second;

            StoreClipInCache(cacheKey, clip);
        }
        return clip;
    }

    void AnimationClip::PrimeCacheFromScene(const std::string& path, const aiScene* scene)
    {
        if (path.empty() || !scene || !scene->mAnimations || scene->mNumAnimations == 0)
            return;

        std::vector<AnimationClipInfo> clipInfos;
        clipInfos.reserve(scene->mNumAnimations);
        for (uint32_t i = 0; i < scene->mNumAnimations; ++i)
        {
            clipInfos.push_back(MakeClipInfo(scene->mAnimations[i], i));
        }

        // 모델 로드 때 이미 Assimp scene이 열려 있으므로, 클립 이름/길이 목록도 같이 저장한다.
        // Inspector나 Animator Graph가 목록만 보려고 같은 FBX를 다시 여는 순간 큰 임시 메모리 산이 한 번 더 생긴다.
        StoreClipInfosInCache(path, clipInfos);

        // 여기서 클립 본 키프레임 전체를 복사하면 모델 메모리 산 뒤에 애니메이션 메모리 산이 한 번 더 생긴다.
        // 그래서 모델 로드 단계에서는 목록만 저장하고, 실제 재생용 클립 데이터는 Play/Preview가 필요할 때 늦게 읽는다.
    }

    bool Animator::StaticPoseChanged(Scene* scene, const std::unordered_map<std::string, entt::entity>* nodeEntityMap)
    {
        if (!scene || !nodeEntityMap)
        {
            m_StaticPoseInitialized = false;
            return true;
        }

        if (!m_StaticPoseInitialized || m_LastStaticPose.size() != nodeEntityMap->size())
        {
            m_LastStaticPose.clear();
            for (const auto& [name, handle] : *nodeEntityMap)
            {
                Entity entity{ handle, scene };
                if (!entity || !entity.HasComponent<TransformComponent>())
                {
                    continue;
                }

                auto& tc = entity.GetComponent<TransformComponent>();
                m_LastStaticPose[handle] = { tc.Translation, tc.QuaternionRotation, tc.Scale };
            }

            m_StaticPoseInitialized = true;
            return true;
        }

        bool changed = false;
        for (const auto& [name, handle] : *nodeEntityMap)
        {
            Entity entity{ handle, scene };
            if (!entity || !entity.HasComponent<TransformComponent>())
            {
                continue;
            }

            auto& tc = entity.GetComponent<TransformComponent>();
            TransformSnapshot snapshot{ tc.Translation, tc.QuaternionRotation, tc.Scale };
            auto it = m_LastStaticPose.find(handle);

            if (it == m_LastStaticPose.end() || !SameSnapshot(it->second, snapshot))
            {
                m_LastStaticPose[handle] = snapshot;
                changed = true;
            }
        }

        return changed;
    }

    bool Animator::SameSnapshot(const TransformSnapshot& left, const TransformSnapshot& right) const
    {
        constexpr float epsilon = 0.00001f;
        auto same = [epsilon](float a, float b)
            {
                return std::abs(a - b) <= epsilon;
            };

        return same(left.Translation.x, right.Translation.x) &&
            same(left.Translation.y, right.Translation.y) &&
            same(left.Translation.z, right.Translation.z) &&
            same(left.Rotation.x, right.Rotation.x) &&
            same(left.Rotation.y, right.Rotation.y) &&
            same(left.Rotation.z, right.Rotation.z) &&
            same(left.Rotation.w, right.Rotation.w) &&
            same(left.Scale.x, right.Scale.x) &&
            same(left.Scale.y, right.Scale.y) &&
            same(left.Scale.z, right.Scale.z);
    }
}
