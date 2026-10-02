#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/Renderer3D.h"
#include "Scripting/ScriptEngine.h"
#include "Physics/PhysicsWorld3D.h"
#include "Animation/AvatarAsset.h"
#include "Core/AssetDatabase.h"
#include "Core/ConsoleLog.h"
#include <box2d/box2d.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace CCEngine
{
    namespace
    {
        bool EntityNameExists(Scene* scene, const std::string& name)
        {
            auto view = scene->GetRegistry().view<TagComponent>();
            for (auto handle : view)
            {
                Entity entity{ handle, scene };
                if (entity.GetComponent<TagComponent>().Tag == name)
                    return true;
            }
            return false;
        }

        std::string ResolveAnimatorSourcePath(AnimatorComponent& animator, const ModelComponent& model)
        {
            if (animator.SourceAssetGuid.empty())
                animator.SourceAssetGuid = model.AssetGuid;

            if (!animator.SourceAssetGuid.empty())
            {
                std::filesystem::path path = AssetDatabase::GetPathFromGuid(animator.SourceAssetGuid);
                if (!path.empty() && std::filesystem::exists(path))
                {
                    animator.SourcePath = path.string();
                    return animator.SourcePath;
                }
            }

            if (!animator.SourcePath.empty() && std::filesystem::exists(animator.SourcePath))
                return animator.SourcePath;

            if (model.TargetModel && !model.TargetModel->GetFilePath().empty())
            {
                animator.SourcePath = model.TargetModel->GetFilePath();
                if (animator.SourceAssetGuid.empty())
                    animator.SourceAssetGuid = AssetDatabase::GetGuidFromPath(animator.SourcePath);
            }

            return animator.SourcePath;
        }

        std::string ResolveAnimatorStateSourcePath(AnimatorComponent& animator, const ModelComponent& model, const AnimatorComponent::State& state)
        {
            if (!state.MotionAssetGuid.empty())
            {
                std::filesystem::path path = AssetDatabase::GetPathFromGuid(state.MotionAssetGuid);
                if (!path.empty() && std::filesystem::exists(path))
                    return path.string();
            }

            if (!state.MotionPath.empty() && std::filesystem::exists(state.MotionPath))
                return state.MotionPath;

            return ResolveAnimatorSourcePath(animator, model);
        }

        float ReadAnimatorFloatParameter(const AnimatorComponent& animator, const std::string& name)
        {
            auto it = std::find_if(animator.Parameters.begin(), animator.Parameters.end(), [&name](const AnimatorComponent::Parameter& parameter)
            {
                return parameter.Name == name;
            });
            if (it == animator.Parameters.end())
                return 0.0f;
            if (it->ParamType == AnimatorComponent::Parameter::Type::Float)
                return it->FloatValue;
            return it->BoolValue ? 1.0f : 0.0f;
        }

        int ResolveAnimatorStateClipIndex(const AnimatorComponent& animator, const AnimatorComponent::State& state)
        {
            if (state.Motion == AnimatorComponent::State::MotionType::Clip && state.ClipIndex < 0)
                return -1;
            if (state.Motion != AnimatorComponent::State::MotionType::BlendTree || state.Tree.Children.empty())
                return state.ClipIndex;

            const auto& children = state.Tree.Children;
            int bestClip = children.front().ClipIndex;
            float bestScore = std::numeric_limits<float>::max();

            // Blend Tree는 여러 클립 중 어느 클립을 평가할지 먼저 고른다.
            // 현재 재생기는 단일 포즈 출력 구조라, 파라미터에 가장 가까운 자식 클립을 선택해 기존 전환/이벤트 경로와 충돌하지 않게 둔다.
            if (state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::Direct)
            {
                for (const auto& child : children)
                {
                    const float score = -child.Weight;
                    if (score < bestScore)
                    {
                        bestScore = score;
                        bestClip = child.ClipIndex;
                    }
                }
                return bestClip;
            }

            const float x = ReadAnimatorFloatParameter(animator, state.Tree.ParameterX);
            const float y = ReadAnimatorFloatParameter(animator, state.Tree.ParameterY);
            for (const auto& child : children)
            {
                float score = 0.0f;
                if (state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::OneD)
                {
                    score = std::abs(x - child.Threshold);
                }
                else
                {
                    const float dx = x - child.Position.x;
                    const float dy = y - child.Position.y;
                    score = dx * dx + dy * dy;
                }

                if (score < bestScore)
                {
                    bestScore = score;
                    bestClip = child.ClipIndex;
                }
            }
            return bestClip;
        }

        std::filesystem::path ResolveAvatarPath(const std::string& guid, const std::string& storedPath)
        {
            if (!guid.empty())
            {
                std::filesystem::path guidPath = AssetDatabase::GetPathFromGuid(guid);
                if (!guidPath.empty() && std::filesystem::exists(guidPath))
                    return guidPath;
            }

            if (!storedPath.empty() && std::filesystem::exists(storedPath))
                return storedPath;

            return {};
        }

        bool LoadAnimatorAvatar(const std::string& guid, const std::string& storedPath, AvatarAsset& outAvatar)
        {
            const std::filesystem::path path = ResolveAvatarPath(guid, storedPath);
            if (path.empty())
                return false;

            struct CachedAvatar
            {
                AvatarAsset Asset;
                std::filesystem::file_time_type LastWriteTime{};
            };

            static std::unordered_map<std::string, CachedAvatar> s_AvatarCache;

            std::error_code ec;
            const std::filesystem::path normalizedPath = std::filesystem::absolute(path, ec).lexically_normal();
            const std::string cacheKey = ec ? path.string() : normalizedPath.string();
            const auto writeTime = std::filesystem::last_write_time(path, ec);

            auto cached = s_AvatarCache.find(cacheKey);
            if (cached != s_AvatarCache.end() && !ec && cached->second.LastWriteTime == writeTime)
            {
                outAvatar = cached->second.Asset;
                return true;
            }

            AvatarAsset loaded;
            if (!AvatarAsset::LoadFromFile(path, loaded))
                return false;

            // Avatar는 런타임 포즈 평가 때 매 프레임 참조된다.
            // 파일을 매번 읽으면 애니메이션 재생 중 잔멈춤이 생기므로, 수정 시간이 바뀔 때만 다시 읽는다.
            s_AvatarCache[cacheKey] = { loaded, ec ? std::filesystem::file_time_type{} : writeTime };
            outAvatar = loaded;
            return true;
        }

        std::string ResolveSourceAvatarBone(const AnimatorComponent& animator, const std::string& humanBone)
        {
            AvatarAsset sourceAvatar;
            if (LoadAnimatorAvatar(animator.SourceAvatarGuid, animator.SourceAvatarPath, sourceAvatar))
            {
                std::string sourceBone;
                if (AvatarAsset::ResolveSourceBone(sourceAvatar, humanBone, nullptr, sourceBone))
                    return sourceBone;
            }

            AvatarAsset defaultAvatar = AvatarAsset::CreateDefault("Default Source Avatar");
            std::string sourceBone;
            if (AvatarAsset::ResolveSourceBone(defaultAvatar, humanBone, nullptr, sourceBone))
                return sourceBone;
            return humanBone;
        }

        struct RetargetContext
        {
            AvatarAsset SourceAvatar;
            std::unordered_map<std::string, std::string> HumanToTargetBone;
            std::unordered_map<std::string, DirectX::XMFLOAT3> HumanRotationOffsets;
        };

        std::unique_ptr<RetargetContext> BuildRetargetContext(const AnimatorComponent& animator, const ModelComponent& targetModel)
        {
            if (!animator.RetargetToHumanoid)
                return nullptr;

            AvatarAsset targetAvatar;
            if (!LoadAnimatorAvatar(animator.AvatarGuid, animator.AvatarPath, targetAvatar))
                return nullptr;

            auto context = std::make_unique<RetargetContext>();
            if (!LoadAnimatorAvatar(animator.SourceAvatarGuid, animator.SourceAvatarPath, context->SourceAvatar))
                context->SourceAvatar = AvatarAsset::CreateDefault("Default Source Avatar");

            for (const HumanoidBoneMapping& mapping : targetAvatar.BoneMappings)
            {
                std::string targetBone;
                if (!AvatarAsset::ResolveSourceBone(targetAvatar, mapping.HumanBone, &targetModel, targetBone))
                    continue;

                // HumanToTargetBone은 표준 Humanoid 이름에서 현재 모델의 실제 본 이름으로 가는 표다.
                // 클립 채널 이름은 프레임마다 SourceAvatar로 해석하고, 여기서 대상 본으로 바꾼다.
                context->HumanToTargetBone[mapping.HumanBone] = targetBone;

                float offsetX = 0.0f;
                float offsetY = 0.0f;
                float offsetZ = 0.0f;
                if (AvatarAsset::GetPoseOffsetDegrees(targetAvatar, mapping.HumanBone, offsetX, offsetY, offsetZ))
                    context->HumanRotationOffsets[mapping.HumanBone] = { offsetX, offsetY, offsetZ };
            }

            if (context->HumanToTargetBone.empty())
                return nullptr;
            return context;
        }

        void PrepareAnimatorClip(AnimatorComponent& animator, const ModelComponent& model)
        {
            std::string sourcePath = ResolveAnimatorSourcePath(animator, model);

            if (!animator.States.empty())
            {
                animator.ActiveStateIndex = std::clamp(animator.ActiveStateIndex, 0, static_cast<int>(animator.States.size() - 1));
                auto& state = animator.States[animator.ActiveStateIndex];
                sourcePath = ResolveAnimatorStateSourcePath(animator, model, state);
                if (sourcePath.empty() || !std::filesystem::exists(sourcePath))
                    return;

                const int resolvedClipIndex = ResolveAnimatorStateClipIndex(animator, state);
                if (resolvedClipIndex < 0)
                {
                    animator.RuntimeClip.reset();
                    animator.RuntimeClipKey.clear();
                    animator.AnimPlayer.StopAnimation();
                    return;
                }
                const bool changedStateSetting =
                    animator.SelectedClipIndex != resolvedClipIndex ||
                    animator.Loop != state.Loop ||
                    std::abs(animator.Speed - state.Speed) > 0.0001f;

                animator.SelectedClipIndex = resolvedClipIndex;
                animator.Loop = state.Loop;
                animator.Speed = state.Speed;

                if (changedStateSetting)
                {
                    animator.RuntimeClip.reset();
                    animator.RuntimeClipKey.clear();
                    animator.AnimPlayer.StopAnimation();
                }
            }

            std::string key = sourcePath + "#" + std::to_string(animator.SelectedClipIndex);
            if (animator.RuntimeClip && animator.RuntimeClipKey == key)
            {
                return;
            }

            // 런타임 재생 준비에서는 "목록 확인"과 "클립 로드"를 나누지 않는다.
            // 둘 다 FBX를 다시 여는 작업이라 시작 시 큰 메모리 산이 두 번 생길 수 있다.
            // Inspector/Graph처럼 목록이 필요한 UI에서만 InspectClips를 쓰고, 실제 재생은 필요한 클립 하나만 읽는다.
            animator.RuntimeClip = AnimationClip::LoadShared(sourcePath, static_cast<uint32_t>((std::max)(0, animator.SelectedClipIndex)));
            if (!animator.RuntimeClip || animator.RuntimeClip->GetName().empty())
            {
                animator.RuntimeClip.reset();
                animator.RuntimeClipKey.clear();
                return;
            }

            animator.SelectedClipIndex = static_cast<int>(animator.RuntimeClip->GetClipIndex());
            animator.SelectedClipName = animator.RuntimeClip->GetName();
            animator.RuntimeClipKey = sourcePath + "#" + std::to_string(animator.SelectedClipIndex);

            if (!animator.States.empty())
            {
                auto& state = animator.States[animator.ActiveStateIndex];
                if (state.Motion == AnimatorComponent::State::MotionType::Clip)
                    state.ClipIndex = animator.SelectedClipIndex;
            }
            else if (sourcePath.empty() || !std::filesystem::exists(sourcePath))
            {
                return;
            }
        }

        std::shared_ptr<AnimationClip> LoadAnimatorStateClip(AnimatorComponent& animator, const ModelComponent& model, const AnimatorComponent::State& state)
        {
            const std::string sourcePath = ResolveAnimatorStateSourcePath(animator, model, state);
            if (sourcePath.empty() || !std::filesystem::exists(sourcePath))
                return nullptr;

            const int clipIndex = ResolveAnimatorStateClipIndex(animator, state);
            if (clipIndex < 0)
                return nullptr;
            return AnimationClip::LoadShared(sourcePath, static_cast<uint32_t>((std::max)(0, clipIndex)));
        }

        float ResolveStateSampleTime(const AnimatorComponent::State& state, const AnimationClip& clip, float stateTimeSeconds)
        {
            float duration = clip.GetDurationSeconds();
            if (duration <= 0.0f)
                return 0.0f;

            float start = 0.0f;
            float end = duration;
            if (state.ImportSettings.UseCustomRange)
            {
                start = std::clamp(state.ImportSettings.StartSeconds, 0.0f, duration);
                end = std::clamp(state.ImportSettings.EndSeconds, start, duration);
            }

            const float range = (std::max)(0.0001f, end - start);
            float localTime = state.Loop ? std::fmod(stateTimeSeconds, range) : (std::min)(stateTimeSeconds, range);
            return start + localTime;
        }

        void ResolveStateSampleRange(const AnimatorComponent::State& state, const AnimationClip& clip, float& outStart, float& outEnd)
        {
            float duration = clip.GetDurationSeconds();
            outStart = 0.0f;
            outEnd = (std::max)(0.0f, duration);
            if (state.ImportSettings.UseCustomRange)
            {
                outStart = std::clamp(state.ImportSettings.StartSeconds, 0.0f, duration);
                outEnd = std::clamp(state.ImportSettings.EndSeconds, outStart, duration);
            }
        }

        void BlendBonePose(BonePose& target, const BonePose& sample, float alpha)
        {
            alpha = std::clamp(alpha, 0.0f, 1.0f);
            if (sample.HasTranslation)
            {
                if (!target.HasTranslation)
                {
                    target.Translation = sample.Translation;
                    target.HasTranslation = true;
                }
                else
                {
                    target.Translation = {
                        target.Translation.x + (sample.Translation.x - target.Translation.x) * alpha,
                        target.Translation.y + (sample.Translation.y - target.Translation.y) * alpha,
                        target.Translation.z + (sample.Translation.z - target.Translation.z) * alpha
                    };
                }
            }

            if (sample.HasScale)
            {
                if (!target.HasScale)
                {
                    target.Scale = sample.Scale;
                    target.HasScale = true;
                }
                else
                {
                    target.Scale = {
                        target.Scale.x + (sample.Scale.x - target.Scale.x) * alpha,
                        target.Scale.y + (sample.Scale.y - target.Scale.y) * alpha,
                        target.Scale.z + (sample.Scale.z - target.Scale.z) * alpha
                    };
                }
            }

            if (sample.HasRotation)
            {
                if (!target.HasRotation)
                {
                    target.Rotation = sample.Rotation;
                    target.HasRotation = true;
                }
                else
                {
                    DirectX::XMVECTOR from = DirectX::XMLoadFloat4(&target.Rotation);
                    DirectX::XMVECTOR to = DirectX::XMLoadFloat4(&sample.Rotation);
                    DirectX::XMStoreFloat4(&target.Rotation, DirectX::XMQuaternionSlerp(from, to, alpha));
                }
            }
        }

        void AddClipPoseSample(
            const std::shared_ptr<AnimationClip>& clip,
            const AnimatorComponent::State& state,
            float stateTimeSeconds,
            float weight,
            const RetargetContext* retarget,
            std::unordered_map<std::string, BonePose>& pose,
            float& accumulatedWeight)
        {
            if (!clip || weight <= 0.0001f)
                return;

            const float sampleTicks = ResolveStateSampleTime(state, *clip, stateTimeSeconds) * clip->GetTicksPerSecond();
            const float alpha = weight / (accumulatedWeight + weight);

            for (const auto& [boneName, channel] : clip->GetChannels())
            {
                std::string targetBoneName = boneName;
                std::string resolvedHumanBone;
                if (retarget)
                {
                    std::string humanBone;
                    if (AvatarAsset::ResolveHumanBone(retarget->SourceAvatar, boneName, humanBone))
                    {
                        auto targetIt = retarget->HumanToTargetBone.find(humanBone);
                        if (targetIt != retarget->HumanToTargetBone.end())
                        {
                            targetBoneName = targetIt->second;
                            resolvedHumanBone = humanBone;
                        }
                    }
                }

                BonePose sample;
                sample.HasTranslation = !channel.PositionKeys.empty();
                sample.HasRotation = !channel.RotationKeys.empty();
                sample.HasScale = !channel.ScaleKeys.empty();
                channel.UpdateLocalTransform(sampleTicks, sample.Translation, sample.Rotation, sample.Scale);
                if (retarget && sample.HasRotation && !resolvedHumanBone.empty())
                {
                    auto offsetIt = retarget->HumanRotationOffsets.find(resolvedHumanBone);
                    if (offsetIt != retarget->HumanRotationOffsets.end())
                    {
                        const DirectX::XMFLOAT3& offsetDegrees = offsetIt->second;
                        const float toRadians = DirectX::XM_PI / 180.0f;
                        DirectX::XMVECTOR sourceRotation = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&sample.Rotation));
                        DirectX::XMVECTOR correction = DirectX::XMQuaternionRotationRollPitchYaw(
                            offsetDegrees.x * toRadians,
                            offsetDegrees.y * toRadians,
                            offsetDegrees.z * toRadians);

                        // Retarget Pose Offset은 Source 리그의 회전에 Target 리그 기준 자세 차이를 더하는 값이다.
                        // 예를 들어 A-Pose 팔을 T-Pose 클립에 맞출 때, 매 프레임 회전 위에 같은 보정 회전을 얹는다.
                        DirectX::XMStoreFloat4(&sample.Rotation, DirectX::XMQuaternionNormalize(DirectX::XMQuaternionMultiply(sourceRotation, correction)));
                    }
                }
                BlendBonePose(pose[targetBoneName], sample, alpha);
            }

            accumulatedWeight += weight;
        }

        std::vector<std::pair<int, float>> ResolveBlendTreeWeights(const AnimatorComponent& animator, const AnimatorComponent::State& state)
        {
            std::vector<std::pair<int, float>> weights;
            const auto& children = state.Tree.Children;
            if (children.empty())
                return weights;

            if (state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::Direct)
            {
                float total = 0.0f;
                for (const auto& child : children)
                    total += (std::max)(0.0f, child.Weight);
                if (total <= 0.0001f)
                    total = 1.0f;
                for (const auto& child : children)
                    weights.push_back({ child.ClipIndex, (std::max)(0.0f, child.Weight) / total });
                return weights;
            }

            const float x = ReadAnimatorFloatParameter(animator, state.Tree.ParameterX);
            if (state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::OneD)
            {
                std::vector<AnimatorComponent::State::BlendTreeChild> sorted = children;
                std::sort(sorted.begin(), sorted.end(), [](const auto& left, const auto& right)
                {
                    return left.Threshold < right.Threshold;
                });

                if (x <= sorted.front().Threshold)
                    return { { sorted.front().ClipIndex, 1.0f } };
                if (x >= sorted.back().Threshold)
                    return { { sorted.back().ClipIndex, 1.0f } };

                for (size_t i = 0; i + 1 < sorted.size(); ++i)
                {
                    if (x < sorted[i].Threshold || x > sorted[i + 1].Threshold)
                        continue;
                    const float span = (std::max)(0.0001f, sorted[i + 1].Threshold - sorted[i].Threshold);
                    const float t = (x - sorted[i].Threshold) / span;
                    return { { sorted[i].ClipIndex, 1.0f - t }, { sorted[i + 1].ClipIndex, t } };
                }
            }

            const float y = ReadAnimatorFloatParameter(animator, state.Tree.ParameterY);
            float total = 0.0f;
            for (const auto& child : children)
            {
                const float dx = x - child.Position.x;
                const float dy = y - child.Position.y;
                const float distanceSq = dx * dx + dy * dy;
                const float weight = 1.0f / ((std::max)(0.0001f, distanceSq));
                weights.push_back({ child.ClipIndex, weight });
                total += weight;
            }
            for (auto& [clipIndex, weight] : weights)
                weight = total > 0.0001f ? weight / total : 0.0f;
            return weights;
        }

        std::unordered_map<std::string, BonePose> EvaluateAnimatorStatePose(
            AnimatorComponent& animator,
            const ModelComponent& model,
            const AnimatorComponent::State& state,
            float stateTimeSeconds,
            const RetargetContext* retarget)
        {
            std::unordered_map<std::string, BonePose> pose;
            float accumulatedWeight = 0.0f;
            if (state.Motion == AnimatorComponent::State::MotionType::BlendTree)
            {
                // Blend Tree는 파라미터를 읽어 여러 클립의 비율을 정하고, 같은 본 이름끼리 포즈를 섞는다.
                // 이렇게 해두면 Direct, 1D, 2D 방식이 모두 같은 최종 포즈 경로를 사용한다.
                const std::string sourcePath = ResolveAnimatorStateSourcePath(animator, model, state);
                if (sourcePath.empty() || !std::filesystem::exists(sourcePath))
                    return pose;
                for (const auto& [clipIndex, weight] : ResolveBlendTreeWeights(animator, state))
                {
                    auto clip = AnimationClip::LoadShared(sourcePath, static_cast<uint32_t>((std::max)(0, clipIndex)));
                    AddClipPoseSample(clip, state, stateTimeSeconds, weight, retarget, pose, accumulatedWeight);
                }
            }
            else if (state.Motion == AnimatorComponent::State::MotionType::Clip)
            {
                AddClipPoseSample(LoadAnimatorStateClip(animator, model, state), state, stateTimeSeconds, 1.0f, retarget, pose, accumulatedWeight);
            }
            return pose;
        }

        void CollectMaskBoneNames(const ModelNode& node, const AnimatorComponent::Layer& layer, bool insideMask, std::unordered_set<std::string>& outBones)
        {
            const bool explicitBone = std::find(layer.MaskBoneNames.begin(), layer.MaskBoneNames.end(), node.Name) != layer.MaskBoneNames.end() ||
                (!node.Path.empty() && std::find(layer.MaskBoneNames.begin(), layer.MaskBoneNames.end(), node.Path) != layer.MaskBoneNames.end());
            const bool nowInside = insideMask || explicitBone || (!layer.MaskRootBone.empty() && (node.Name == layer.MaskRootBone || node.Path == layer.MaskRootBone));
            if (nowInside)
                outBones.insert(node.Name);
            for (const auto& child : node.Children)
                CollectMaskBoneNames(child, layer, nowInside, outBones);
        }

        bool LayerAllowsBone(const AnimatorComponent::Layer& layer, const Model& model, const std::string& boneName, std::unordered_set<std::string>& maskCache)
        {
            if (layer.MaskRootBone.empty() && layer.MaskBoneNames.empty())
                return true;
            if (maskCache.empty())
                CollectMaskBoneNames(model.GetRootNode(), layer, false, maskCache);
            return maskCache.find(boneName) != maskCache.end();
        }

        void ApplyLayerPose(std::unordered_map<std::string, BonePose>& finalPose, const std::unordered_map<std::string, BonePose>& layerPose, const AnimatorComponent::Layer& layer, const Model& model)
        {
            const float layerWeight = std::clamp(layer.Weight, 0.0f, 1.0f);
            if (layerWeight <= 0.0001f)
                return;

            std::unordered_set<std::string> maskCache;
            for (const auto& [boneName, pose] : layerPose)
            {
                if (!LayerAllowsBone(layer, model, boneName, maskCache))
                    continue;
                // 아래 레이어가 나중에 적용되므로 같은 본을 다시 쓰면 아래 레이어가 우선한다.
                // Weight는 그 우선권을 얼마나 강하게 적용할지 정하는 값이다.
                BlendBonePose(finalPose[boneName], pose, layerWeight);
            }
        }

        std::unordered_map<std::string, BonePose> EvaluateAnimatorLayeredPose(AnimatorComponent& animator, const ModelComponent& modelComponent)
        {
            std::unordered_map<std::string, BonePose> finalPose;
            if (!modelComponent.TargetModel)
                return finalPose;

            const std::unique_ptr<RetargetContext> retarget = BuildRetargetContext(animator, modelComponent);
            const RetargetContext* retargetContext = retarget.get();

            for (auto& layer : animator.Layers)
            {
                if (layer.Exited || layer.Weight <= 0.0001f || layer.ActiveStateIndex < 0 || layer.ActiveStateIndex >= static_cast<int>(layer.States.size()))
                    continue;

                const auto& currentState = layer.States[layer.ActiveStateIndex];
                auto layerPose = EvaluateAnimatorStatePose(animator, modelComponent, currentState, layer.StateTime, retargetContext);
                if (layer.PreviousStateIndex >= 0 && layer.PreviousStateIndex < static_cast<int>(layer.States.size()) && layer.BlendDuration > 0.0001f)
                {
                    auto previousPose = EvaluateAnimatorStatePose(animator, modelComponent, layer.States[layer.PreviousStateIndex], layer.PreviousStateTime, retargetContext);
                    const float alpha = std::clamp(layer.BlendElapsed / layer.BlendDuration, 0.0f, 1.0f);
                    for (const auto& [boneName, pose] : layerPose)
                        BlendBonePose(previousPose[boneName], pose, alpha);
                    layerPose = std::move(previousPose);
                }

                ApplyLayerPose(finalPose, layerPose, layer, *modelComponent.TargetModel);
            }
            return finalPose;
        }

        DirectX::XMFLOAT4 EvaluatePropertyKeys(const std::vector<AnimatorComponent::State::PropertyKey>& keys, float timeSeconds)
        {
            if (keys.empty())
                return {};
            if (keys.size() == 1 || timeSeconds <= keys.front().TimeSeconds)
                return keys.front().Value;
            if (timeSeconds >= keys.back().TimeSeconds)
                return keys.back().Value;

            for (size_t i = 0; i + 1 < keys.size(); ++i)
            {
                if (timeSeconds < keys[i].TimeSeconds || timeSeconds > keys[i + 1].TimeSeconds)
                    continue;

                const float span = (std::max)(0.0001f, keys[i + 1].TimeSeconds - keys[i].TimeSeconds);
                const float t = (timeSeconds - keys[i].TimeSeconds) / span;
                const auto& a = keys[i].Value;
                const auto& b = keys[i + 1].Value;
                if (keys[i].Interp == AnimatorComponent::State::PropertyKey::Interpolation::Constant)
                    return a;

                const float blend = keys[i].Interp == AnimatorComponent::State::PropertyKey::Interpolation::EaseInOut
                    ? t * t * (3.0f - 2.0f * t)
                    : t;
                // EaseInOut은 양 끝에서 천천히 시작/종료하는 보간이다.
                // 위치/색처럼 눈에 보이는 값은 Linear보다 부드럽게 보이고, Bool은 Constant로 처리해 중간값을 만들지 않는다.
                return {
                    a.x + (b.x - a.x) * blend,
                    a.y + (b.y - a.y) * blend,
                    a.z + (b.z - a.z) * blend,
                    a.w + (b.w - a.w) * blend
                };
            }
            return keys.back().Value;
        }

        Entity ResolvePropertyTrackTarget(Scene* scene, entt::entity owner, const std::string& path)
        {
            if (!scene || path.empty() || path == ".")
                return { owner, scene };
            return scene->FindEntityByName(path);
        }

        void ApplyAnimatorPropertyTrack(Scene* scene, entt::entity owner, const AnimatorComponent::State::PropertyTrack& track, float stateTimeSeconds)
        {
            Entity target = ResolvePropertyTrackTarget(scene, owner, track.EntityPath);
            if (!target || track.Keys.empty())
                return;

            const DirectX::XMFLOAT4 value = EvaluatePropertyKeys(track.Keys, stateTimeSeconds);
            if (track.ComponentName == "Transform" && target.HasComponent<TransformComponent>())
            {
                auto& transform = target.GetComponent<TransformComponent>();
                if (track.PropertyName == "Position" || track.PropertyName == "Translation")
                    transform.Translation = { value.x, value.y, value.z };
                else if (track.PropertyName == "Rotation")
                    transform.Rotation = { value.x, value.y, value.z };
                else if (track.PropertyName == "Scale")
                    transform.Scale = { value.x, value.y, value.z };
                return;
            }

            if (track.ComponentName == "Active")
            {
                auto& active = target.HasComponent<ActiveComponent>() ? target.GetComponent<ActiveComponent>() : target.AddComponent<ActiveComponent>();
                active.ActiveSelf = value.x >= 0.5f;
                return;
            }

            if (track.ComponentName == "Light" && target.HasComponent<LightComponent>())
            {
                auto& light = target.GetComponent<LightComponent>();
                if (track.PropertyName == "Intensity")
                    light.Intensity = value.x;
                else if (track.PropertyName == "Color")
                    light.LightColor = { value.x, value.y, value.z };
                return;
            }

            if (track.ComponentName == "Material" && target.HasComponent<MeshComponent>())
            {
                auto& mesh = target.GetComponent<MeshComponent>();
                if (track.PropertyName == "AlbedoColor")
                {
                    if (mesh.Material)
                        mesh.Material->AlbedoColor = value;
                    else
                        mesh.BaseColor = value;
                }
                return;
            }

            if (track.ComponentName == "Script" && target.HasComponent<ScriptComponent>())
            {
                if (track.PropertyName == "Enabled")
                    target.GetComponent<ScriptComponent>().Enabled = value.x >= 0.5f;
                return;
            }

            if (track.ComponentName == "Camera" && target.HasComponent<CameraComponent>())
            {
                auto& camera = target.GetComponent<CameraComponent>();
                if (track.PropertyName == "FOV")
                    camera.FOV = value.x;
                return;
            }

            if (track.ComponentName == "Audio" && target.HasComponent<AudioComponent>())
            {
                auto& audio = target.GetComponent<AudioComponent>();
                if (track.PropertyName == "Volume")
                    audio.Volume = std::clamp(value.x, 0.0f, 1.0f);
                else if (track.PropertyName == "Pitch")
                    audio.Pitch = (std::max)(0.01f, value.x);
                else if (track.PropertyName == "PlayTrigger")
                {
                    const bool risingEdge = audio.RuntimeLastPlaySignal < 0.5f && value.x >= 0.5f;
                    // Trigger 키프레임은 값 자체가 아니라 "눌린 순간"이 중요하다.
                    // 1 상태를 매 프레임 재생으로 해석하면 같은 사운드가 프레임마다 다시 시작되므로 0->1 변화만 요청으로 남긴다.
                    audio.RuntimePlayRequested = audio.RuntimePlayRequested || risingEdge;
                    audio.RuntimeLastPlaySignal = value.x;
                }
            }
        }

        void ApplyAnimatorPropertyTracks(Scene* scene, entt::entity owner, AnimatorComponent& animator)
        {
            for (const auto& layer : animator.Layers)
            {
                if (layer.Exited || layer.Weight <= 0.0001f || layer.ActiveStateIndex < 0 || layer.ActiveStateIndex >= static_cast<int>(layer.States.size()))
                    continue;

                const auto& state = layer.States[layer.ActiveStateIndex];
                if (state.Motion != AnimatorComponent::State::MotionType::PropertyClip)
                    continue;

                // Property Animation은 본이 없는 일반 오브젝트도 State Machine에서 움직이게 하는 경로다.
                // Transform, Active, Light, Camera처럼 게임 결과에 영향을 주는 값을 같은 시간축으로 적용한다.
                for (const auto& track : state.PropertyTracks)
                    ApplyAnimatorPropertyTrack(scene, owner, track, layer.StateTime);
            }
        }

        bool SampleRootTransform(const std::shared_ptr<AnimationClip>& clip, const AnimatorComponent::State& state, float stateTimeSeconds, const std::string& rootBone, DirectX::XMFLOAT3& outTranslation, DirectX::XMFLOAT4& outRotation)
        {
            if (!clip)
                return false;
            auto channelIt = clip->GetChannels().find(rootBone);
            if (channelIt == clip->GetChannels().end() || channelIt->second.PositionKeys.empty())
                return false;

            DirectX::XMFLOAT3 scale;
            channelIt->second.UpdateLocalTransform(ResolveStateSampleTime(state, *clip, stateTimeSeconds) * clip->GetTicksPerSecond(), outTranslation, outRotation, scale);
            return true;
        }

        bool SampleRootTransformAtClipTime(const std::shared_ptr<AnimationClip>& clip, float clipTimeSeconds, const std::string& rootBone, DirectX::XMFLOAT3& outTranslation, DirectX::XMFLOAT4& outRotation)
        {
            if (!clip)
                return false;
            auto channelIt = clip->GetChannels().find(rootBone);
            if (channelIt == clip->GetChannels().end() || channelIt->second.PositionKeys.empty())
                return false;

            DirectX::XMFLOAT3 scale;
            channelIt->second.UpdateLocalTransform(clipTimeSeconds * clip->GetTicksPerSecond(), outTranslation, outRotation, scale);
            return true;
        }

        DirectX::XMFLOAT3 TransformRootMotionPoint(const TransformComponent& transform, const DirectX::XMFLOAT3& localPoint)
        {
            DirectX::XMMATRIX world =
                DirectX::XMMatrixScaling(transform.Scale.x, transform.Scale.y, transform.Scale.z) *
                DirectX::XMMatrixRotationQuaternion(DirectX::XMLoadFloat4(&transform.QuaternionRotation)) *
                DirectX::XMMatrixTranslation(transform.Translation.x, transform.Translation.y, transform.Translation.z);

            DirectX::XMFLOAT3 result;
            DirectX::XMStoreFloat3(&result, DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&localPoint), world));
            return result;
        }

        void ResetRootMotionDebugTrace(AnimatorComponent& animator)
        {
            // Root Motion 디버그 경로는 실행 결과라서 Play 시작/종료 경계에서 항상 비운다.
            // 이전 실행의 궤적이 남으면 새 테스트에서 실제 이동 경로를 오해하기 쉽다.
            animator.RuntimeRootMotionDelta = { 0.0f, 0.0f, 0.0f };
            animator.RuntimeRootMotionRotationDelta = { 0.0f, 0.0f, 0.0f, 1.0f };
            animator.RuntimeRootMotionRootWorld = { 0.0f, 0.0f, 0.0f };
            animator.RuntimeRootMotionRootMissing = false;
            animator.RuntimeRootMotionWrappedLoop = false;
            animator.RuntimeRootMotionLockXZ = false;
            animator.RuntimeRootMotionLockY = false;
            animator.RuntimeRootMotionLockRotation = false;
            animator.RuntimeRootMotionBaked = false;
            animator.RuntimeRootMotionRotationDegrees = 0.0f;
            animator.RuntimeRootMotionPath.clear();
        }

        void ApplyAnimatorRootMotion(Scene* scene, entt::entity owner, AnimatorComponent& animator, AnimatorComponent::Layer& layer, const ModelComponent& model, float previousTimeSeconds)
        {
            if (!scene || layer.ActiveStateIndex < 0 || layer.ActiveStateIndex >= static_cast<int>(layer.States.size()))
                return;

            auto& state = layer.States[layer.ActiveStateIndex];
            if (!animator.ApplyRootMotion && !state.ApplyRootMotion)
                return;

            animator.RuntimeRootMotionRootMissing = false;
            animator.RuntimeRootMotionWrappedLoop = false;
            animator.RuntimeRootMotionLockXZ = state.ImportSettings.LockRootPositionXZ;
            animator.RuntimeRootMotionLockY = state.ImportSettings.LockRootPositionY;
            animator.RuntimeRootMotionLockRotation = state.ImportSettings.LockRootRotation;
            animator.RuntimeRootMotionBaked = state.ImportSettings.BakeRootTransform;
            animator.RuntimeRootMotionRotationDegrees = 0.0f;

            auto clip = LoadAnimatorStateClip(animator, model, state);
            DirectX::XMFLOAT3 prevRoot;
            DirectX::XMFLOAT3 nowRoot;
            DirectX::XMFLOAT4 prevRootRotation;
            DirectX::XMFLOAT4 nowRootRotation;
            const std::string sourceRootBone = animator.RetargetToHumanoid
                ? ResolveSourceAvatarBone(animator, animator.HumanoidRootBone.empty() ? "Hips" : animator.HumanoidRootBone)
                : animator.HumanoidRootBone;

            if (!SampleRootTransform(clip, state, previousTimeSeconds, sourceRootBone, prevRoot, prevRootRotation) ||
                !SampleRootTransform(clip, state, layer.StateTime, sourceRootBone, nowRoot, nowRootRotation))
            {
                // 루트 본 이름이 틀리거나 클립에 루트 채널이 없으면 이동값을 만들 수 없다.
                // 콘솔을 매 프레임 찍지 않고 상태값만 남겨 Scene View Debug가 한 번에 보여주게 한다.
                animator.RuntimeRootMotionRootMissing = true;
                return;
            }

            const float previousSampleTime = ResolveStateSampleTime(state, *clip, previousTimeSeconds);
            const float currentSampleTime = ResolveStateSampleTime(state, *clip, layer.StateTime);
            const bool wrappedLoop = state.Loop && currentSampleTime + 0.0001f < previousSampleTime;
            animator.RuntimeRootMotionWrappedLoop = wrappedLoop;

            DirectX::XMFLOAT3 delta = {
                (nowRoot.x - prevRoot.x) * std::clamp(layer.Weight, 0.0f, 1.0f),
                (nowRoot.y - prevRoot.y) * std::clamp(layer.Weight, 0.0f, 1.0f),
                (nowRoot.z - prevRoot.z) * std::clamp(layer.Weight, 0.0f, 1.0f)
            };
            DirectX::XMVECTOR rotationDelta = DirectX::XMQuaternionIdentity();

            if (wrappedLoop)
            {
                float sampleStart = 0.0f;
                float sampleEnd = 0.0f;
                ResolveStateSampleRange(state, *clip, sampleStart, sampleEnd);

                DirectX::XMFLOAT3 startRoot;
                DirectX::XMFLOAT3 endRoot;
                DirectX::XMFLOAT4 startRotation;
                DirectX::XMFLOAT4 endRotation;
                if (SampleRootTransformAtClipTime(clip, sampleStart, sourceRootBone, startRoot, startRotation) &&
                    SampleRootTransformAtClipTime(clip, sampleEnd, sourceRootBone, endRoot, endRotation))
                {
                    // 루프가 끝에서 처음으로 감길 때는 단순 now-prev가 반대 방향 큰 이동으로 보일 수 있다.
                    // 끝까지 간 거리와 시작점부터 현재까지 간 거리를 나눠 더해야 자연스러운 Root Motion이 된다.
                    const float layerWeight = std::clamp(layer.Weight, 0.0f, 1.0f);
                    delta = {
                        ((endRoot.x - prevRoot.x) + (nowRoot.x - startRoot.x)) * layerWeight,
                        ((endRoot.y - prevRoot.y) + (nowRoot.y - startRoot.y)) * layerWeight,
                        ((endRoot.z - prevRoot.z) + (nowRoot.z - startRoot.z)) * layerWeight
                    };

                    DirectX::XMVECTOR previousRotation = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&prevRootRotation));
                    DirectX::XMVECTOR endRotationQ = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&endRotation));
                    DirectX::XMVECTOR startRotationQ = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&startRotation));
                    DirectX::XMVECTOR currentRotation = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&nowRootRotation));
                    DirectX::XMVECTOR firstDelta = DirectX::XMQuaternionMultiply(DirectX::XMQuaternionInverse(previousRotation), endRotationQ);
                    DirectX::XMVECTOR secondDelta = DirectX::XMQuaternionMultiply(DirectX::XMQuaternionInverse(startRotationQ), currentRotation);
                    rotationDelta = DirectX::XMQuaternionMultiply(firstDelta, secondDelta);
                }
            }
            else
            {
                DirectX::XMVECTOR previousRotation = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&prevRootRotation));
                DirectX::XMVECTOR currentRotation = DirectX::XMQuaternionNormalize(DirectX::XMLoadFloat4(&nowRootRotation));
                rotationDelta = DirectX::XMQuaternionMultiply(DirectX::XMQuaternionInverse(previousRotation), currentRotation);
            }

            if (state.ImportSettings.LockRootPositionXZ)
            {
                delta.x = 0.0f;
                delta.z = 0.0f;
            }
            if (state.ImportSettings.LockRootPositionY)
                delta.y = 0.0f;
            if (state.ImportSettings.BakeRootTransform)
            {
                // Bake Root Transform은 루트 이동을 오브젝트 Transform에 누적하지 않는 옵션이다.
                // 루트 본의 움직임은 클립 포즈에 남고, 씬 오브젝트 위치는 제자리에서 유지된다.
                delta = { 0.0f, 0.0f, 0.0f };
                rotationDelta = DirectX::XMQuaternionIdentity();
            }

            Entity entity{ owner, scene };
            if (!entity || !entity.HasComponent<TransformComponent>())
                return;
            auto& transform = entity.GetComponent<TransformComponent>();
            transform.Translation.x += delta.x;
            transform.Translation.y += delta.y;
            transform.Translation.z += delta.z;
            animator.RuntimeRootMotionDelta = delta;

            if (!state.ImportSettings.LockRootRotation && !state.ImportSettings.BakeRootTransform)
            {
                // 회전 Root Motion은 이전 루트 회전에서 현재 루트 회전으로 가는 차이값만 뽑는다.
                // 절대 회전을 덮어쓰면 캐릭터가 가진 배치 회전까지 잃기 때문에 delta만 오브젝트에 누적한다.
                rotationDelta = DirectX::XMQuaternionSlerp(DirectX::XMQuaternionIdentity(), rotationDelta, std::clamp(layer.Weight, 0.0f, 1.0f));

                DirectX::XMVECTOR entityRotation = DirectX::XMLoadFloat4(&transform.QuaternionRotation);
                entityRotation = DirectX::XMQuaternionNormalize(DirectX::XMQuaternionMultiply(entityRotation, rotationDelta));
                DirectX::XMStoreFloat4(&transform.QuaternionRotation, entityRotation);
            }
            else
            {
                rotationDelta = DirectX::XMQuaternionIdentity();
            }

            DirectX::XMStoreFloat4(&animator.RuntimeRootMotionRotationDelta, rotationDelta);
            DirectX::XMFLOAT4 rotationDebug;
            DirectX::XMStoreFloat4(&rotationDebug, DirectX::XMQuaternionNormalize(rotationDelta));
            const float clampedW = std::clamp(rotationDebug.w, -1.0f, 1.0f);
            float angleDegrees = DirectX::XMConvertToDegrees(2.0f * std::acos(clampedW));
            if (angleDegrees > 180.0f)
                angleDegrees = 360.0f - angleDegrees;
            animator.RuntimeRootMotionRotationDegrees = angleDegrees;
            animator.RuntimeRootMotionRootWorld = TransformRootMotionPoint(transform, nowRoot);
            animator.RuntimeRootMotionPath.push_back(animator.RuntimeRootMotionRootWorld);
            const size_t maxPathPoints = std::clamp(animator.RuntimeRootMotionMaxPathPoints, (size_t)16, (size_t)2048);
            if (animator.RuntimeRootMotionPath.size() > maxPathPoints)
                animator.RuntimeRootMotionPath.erase(animator.RuntimeRootMotionPath.begin(), animator.RuntimeRootMotionPath.begin() + (animator.RuntimeRootMotionPath.size() - maxPathPoints));
        }

        void EnsureAnimatorRuntimeLayers(AnimatorComponent& animator)
        {
            if (animator.Layers.empty())
                animator.Layers.push_back({});

            if (animator.Layers[0].States.empty() && !animator.States.empty())
            {
                animator.Layers[0].States = animator.States;
                animator.Layers[0].Transitions = animator.Transitions;
                animator.Layers[0].ActiveStateIndex = animator.ActiveStateIndex;
                animator.Layers[0].EntryStateIndex = animator.EntryStateIndex;
                animator.Layers[0].SelectedTransitionIndex = animator.SelectedTransitionIndex;
            }

            animator.ActiveLayerIndex = std::clamp(animator.ActiveLayerIndex, 0, static_cast<int>(animator.Layers.size()) - 1);
            for (auto& layer : animator.Layers)
            {
                if (layer.States.empty())
                {
                    layer.ActiveStateIndex = -1;
                    layer.EntryStateIndex = -1;
                    layer.PreviousStateIndex = -1;
                    layer.StateTime = 0.0f;
                    layer.PreviousStateTime = 0.0f;
                    layer.BlendElapsed = 0.0f;
                    layer.BlendDuration = 0.0f;
                    layer.PreviousRuntimeClip.reset();
                    layer.Exited = false;
                    continue;
                }

                layer.EntryStateIndex = std::clamp(layer.EntryStateIndex, 0, static_cast<int>(layer.States.size()) - 1);
                if (!layer.Exited && (layer.ActiveStateIndex < 0 || layer.ActiveStateIndex >= static_cast<int>(layer.States.size())))
                    layer.ActiveStateIndex = layer.EntryStateIndex;
                layer.PreviousStateIndex = std::clamp(layer.PreviousStateIndex, -1, static_cast<int>(layer.States.size()) - 1);
            }
        }

        AnimatorComponent::Parameter* FindAnimatorParameter(AnimatorComponent& animator, const std::string& name)
        {
            auto it = std::find_if(animator.Parameters.begin(), animator.Parameters.end(), [&name](const AnimatorComponent::Parameter& parameter)
            {
                return parameter.Name == name;
            });
            return it != animator.Parameters.end() ? &(*it) : nullptr;
        }

        bool EvaluateAnimatorCondition(AnimatorComponent& animator, const AnimatorComponent::TransitionCondition& condition)
        {
            AnimatorComponent::Parameter* parameter = FindAnimatorParameter(animator, condition.ParameterName);
            if (!parameter)
                return false;

            switch (parameter->ParamType)
            {
                case AnimatorComponent::Parameter::Type::Bool:
                {
                    if (condition.Mode == AnimatorComponent::TransitionCondition::CompareMode::If)
                        return parameter->BoolValue;
                    if (condition.Mode == AnimatorComponent::TransitionCondition::CompareMode::IfNot)
                        return !parameter->BoolValue;
                    return parameter->BoolValue == condition.BoolValue;
                }
                case AnimatorComponent::Parameter::Type::Trigger:
                {
                    // Trigger는 Bool처럼 보이지만, 전이에 성공하면 한 번 쓰고 꺼지는 입력이다.
                    // 점프 버튼처럼 한 프레임성 신호를 상태머신에 전달할 때 사용한다.
                    if (condition.Mode == AnimatorComponent::TransitionCondition::CompareMode::IfNot)
                        return !parameter->BoolValue;
                    return parameter->BoolValue;
                }
                case AnimatorComponent::Parameter::Type::Float:
                default:
                {
                    const float value = parameter->FloatValue;
                    switch (condition.Mode)
                    {
                        case AnimatorComponent::TransitionCondition::CompareMode::Greater: return value > condition.FloatValue;
                        case AnimatorComponent::TransitionCondition::CompareMode::Less: return value < condition.FloatValue;
                        case AnimatorComponent::TransitionCondition::CompareMode::Equals: return std::abs(value - condition.FloatValue) <= 0.0001f;
                        case AnimatorComponent::TransitionCondition::CompareMode::NotEquals: return std::abs(value - condition.FloatValue) > 0.0001f;
                        case AnimatorComponent::TransitionCondition::CompareMode::IfNot: return value <= 0.0f;
                        case AnimatorComponent::TransitionCondition::CompareMode::If:
                        default: return value > 0.0f;
                    }
                }
            }
        }

        void ConsumeAnimatorTriggers(AnimatorComponent& animator, const AnimatorComponent::Transition& transition)
        {
            for (const auto& condition : transition.Conditions)
            {
                AnimatorComponent::Parameter* parameter = FindAnimatorParameter(animator, condition.ParameterName);
                if (parameter && parameter->ParamType == AnimatorComponent::Parameter::Type::Trigger)
                    parameter->BoolValue = false;
            }
        }

        bool TransitionExitTimeReached(const AnimatorComponent::Layer& layer, const AnimatorComponent::Transition& transition, const std::shared_ptr<AnimationClip>& currentClip)
        {
            if (!transition.HasExitTime)
                return true;
            if (layer.ActiveStateIndex < 0 || layer.ActiveStateIndex >= static_cast<int>(layer.States.size()))
                return false;
            if (!currentClip || currentClip->GetDurationSeconds() <= 0.0f)
                return false;

            const auto& state = layer.States[layer.ActiveStateIndex];
            const float duration = currentClip->GetDurationSeconds();
            const float normalizedTime = state.Loop
                ? std::fmod(layer.StateTime, duration) / duration
                : (std::min)(layer.StateTime / duration, 1.0f);
            return normalizedTime >= std::clamp(transition.ExitTime, 0.0f, 1.0f);
        }

        bool EvaluateAnimatorTransition(AnimatorComponent& animator, AnimatorComponent::Layer& layer, const AnimatorComponent::Transition& transition, const std::shared_ptr<AnimationClip>& currentClip)
        {
            const bool fromAnyState = transition.FromStateIndex == AnimatorComponent::Transition::AnyStateIndex;
            const bool fromCurrentState = transition.FromStateIndex == layer.ActiveStateIndex;
            if (!fromAnyState && !fromCurrentState)
                return false;
            if (transition.ToStateIndex != AnimatorComponent::Transition::ExitStateIndex &&
                (transition.ToStateIndex < 0 || transition.ToStateIndex >= static_cast<int>(layer.States.size())))
                return false;
            if (fromAnyState && transition.ToStateIndex == layer.ActiveStateIndex)
                return false;

            // 조건도 Exit Time도 없는 전이는 실수로 만든 선일 가능성이 높다.
            // 자동으로 매 프레임 전환시키지 않고, 조건이나 Exit Time을 사용자가 명확히 넣었을 때만 실행한다.
            if (!transition.HasExitTime && transition.Conditions.empty())
                return false;
            if (!TransitionExitTimeReached(layer, transition, currentClip))
                return false;

            for (const auto& condition : transition.Conditions)
            {
                if (!EvaluateAnimatorCondition(animator, condition))
                    return false;
            }
            return true;
        }

        bool IsAnimatorLayerBlending(const AnimatorComponent::Layer& layer)
        {
            return layer.PreviousStateIndex >= 0 && layer.BlendDuration > 0.0001f && layer.BlendElapsed < layer.BlendDuration;
        }

        int SelectAnimatorTransition(AnimatorComponent& animator, AnimatorComponent::Layer& layer, const std::shared_ptr<AnimationClip>& currentClip)
        {
            int selectedIndex = -1;
            int selectedPriority = std::numeric_limits<int>::max();
            int selectedSourceRank = std::numeric_limits<int>::max();
            const bool isBlending = IsAnimatorLayerBlending(layer);

            for (int i = 0; i < static_cast<int>(layer.Transitions.size()); ++i)
            {
                const auto& transition = layer.Transitions[i];
                if (isBlending && !transition.CanInterrupt)
                    continue;
                if (!EvaluateAnimatorTransition(animator, layer, transition, currentClip))
                    continue;

                // 같은 프레임에 여러 전이가 참이면 Priority가 낮은 전이를 먼저 탄다.
                // Priority가 같으면 현재 State 전이를 Any State 전이보다 먼저 골라 사용자가 만든 직접 연결을 우선한다.
                const int sourceRank = transition.FromStateIndex == AnimatorComponent::Transition::AnyStateIndex ? 1 : 0;
                if (transition.Priority < selectedPriority ||
                    (transition.Priority == selectedPriority && sourceRank < selectedSourceRank))
                {
                    selectedIndex = i;
                    selectedPriority = transition.Priority;
                    selectedSourceRank = sourceRank;
                }
            }

            return selectedIndex;
        }

        void DispatchAnimatorEvents(entt::entity owner, AnimatorComponent::Layer& layer, const AnimatorComponent::State& state, const std::shared_ptr<AnimationClip>& currentClip)
        {
            if (!currentClip || state.Events.empty())
                return;

            const float duration = currentClip->GetDurationSeconds();
            if (duration <= 0.0f)
                return;

            const float previousTime = layer.PreviousLoopTime;
            const float currentTime = state.Loop ? std::fmod(layer.StateTime, duration) : (std::min)(layer.StateTime, duration);
            const bool looped = state.Loop && currentTime < previousTime;
            if (looped)
                layer.FiredEventIndices.clear();

            for (int i = 0; i < (int)state.Events.size(); ++i)
            {
                const auto& event = state.Events[i];
                const float eventTime = std::clamp(event.TimeSeconds, 0.0f, duration);
                const bool alreadyFired = std::find(layer.FiredEventIndices.begin(), layer.FiredEventIndices.end(), i) != layer.FiredEventIndices.end();
                if (alreadyFired)
                    continue;

                const bool crossed = looped
                    ? (eventTime >= previousTime || eventTime <= currentTime)
                    : (eventTime >= previousTime && eventTime <= currentTime);
                if (!crossed)
                    continue;

                layer.FiredEventIndices.push_back(i);
                if (ScriptEngine::IsRunning())
                {
                    // Animation Event는 State가 가진 마커를 Play 중 C# 스크립트 함수 호출로 바꾸는 지점이다.
                    // 에디터 저장 데이터는 시간과 문자열만 알고, 실제 호출 여부는 ScriptEngine 실행 상태에서 결정한다.
                    ScriptEngine::InvokeAnimationEvent(static_cast<uint32_t>(owner), event.FunctionName.c_str(), event.StringArgument.c_str());
                }
                else
                {
                    ConsoleLog::Info("Animation Event: " + state.Name + "." + event.FunctionName + "(" + event.StringArgument + ")");
                }
            }

            layer.PreviousLoopTime = currentTime;
        }

        void ClearAnimatorBlendState(AnimatorComponent::Layer& layer)
        {
            layer.PreviousStateIndex = -1;
            layer.PreviousStateTime = 0.0f;
            layer.BlendElapsed = 0.0f;
            layer.BlendDuration = 0.0f;
            layer.PreviousRuntimeClip.reset();
        }

        void BeginAnimatorTransition(AnimatorComponent& animator, AnimatorComponent::Layer& layer, const AnimatorComponent::Transition& transition, const std::shared_ptr<AnimationClip>& currentClip)
        {
            layer.PreviousStateIndex = layer.ActiveStateIndex;
            layer.PreviousStateTime = layer.StateTime;
            layer.PreviousRuntimeClip = currentClip;
            if (transition.ToStateIndex == AnimatorComponent::Transition::ExitStateIndex)
            {
                if (transition.ExitTargetStateIndex >= 0 && transition.ExitTargetStateIndex < static_cast<int>(layer.States.size()))
                {
                    // 중첩 상태머신에서는 Exit가 레이어 종료가 아니라 부모 그래프의 다음 State로 빠질 수 있다.
                    // 아직 UI는 단순하지만 저장/런타임 규칙을 먼저 열어두면 나중에 Sub-State Machine을 붙일 때 데이터 포맷을 다시 바꾸지 않아도 된다.
                    layer.ActiveStateIndex = transition.ExitTargetStateIndex;
                    layer.Exited = false;
                    layer.StateTime = 0.0f;
                    layer.PreviousLoopTime = 0.0f;
                    layer.FiredEventIndices.clear();
                    layer.BlendElapsed = 0.0f;
                    layer.BlendDuration = (std::max)(0.0f, transition.BlendTime);
                    if (layer.BlendDuration <= 0.0001f || !layer.PreviousRuntimeClip)
                        ClearAnimatorBlendState(layer);
                    ConsumeAnimatorTriggers(animator, transition);
                    return;
                }

                // Exit는 실제 클립을 재생하는 State가 아니라, 현재 레이어의 평가를 끝내는 목적지다.
                // 위 레이어가 빠지면 아래 레이어가 다시 포즈를 담당하므로 레이어 우선순위가 자연스럽게 유지된다.
                layer.ActiveStateIndex = -1;
                layer.Exited = true;
                ClearAnimatorBlendState(layer);
                ConsumeAnimatorTriggers(animator, transition);
                return;
            }
            layer.ActiveStateIndex = transition.ToStateIndex;
            layer.Exited = false;
            layer.StateTime = 0.0f;
            layer.PreviousLoopTime = 0.0f;
            layer.FiredEventIndices.clear();
            layer.BlendElapsed = 0.0f;
            layer.BlendDuration = (std::max)(0.0f, transition.BlendTime);

            if (layer.BlendDuration <= 0.0001f || !layer.PreviousRuntimeClip)
                ClearAnimatorBlendState(layer);

            ConsumeAnimatorTriggers(animator, transition);
        }

        void AdvanceAnimatorLayer(AnimatorComponent& animator, AnimatorComponent::Layer& layer, const ModelComponent* model, entt::entity owner, float deltaTime, bool shouldPlay)
        {
            if (layer.States.empty())
                return;
            if (layer.Exited)
                return;

            layer.ActiveStateIndex = std::clamp(layer.ActiveStateIndex, 0, static_cast<int>(layer.States.size()) - 1);
            auto& activeState = layer.States[layer.ActiveStateIndex];
            const float activeSpeed = (std::max)(0.0f, activeState.Speed);

            if (shouldPlay)
            {
                layer.StateTime += deltaTime * activeSpeed;
                if (layer.PreviousStateIndex >= 0)
                {
                    const auto& previousState = layer.States[layer.PreviousStateIndex];
                    layer.PreviousStateTime += deltaTime * (std::max)(0.0f, previousState.Speed);
                    layer.BlendElapsed += deltaTime;
                    if (layer.BlendElapsed >= layer.BlendDuration)
                        ClearAnimatorBlendState(layer);
                }

                std::shared_ptr<AnimationClip> currentClip = model ? LoadAnimatorStateClip(animator, *model, activeState) : nullptr;
                DispatchAnimatorEvents(owner, layer, activeState, currentClip);
                const int selectedTransitionIndex = SelectAnimatorTransition(animator, layer, currentClip);
                if (selectedTransitionIndex >= 0 && selectedTransitionIndex < static_cast<int>(layer.Transitions.size()))
                {
                    BeginAnimatorTransition(animator, layer, layer.Transitions[selectedTransitionIndex], currentClip);
                }
            }
            else
            {
                layer.StateTime = 0.0f;
                layer.PreviousLoopTime = 0.0f;
                layer.FiredEventIndices.clear();
                ClearAnimatorBlendState(layer);
            }
        }

        int SelectRuntimeAnimatorLayer(const AnimatorComponent& animator)
        {
            int selectedLayer = -1;
            for (int i = 0; i < static_cast<int>(animator.Layers.size()); ++i)
            {
                const auto& layer = animator.Layers[i];
                if (!layer.Exited && !layer.States.empty() && layer.Weight > 0.0001f && layer.ActiveStateIndex >= 0 && layer.ActiveStateIndex < static_cast<int>(layer.States.size()))
                    selectedLayer = i;
            }
            return selectedLayer;
        }

        void ApplyRuntimeLayerToLegacyFields(AnimatorComponent& animator, int layerIndex)
        {
            if (layerIndex < 0 || layerIndex >= static_cast<int>(animator.Layers.size()))
                return;

            const auto& layer = animator.Layers[layerIndex];
            // 현재 스키닝 재생기는 한 번에 한 포즈를 출력한다.
            // 본 마스크가 들어오기 전까지는 Unity처럼 아래 레이어를 우선권으로 보고, 선택된 레이어를 기존 재생 필드에 복사한다.
            const bool graphStructureChanged =
                animator.RuntimePlaybackLayerIndex != layerIndex ||
                animator.States.size() != layer.States.size() ||
                animator.Transitions.size() != layer.Transitions.size();

            if (graphStructureChanged)
            {
                animator.States = layer.States;
                animator.Transitions = layer.Transitions;
                animator.RuntimePlaybackLayerIndex = layerIndex;
            }
            else if (layer.ActiveStateIndex >= 0 && layer.ActiveStateIndex < static_cast<int>(layer.States.size()) &&
                layer.ActiveStateIndex < static_cast<int>(animator.States.size()))
            {
                animator.States[layer.ActiveStateIndex] = layer.States[layer.ActiveStateIndex];
            }
            animator.ActiveStateIndex = layer.ActiveStateIndex;
            animator.EntryStateIndex = layer.EntryStateIndex;
            animator.SelectedTransitionIndex = layer.SelectedTransitionIndex;
        }

        std::string MakeDuplicateName(Scene* scene, const std::string& sourceName)
        {
            std::string baseName = sourceName.empty() ? "Entity" : sourceName;
            int firstSuffix = 1;

            size_t numberStart = baseName.find_last_not_of("0123456789");
            if (numberStart != std::string::npos && numberStart + 1 < baseName.size() && baseName[numberStart] == ' ')
            {
                std::string numberText = baseName.substr(numberStart + 1);
                baseName = baseName.substr(0, numberStart);
                firstSuffix = std::max(1, std::stoi(numberText) + 1);
            }

            // 이미 번호가 붙은 복제본을 다시 복제해도 "Name 1 1"이 아니라 "Name 2"처럼 이어 붙인다.
            int suffix = firstSuffix;
            std::string candidate = baseName + " " + std::to_string(suffix);

            while (EntityNameExists(scene, candidate))
            {
                ++suffix;
                candidate = baseName + " " + std::to_string(suffix);
            }

            return candidate;
        }

        void CopyEntityComponents(Entity srcEntity, Entity dstEntity)
        {
            if (srcEntity.HasComponent<ActiveComponent>())
                dstEntity.GetComponent<ActiveComponent>() = srcEntity.GetComponent<ActiveComponent>();

            if (srcEntity.HasComponent<TransformComponent>())
                dstEntity.GetComponent<TransformComponent>() = srcEntity.GetComponent<TransformComponent>();

            if (srcEntity.HasComponent<CameraComponent>())
                dstEntity.AddComponent<CameraComponent>(srcEntity.GetComponent<CameraComponent>());

            if (srcEntity.HasComponent<AudioComponent>())
                dstEntity.AddComponent<AudioComponent>(srcEntity.GetComponent<AudioComponent>());

            if (srcEntity.HasComponent<SpriteRendererComponent>())
                dstEntity.AddComponent<SpriteRendererComponent>(srcEntity.GetComponent<SpriteRendererComponent>());

            if (srcEntity.HasComponent<MeshComponent>())
                dstEntity.AddComponent<MeshComponent>(srcEntity.GetComponent<MeshComponent>());

            if (srcEntity.HasComponent<ModelComponent>())
                dstEntity.AddComponent<ModelComponent>(srcEntity.GetComponent<ModelComponent>());

            if (srcEntity.HasComponent<AnimatorComponent>())
                dstEntity.AddComponent<AnimatorComponent>(srcEntity.GetComponent<AnimatorComponent>());

            if (srcEntity.HasComponent<LightComponent>())
                dstEntity.AddComponent<LightComponent>(srcEntity.GetComponent<LightComponent>());

            if (srcEntity.HasComponent<Rigidbody2DComponent>())
            {
                auto& srcRb = srcEntity.GetComponent<Rigidbody2DComponent>();
                auto& dstRb = dstEntity.AddComponent<Rigidbody2DComponent>();
                dstRb.Type = srcRb.Type;
                dstRb.FixedRotation = srcRb.FixedRotation;
            }

            if (srcEntity.HasComponent<Rigidbody3DComponent>())
                dstEntity.AddComponent<Rigidbody3DComponent>(srcEntity.GetComponent<Rigidbody3DComponent>());

            if (srcEntity.HasComponent<BoxCollider2DComponent>())
            {
                auto& srcBc = srcEntity.GetComponent<BoxCollider2DComponent>();
                auto& dstBc = dstEntity.AddComponent<BoxCollider2DComponent>();
                dstBc.Offset = srcBc.Offset;
                dstBc.Size = srcBc.Size;
                dstBc.IsTrigger = srcBc.IsTrigger;
                dstBc.Density = srcBc.Density;
                dstBc.Friction = srcBc.Friction;
                dstBc.Restitution = srcBc.Restitution;
            }

            if (srcEntity.HasComponent<BoxCollider3DComponent>())
                dstEntity.AddComponent<BoxCollider3DComponent>(srcEntity.GetComponent<BoxCollider3DComponent>());

            if (srcEntity.HasComponent<SphereCollider3DComponent>())
                dstEntity.AddComponent<SphereCollider3DComponent>(srcEntity.GetComponent<SphereCollider3DComponent>());

            if (srcEntity.HasComponent<CylinderCollider3DComponent>())
                dstEntity.AddComponent<CylinderCollider3DComponent>(srcEntity.GetComponent<CylinderCollider3DComponent>());

            if (srcEntity.HasComponent<MeshCollider3DComponent>())
                dstEntity.AddComponent<MeshCollider3DComponent>(srcEntity.GetComponent<MeshCollider3DComponent>());

            if (srcEntity.HasComponent<NativeScriptComponent>())
            {
                auto& srcNsc = srcEntity.GetComponent<NativeScriptComponent>();
                auto& dstNsc = dstEntity.AddComponent<NativeScriptComponent>();
                dstNsc.InstantiateScript = srcNsc.InstantiateScript;
                dstNsc.DestroyScript = srcNsc.DestroyScript;
            }

            if (srcEntity.HasComponent<ScriptComponent>())
            {
                auto script = srcEntity.GetComponent<ScriptComponent>();
                script.RuntimeInstanceCreated = false;
                script.RuntimeAwakeCalled = false;
                script.RuntimeEnabledCalled = false;
                script.RuntimeStartCalled = false;
                dstEntity.AddComponent<ScriptComponent>(script);
            }
        }

        void* EntityHandleToUserData(entt::entity handle)
        {
            return reinterpret_cast<void*>(static_cast<uintptr_t>(static_cast<uint32_t>(handle)) + 1u);
        }

        entt::entity UserDataToEntityHandle(void* userData)
        {
            uintptr_t value = reinterpret_cast<uintptr_t>(userData);
            if (value == 0)
                return entt::null;
            return static_cast<entt::entity>(static_cast<uint32_t>(value - 1u));
        }

    }

    Scene::Scene()
    {
    }

    Scene::~Scene()
    {
    }

    Entity Scene::CreateEntity(const std::string& name)
    {
        Entity entity = { m_Registry.create(), this };

        entity.AddComponent<ActiveComponent>();
        entity.AddComponent<TransformComponent>();
        auto& tag = entity.AddComponent<TagComponent>();
        tag.Tag = name.empty() ? "Entity" : name;

        return entity;
    }

    void Scene::DestroyEntity(Entity entity)
    {
        entt::entity handle = (entt::entity)entity;
        if (handle == entt::null || !m_Registry.valid(handle))
            return;

        if (m_State == SceneState::Play)
        {
            QueueDestroyEntity(handle);
            return;
        }

        DestroyEntityImmediate(entity);
    }

    void Scene::DestroyEntityImmediate(Entity entity)
    {
        entt::entity handle = (entt::entity)entity;
        if (handle == entt::null || !m_Registry.valid(handle))
            return;

        if (m_State == SceneState::Play && m_Registry.all_of<ScriptComponent>(handle))
            DestroyRuntimeScript(handle);

        if (m_Registry.all_of<RelationshipComponent>(handle))
        {
            auto& relationship = m_Registry.get<RelationshipComponent>(handle);
            std::vector<entt::entity> children = relationship.Children;
            for (entt::entity child : children)
            {
                if (m_Registry.valid(child))
                    DestroyEntityImmediate(Entity{ child, this });
            }

            if (relationship.Parent != entt::null && m_Registry.valid(relationship.Parent) &&
                m_Registry.all_of<RelationshipComponent>(relationship.Parent))
            {
                auto& parentRelationship = m_Registry.get<RelationshipComponent>(relationship.Parent);
                parentRelationship.Children.erase(
                    std::remove(parentRelationship.Children.begin(), parentRelationship.Children.end(), handle),
                    parentRelationship.Children.end());
            }
        }

        m_Registry.destroy(handle);
    }

    void Scene::QueueDestroyEntity(entt::entity handle)
    {
        if (handle == entt::null || !m_Registry.valid(handle))
            return;

        if (std::find(m_DestroyQueue.begin(), m_DestroyQueue.end(), handle) != m_DestroyQueue.end())
            return;

        // Play 중 Destroy는 즉시 registry를 지우지 않는다.
        // 프레임 중간에 삭제하면 같은 프레임의 view 반복자가 깨질 수 있어서, 프레임 끝에서 한 번에 처리한다.
        if (m_Registry.all_of<ActiveComponent>(handle))
            m_Registry.get<ActiveComponent>(handle).ActiveSelf = false;
        m_DestroyQueue.push_back(handle);
    }

    void Scene::FlushDestroyQueue()
    {
        if (m_DestroyQueue.empty())
            return;

        std::vector<entt::entity> pending;
        pending.swap(m_DestroyQueue);

        for (entt::entity handle : pending)
        {
            if (!m_Registry.valid(handle))
                continue;

            DestroyEntityImmediate(Entity{ handle, this });
        }
    }

    bool Scene::IsEntityActiveSelf(Entity entity) const
    {
        if (!entity || entity.GetScene() != this)
            return false;

        entt::entity handle = (entt::entity)entity;
        if (!m_Registry.valid(handle))
            return false;

        if (!m_Registry.all_of<ActiveComponent>(handle))
            return true;

        return m_Registry.get<ActiveComponent>(handle).ActiveSelf;
    }

    bool Scene::IsEntityActiveInHierarchy(Entity entity) const
    {
        if (!entity || entity.GetScene() != this)
            return false;

        entt::entity current = (entt::entity)entity;
        while (current != entt::null)
        {
            if (!m_Registry.valid(current))
                return false;

            if (m_Registry.all_of<ActiveComponent>(current) &&
                !m_Registry.get<ActiveComponent>(current).ActiveSelf)
            {
                return false;
            }

            if (!m_Registry.all_of<RelationshipComponent>(current))
                break;

            current = m_Registry.get<RelationshipComponent>(current).Parent;
        }

        return true;
    }

    void Scene::SetEntityActiveSelf(Entity entity, bool active)
    {
        if (!entity || entity.GetScene() != this)
            return;

        entt::entity handle = (entt::entity)entity;
        if (!m_Registry.valid(handle))
            return;

        auto& activeComponent = m_Registry.all_of<ActiveComponent>(handle) ?
            m_Registry.get<ActiveComponent>(handle) :
            m_Registry.emplace<ActiveComponent>(handle);

        activeComponent.ActiveSelf = active;
    }

    ScriptComponent& Scene::AddScriptComponent(Entity entity, const std::string& className, bool enabled)
    {
        assert(entity && entity.GetScene() == this && "Script component target must belong to this scene.");
        assert(!entity.HasComponent<ScriptComponent>() && "Entity already has ScriptComponent.");

        auto& script = entity.AddComponent<ScriptComponent>();
        script.ClassName = className;
        script.Enabled = enabled;
        script.RuntimeInstanceCreated = false;
        script.RuntimeAwakeCalled = false;
        script.RuntimeEnabledCalled = false;
        script.RuntimeStartCalled = false;

        if (m_State == SceneState::Play && ScriptEngine::IsRunning() && IsEntityActiveInHierarchy(entity) && !script.ClassName.empty())
        {
            script.RuntimeInstanceCreated = ScriptEngine::CreateInstance(static_cast<uint32_t>((entt::entity)entity), script);
            if (script.RuntimeInstanceCreated)
            {
                // Play 중 컴포넌트를 붙이면 씬 시작 때 붙어 있던 스크립트와 같은 순서로 진입한다.
                // Start는 다음 Update 전 대기열에서 처리되어, 모든 Awake/OnEnable 뒤에 호출된다.
                ScriptEngine::InvokeLifecycle(static_cast<uint32_t>((entt::entity)entity), ScriptLifecycleEvent::Awake);
                script.RuntimeAwakeCalled = true;

                if (script.Enabled)
                {
                    ScriptEngine::InvokeLifecycle(static_cast<uint32_t>((entt::entity)entity), ScriptLifecycleEvent::OnEnable);
                    script.RuntimeEnabledCalled = true;
                }
            }
        }

        return script;
    }

    void Scene::RemoveScriptComponent(Entity entity)
    {
        if (!entity || entity.GetScene() != this || !entity.HasComponent<ScriptComponent>())
            return;

        if (m_State == SceneState::Play)
            DestroyRuntimeScript((entt::entity)entity);

        entity.RemoveComponent<ScriptComponent>();
    }

    Entity Scene::DuplicateEntity(Entity source)
    {
        if (!source || !m_Registry.valid((entt::entity)source))
            return {};

        std::vector<entt::entity> sourceHandles;
        std::function<void(Entity)> collectSubtree = [&](Entity current)
        {
            sourceHandles.push_back((entt::entity)current);

            if (!current.HasComponent<RelationshipComponent>())
                return;

            for (entt::entity childHandle : current.GetComponent<RelationshipComponent>().Children)
            {
                if (m_Registry.valid(childHandle))
                    collectSubtree(Entity{ childHandle, this });
            }
        };

        collectSubtree(source);

        std::unordered_map<entt::entity, entt::entity> entityMap;
        Entity duplicatedRoot;

        for (entt::entity srcHandle : sourceHandles)
        {
            Entity srcEntity{ srcHandle, this };
            std::string name = srcEntity.HasComponent<TagComponent>() ? srcEntity.GetComponent<TagComponent>().Tag : "Entity";

            if (srcHandle == (entt::entity)source)
                name = MakeDuplicateName(this, name);

            Entity dstEntity = CreateEntity(name);
            entityMap[srcHandle] = (entt::entity)dstEntity;

            if (srcHandle == (entt::entity)source)
                duplicatedRoot = dstEntity;

            CopyEntityComponents(srcEntity, dstEntity);
        }

        for (entt::entity srcHandle : sourceHandles)
        {
            Entity srcEntity{ srcHandle, this };
            Entity dstEntity{ entityMap[srcHandle], this };

            if (!srcEntity.HasComponent<RelationshipComponent>())
                continue;

            auto& srcRel = srcEntity.GetComponent<RelationshipComponent>();
            auto& dstRel = dstEntity.HasComponent<RelationshipComponent>() ? dstEntity.GetComponent<RelationshipComponent>() : dstEntity.AddComponent<RelationshipComponent>();
            dstRel.Children.clear();

            // 복제 대상 내부의 부모는 새 엔티티로 바꾸고, 루트의 원래 부모는 그대로 공유한다.
            auto parentIt = entityMap.find(srcRel.Parent);
            if (parentIt != entityMap.end())
            {
                dstRel.Parent = parentIt->second;
            }
            else
            {
                dstRel.Parent = (srcHandle == (entt::entity)source) ? srcRel.Parent : entt::null;
            }

            for (entt::entity srcChild : srcRel.Children)
            {
                auto childIt = entityMap.find(srcChild);
                if (childIt != entityMap.end())
                    dstRel.Children.push_back(childIt->second);
            }
        }

        if (source.HasComponent<RelationshipComponent>())
        {
            entt::entity originalParent = source.GetComponent<RelationshipComponent>().Parent;
            if (originalParent != entt::null && m_Registry.valid(originalParent))
            {
                Entity parent{ originalParent, this };
                auto& parentRel = parent.HasComponent<RelationshipComponent>() ? parent.GetComponent<RelationshipComponent>() : parent.AddComponent<RelationshipComponent>();
                parentRel.Children.push_back((entt::entity)duplicatedRoot);
            }
        }

        for (entt::entity srcHandle : sourceHandles)
        {
            Entity dstEntity{ entityMap[srcHandle], this };
            if (!dstEntity.HasComponent<ModelComponent>())
                continue;

            auto& model = dstEntity.GetComponent<ModelComponent>();

            // 모델 컴포넌트는 노드 경로별 엔티티 맵을 들고 있다. 복제 후에는 새 엔티티 핸들로 다시 연결해야 한다.
            for (auto& [name, mappedEntity] : model.NodeEntityMap)
            {
                auto it = entityMap.find(mappedEntity);
                if (it != entityMap.end())
                    mappedEntity = it->second;
            }

            for (auto& [path, mappedEntity] : model.NodePathEntityMap)
            {
                auto it = entityMap.find(mappedEntity);
                if (it != entityMap.end())
                    mappedEntity = it->second;
            }
        }

        return duplicatedRoot;
    }

    // ====================================================================
    // Play 버튼 씬 복사
    // ====================================================================
    Scene* Scene::Copy(Scene* srcScene)
    {
        Scene* newScene = new Scene();

        std::unordered_map<entt::entity, entt::entity> enttMap;

        srcScene->m_Registry.view<TagComponent>().each([&](auto srcHandle, auto& tagComp)
            {
                Entity srcEntity = { srcHandle, srcScene };

                // 1. 새 씬에 엔티티 생성 (Tag 이름 그대로 사용)
                Entity dstEntity = newScene->CreateEntity(tagComp.Tag);

                // 2. 맵핑 테이블에 기록 (구 ID -> 신 ID)
                enttMap[srcHandle] = (entt::entity)dstEntity;

                // 3. RelationshipComponent 복사 (옛날 ID 그대로 복사됨)
                if (srcEntity.HasComponent<RelationshipComponent>())
                {
                    auto& srcRel = srcEntity.GetComponent<RelationshipComponent>();
                    auto& dstRel = dstEntity.HasComponent<RelationshipComponent>() ?
                        dstEntity.GetComponent<RelationshipComponent>() :
                        dstEntity.AddComponent<RelationshipComponent>();

                    dstRel.Parent = srcRel.Parent;
                    dstRel.Children = srcRel.Children;
                }

                if (srcEntity.HasComponent<ActiveComponent>())
                {
                    dstEntity.GetComponent<ActiveComponent>() = srcEntity.GetComponent<ActiveComponent>();
                }

                if (srcEntity.HasComponent<TransformComponent>())
                {
                    dstEntity.GetComponent<TransformComponent>() = srcEntity.GetComponent<TransformComponent>();
                }

                if (srcEntity.HasComponent<CameraComponent>())
                {
                    dstEntity.AddComponent<CameraComponent>(srcEntity.GetComponent<CameraComponent>());
                }

                if (srcEntity.HasComponent<AudioComponent>())
                {
                    dstEntity.AddComponent<AudioComponent>(srcEntity.GetComponent<AudioComponent>());
                }

                if (srcEntity.HasComponent<SpriteRendererComponent>())
                {
                    dstEntity.AddComponent<SpriteRendererComponent>(srcEntity.GetComponent<SpriteRendererComponent>());
                }

                if (srcEntity.HasComponent<MeshComponent>())
                {
                    dstEntity.AddComponent<MeshComponent>(srcEntity.GetComponent<MeshComponent>());
                }

                if (srcEntity.HasComponent<ModelComponent>())
                {
                    dstEntity.AddComponent<ModelComponent>(srcEntity.GetComponent<ModelComponent>());
                }

                if (srcEntity.HasComponent<AnimatorComponent>())
                {
                    dstEntity.AddComponent<AnimatorComponent>(srcEntity.GetComponent<AnimatorComponent>());
                }

                if (srcEntity.HasComponent<LightComponent>())
                {
                    dstEntity.AddComponent<LightComponent>(srcEntity.GetComponent<LightComponent>());
                }

                // 물리 설정 복사
                if (srcEntity.HasComponent<Rigidbody2DComponent>())
                {
                    auto& srcRb = srcEntity.GetComponent<Rigidbody2DComponent>();
                    auto& dstRb = dstEntity.AddComponent<Rigidbody2DComponent>();
                    dstRb.Type = srcRb.Type;
                    dstRb.FixedRotation = srcRb.FixedRotation;
                }

                if (srcEntity.HasComponent<Rigidbody3DComponent>())
                    dstEntity.AddComponent<Rigidbody3DComponent>(srcEntity.GetComponent<Rigidbody3DComponent>());

                if (srcEntity.HasComponent<BoxCollider2DComponent>())
                {
                    auto& srcBc = srcEntity.GetComponent<BoxCollider2DComponent>();
                    auto& dstBc = dstEntity.AddComponent<BoxCollider2DComponent>();
                    dstBc.Offset = srcBc.Offset;
                    dstBc.Size = srcBc.Size;
                    dstBc.IsTrigger = srcBc.IsTrigger;
                    dstBc.Density = srcBc.Density;
                    dstBc.Friction = srcBc.Friction;
                    dstBc.Restitution = srcBc.Restitution;
                }

                if (srcEntity.HasComponent<BoxCollider3DComponent>())
                    dstEntity.AddComponent<BoxCollider3DComponent>(srcEntity.GetComponent<BoxCollider3DComponent>());

                if (srcEntity.HasComponent<SphereCollider3DComponent>())
                    dstEntity.AddComponent<SphereCollider3DComponent>(srcEntity.GetComponent<SphereCollider3DComponent>());

                if (srcEntity.HasComponent<CylinderCollider3DComponent>())
                    dstEntity.AddComponent<CylinderCollider3DComponent>(srcEntity.GetComponent<CylinderCollider3DComponent>());

                if (srcEntity.HasComponent<MeshCollider3DComponent>())
                    dstEntity.AddComponent<MeshCollider3DComponent>(srcEntity.GetComponent<MeshCollider3DComponent>());

                // 스크립트 복사
                if (srcEntity.HasComponent<NativeScriptComponent>())
                {
                    auto& srcNsc = srcEntity.GetComponent<NativeScriptComponent>();
                    auto& dstNsc = dstEntity.AddComponent<NativeScriptComponent>();
                    dstNsc.InstantiateScript = srcNsc.InstantiateScript;
                    dstNsc.DestroyScript = srcNsc.DestroyScript;
                }

                if (srcEntity.HasComponent<ScriptComponent>())
                {
                    auto script = srcEntity.GetComponent<ScriptComponent>();
                    script.RuntimeInstanceCreated = false;
                    script.RuntimeAwakeCalled = false;
                    script.RuntimeEnabledCalled = false;
                    script.RuntimeStartCalled = false;
                    dstEntity.AddComponent<ScriptComponent>(script);
                }
            });

        newScene->GetRegistry().view<RelationshipComponent>().each([&](auto dstHandle, auto& rel)
            {
                // 1. 부모 ID 갱신
                if (rel.Parent != entt::null)
                {
                    if (enttMap.find(rel.Parent) != enttMap.end())
                        rel.Parent = enttMap[rel.Parent]; // 새 ID로 교체
                    else
                        rel.Parent = entt::null; // 맵핑 실패 시 고아 처리
                }

                // 2. 자식들 ID 갱신
                for (size_t i = 0; i < rel.Children.size(); ++i)
                {
                    if (enttMap.find(rel.Children[i]) != enttMap.end())
                        rel.Children[i] = enttMap[rel.Children[i]]; // 새 ID로 교체
                }
            });

        newScene->GetRegistry().view<ModelComponent>().each([&](auto dstHandle, auto& modelComp)
            {
                for (auto& [name, mappedEntity] : modelComp.NodeEntityMap)
                {
                    auto it = enttMap.find(mappedEntity);
                    mappedEntity = it != enttMap.end() ? it->second : entt::null;
                }

                for (auto& [path, mappedEntity] : modelComp.NodePathEntityMap)
                {
                    auto it = enttMap.find(mappedEntity);
                    mappedEntity = it != enttMap.end() ? it->second : entt::null;
                }
            });

        return newScene;
    }

    void Scene::ResetScriptRuntimeState()
    {
        m_Registry.view<ScriptComponent>().each([](auto, auto& script)
            {
                script.RuntimeInstanceCreated = false;
                script.RuntimeAwakeCalled = false;
                script.RuntimeEnabledCalled = false;
                script.RuntimeStartCalled = false;
            });
    }

    void Scene::StartScriptRuntime()
    {
        ResetScriptRuntimeState();

        if (!ScriptEngine::Start(this))
            return;

        auto scriptView = m_Registry.view<ScriptComponent>();
        for (auto entityID : scriptView)
        {
            auto& script = scriptView.get<ScriptComponent>(entityID);
            Entity entity{ entityID, this };
            if (!IsEntityActiveInHierarchy(entity))
                continue;

            if (script.ClassName.empty())
                continue;

            script.RuntimeInstanceCreated = ScriptEngine::CreateInstance(static_cast<uint32_t>(entityID), script);
            if (script.RuntimeInstanceCreated)
            {
                ScriptEngine::InvokeLifecycle(static_cast<uint32_t>(entityID), ScriptLifecycleEvent::Awake);
                script.RuntimeAwakeCalled = true;
            }
        }

        for (auto entityID : scriptView)
        {
            auto& script = scriptView.get<ScriptComponent>(entityID);
            Entity entity{ entityID, this };
            if (!IsEntityActiveInHierarchy(entity) || !script.Enabled || !script.RuntimeInstanceCreated)
                continue;

            ScriptEngine::InvokeLifecycle(static_cast<uint32_t>(entityID), ScriptLifecycleEvent::OnEnable);
            script.RuntimeEnabledCalled = true;
        }

        InvokeScriptStartQueue();
    }

    void Scene::StopScriptRuntime()
    {
        if (ScriptEngine::IsRunning())
        {
            auto scriptView = m_Registry.view<ScriptComponent>();
            for (auto entityID : scriptView)
            {
                DestroyRuntimeScript(entityID);
            }
        }

        ScriptEngine::Stop();
        ResetScriptRuntimeState();
        m_DestroyQueue.clear();
    }

    void Scene::SyncScriptEnabledState()
    {
        if (!ScriptEngine::IsRunning())
            return;

        auto scriptView = m_Registry.view<ScriptComponent>();
        for (auto entityID : scriptView)
        {
            auto& script = scriptView.get<ScriptComponent>(entityID);
            Entity entity{ entityID, this };
            const bool activeInHierarchy = IsEntityActiveInHierarchy(entity);

            if (activeInHierarchy && !script.RuntimeInstanceCreated && !script.ClassName.empty())
            {
                script.RuntimeInstanceCreated = ScriptEngine::CreateInstance(static_cast<uint32_t>(entityID), script);
                if (script.RuntimeInstanceCreated)
                {
                    ScriptEngine::InvokeLifecycle(static_cast<uint32_t>(entityID), ScriptLifecycleEvent::Awake);
                    script.RuntimeAwakeCalled = true;
                }
            }

            if (!script.RuntimeInstanceCreated)
                continue;

            // Enabled는 인스펙터에서 즉시 바뀔 수 있다.
            // 런타임 플래그와 비교해서 변화가 있을 때만 OnEnable/OnDisable을 보낸다.
            if (activeInHierarchy && script.Enabled && !script.RuntimeEnabledCalled)
            {
                ScriptEngine::InvokeLifecycle(static_cast<uint32_t>(entityID), ScriptLifecycleEvent::OnEnable);
                script.RuntimeEnabledCalled = true;
            }
            else if ((!activeInHierarchy || !script.Enabled) && script.RuntimeEnabledCalled)
            {
                ScriptEngine::InvokeLifecycle(static_cast<uint32_t>(entityID), ScriptLifecycleEvent::OnDisable);
                script.RuntimeEnabledCalled = false;
            }
        }
    }

    void Scene::InvokeScriptStartQueue()
    {
        if (!ScriptEngine::IsRunning())
            return;

        auto scriptView = m_Registry.view<ScriptComponent>();
        for (auto entityID : scriptView)
        {
            auto& script = scriptView.get<ScriptComponent>(entityID);
            Entity entity{ entityID, this };
            if (!IsEntityActiveInHierarchy(entity) || !script.Enabled || !script.RuntimeInstanceCreated || !script.RuntimeEnabledCalled || script.RuntimeStartCalled)
                continue;

            ScriptEngine::InvokeLifecycle(static_cast<uint32_t>(entityID), ScriptLifecycleEvent::Start);
            script.RuntimeStartCalled = true;
        }
    }

    void Scene::InvokeScriptUpdatePass(ScriptLifecycleEvent eventType, float deltaTime)
    {
        if (!ScriptEngine::IsRunning())
            return;

        auto scriptView = m_Registry.view<ScriptComponent>();
        for (auto entityID : scriptView)
        {
            auto& script = scriptView.get<ScriptComponent>(entityID);
            Entity entity{ entityID, this };
            if (IsEntityActiveInHierarchy(entity) && script.Enabled && script.RuntimeInstanceCreated && script.RuntimeEnabledCalled && script.RuntimeStartCalled)
                ScriptEngine::InvokeLifecycle(static_cast<uint32_t>(entityID), eventType, deltaTime);
        }
    }

    Scene::PhysicsPair Scene::MakePhysicsPair(entt::entity a, entt::entity b) const
    {
        if (static_cast<uint32_t>(a) > static_cast<uint32_t>(b))
            std::swap(a, b);
        return { a, b };
    }

    void Scene::CollectPhysicsEvents()
    {
        if (!b2World_IsValid(m_PhysicsWorldId))
            return;

        std::unordered_set<PhysicsPair, PhysicsPairHash> beganCollisions;
        std::unordered_set<PhysicsPair, PhysicsPairHash> beganTriggers;

        auto eraseInvalidPairs = [this](std::unordered_set<PhysicsPair, PhysicsPairHash>& pairs)
            {
                for (auto it = pairs.begin(); it != pairs.end();)
                {
                    if (it->A == entt::null || it->B == entt::null || !m_Registry.valid(it->A) || !m_Registry.valid(it->B))
                        it = pairs.erase(it);
                    else
                        ++it;
                }
            };

        eraseInvalidPairs(m_ActiveCollisionPairs);
        eraseInvalidPairs(m_ActiveTriggerPairs);

        auto resolveShapeEntity = [this](b2ShapeId shapeId) -> entt::entity
            {
                if (!b2Shape_IsValid(shapeId))
                    return entt::null;

                entt::entity handle = UserDataToEntityHandle(b2Shape_GetUserData(shapeId));
                if (handle == entt::null || !m_Registry.valid(handle))
                    return entt::null;

                return handle;
            };

        auto queueTwoWayEvent = [this](ScriptPhysicsEvent eventType, entt::entity a, entt::entity b)
            {
                if (a == entt::null || b == entt::null || a == b)
                    return;

                // 물리 월드가 이벤트를 만든 직후 바로 C#을 호출하지 않고 큐에 복사한다.
                // 스크립트 안에서 Destroy 같은 구조 변경이 일어나도 Box2D 이벤트 배열을 건드리지 않게 하기 위해서다.
                m_PhysicsEventQueue.push_back({ eventType, a, b });
                m_PhysicsEventQueue.push_back({ eventType, b, a });
            };

        b2ContactEvents contactEvents = b2World_GetContactEvents(m_PhysicsWorldId);
        for (int i = 0; i < contactEvents.beginCount; ++i)
        {
            const b2ContactBeginTouchEvent& event = contactEvents.beginEvents[i];
            entt::entity a = resolveShapeEntity(event.shapeIdA);
            entt::entity b = resolveShapeEntity(event.shapeIdB);
            PhysicsPair pair = MakePhysicsPair(a, b);
            if (pair.A == entt::null || pair.B == entt::null)
                continue;

            m_ActiveCollisionPairs.insert(pair);
            beganCollisions.insert(pair);
            queueTwoWayEvent(ScriptPhysicsEvent::OnCollisionEnter2D, pair.A, pair.B);
        }

        for (int i = 0; i < contactEvents.endCount; ++i)
        {
            const b2ContactEndTouchEvent& event = contactEvents.endEvents[i];
            entt::entity a = resolveShapeEntity(event.shapeIdA);
            entt::entity b = resolveShapeEntity(event.shapeIdB);
            PhysicsPair pair = MakePhysicsPair(a, b);
            if (pair.A == entt::null || pair.B == entt::null)
                continue;

            m_ActiveCollisionPairs.erase(pair);
            queueTwoWayEvent(ScriptPhysicsEvent::OnCollisionExit2D, pair.A, pair.B);
        }

        b2SensorEvents sensorEvents = b2World_GetSensorEvents(m_PhysicsWorldId);
        for (int i = 0; i < sensorEvents.beginCount; ++i)
        {
            const b2SensorBeginTouchEvent& event = sensorEvents.beginEvents[i];
            entt::entity sensor = resolveShapeEntity(event.sensorShapeId);
            entt::entity visitor = resolveShapeEntity(event.visitorShapeId);
            PhysicsPair pair = MakePhysicsPair(sensor, visitor);
            if (pair.A == entt::null || pair.B == entt::null)
                continue;

            m_ActiveTriggerPairs.insert(pair);
            beganTriggers.insert(pair);
            queueTwoWayEvent(ScriptPhysicsEvent::OnTriggerEnter2D, pair.A, pair.B);
        }

        for (int i = 0; i < sensorEvents.endCount; ++i)
        {
            const b2SensorEndTouchEvent& event = sensorEvents.endEvents[i];
            entt::entity sensor = resolveShapeEntity(event.sensorShapeId);
            entt::entity visitor = resolveShapeEntity(event.visitorShapeId);
            PhysicsPair pair = MakePhysicsPair(sensor, visitor);
            if (pair.A == entt::null || pair.B == entt::null)
                continue;

            m_ActiveTriggerPairs.erase(pair);
            queueTwoWayEvent(ScriptPhysicsEvent::OnTriggerExit2D, pair.A, pair.B);
        }

        for (const PhysicsPair& pair : m_ActiveCollisionPairs)
        {
            if (!beganCollisions.contains(pair))
                queueTwoWayEvent(ScriptPhysicsEvent::OnCollisionStay2D, pair.A, pair.B);
        }

        for (const PhysicsPair& pair : m_ActiveTriggerPairs)
        {
            if (!beganTriggers.contains(pair))
                queueTwoWayEvent(ScriptPhysicsEvent::OnTriggerStay2D, pair.A, pair.B);
        }
    }

    void Scene::CreatePhysicsWorld3D()
    {
        m_PhysicsWorld3D = std::make_unique<PhysicsWorld3D>();
        auto view = m_Registry.view<TransformComponent>();
        for (auto entityID : view)
        {
            Entity entity{ entityID, this };
            if (!IsEntityActiveInHierarchy(entity))
                continue;

            const bool hasBox = entity.HasComponent<BoxCollider3DComponent>();
            const bool hasSphere = entity.HasComponent<SphereCollider3DComponent>();
            const bool hasCylinder = entity.HasComponent<CylinderCollider3DComponent>();
            const bool hasMesh = entity.HasComponent<MeshCollider3DComponent>();
            if (!hasBox && !hasSphere && !hasCylinder && !hasMesh)
                continue;

            const auto& transform = entity.GetComponent<TransformComponent>();
            PhysicsWorld3D::BodyDesc desc;
            desc.EntityID = static_cast<uint32_t>(entityID);
            desc.Position = transform.Translation;
            desc.Rotation = transform.Rotation;
            desc.Scale = transform.Scale;

            if (entity.HasComponent<Rigidbody3DComponent>())
            {
                const auto& rigidbody = entity.GetComponent<Rigidbody3DComponent>();
                desc.Type = static_cast<PhysicsWorld3D::BodyType>(rigidbody.Type);
                desc.Mass = rigidbody.Mass;
                desc.LinearDamping = rigidbody.LinearDamping;
                desc.AngularDamping = rigidbody.AngularDamping;
                desc.Friction = rigidbody.Friction;
                desc.Restitution = rigidbody.Restitution;
                desc.UseGravity = rigidbody.UseGravity;
                desc.FixedRotation = rigidbody.FixedRotation;
                desc.LinearVelocity = rigidbody.LinearVelocity;
                desc.AngularVelocity = rigidbody.AngularVelocity;
            }

            if (hasBox)
            {
                const auto& collider = entity.GetComponent<BoxCollider3DComponent>();
                desc.Shape = PhysicsWorld3D::ShapeType::Box;
                desc.Offset = collider.Offset;
                desc.Size = collider.Size;
                desc.IsTrigger = collider.IsTrigger;
            }
            else if (hasSphere)
            {
                const auto& collider = entity.GetComponent<SphereCollider3DComponent>();
                desc.Shape = PhysicsWorld3D::ShapeType::Sphere;
                desc.Offset = collider.Offset;
                desc.Size = { collider.Radius, collider.Radius, collider.Radius };
                desc.IsTrigger = collider.IsTrigger;
            }
            else if (hasCylinder)
            {
                const auto& collider = entity.GetComponent<CylinderCollider3DComponent>();
                desc.Shape = PhysicsWorld3D::ShapeType::Cylinder;
                desc.Offset = collider.Offset;
                desc.Size = { collider.Radius, collider.Height, collider.Radius };
                desc.IsTrigger = collider.IsTrigger;
            }
            else
            {
                const auto& collider = entity.GetComponent<MeshCollider3DComponent>();
                desc.Shape = PhysicsWorld3D::ShapeType::MeshBounds;
                desc.Offset = collider.Offset;
                desc.Size = collider.Size;
                desc.IsTrigger = collider.IsTrigger;
                // 비볼록 삼각형 메시를 움직이면 연속 충돌과 관성 계산이 필요하다.
                // 1차 백엔드에서는 잘못된 결과를 내는 대신 정적 bounds로 명확히 제한한다.
                if (!collider.Convex && desc.Type == PhysicsWorld3D::BodyType::Dynamic)
                {
                    ConsoleLog::Warning("Rigidbody3D dynamic MeshCollider requires Convex. Using static bounds for entity " + std::to_string(desc.EntityID));
                    desc.Type = PhysicsWorld3D::BodyType::Static;
                }
            }

            m_PhysicsWorld3D->AddBody(desc);
        }
    }

    void Scene::CollectPhysicsEvents3D()
    {
        if (!m_PhysicsWorld3D)
            return;

        for (const PhysicsWorld3D::Event& event : m_PhysicsWorld3D->GetEvents())
        {
            entt::entity a = static_cast<entt::entity>(event.EntityA);
            entt::entity b = static_cast<entt::entity>(event.EntityB);
            if (!m_Registry.valid(a) || !m_Registry.valid(b) || a == b)
                continue;

            ScriptPhysicsEvent type;
            if (event.IsTrigger)
            {
                type = event.Phase == PhysicsWorld3D::EventPhase::Enter ? ScriptPhysicsEvent::OnTriggerEnter3D :
                    event.Phase == PhysicsWorld3D::EventPhase::Stay ? ScriptPhysicsEvent::OnTriggerStay3D : ScriptPhysicsEvent::OnTriggerExit3D;
            }
            else
            {
                type = event.Phase == PhysicsWorld3D::EventPhase::Enter ? ScriptPhysicsEvent::OnCollisionEnter3D :
                    event.Phase == PhysicsWorld3D::EventPhase::Stay ? ScriptPhysicsEvent::OnCollisionStay3D : ScriptPhysicsEvent::OnCollisionExit3D;
            }
            m_PhysicsEventQueue.push_back({ type, a, b });
            m_PhysicsEventQueue.push_back({ type, b, a });
        }
    }

    void Scene::StepPhysicsWorld3D(float deltaTime)
    {
        if (!m_PhysicsWorld3D)
            return;

        auto rigidbodyView = m_Registry.view<Rigidbody3DComponent, TransformComponent>();
        rigidbodyView.each([&](auto entityID, auto& rigidbody, auto& transform)
        {
            if (rigidbody.Type == Rigidbody3DComponent::BodyType::Kinematic && IsEntityActiveInHierarchy(Entity{ entityID, this }))
                m_PhysicsWorld3D->SetKinematicTransform(static_cast<uint32_t>(entityID), transform.Translation, transform.Rotation, deltaTime);
        });

        m_PhysicsWorld3D->Step(deltaTime);
        rigidbodyView.each([&](auto entityID, auto& rigidbody, auto& transform)
        {
            if (rigidbody.Type != Rigidbody3DComponent::BodyType::Dynamic || !IsEntityActiveInHierarchy(Entity{ entityID, this }))
                return;
            PhysicsWorld3D::BodyState state;
            if (!m_PhysicsWorld3D->GetBodyState(static_cast<uint32_t>(entityID), state))
                return;
            transform.Translation = state.Position;
            transform.Rotation = state.Rotation;
            rigidbody.LinearVelocity = state.LinearVelocity;
            rigidbody.AngularVelocity = state.AngularVelocity;
        });
        CollectPhysicsEvents3D();
    }

    void Scene::DispatchPhysicsEventQueue()
    {
        if (!ScriptEngine::IsRunning() || m_PhysicsEventQueue.empty())
            return;

        std::vector<QueuedPhysicsEvent> events;
        events.swap(m_PhysicsEventQueue);

        for (const QueuedPhysicsEvent& event : events)
        {
            if (event.Entity == entt::null || event.Other == entt::null || !m_Registry.valid(event.Entity) || !m_Registry.valid(event.Other))
                continue;

            Entity entity{ event.Entity, this };
            if (!IsEntityActiveInHierarchy(entity) || !m_Registry.all_of<ScriptComponent>(event.Entity))
                continue;

            const auto& script = m_Registry.get<ScriptComponent>(event.Entity);
            if (!script.Enabled || !script.RuntimeInstanceCreated || !script.RuntimeEnabledCalled || !script.RuntimeStartCalled)
                continue;

            ScriptEngine::InvokePhysicsEvent(static_cast<uint32_t>(event.Entity), event.EventType, static_cast<uint32_t>(event.Other));
        }
    }

    void Scene::DestroyRuntimeScript(entt::entity handle)
    {
        if (!ScriptEngine::IsRunning() || !m_Registry.valid(handle) || !m_Registry.all_of<ScriptComponent>(handle))
            return;

        auto& script = m_Registry.get<ScriptComponent>(handle);
        const uint32_t entityID = static_cast<uint32_t>(handle);
        if (script.RuntimeInstanceCreated && script.RuntimeEnabledCalled)
        {
            ScriptEngine::InvokeLifecycle(entityID, ScriptLifecycleEvent::OnDisable);
            script.RuntimeEnabledCalled = false;
        }

        if (script.RuntimeInstanceCreated)
        {
            ScriptEngine::InvokeLifecycle(entityID, ScriptLifecycleEvent::OnDestroy);
            ScriptEngine::DestroyInstance(entityID);
        }

        script.RuntimeInstanceCreated = false;
        script.RuntimeAwakeCalled = false;
        script.RuntimeEnabledCalled = false;
        script.RuntimeStartCalled = false;
    }

    // ====================================================================
    // Play 모드 시작
    // ====================================================================
    void Scene::OnRuntimeStart()
    {
        m_State = SceneState::Play;
        m_FixedAccumulator = 0.0f;
        m_PhysicsEventQueue.clear();
        m_ActiveCollisionPairs.clear();
        m_ActiveTriggerPairs.clear();

        b2WorldDef worldDef = b2DefaultWorldDef();
        worldDef.gravity = { 0.0f, -9.8f };
        m_PhysicsWorldId = b2CreateWorld(&worldDef);
        CreatePhysicsWorld3D();

        auto view = m_Registry.view<Rigidbody2DComponent>();
        for (auto e : view)
        {
            Entity entity = { e, this };
            if (!IsEntityActiveInHierarchy(entity))
                continue;

            auto& transform = entity.GetComponent<TransformComponent>();
            auto& rb2d = entity.GetComponent<Rigidbody2DComponent>();

            b2BodyDef bodyDef = b2DefaultBodyDef();

            if (rb2d.Type == Rigidbody2DComponent::BodyType::Static)
            {
                bodyDef.type = b2_staticBody;
            }
            else if (rb2d.Type == Rigidbody2DComponent::BodyType::Dynamic)
            {
                bodyDef.type = b2_dynamicBody;
            }
            else if (rb2d.Type == Rigidbody2DComponent::BodyType::Kinematic)
            {
                bodyDef.type = b2_kinematicBody;
            }

            bodyDef.position = { transform.Translation.x, transform.Translation.y };
            bodyDef.rotation = b2MakeRot(transform.Rotation.z);
            bodyDef.fixedRotation = rb2d.FixedRotation;
            bodyDef.userData = EntityHandleToUserData(e);

            rb2d.RuntimeBodyId = b2CreateBody(m_PhysicsWorldId, &bodyDef);

            if (entity.HasComponent<BoxCollider2DComponent>())
            {
                auto& bc2d = entity.GetComponent<BoxCollider2DComponent>();

                b2ShapeDef shapeDef = b2DefaultShapeDef();
                shapeDef.userData = EntityHandleToUserData(e);
                shapeDef.isSensor = bc2d.IsTrigger;
                shapeDef.enableSensorEvents = true;
                shapeDef.enableContactEvents = !bc2d.IsTrigger;
                shapeDef.density = bc2d.Density;
                shapeDef.material.friction = bc2d.Friction;
                shapeDef.material.restitution = bc2d.Restitution;

                float hx = bc2d.Size.x * transform.Scale.x * 0.5f;
                float hy = bc2d.Size.y * transform.Scale.y * 0.5f;
                b2Polygon box = b2MakeBox(hx, hy);

                bc2d.RuntimeShapeId = b2CreatePolygonShape(rb2d.RuntimeBodyId, &shapeDef, &box);
            }
        }

        auto animatorView = m_Registry.view<AnimatorComponent>();
        for (auto e : animatorView)
        {
            auto& animator = animatorView.get<AnimatorComponent>(e);
            EnsureAnimatorRuntimeLayers(animator);
            ResetRootMotionDebugTrace(animator);
            if (animator.Layers.empty())
                continue;

            for (auto& layer : animator.Layers)
            {
                if (layer.States.empty())
                    continue;

                // Play 모드에 들어갈 때는 에디터에서 마지막으로 눌러 둔 상태가 아니라 Entry State에서 시작한다.
                // 이렇게 해야 Unity Animator처럼 런타임 시작점이 명확하고, 에디터 프리뷰 선택이 게임 실행에 섞이지 않는다.
                layer.EntryStateIndex = std::clamp(layer.EntryStateIndex, 0, static_cast<int>(layer.States.size() - 1));
                layer.ActiveStateIndex = layer.EntryStateIndex;
                layer.Exited = false;
                layer.PreviousStateIndex = -1;
                layer.StateTime = 0.0f;
                layer.PreviousLoopTime = 0.0f;
                layer.PreviousStateTime = 0.0f;
                layer.BlendElapsed = 0.0f;
                layer.BlendDuration = 0.0f;
                layer.FiredEventIndices.clear();
                layer.PreviousRuntimeClip.reset();
            }

            ApplyRuntimeLayerToLegacyFields(animator, SelectRuntimeAnimatorLayer(animator));
            animator.RuntimeClip.reset();
            animator.RuntimeClipKey.clear();
            animator.AnimPlayer.StopAnimation();
            animator.IsPlaying = animator.AutoPlay;
        }

        auto audioView = m_Registry.view<AudioComponent>();
        for (auto e : audioView)
        {
            auto& audio = audioView.get<AudioComponent>(e);
            audio.RuntimePlaying = false;
            audio.RuntimeStopRequested = false;
            audio.RuntimeLastPlaySignal = 0.0f;
            audio.RuntimePlayRequested = audio.Enabled && audio.PlayOnStart;
        }

        StartScriptRuntime();
    }

    // ====================================================================
    // Edit 모드 복귀
    // ====================================================================
    void Scene::OnRuntimeStop()
    {
        // 관리 객체를 먼저 정리해야 OnDisable/OnDestroy에서 아직 살아 있는 엔티티 컴포넌트에 접근할 수 있다.
        FlushDestroyQueue();
        StopScriptRuntime();

        m_State = SceneState::Edit;
        m_FixedAccumulator = 0.0f;
        m_PhysicsEventQueue.clear();
        m_ActiveCollisionPairs.clear();
        m_ActiveTriggerPairs.clear();

        if (b2World_IsValid(m_PhysicsWorldId))
        {
            b2DestroyWorld(m_PhysicsWorldId);
            m_PhysicsWorldId = b2_nullWorldId;
        }
        m_PhysicsWorld3D.reset();

        auto animatorView = m_Registry.view<AnimatorComponent>();
        for (auto e : animatorView)
            ResetRootMotionDebugTrace(animatorView.get<AnimatorComponent>(e));

        auto audioView = m_Registry.view<AudioComponent>();
        for (auto e : audioView)
        {
            auto& audio = audioView.get<AudioComponent>(e);
            audio.RuntimePlaying = false;
            audio.RuntimePlayRequested = false;
            audio.RuntimeStopRequested = false;
            audio.RuntimeLastPlaySignal = 0.0f;
        }

        m_Registry.view<NativeScriptComponent>().each([](auto entityID, auto& nsc)
            {
                if (nsc.Instance)
                {
                    nsc.DestroyScript(&nsc);
                }
            });
    }

    // ====================================================================
    // 매 프레임 업데이트
    // ====================================================================
    void Scene::OnUpdate(float deltaTime)
    {
        // 1. 물리 & 로직 (오직 Play 모드에서만!)
        if (m_State == SceneState::Play)
        {
            SyncScriptEnabledState();
            InvokeScriptStartQueue();

            // 창 드래그나 디버거 중단 뒤 큰 delta가 들어와도 물리 catch-up이 무한히 쌓이지 않게 제한한다.
            m_FixedAccumulator += (std::min)(deltaTime, 0.25f);
            while (m_FixedAccumulator >= m_FixedTimeStep)
            {
                InvokeScriptUpdatePass(ScriptLifecycleEvent::FixedUpdate, m_FixedTimeStep);
                StepPhysicsWorld3D(m_FixedTimeStep);
                m_FixedAccumulator -= m_FixedTimeStep;
            }

            if (b2World_IsValid(m_PhysicsWorldId))
            {
                b2World_Step(m_PhysicsWorldId, deltaTime, 4);

                auto rbView = m_Registry.view<Rigidbody2DComponent, TransformComponent>();
                rbView.each([&](auto entityID, auto& rb2d, auto& transform)
                    {
                        if (!IsEntityActiveInHierarchy(Entity{ entityID, this }))
                            return;

                        b2Vec2 position = b2Body_GetPosition(rb2d.RuntimeBodyId);
                        b2Rot rotation = b2Body_GetRotation(rb2d.RuntimeBodyId);

                        transform.Translation.x = position.x;
                        transform.Translation.y = position.y;
                        transform.Rotation.z = b2Rot_GetAngle(rotation);
                    });

                CollectPhysicsEvents();
            }

            DispatchPhysicsEventQueue();

            m_Registry.view<NativeScriptComponent>().each([=](auto entityID, auto& nsc)
                {
                    if (!IsEntityActiveInHierarchy(Entity{ entityID, this }))
                        return;

                    if (!nsc.Instance)
                    {
                        nsc.Instance = nsc.InstantiateScript();
                        nsc.Instance->m_Entity = Entity{ entityID, this };
                        nsc.Instance->OnCreate();
                    }
                    nsc.Instance->OnUpdate(deltaTime);
                });

            InvokeScriptUpdatePass(ScriptLifecycleEvent::Update, deltaTime);
            InvokeScriptUpdatePass(ScriptLifecycleEvent::LateUpdate, deltaTime);
            FlushDestroyQueue();
        } 

        // =========================================================
        // 2. 애니메이터 재생 업데이트 
        // =========================================================
        auto animView = m_Registry.view<AnimatorComponent>();
        animView.each([&](auto entityID, auto& animComp)
            {
                Entity entity{ entityID, this };
                if (!IsEntityActiveInHierarchy(entity))
                    return;

                bool shouldPlay = false;
                if (m_State == SceneState::Play)
                {
                    shouldPlay = animComp.AutoPlay || animComp.IsPlaying;
                    if (animComp.AutoPlay)
                        animComp.IsPlaying = true;
                }
                else
                {
                    shouldPlay = animComp.PreviewInEdit && animComp.IsPlaying;
                }

                EnsureAnimatorRuntimeLayers(animComp);

                Entity current = entity;
                while (current.HasComponent<RelationshipComponent>() && !current.HasComponent<ModelComponent>())
                {
                    entt::entity parentID = current.GetComponent<RelationshipComponent>().Parent;
                    if (parentID != entt::null)
                    {
                        current = { parentID, this };
                    }
                    else
                    {
                        break;
                    }
                }

                if (current.HasComponent<ModelComponent>())
                {
                    auto& modelComponent = current.GetComponent<ModelComponent>();
                    auto& model = modelComponent.TargetModel;
                    if (!model)
                        return;

                    if (shouldPlay)
                    {
                        for (auto& layer : animComp.Layers)
                        {
                            const float previousTime = layer.StateTime;
                            AdvanceAnimatorLayer(animComp, layer, &modelComponent, entityID, deltaTime, true);
                            ApplyAnimatorRootMotion(this, entityID, animComp, layer, modelComponent, previousTime);
                        }
                        ApplyAnimatorPropertyTracks(this, entityID, animComp);
                    }

                    const int runtimeLayerIndex = SelectRuntimeAnimatorLayer(animComp);
                    ApplyRuntimeLayerToLegacyFields(animComp, runtimeLayerIndex);

                    if (shouldPlay)
                    {
                        const auto finalPose = EvaluateAnimatorLayeredPose(animComp, modelComponent);
                        if (!finalPose.empty())
                        {
                            // State Machine이 계산한 최종 포즈를 재생기에 직접 넣는다.
                            // 단일 클립 재생 경로를 유지하면서도 Layer Weight와 Blend Tree 결과를 스키닝에 반영하기 위한 연결점이다.
                            animComp.AnimPlayer.SetWriteDefaults(true);
                            animComp.AnimPlayer.SetPoseOverride(finalPose);
                        }
                        else
                        {
                            animComp.AnimPlayer.ClearPoseOverride();
                            animComp.AnimPlayer.StopAnimation();
                        }
                    }
                    else if (animComp.AnimPlayer.IsPlaying())
                    {
                        animComp.AnimPlayer.ClearPoseOverride();
                        animComp.AnimPlayer.StopAnimation();
                    }
                    else
                    {
                        animComp.AnimPlayer.ClearPoseOverride();
                        animComp.AnimPlayer.ClearBlendSource();
                    }

                    const auto& nodeMap = modelComponent.NodePathEntityMap.empty() ? modelComponent.NodeEntityMap : modelComponent.NodePathEntityMap;
                    animComp.AnimPlayer.Update(0.0f, model.get(), this, &nodeMap);
                }
                else if (shouldPlay)
                {
                    for (auto& layer : animComp.Layers)
                        AdvanceAnimatorLayer(animComp, layer, nullptr, entityID, deltaTime, true);
                    ApplyAnimatorPropertyTracks(this, entityID, animComp);
                }
            });
    }

    // ====================================================================
    // 2D 렌더링
    // ====================================================================
    void Scene::OnRender2D(const PerspectiveCamera& camera)
    {
        Renderer2D::BeginScene(camera);

        auto renderView = m_Registry.view<TransformComponent, SpriteRendererComponent>();
        renderView.each([&](auto entityID, auto& transform, auto& sprite)
            {
                if (!IsEntityActiveInHierarchy(Entity{ entityID, this }))
                    return;

                DirectX::XMFLOAT2 size = { transform.Scale.x, transform.Scale.y };
                Renderer2D::DrawQuad(transform.Translation, size, sprite.Color, (int)entityID);
            });

        Renderer2D::EndScene();
    }

    // ====================================================================
    // 3D 렌더링
    // ====================================================================
    void Scene::OnRender3D(const PerspectiveCamera& camera)
    {
        SceneLightData sceneLight;
        sceneLight.LightCount = 0; // 초기화

        auto lightView = m_Registry.view<TransformComponent, LightComponent>();

        lightView.each([&](auto entityID, auto& tc, auto& lc)
            {
                if (!IsEntityActiveInHierarchy(Entity{ entityID, this }))
                    return;

                // 이미 조명을 4개(배열 꽉 참) 찾았다면, 더 이상 계산하지 않고 스킵
                if (sceneLight.LightCount >= 4)
                {
                    return;
                }

                auto q = DirectX::XMLoadFloat4(&tc.QuaternionRotation);
                DirectX::XMVECTOR forward = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
                DirectX::XMVECTOR rotatedForward = DirectX::XMVector3Rotate(forward, q);

                DirectX::XMStoreFloat3(&sceneLight.Lights[sceneLight.LightCount].Direction, rotatedForward);
                sceneLight.Lights[sceneLight.LightCount].Color = lc.LightColor;
                sceneLight.Lights[sceneLight.LightCount].Intensity = lc.Intensity;

                sceneLight.LightCount++; // 저장했으니 카운트 1 증가
            });

        Renderer3D::BeginScene(camera, sceneLight);

        std::function<DirectX::XMMATRIX(Entity)> getTransform = [&](Entity e) -> DirectX::XMMATRIX
            {
                auto& tc = e.GetComponent<TransformComponent>();
                auto q = DirectX::XMLoadFloat4(&tc.QuaternionRotation);
                DirectX::XMMATRIX transform = DirectX::XMMatrixScaling(tc.Scale.x, tc.Scale.y, tc.Scale.z) *
                    DirectX::XMMatrixRotationQuaternion(q) *
                    DirectX::XMMatrixTranslation(tc.Translation.x, tc.Translation.y, tc.Translation.z);

                if (e.HasComponent<RelationshipComponent>())
                {
                    entt::entity parentID = e.GetComponent<RelationshipComponent>().Parent;
                    if (parentID != entt::null)
                    {
                        Entity parent{ parentID, this };
                        DirectX::XMMATRIX parentWorld;

                        // [핵심 로직 추가] 부모가 애니메이터를 가지고 있는지 확인
                        if (parent.HasComponent<AnimatorComponent>())
                        {
                            auto& anim = parent.GetComponent<AnimatorComponent>().AnimPlayer;
                            auto& tag = e.GetComponent<TagComponent>().Tag;

                            // 부모의 모델 데이터가 필요함 (이름으로 인덱스를 찾기 위해)
                            // 현재 구조상 부모가 ModelComponent도 같이 가지고 있다고 가정
                            if (parent.HasComponent<ModelComponent>())
                            {
                                auto model = parent.GetComponent<ModelComponent>().TargetModel.get();
                                int boneIdx = anim.GetBoneIndex(tag, model);

                                if (boneIdx != -1)
                                {
                                    // 부모의 단순 Transform이 아니라 애니메이션이 적용된 '뼈 행렬'을 부모 행렬로 사용!
                                    parentWorld = anim.GetFinalMatrix(boneIdx);
                                }
                                else
                                {
                                    parentWorld = getTransform(parent);
                                }
                            }
                            else
                            {
                                parentWorld = getTransform(parent);
                            }
                        }
                        else
                        {
                            parentWorld = getTransform(parent);
                        }

                        transform = transform * parentWorld;
                    }
                }
                return transform;
            };

        std::function<AnimatorComponent* (Entity)> findAnimator = [&](Entity e) -> AnimatorComponent*
            {
                if (e.HasComponent<AnimatorComponent>())
                {
                    return &e.GetComponent<AnimatorComponent>();
                }

                if (e.HasComponent<RelationshipComponent>())
                {
                    entt::entity parentID = e.GetComponent<RelationshipComponent>().Parent;
                    if (parentID != entt::null)
                    {
                        return findAnimator({ parentID, this });
                    }
                }
                return nullptr;
            };

        auto meshView = m_Registry.view<TransformComponent, MeshComponent>();
        meshView.each([&](auto entityID, auto& tc, auto& mesh)
            {
                Entity entity{ entityID, this };
                if (!IsEntityActiveInHierarchy(entity))
                    return;

                // 애니메이터와 루트 엔티티를 찾기 위한 변수
                Entity current = entity;
                AnimatorComponent* animatorComp = nullptr;
                Entity rootEntity = entity; // 루트를 기억할 변수

                // 부모를 타고 올라가며 애니메이터 찾기
                while (true)
                {
                    if (current.HasComponent<AnimatorComponent>())
                    {
                        animatorComp = &current.GetComponent<AnimatorComponent>();
                        rootEntity = current; // 애니메이터를 가진 놈이 바로 진짜 루트!
                        break;
                    }

                    if (current.HasComponent<RelationshipComponent>() && current.GetComponent<RelationshipComponent>().Parent != entt::null)
                    {
                        current = { current.GetComponent<RelationshipComponent>().Parent, this };
                    }
                    else
                    {
                        break;
                    }
                }

                DirectX::XMFLOAT4 renderColor = mesh.BaseColor;
                std::shared_ptr<Texture2D> renderTexture = mesh.AlbedoMap;
                if (mesh.Material)
                {
                    // Material이 연결된 메시만 재질 값을 우선한다.
                    // 기존 씬은 BaseColor/AlbedoMap을 그대로 쓰기 때문에 구버전 데이터가 깨지지 않는다.
                    renderColor = mesh.Material->AlbedoColor;
                    if (mesh.Material->AlbedoTexture)
                        renderTexture = mesh.Material->AlbedoTexture;
                }
                const bool forceErrorShader = mesh.MaterialMissing;

                const bool canUseSkinning = animatorComp && mesh.MeshData && mesh.MeshData->HasSkinWeights();
                if (canUseSkinning)
                {
                    DirectX::XMMATRIX rootWorldTransform = getTransform(rootEntity);
                    auto& animator = animatorComp->AnimPlayer;

                    Renderer3D::DrawSkinnedMesh(
                        rootWorldTransform,
                        mesh.MeshData,
                        renderTexture,
                        renderColor,
                        mesh.Material.get(),
                        (int)entityID,
                        animator.GetFinalBoneMatrices(),
                        forceErrorShader
                    );
                }
                else
                {
                    // Animator 컴포넌트가 붙어 있어도 큐브/스피어 같은 일반 메시에는 본 가중치가 없다.
                    // 이런 메시를 스킨드 셰이더로 보내면 빈 본 행렬을 기준으로 변형되어 사라져 보일 수 있다.
                    DirectX::XMMATRIX worldTransform = getTransform(entity);
                    Renderer3D::DrawMesh(worldTransform, mesh.MeshData, renderTexture, renderColor, mesh.Material.get(), (int)entityID, forceErrorShader);
                }
            });

        Renderer3D::EndScene();
    }

    // ====================================================================
    // 엔티티 이름으로 찾기 (에디터 본 조작 연동용)
    // ====================================================================
    Entity Scene::FindEntityByName(std::string_view name)
    {
        entt::entity found = entt::null;

        m_Registry.view<TagComponent>().each([&](auto entity, auto& tag)
            {
                if (found != entt::null) return; // 이미 찾았으면 스킵
                if (tag.Tag == name)
                {
                    found = entity;
                }
            });

        return found != entt::null ? Entity{ found, this } : Entity{};
    }
}
