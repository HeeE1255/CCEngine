#include "UI/AnimatorGraphPanel.h"

#include "Application.h"
#include "Animation/Animator.h"
#include "Animation/AnimatorControllerAsset.h"
#include "Core/AssetDatabase.h"
#include "Events/KeyEvent.h"
#include "Events/MouseEvent.h"
#include "Renderer/UIRenderer.h"

#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace CCEngine::UI
{
    namespace
    {
        constexpr DirectX::XMFLOAT4 CanvasColor = { 0.045f, 0.047f, 0.052f, 1.0f };
        constexpr DirectX::XMFLOAT4 SidebarColor = { 0.090f, 0.094f, 0.102f, 1.0f };
        constexpr DirectX::XMFLOAT4 PanelStroke = { 0.23f, 0.24f, 0.27f, 1.0f };
        constexpr DirectX::XMFLOAT4 GridFine = { 0.115f, 0.120f, 0.132f, 0.42f };
        constexpr DirectX::XMFLOAT4 GridMajor = { 0.160f, 0.165f, 0.180f, 0.55f };
        constexpr DirectX::XMFLOAT4 TextStrong = { 0.88f, 0.90f, 0.94f, 1.0f };
        constexpr DirectX::XMFLOAT4 TextMuted = { 0.58f, 0.60f, 0.65f, 1.0f };
        constexpr DirectX::XMFLOAT4 AccentBlue = { 0.32f, 0.54f, 0.82f, 1.0f };
        constexpr DirectX::XMFLOAT4 AccentGreen = { 0.28f, 0.68f, 0.40f, 1.0f };
        constexpr DirectX::XMFLOAT4 AccentOrange = { 0.78f, 0.48f, 0.16f, 1.0f };
        constexpr DirectX::XMFLOAT4 AccentTeal = { 0.30f, 0.62f, 0.62f, 1.0f };
        constexpr DirectX::XMFLOAT4 AccentRed = { 0.62f, 0.14f, 0.16f, 1.0f };

        void DrawBorder(float x, float y, float w, float h, const DirectX::XMFLOAT4& color, float t = 1.0f)
        {
            UIRenderer::DrawRectFilled(x, y, w, t, color);
            UIRenderer::DrawRectFilled(x, y + h - t, w, t, color);
            UIRenderer::DrawRectFilled(x, y, t, h, color);
            UIRenderer::DrawRectFilled(x + w - t, y, t, h, color);
        }

        void DrawLine(DirectX::XMFLOAT2 a, DirectX::XMFLOAT2 b, const DirectX::XMFLOAT4& color, float thickness)
        {
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            const int steps = (std::max)(1, (int)(std::sqrt(dx * dx + dy * dy) / 4.0f));
            for (int i = 0; i <= steps; ++i)
            {
                const float t = (float)i / (float)steps;
                const float x = a.x + dx * t;
                const float y = a.y + dy * t;
                UIRenderer::DrawRectFilled(x - thickness * 0.5f, y - thickness * 0.5f, thickness, thickness, color);
            }
        }

        void ClearAnimatorRuntimeCache(AnimatorComponent& animator)
        {
            // 그래프 Undo/Redo는 편집 내용만 되돌린다.
            // 재생 중에 생긴 클립 캐시와 블렌딩 상태까지 저장하면, 되돌린 뒤 엉뚱한 포즈가 남을 수 있다.
            animator.RuntimeClip.reset();
            animator.RuntimeClipKey.clear();
            animator.AnimPlayer.StopAnimation();
            animator.IsPlaying = false;
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

            for (auto& layer : animator.Layers)
            {
                layer.PreviousRuntimeClip.reset();
                layer.PreviousStateIndex = -1;
                layer.BlendElapsed = 0.0f;
                layer.BlendDuration = 0.0f;
                layer.FiredEventIndices.clear();
                layer.Exited = false;
            }
        }

        const char* ParameterTypeName(AnimatorComponent::Parameter::Type type)
        {
            switch (type)
            {
                case AnimatorComponent::Parameter::Type::Bool: return "Bool";
                case AnimatorComponent::Parameter::Type::Trigger: return "Trigger";
                default: return "Float";
            }
        }

        const char* ConditionModeName(AnimatorComponent::TransitionCondition::CompareMode mode)
        {
            switch (mode)
            {
                case AnimatorComponent::TransitionCondition::CompareMode::IfNot: return "If Not";
                case AnimatorComponent::TransitionCondition::CompareMode::Greater: return ">";
                case AnimatorComponent::TransitionCondition::CompareMode::Less: return "<";
                case AnimatorComponent::TransitionCondition::CompareMode::Equals: return "==";
                case AnimatorComponent::TransitionCondition::CompareMode::NotEquals: return "!=";
                default: return "If";
            }
        }

        bool IsSpecialAnimatorStateIndex(int stateIndex)
        {
            return stateIndex == AnimatorComponent::Transition::AnyStateIndex ||
                stateIndex == AnimatorComponent::Transition::ExitStateIndex;
        }

        bool IsValidAnimatorStateEndpoint(const AnimatorComponent::Layer& layer, int stateIndex)
        {
            return IsSpecialAnimatorStateIndex(stateIndex) ||
                (stateIndex >= 0 && stateIndex < static_cast<int>(layer.States.size()));
        }

        std::string GetAnimatorStateLabel(const AnimatorComponent::Layer& layer, int stateIndex)
        {
            if (stateIndex == AnimatorComponent::Transition::AnyStateIndex)
                return "Any State";
            if (stateIndex == AnimatorComponent::Transition::ExitStateIndex)
                return "Exit";
            if (stateIndex >= 0 && stateIndex < static_cast<int>(layer.States.size()))
                return layer.States[stateIndex].Name;
            return "(Missing State)";
        }

        DirectX::XMFLOAT2 GetSpecialAnimatorNodeGraphPosition(int stateIndex)
        {
            if (stateIndex == AnimatorComponent::Transition::AnyStateIndex)
                return { 120.0f, 320.0f };
            if (stateIndex == AnimatorComponent::Transition::ExitStateIndex)
                return { 120.0f, 460.0f };
            return { 120.0f, 180.0f };
        }

        const AnimatorComponent::Parameter* FindAnimatorParameter(const AnimatorComponent& animator, const std::string& name)
        {
            auto it = std::find_if(animator.Parameters.begin(), animator.Parameters.end(), [&name](const AnimatorComponent::Parameter& parameter)
            {
                return parameter.Name == name;
            });
            return it != animator.Parameters.end() ? &(*it) : nullptr;
        }

        AnimatorComponent::TransitionCondition::CompareMode DefaultModeForParameter(AnimatorComponent::Parameter::Type type)
        {
            if (type == AnimatorComponent::Parameter::Type::Float)
                return AnimatorComponent::TransitionCondition::CompareMode::Greater;
            return AnimatorComponent::TransitionCondition::CompareMode::If;
        }

        AnimatorComponent::TransitionCondition::CompareMode NextConditionModeForParameter(
            AnimatorComponent::TransitionCondition::CompareMode mode,
            AnimatorComponent::Parameter::Type type)
        {
            if (type == AnimatorComponent::Parameter::Type::Bool || type == AnimatorComponent::Parameter::Type::Trigger)
                return mode == AnimatorComponent::TransitionCondition::CompareMode::If
                    ? AnimatorComponent::TransitionCondition::CompareMode::IfNot
                    : AnimatorComponent::TransitionCondition::CompareMode::If;

            int next = (static_cast<int>(mode) + 1) % 6;
            return static_cast<AnimatorComponent::TransitionCondition::CompareMode>(next);
        }

        void CycleConditionParameter(AnimatorComponent& animator, AnimatorComponent::TransitionCondition& condition)
        {
            if (animator.Parameters.empty())
            {
                condition.ParameterName.clear();
                return;
            }

            int index = -1;
            for (int i = 0; i < static_cast<int>(animator.Parameters.size()); ++i)
            {
                if (animator.Parameters[i].Name == condition.ParameterName)
                {
                    index = i;
                    break;
                }
            }

            index = (index + 1) % static_cast<int>(animator.Parameters.size());
            condition.ParameterName = animator.Parameters[index].Name;
            condition.Mode = DefaultModeForParameter(animator.Parameters[index].ParamType);
        }

        AnimatorComponent::TransitionCondition MakeDefaultTransitionCondition(const AnimatorComponent& animator)
        {
            AnimatorComponent::TransitionCondition condition;
            if (!animator.Parameters.empty())
            {
                condition.ParameterName = animator.Parameters.front().Name;
                condition.Mode = DefaultModeForParameter(animator.Parameters.front().ParamType);
            }
            return condition;
        }

        std::vector<std::string> ValidateTransition(const AnimatorComponent& animator, const AnimatorComponent::Layer& layer, const AnimatorComponent::Transition& transition)
        {
            std::vector<std::string> issues;
            if (!IsValidAnimatorStateEndpoint(layer, transition.FromStateIndex))
                issues.push_back("Missing source state");
            if (!IsValidAnimatorStateEndpoint(layer, transition.ToStateIndex))
                issues.push_back("Missing target state");
            if (transition.FromStateIndex == AnimatorComponent::Transition::ExitStateIndex)
                issues.push_back("Exit cannot be source");
            if (transition.ToStateIndex == AnimatorComponent::Transition::AnyStateIndex)
                issues.push_back("Any State cannot be target");
            if (!transition.HasExitTime && transition.Conditions.empty())
                issues.push_back("No condition or exit time");

            for (const auto& condition : transition.Conditions)
            {
                const auto* parameter = FindAnimatorParameter(animator, condition.ParameterName);
                if (condition.ParameterName.empty() || !parameter)
                {
                    issues.push_back("Missing parameter: " + (condition.ParameterName.empty() ? std::string("(empty)") : condition.ParameterName));
                    continue;
                }

                if ((parameter->ParamType == AnimatorComponent::Parameter::Type::Bool ||
                    parameter->ParamType == AnimatorComponent::Parameter::Type::Trigger) &&
                    condition.Mode != AnimatorComponent::TransitionCondition::CompareMode::If &&
                    condition.Mode != AnimatorComponent::TransitionCondition::CompareMode::IfNot)
                {
                    issues.push_back("Wrong mode for " + condition.ParameterName);
                }
            }

            for (int i = 0; i < static_cast<int>(transition.Conditions.size()); ++i)
            {
                for (int j = i + 1; j < static_cast<int>(transition.Conditions.size()); ++j)
                {
                    const auto& a = transition.Conditions[i];
                    const auto& b = transition.Conditions[j];
                    if (a.ParameterName.empty() || a.ParameterName != b.ParameterName)
                        continue;

                    if ((a.Mode == AnimatorComponent::TransitionCondition::CompareMode::If && b.Mode == AnimatorComponent::TransitionCondition::CompareMode::IfNot) ||
                        (a.Mode == AnimatorComponent::TransitionCondition::CompareMode::IfNot && b.Mode == AnimatorComponent::TransitionCondition::CompareMode::If))
                        issues.push_back("Conflict: " + a.ParameterName + " true/false");

                    if ((a.Mode == AnimatorComponent::TransitionCondition::CompareMode::Greater && b.Mode == AnimatorComponent::TransitionCondition::CompareMode::Less && a.FloatValue >= b.FloatValue) ||
                        (a.Mode == AnimatorComponent::TransitionCondition::CompareMode::Less && b.Mode == AnimatorComponent::TransitionCondition::CompareMode::Greater && b.FloatValue >= a.FloatValue))
                        issues.push_back("Conflict: " + a.ParameterName + " range");
                }
            }

            return issues;
        }

        const char* LayerBlendModeName(AnimatorComponent::Layer::BlendMode mode)
        {
            return mode == AnimatorComponent::Layer::BlendMode::Additive ? "Additive" : "Override";
        }

        const char* BlendTreeTypeName(AnimatorComponent::State::BlendTree::Type type)
        {
            switch (type)
            {
                case AnimatorComponent::State::BlendTree::Type::OneD: return "1D";
                case AnimatorComponent::State::BlendTree::Type::TwoD: return "2D";
                case AnimatorComponent::State::BlendTree::Type::TwoDFreeform: return "2D + 2 Axis";
                default: return "Direct";
            }
        }

        std::string MotionLabel(const AnimatorComponent::State& state)
        {
            if (state.Motion == AnimatorComponent::State::MotionType::BlendTree)
                return "Motion: Blend Tree";
            if (state.Motion == AnimatorComponent::State::MotionType::PropertyClip)
                return "Motion: Property Clip " + std::to_string(state.PropertyTracks.size());
            if (state.ClipIndex < 0)
                return "Motion: None";
            return "Motion: Clip " + std::to_string(state.ClipIndex);
        }

        std::string PropertyTrackLabel(const AnimatorComponent::State::PropertyTrack& track)
        {
            const std::string target = track.EntityPath.empty() || track.EntityPath == "." ? std::string("Self") : track.EntityPath;
            return target + "." + track.ComponentName + "." + track.PropertyName;
        }

        std::string PropertyTrackTypeName(AnimatorComponent::State::PropertyTrack::ValueType type)
        {
            switch (type)
            {
                case AnimatorComponent::State::PropertyTrack::ValueType::Bool: return "Bool";
                case AnimatorComponent::State::PropertyTrack::ValueType::Float3: return "Float3";
                case AnimatorComponent::State::PropertyTrack::ValueType::Float4: return "Float4";
                default: return "Float";
            }
        }

        std::string PickFirstFloatParameter(const AnimatorComponent& animator)
        {
            for (const auto& parameter : animator.Parameters)
            {
                if (parameter.ParamType == AnimatorComponent::Parameter::Type::Float)
                    return parameter.Name;
            }
            return animator.Parameters.empty() ? std::string{} : animator.Parameters.front().Name;
        }

        bool IsFloatParameter(const AnimatorComponent& animator, const std::string& name)
        {
            const auto* parameter = FindAnimatorParameter(animator, name);
            return parameter && parameter->ParamType == AnimatorComponent::Parameter::Type::Float;
        }

        Entity FindAnimatorModelRoot(Entity entity)
        {
            Entity current = entity;
            while (current)
            {
                if (current.HasComponent<ModelComponent>())
                    return current;

                if (!current.HasComponent<RelationshipComponent>())
                    break;

                entt::entity parentID = current.GetComponent<RelationshipComponent>().Parent;
                if (parentID == entt::null)
                    break;
                current = { parentID, current.GetScene() };
            }
            return {};
        }

        std::string NormalizeRootBoneKey(std::string value)
        {
            std::replace(value.begin(), value.end(), '\\', '/');
            const size_t slash = value.find_last_of('/');
            if (slash != std::string::npos)
                value = value.substr(slash + 1);
            const size_t colon = value.find_last_of(':');
            if (colon != std::string::npos)
                value = value.substr(colon + 1);

            std::string normalized;
            normalized.reserve(value.size());
            for (char c : value)
            {
                if (c == '_' || c == '-' || std::isspace(static_cast<unsigned char>(c)))
                    continue;
                normalized.push_back((char)std::tolower(static_cast<unsigned char>(c)));
            }
            return normalized;
        }

        int ScoreRootBoneCandidate(const std::string& name)
        {
            const std::string key = NormalizeRootBoneKey(name);
            if (key == "hips" || key == "pelvis")
                return 100;
            if (key.find("hips") != std::string::npos || key.find("pelvis") != std::string::npos)
                return 90;
            if (key == "root")
                return 70;
            if (key.find("root") != std::string::npos)
                return 55;
            if (key.find("armature") != std::string::npos)
                return 35;
            return 0;
        }

        std::vector<std::string> CollectRootBoneCandidates(Entity entity)
        {
            std::vector<std::string> candidates;
            std::unordered_set<std::string> seen;
            Entity modelRoot = FindAnimatorModelRoot(entity);
            if (!modelRoot || !modelRoot.HasComponent<ModelComponent>())
                return candidates;

            auto pushCandidate = [&](const std::string& value)
            {
                if (value.empty())
                    return;
                if (seen.insert(value).second)
                    candidates.push_back(value);
            };

            const auto& model = modelRoot.GetComponent<ModelComponent>();
            for (const auto& [path, handle] : model.NodePathEntityMap)
                pushCandidate(path);
            for (const auto& [name, handle] : model.NodeEntityMap)
                pushCandidate(name);

            std::stable_sort(candidates.begin(), candidates.end(), [](const std::string& a, const std::string& b)
            {
                const int scoreA = ScoreRootBoneCandidate(a);
                const int scoreB = ScoreRootBoneCandidate(b);
                if (scoreA != scoreB)
                    return scoreA > scoreB;
                return a.size() < b.size();
            });
            return candidates;
        }

        std::string FindBestRootBoneCandidate(Entity entity)
        {
            const auto candidates = CollectRootBoneCandidates(entity);
            for (const std::string& candidate : candidates)
            {
                if (ScoreRootBoneCandidate(candidate) > 0)
                    return candidate;
            }
            return candidates.empty() ? std::string("Hips") : candidates.front();
        }

        std::filesystem::path ResolveAnimatorSourcePathForRootCandidate(const AnimatorComponent& animator, Entity entity)
        {
            if (!animator.SourceAssetGuid.empty())
            {
                std::filesystem::path guidPath = AssetDatabase::GetPathFromGuid(animator.SourceAssetGuid);
                if (!guidPath.empty() && std::filesystem::exists(guidPath))
                    return guidPath;
            }
            if (!animator.SourcePath.empty() && std::filesystem::exists(animator.SourcePath))
                return animator.SourcePath;

            Entity modelRoot = FindAnimatorModelRoot(entity);
            if (modelRoot && modelRoot.HasComponent<ModelComponent>())
            {
                const auto& model = modelRoot.GetComponent<ModelComponent>();
                if (!model.AssetGuid.empty())
                {
                    std::filesystem::path modelPath = AssetDatabase::GetPathFromGuid(model.AssetGuid);
                    if (!modelPath.empty() && std::filesystem::exists(modelPath))
                        return modelPath;
                }
                if (model.TargetModel)
                    return model.TargetModel->GetFilePath();
            }
            return {};
        }

        std::string FindBestRootBoneCandidateForState(Entity entity, const AnimatorComponent& animator, const AnimatorComponent::State& state)
        {
            std::vector<std::string> candidates;
            std::unordered_set<std::string> seen;
            const std::filesystem::path sourcePath = ResolveAnimatorSourcePathForRootCandidate(animator, entity);
            if (!sourcePath.empty())
            {
                auto clip = AnimationClip::LoadShared(sourcePath.string(), (uint32_t)(std::max)(0, state.ClipIndex));
                if (clip)
                {
                    for (const auto& [channelName, channel] : clip->GetChannels())
                    {
                        if (!channelName.empty() && seen.insert(channelName).second)
                            candidates.push_back(channelName);
                    }
                }
            }

            std::stable_sort(candidates.begin(), candidates.end(), [](const std::string& a, const std::string& b)
            {
                const int scoreA = ScoreRootBoneCandidate(a);
                const int scoreB = ScoreRootBoneCandidate(b);
                if (scoreA != scoreB)
                    return scoreA > scoreB;
                return a.size() < b.size();
            });

            for (const std::string& candidate : candidates)
            {
                if (ScoreRootBoneCandidate(candidate) > 0)
                    return candidate;
            }
            return FindBestRootBoneCandidate(entity);
        }

        float GetFloatParameterValue(const AnimatorComponent& animator, const std::string& name)
        {
            const auto* parameter = FindAnimatorParameter(animator, name);
            return (parameter && parameter->ParamType == AnimatorComponent::Parameter::Type::Float) ? parameter->FloatValue : 0.0f;
        }

        std::string FormatBlendValue(float value)
        {
            char buffer[32] = {};
            snprintf(buffer, sizeof(buffer), "%.2f", value);
            return buffer;
        }

        std::string FormatPropertyValue(float value)
        {
            char buffer[32] = {};
            snprintf(buffer, sizeof(buffer), "%.3f", value);
            return buffer;
        }

        const char* PropertyInterpolationName(AnimatorComponent::State::PropertyKey::Interpolation interpolation)
        {
            switch (interpolation)
            {
                case AnimatorComponent::State::PropertyKey::Interpolation::Constant: return "Constant";
                case AnimatorComponent::State::PropertyKey::Interpolation::EaseInOut: return "Ease";
                default: return "Linear";
            }
        }

        AnimatorComponent::State::PropertyKey::Interpolation NextPropertyInterpolation(AnimatorComponent::State::PropertyKey::Interpolation interpolation)
        {
            switch (interpolation)
            {
                case AnimatorComponent::State::PropertyKey::Interpolation::Constant:
                    return AnimatorComponent::State::PropertyKey::Interpolation::Linear;
                case AnimatorComponent::State::PropertyKey::Interpolation::Linear:
                    return AnimatorComponent::State::PropertyKey::Interpolation::EaseInOut;
                default:
                    return AnimatorComponent::State::PropertyKey::Interpolation::Constant;
            }
        }

        bool IsNumericEditCharacter(char c)
        {
            return std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '.';
        }

        DirectX::XMFLOAT4 EvaluatePropertyKeysForEditor(const std::vector<AnimatorComponent::State::PropertyKey>& keys, float timeSeconds)
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
                return {
                    a.x + (b.x - a.x) * blend,
                    a.y + (b.y - a.y) * blend,
                    a.z + (b.z - a.z) * blend,
                    a.w + (b.w - a.w) * blend
                };
            }
            return keys.back().Value;
        }

        void EnsureAnimatorLayers(AnimatorComponent& animator)
        {
            if (animator.Layers.empty())
                animator.Layers.push_back({});
            animator.ActiveLayerIndex = std::clamp(animator.ActiveLayerIndex, 0, (int)animator.Layers.size() - 1);

            auto& baseLayer = animator.Layers[0];
            if (baseLayer.States.empty() && !animator.States.empty())
            {
                // 예전 저장 파일은 상태/전이를 AnimatorComponent 바로 아래에 저장했다.
                // 처음 열 때만 Base Layer로 옮겨 두면 기존 씬도 새 레이어 구조에서 그대로 편집된다.
                baseLayer.States = animator.States;
                baseLayer.Transitions = animator.Transitions;
                baseLayer.ActiveStateIndex = animator.ActiveStateIndex;
                baseLayer.EntryStateIndex = animator.EntryStateIndex;
                baseLayer.SelectedTransitionIndex = animator.SelectedTransitionIndex;
            }

            for (auto& layer : animator.Layers)
            {
                if (layer.States.empty())
                {
                    layer.ActiveStateIndex = -1;
                    layer.EntryStateIndex = -1;
                    layer.SelectedTransitionIndex = -1;
                    layer.Transitions.clear();
                    continue;
                }

                layer.ActiveStateIndex = std::clamp(layer.ActiveStateIndex, 0, (int)layer.States.size() - 1);
                layer.EntryStateIndex = std::clamp(layer.EntryStateIndex, 0, (int)layer.States.size() - 1);
                layer.Transitions.erase(
                    std::remove_if(layer.Transitions.begin(), layer.Transitions.end(), [&layer](const AnimatorComponent::Transition& transition)
                    {
                        // Any State와 Exit는 실제 State 배열에는 없지만 전이 그래프에서는 유효한 특수 노드다.
                        // 여기서 음수 인덱스를 전부 지우면 저장해 둔 특수 전이가 편집기를 여는 순간 사라진다.
                        return !IsValidAnimatorStateEndpoint(layer, transition.FromStateIndex) ||
                            !IsValidAnimatorStateEndpoint(layer, transition.ToStateIndex) ||
                            transition.FromStateIndex == AnimatorComponent::Transition::ExitStateIndex ||
                            transition.ToStateIndex == AnimatorComponent::Transition::AnyStateIndex;
                    }),
                    layer.Transitions.end());
                layer.SelectedTransitionIndex = std::clamp(layer.SelectedTransitionIndex, -1, (int)layer.Transitions.size() - 1);
            }
        }

        AnimatorComponent::Layer* GetActiveLayer(AnimatorComponent& animator)
        {
            EnsureAnimatorLayers(animator);
            return &animator.Layers[animator.ActiveLayerIndex];
        }

        const AnimatorComponent::Layer* GetActiveLayer(const AnimatorComponent& animator)
        {
            if (animator.Layers.empty())
                return nullptr;
            const int index = std::clamp(animator.ActiveLayerIndex, 0, (int)animator.Layers.size() - 1);
            return &animator.Layers[index];
        }

        void SyncBaseLayerToLegacyGraph(AnimatorComponent& animator)
        {
            if (animator.Layers.empty())
                return;

            const auto& baseLayer = animator.Layers[0];
            // 현재 런타임은 아직 Base Layer의 그래프만 재생한다.
            // 편집 데이터는 레이어 안에 두고, 런타임 호환용 필드에는 Base Layer만 복사한다.
            animator.States = baseLayer.States;
            animator.Transitions = baseLayer.Transitions;
            animator.ActiveStateIndex = baseLayer.ActiveStateIndex;
            animator.EntryStateIndex = baseLayer.EntryStateIndex;
            animator.SelectedTransitionIndex = baseLayer.SelectedTransitionIndex;
        }

        std::filesystem::path ResolveControllerPath(const AnimatorComponent& animator)
        {
            if (!animator.ControllerAssetGuid.empty())
            {
                std::filesystem::path guidPath = AssetDatabase::GetPathFromGuid(animator.ControllerAssetGuid);
                if (!guidPath.empty())
                    return guidPath;
            }
            return animator.ControllerPath;
        }

        std::string NormalizeControllerPathKey(const std::filesystem::path& path)
        {
            std::string key = path.lexically_normal().string();
            std::replace(key.begin(), key.end(), '\\', '/');
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c)
            {
                return (char)std::tolower(c);
            });
            return key;
        }

        bool SaveControllerIfAssigned(AnimatorComponent& animator)
        {
            std::filesystem::path controllerPath = ResolveControllerPath(animator);
            if (controllerPath.empty())
                return false;

            AnimatorControllerAsset::Normalize(animator);
            // 상태머신 그래프는 오브젝트가 아니라 Controller 에셋이 원본이다.
            // 편집 직후 이 파일을 갱신해야 같은 Controller를 쓰는 다른 오브젝트도 같은 규칙을 공유한다.
            return AnimatorControllerAsset::SaveToFile(controllerPath, animator);
        }

        void CommitAnimatorGraphChange(AnimatorComponent& animator)
        {
            SyncBaseLayerToLegacyGraph(animator);
            SaveControllerIfAssigned(animator);
        }

        std::string FitText(const std::string& text, float availableWidth, float approximateCharWidth = 8.0f)
        {
            const int maxChars = (std::max)(0, (int)(availableWidth / approximateCharWidth));
            if ((int)text.size() <= maxChars)
                return text;
            if (maxChars <= 3)
                return text.substr(0, (size_t)(std::max)(0, maxChars));
            return text.substr(0, (size_t)maxChars - 3) + "...";
        }

        float DistancePointToSegment(float px, float py, DirectX::XMFLOAT2 a, DirectX::XMFLOAT2 b)
        {
            const float vx = b.x - a.x;
            const float vy = b.y - a.y;
            const float wx = px - a.x;
            const float wy = py - a.y;
            const float lenSq = vx * vx + vy * vy;
            const float t = lenSq <= 0.0001f ? 0.0f : (std::clamp)((wx * vx + wy * vy) / lenSq, 0.0f, 1.0f);
            const float cx = a.x + vx * t;
            const float cy = a.y + vy * t;
            const float dx = px - cx;
            const float dy = py - cy;
            return std::sqrt(dx * dx + dy * dy);
        }

        void DrawArrowHead(DirectX::XMFLOAT2 from, DirectX::XMFLOAT2 to, const DirectX::XMFLOAT4& color)
        {
            const float dx = to.x - from.x;
            const float dy = to.y - from.y;
            const float len = (std::max)(0.001f, std::sqrt(dx * dx + dy * dy));
            const float nx = dx / len;
            const float ny = dy / len;
            const float px = -ny;
            const float py = nx;
            DirectX::XMFLOAT2 left = { to.x - nx * 12.0f + px * 5.0f, to.y - ny * 12.0f + py * 5.0f };
            DirectX::XMFLOAT2 right = { to.x - nx * 12.0f - px * 5.0f, to.y - ny * 12.0f - py * 5.0f };
            DrawLine(to, left, color, 2.0f);
            DrawLine(to, right, color, 2.0f);
        }

        std::string TrimName(const std::string& input)
        {
            const auto begin = std::find_if_not(input.begin(), input.end(), [](unsigned char c) { return std::isspace(c); });
            const auto end = std::find_if_not(input.rbegin(), input.rend(), [](unsigned char c) { return std::isspace(c); }).base();
            if (begin >= end)
                return {};
            return std::string(begin, end);
        }
    }

    AnimatorGraphPanel::AnimatorGraphPanel(const std::string& name)
        : WindowPanel(name, "Animator")
    {
        SetClipToBounds(true);
    }

    void AnimatorGraphPanel::SetTarget(Entity entity)
    {
        m_TargetEntity = entity;
        m_UndoStack.clear();
        m_RedoStack.clear();
        m_HasCommittedAnimator = false;
        m_GraphViewMode = GraphViewMode::StateMachine;
        m_SelectedBlendChildIndex = -1;
        m_DraggingBlendChildIndex = -1;
        m_ClipPickerBlendChildIndex = -1;
        m_EditingStateNameIndex = -1;
        m_StateNameEditBuffer.clear();
        m_StateEditMessage.clear();
        m_EditingBlendChildIndex = -1;
        m_EditingBlendField = BlendTreeValueField::None;
        m_BlendValueEditBuffer.clear();
        m_BlendTreeMessage.clear();
        m_BlendTreeChildScrollY = 0.0f;
        if (AnimatorComponent* animator = GetAnimator())
        {
            std::filesystem::path controllerPath = animator->ControllerPath;
            if (!animator->ControllerAssetGuid.empty())
            {
                std::filesystem::path guidPath = AssetDatabase::GetPathFromGuid(animator->ControllerAssetGuid);
                if (!guidPath.empty())
                    controllerPath = guidPath;
            }
            if (!controllerPath.empty())
                AnimatorControllerAsset::LoadFromFile(controllerPath, *animator);

            EnsureAnimatorLayers(*animator);
            auto* layer = GetActiveLayer(*animator);
            for (int i = 0; layer && i < (int)layer->States.size(); ++i)
            {
                auto& pos = layer->States[i].GraphPosition;
                if (i > 0 && std::abs(pos.x - 260.0f) < 0.01f && std::abs(pos.y - 180.0f) < 0.01f)
                    pos = { 320.0f + (float)(i % 4) * 210.0f, 180.0f + (float)(i / 4) * 110.0f };
            }
            ClampAnimatorSelection(*animator);
            layer = GetActiveLayer(*animator);
            m_SelectedStateIndex = layer ? layer->ActiveStateIndex : -1;
            m_SelectedTransitionIndex = layer ? layer->SelectedTransitionIndex : -1;
            if (m_SelectedTransitionIndex >= 0)
                m_SelectedStateIndex = -1;
            m_SelectedStateIndices.clear();
            if (m_SelectedStateIndex >= 0)
                m_SelectedStateIndices.push_back(m_SelectedStateIndex);
            CaptureCommittedAnimator(*animator);
        }
        SetVisible(true);
        BringToFront();
    }

    bool AnimatorGraphPanel::TryAcceptAssetDrop(const std::string& filepath, const std::string& assetType, float mouseX, float mouseY)
    {
        if (!IsVisible() || m_GraphViewMode != GraphViewMode::BlendTree || !IsPointInside(mouseX, mouseY))
            return false;
        if (assetType != "model" && assetType != "mesh")
            return false;

        AnimatorComponent* animator = GetAnimator();
        auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer || m_SelectedStateIndex < 0 || m_SelectedStateIndex >= (int)layer->States.size())
            return false;

        int clipIndex = -1;
        std::string clipName;
        if (!TryPickClipFromDroppedAsset(filepath, clipIndex, clipName))
        {
            m_BlendTreeMessage = "Drop a clip from this Animator source.";
            return true;
        }

        auto& state = layer->States[m_SelectedStateIndex];
        if (!EnsureSelectedBlendTree(*animator))
            return true;

        const float graphX = m_CalculatedPos.x + m_SidebarWidth;
        const float graphY = m_CalculatedPos.y + m_TitleContentTop + m_ToolbarHeight;
        const float canvasX = graphX + 22.0f;
        const float canvasY = graphY + 98.0f;
        const float canvasW = (std::max)(160.0f, (m_CalculatedSize.x - m_SidebarWidth) - 44.0f);
        const float canvasH = (std::max)(120.0f, (m_CalculatedSize.y - m_TitleContentTop - m_ToolbarHeight) - 122.0f);
        const float detailX = canvasX + 14.0f;
        const float detailY = canvasY + canvasH - 72.0f;

        if (m_SelectedBlendChildIndex >= 0 && m_SelectedBlendChildIndex < (int)state.Tree.Children.size() &&
            IsPointInRect(mouseX, mouseY, detailX + 128.0f, detailY + 8.0f, 210.0f, 22.0f))
        {
            ReplaceBlendTreeChild(m_SelectedStateIndex, m_SelectedBlendChildIndex, clipIndex, clipName);
            m_BlendTreeMessage.clear();
            return true;
        }

        AddBlendTreeChild(clipIndex, clipName);
        m_BlendTreeMessage.clear();
        return true;
    }

    AnimatorComponent* AnimatorGraphPanel::GetAnimator() const
    {
        Entity entity = m_TargetEntity;
        if (!entity || !entity.HasComponent<AnimatorComponent>())
            return nullptr;
        return &entity.GetComponent<AnimatorComponent>();
    }

    void AnimatorGraphPanel::OnRender()
    {
        WindowPanel::OnRender();
        if (!IsVisible())
            return;

        UIRenderer::PushClipRect(m_CalculatedPos.x, m_CalculatedPos.y, m_CalculatedSize.x, m_CalculatedSize.y);

        m_TitleContentTop = GetContentPosition().y - m_CalculatedPos.y;
        const float toolbarX = m_CalculatedPos.x;
        const float toolbarY = m_CalculatedPos.y + m_TitleContentTop;
        const float bodyY = toolbarY + m_ToolbarHeight;
        const float bodyH = (std::max)(0.0f, m_CalculatedSize.y - m_TitleContentTop - m_ToolbarHeight);

        UIRenderer::DrawRectFilled(toolbarX, toolbarY, m_CalculatedSize.x, m_ToolbarHeight, { 0.080f, 0.083f, 0.092f, 1.0f });
        UIRenderer::DrawRectFilled(toolbarX, bodyY, m_CalculatedSize.x, bodyH, CanvasColor);
        DrawToolbar(toolbarX + 8.0f, toolbarY + 4.0f, m_CalculatedSize.x - 16.0f);

        DrawSidebar(m_CalculatedPos.x, bodyY, m_SidebarWidth, bodyH);
        DrawGraph(m_CalculatedPos.x + m_SidebarWidth, bodyY, m_CalculatedSize.x - m_SidebarWidth, bodyH);
        DrawContextMenu();
        DrawClipPicker();

        UIRenderer::PopClipRect();
    }

    bool AnimatorGraphPanel::OnEvent(Event& e)
    {
        if (!IsVisible())
            return false;

        if (e.GetEventType() == EventType::MouseScrolled)
        {
            MouseScrolledEvent& scroll = static_cast<MouseScrolledEvent&>(e);
            Window* renderWindow = Widget::GetCurrentRenderWindow();
            auto [mouseX, mouseY] = renderWindow
                ? renderWindow->GetMousePosition()
                : Application::Get()->GetWindow().GetMousePosition();

            const float graphX = m_CalculatedPos.x + m_SidebarWidth;
            const float graphY = m_CalculatedPos.y + m_TitleContentTop + m_ToolbarHeight;
            if (IsPointInRect(mouseX, mouseY, graphX, graphY, m_CalculatedSize.x - m_SidebarWidth, m_CalculatedSize.y - m_TitleContentTop - m_ToolbarHeight))
            {
                if (m_GraphViewMode == GraphViewMode::BlendTree)
                {
                    AnimatorComponent* animator = GetAnimator();
                    auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
                    if (layer && m_SelectedStateIndex >= 0 && m_SelectedStateIndex < (int)layer->States.size())
                    {
                        const auto& state = layer->States[m_SelectedStateIndex];
                        if (state.Motion == AnimatorComponent::State::MotionType::BlendTree &&
                            state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::Direct)
                        {
                            const float canvasH = (std::max)(120.0f, (m_CalculatedSize.y - m_TitleContentTop - m_ToolbarHeight) - 122.0f);
                            m_BlendTreeChildScrollY = std::clamp(
                                m_BlendTreeChildScrollY - scroll.GetYOffset() * 36.0f,
                                0.0f,
                                GetBlendTreeMaxScroll(state, canvasH));
                            e.Handled = true;
                            return true;
                        }
                    }
                }

                const DirectX::XMFLOAT2 before = ScreenToGraph(mouseX, mouseY);
                m_Zoom = (std::clamp)(m_Zoom + scroll.GetYOffset() * 0.10f, 0.55f, 1.60f);
                const DirectX::XMFLOAT2 after = ScreenToGraph(mouseX, mouseY);
                m_ViewOffsetX += after.x - before.x;
                m_ViewOffsetY += after.y - before.y;
                e.Handled = true;
                return true;
            }
        }

        return WindowPanel::OnEvent(e);
    }

    bool AnimatorGraphPanel::WantsMouseCapture() const
    {
        return WindowPanel::WantsMouseCapture() || m_IsDraggingState || m_IsPanningGraph || m_IsBoxSelecting || m_IsScrubbingTimeline || m_IsDraggingBlendChild || m_IsCreatingTransition || m_IsContextMenuOpen || m_IsClipPickerOpen;
    }

    bool AnimatorGraphPanel::OnMouseButtonPressed(MouseButtonPressedEvent& e)
    {
        const bool wasVisible = IsVisible();
        Window* ownerBefore = GetOwnerWindow();
        if (WindowPanel::OnMouseButtonPressed(e))
        {
            const bool closedInsideMainWindow = wasVisible && !IsVisible();
            const bool closedTornOffWindow = ownerBefore && ownerBefore->ShouldClose();
            if ((closedInsideMainWindow || closedTornOffWindow) && m_OnClosed)
                m_OnClosed();
            return true;
        }

        if (!IsPointInside(e.GetX(), e.GetY()))
            return false;

        BringToFront();
        Widget::SetKeyboardFocus(this);
        m_LastMouseX = e.GetX();
        m_LastMouseY = e.GetY();

        if (e.GetButton() == 1)
        {
            if (m_IsCreatingTransition)
            {
                CancelTransitionCreation();
                e.Handled = true;
                return true;
            }

            if (m_IsClipPickerOpen)
            {
                e.Handled = true;
                return true;
            }

            const float graphX = m_CalculatedPos.x + m_SidebarWidth;
            const float graphY = m_CalculatedPos.y + m_TitleContentTop + m_ToolbarHeight;
            const float graphW = m_CalculatedSize.x - m_SidebarWidth;
            const float graphH = m_CalculatedSize.y - m_TitleContentTop - m_ToolbarHeight;
            if (IsPointInRect(e.GetX(), e.GetY(), graphX, graphY, graphW, graphH))
            {
                if (m_GraphViewMode == GraphViewMode::BlendTree)
                {
                    OpenClipPicker(ContextMenuMode::BlendTreeAddChild, m_SelectedStateIndex, { 0.0f, 0.0f });
                    e.Handled = true;
                    return true;
                }

                if (AnimatorComponent* animator = GetAnimator())
                {
                    auto* layer = GetActiveLayer(*animator);
                    if (!layer)
                        return true;
                    const int transitionIndex = GetTransitionAt(e.GetX(), e.GetY());
                    const int stateIndex = GetStateAt(e.GetX(), e.GetY());
                    if (transitionIndex >= 0)
                    {
                        layer->SelectedTransitionIndex = transitionIndex;
                        m_SelectedTransitionIndex = transitionIndex;
                        m_SelectedStateIndex = -1;
                        OpenContextMenu(ContextMenuMode::Transition, e.GetX(), e.GetY(), -1, transitionIndex);
                    }
                    else if (stateIndex >= 0)
                    {
                        m_SelectedTransitionIndex = -1;
                        layer->SelectedTransitionIndex = -1;
                        OpenContextMenu(ContextMenuMode::ReplaceState, e.GetX(), e.GetY(), stateIndex, -1);
                    }
                    else
                    {
                        OpenContextMenu(ContextMenuMode::AddState, e.GetX(), e.GetY(), -1, -1);
                    }
                }
                e.Handled = true;
                return true;
            }
        }

        if (e.GetButton() == 2)
        {
            const float graphX = m_CalculatedPos.x + m_SidebarWidth;
            const float graphY = m_CalculatedPos.y + m_TitleContentTop + m_ToolbarHeight;
            const float graphW = m_CalculatedSize.x - m_SidebarWidth;
            const float graphH = m_CalculatedSize.y - m_TitleContentTop - m_ToolbarHeight;
            if (!IsPointInRect(e.GetX(), e.GetY(), graphX, graphY, graphW, graphH))
                return true;

            CloseContextMenu();
            m_IsPanningGraph = true;
            m_PanStartMouseX = e.GetX();
            m_PanStartMouseY = e.GetY();
            m_PanStartOffsetX = m_ViewOffsetX;
            m_PanStartOffsetY = m_ViewOffsetY;
            Widget::BeginMouseInteraction(this);
            e.Handled = true;
            return true;
        }

        if (e.GetButton() != 0)
            return false;

        if (m_IsCreatingTransition)
        {
            if (AnimatorComponent* animator = GetAnimator())
            {
                auto* layer = GetActiveLayer(*animator);
                const int targetStateIndex = layer ? GetStateAt(e.GetX(), e.GetY()) : -1;
                if (targetStateIndex >= 0 && targetStateIndex != m_TransitionSourceStateIndex)
                    AddTransition(m_TransitionSourceStateIndex, targetStateIndex);
            }
            CancelTransitionCreation();
            e.Handled = true;
            return true;
        }

        if (m_IsClipPickerOpen)
        {
            HandleClipPickerClick(e.GetX(), e.GetY());
            e.Handled = true;
            return true;
        }

        if (m_IsContextMenuOpen)
        {
            if (!HandleContextMenuClick(e.GetX(), e.GetY()))
                CloseContextMenu();
            e.Handled = true;
            return true;
        }

        if (HandleToolbarClick(e.GetX(), e.GetY()) || HandleSidebarClick(e.GetX(), e.GetY()))
        {
            CloseContextMenu();
            e.Handled = true;
            return true;
        }

        if (m_GraphViewMode == GraphViewMode::BlendTree && HandleBlendTreeClick(e.GetX(), e.GetY()))
        {
            CloseContextMenu();
            e.Handled = true;
            return true;
        }

        if (HandleTimelineClick(e.GetX(), e.GetY()))
        {
            CloseContextMenu();
            e.Handled = true;
            return true;
        }

        if (AnimatorComponent* animator = GetAnimator())
        {
            auto* layer = GetActiveLayer(*animator);
            if (!layer)
                return true;
            const float graphX = m_CalculatedPos.x + m_SidebarWidth;
            const float graphY = m_CalculatedPos.y + m_TitleContentTop + m_ToolbarHeight;
            const float graphW = m_CalculatedSize.x - m_SidebarWidth;
            const float graphH = m_CalculatedSize.y - m_TitleContentTop - m_ToolbarHeight;
            if (!IsPointInRect(e.GetX(), e.GetY(), graphX, graphY, graphW, graphH))
                return true;
            if (m_GraphViewMode == GraphViewMode::BlendTree)
                return true;

            const int transitionIndex = GetTransitionAt(e.GetX(), e.GetY());
            if (transitionIndex >= 0)
            {
                m_SelectedStateIndices.clear();
                m_SelectedTransitionIndex = transitionIndex;
                layer->SelectedTransitionIndex = transitionIndex;
                m_SelectedStateIndex = -1;
                m_SelectedStateIndices.clear();
                e.Handled = true;
                return true;
            }

            int stateIndex = GetStateAt(e.GetX(), e.GetY());
            if (stateIndex >= 0 && stateIndex < (int)layer->States.size())
            {
                if (!IsStateSelected(stateIndex))
                    SelectOnlyState(*animator, stateIndex);
                else
                {
                    // 이미 선택된 묶음 안의 노드를 다시 누르면 선택 묶음은 유지하고
                    // Inspector에 표시할 대표 상태만 바꾼다.
                    layer->ActiveStateIndex = stateIndex;
                    m_SelectedStateIndex = stateIndex;
                }
                layer->SelectedTransitionIndex = -1;
                m_SelectedTransitionIndex = -1;
                SyncBaseLayerToLegacyGraph(*animator);
                ResetRuntime(*animator);

                DirectX::XMFLOAT2 graphMouse = ScreenToGraph(e.GetX(), e.GetY());
                m_DraggingStateIndex = stateIndex;
                m_IsDraggingState = true;
                m_DragOffsetX = graphMouse.x - layer->States[stateIndex].GraphPosition.x;
                m_DragOffsetY = graphMouse.y - layer->States[stateIndex].GraphPosition.y;
                Widget::BeginMouseInteraction(this);
            }
            else
            {
                // 빈 그래프 영역에서 왼쪽 드래그를 시작하면 박스 선택 모드로 들어간다.
                // 선택 박스는 화면 좌표로 그리고, 판정할 때만 노드 사각형과 겹침을 계산한다.
                ClearStateSelection(*animator);
                m_IsBoxSelecting = true;
                m_BoxSelectStart = { e.GetX(), e.GetY() };
                m_BoxSelectEnd = m_BoxSelectStart;
                Widget::BeginMouseInteraction(this);
            }
        }

        e.Handled = true;
        return true;
    }

    bool AnimatorGraphPanel::OnMouseMoved(MouseMovedEvent& e)
    {
        if (WindowPanel::OnMouseMoved(e))
            return true;

        m_LastMouseX = e.GetX();
        m_LastMouseY = e.GetY();

        if (m_IsPanningGraph)
        {
            m_ViewOffsetX = m_PanStartOffsetX + (e.GetX() - m_PanStartMouseX) / m_Zoom;
            m_ViewOffsetY = m_PanStartOffsetY + (e.GetY() - m_PanStartMouseY) / m_Zoom;
            e.Handled = true;
            return true;
        }

        if (m_IsDraggingState)
        {
            if (AnimatorComponent* animator = GetAnimator())
            {
                auto* layer = GetActiveLayer(*animator);
                if (layer && m_DraggingStateIndex >= 0 && m_DraggingStateIndex < (int)layer->States.size())
                {
                    DirectX::XMFLOAT2 graphMouse = ScreenToGraph(e.GetX(), e.GetY());
                    auto& pos = layer->States[m_DraggingStateIndex].GraphPosition;
                    const float dx = graphMouse.x - m_DragOffsetX - pos.x;
                    const float dy = graphMouse.y - m_DragOffsetY - pos.y;
                    pos.x = graphMouse.x - m_DragOffsetX;
                    pos.y = graphMouse.y - m_DragOffsetY;
                    if (IsStateSelected(m_DraggingStateIndex))
                    {
                        for (int selectedIndex : m_SelectedStateIndices)
                        {
                            if (selectedIndex == m_DraggingStateIndex || selectedIndex < 0 || selectedIndex >= (int)layer->States.size())
                                continue;
                            layer->States[selectedIndex].GraphPosition.x += dx;
                            layer->States[selectedIndex].GraphPosition.y += dy;
                        }
                    }
                    SyncBaseLayerToLegacyGraph(*animator);
                }
            }
            e.Handled = true;
            return true;
        }

        if (m_IsScrubbingTimeline)
        {
            if (AnimatorComponent* animator = GetAnimator())
            {
                auto* layer = GetActiveLayer(*animator);
                if (layer && m_SelectedStateIndex >= 0 && m_SelectedStateIndex < (int)layer->States.size())
                {
                    auto& state = layer->States[m_SelectedStateIndex];
                    const float rawDuration = (std::max)(0.05f, GetSelectedStateRawDurationSeconds(*animator, state));
                    const float duration = (std::max)(0.05f, GetSelectedStateDurationSeconds(*animator, state));
                    const float trackX = m_TimelineX + 18.0f;
                    const float trackW = (std::max)(48.0f, m_TimelineW - 36.0f);
                    const float ratio = std::clamp((e.GetX() - trackX) / trackW, 0.0f, 1.0f);
                    if (state.Motion == AnimatorComponent::State::MotionType::PropertyClip)
                    {
                        layer->StateTime = ratio * duration;
                        ApplyPropertyClipPreview(state, layer->StateTime);
                        e.Handled = true;
                        return true;
                    }
                    const float sourceTime = ratio * rawDuration;
                    const float rangeStart = state.ImportSettings.UseCustomRange
                        ? std::clamp(state.ImportSettings.StartSeconds, 0.0f, rawDuration)
                        : 0.0f;
                    ApplyTimelinePreview(*animator, state, std::clamp(sourceTime - rangeStart, 0.0f, duration));
                }
            }
            e.Handled = true;
            return true;
        }

        if (m_IsDraggingBlendChild)
        {
            if (AnimatorComponent* animator = GetAnimator())
            {
                auto* layer = GetActiveLayer(*animator);
                if (layer && m_SelectedStateIndex >= 0 && m_SelectedStateIndex < (int)layer->States.size())
                {
                    auto& state = layer->States[m_SelectedStateIndex];
                    if (m_DraggingBlendChildIndex >= 0 && m_DraggingBlendChildIndex < (int)state.Tree.Children.size())
                    {
                        const float graphX = m_CalculatedPos.x + m_SidebarWidth;
                        const float graphY = m_CalculatedPos.y + m_TitleContentTop + m_ToolbarHeight;
                        const float graphW = m_CalculatedSize.x - m_SidebarWidth;
                        const float graphH = m_CalculatedSize.y - m_TitleContentTop - m_ToolbarHeight;
                        const float canvasX = graphX + 22.0f;
                        const float canvasY = graphY + 98.0f;
                        const float canvasW = (std::max)(160.0f, graphW - 44.0f);
                        const float canvasH = (std::max)(120.0f, graphH - 122.0f);
                        const float axisX = canvasX + 64.0f;
                        const float axisY = canvasY + 76.0f;
                        const float axisW = (std::max)(80.0f, canvasW - 128.0f);
                        const float axisH = (std::max)(60.0f, canvasH - 134.0f);
                        auto& child = state.Tree.Children[m_DraggingBlendChildIndex];

                        if (state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::OneD)
                        {
                            // 마커를 그린 축 영역과 같은 좌표계로 값을 계산한다.
                            // 그리는 영역과 입력 영역이 다르면 드래그할 때 값이 마우스를 따라오지 않는다.
                            const float ratio = std::clamp((e.GetX() - axisX) / axisW, 0.0f, 1.0f);
                            child.Threshold = ratio * 2.0f - 1.0f;
                        }
                        else if (state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::TwoD ||
                            state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::TwoDFreeform)
                        {
                            child.Position.x = std::clamp(((e.GetX() - axisX) / axisW) * 2.0f - 1.0f, -1.0f, 1.0f);
                            child.Position.y = std::clamp(1.0f - ((e.GetY() - axisY) / axisH) * 2.0f, -1.0f, 1.0f);
                        }
                    }
                }
            }
            e.Handled = true;
            return true;
        }

        if (m_IsCreatingTransition)
        {
            e.Handled = true;
            return true;
        }

        if (m_IsBoxSelecting)
        {
            m_BoxSelectEnd = { e.GetX(), e.GetY() };
            if (AnimatorComponent* animator = GetAnimator())
                SelectStatesInBox(*animator);
            e.Handled = true;
            return true;
        }

        return false;
    }

    bool AnimatorGraphPanel::OnMouseButtonReleased(MouseButtonReleasedEvent& e)
    {
        if (WindowPanel::OnMouseButtonReleased(e))
            return true;

        if ((m_IsDraggingState && e.GetButton() == 0) || (m_IsPanningGraph && e.GetButton() == 2) || (m_IsBoxSelecting && e.GetButton() == 0) || (m_IsScrubbingTimeline && e.GetButton() == 0) || (m_IsDraggingBlendChild && e.GetButton() == 0))
        {
            if (m_IsDraggingState || m_IsBoxSelecting || m_IsScrubbingTimeline || m_IsDraggingBlendChild)
            {
                if (AnimatorComponent* animator = GetAnimator())
                    CommitGraphEdit(*animator);
            }
            m_IsDraggingState = false;
            m_IsPanningGraph = false;
            m_IsBoxSelecting = false;
            m_IsScrubbingTimeline = false;
            m_IsDraggingBlendChild = false;
            m_DraggingStateIndex = -1;
            m_DraggingBlendChildIndex = -1;
            Widget::EndMouseInteraction(this);
            e.Handled = true;
            return true;
        }

        return false;
    }

    bool AnimatorGraphPanel::OnKeyPressed(KeyPressedEvent& e)
    {
        if (!IsVisible() || !Widget::IsKeyboardFocusOwner(this))
            return false;

        if (m_EditingStateNameIndex >= 0)
        {
            if (e.GetKeyCode() == 8 && !m_StateNameEditBuffer.empty())
                m_StateNameEditBuffer.pop_back();
            else if (e.GetKeyCode() == 13)
                CommitStateRename();
            else if (e.GetKeyCode() == 27)
                CancelStateRename();

            e.Handled = true;
            return true;
        }

        if (m_EditingParameterIndex >= 0)
        {
            if (e.GetKeyCode() == 8 && !m_ParameterEditBuffer.empty())
                m_ParameterEditBuffer.pop_back();
            else if (e.GetKeyCode() == 13)
                CommitParameterRename();
            else if (e.GetKeyCode() == 27)
                CancelParameterRename();

            e.Handled = true;
            return true;
        }

        if (m_IsCreatingTransition && e.GetKeyCode() == 27)
        {
            CancelTransitionCreation();
            e.Handled = true;
            return true;
        }

        if (m_EditingBlendField != BlendTreeValueField::None)
        {
            if (e.GetKeyCode() == 8 && !m_BlendValueEditBuffer.empty())
                m_BlendValueEditBuffer.pop_back();
            else if (e.GetKeyCode() == 13)
                CommitBlendTreeValueEdit();
            else if (e.GetKeyCode() == 27)
                CancelBlendTreeValueEdit();

            e.Handled = true;
            return true;
        }

        if (m_EditingPropertyField != PropertyEditField::None)
        {
            if (e.GetKeyCode() == 8 && !m_PropertyEditBuffer.empty())
                m_PropertyEditBuffer.pop_back();
            else if (e.GetKeyCode() == 13)
                CommitPropertyEdit();
            else if (e.GetKeyCode() == 27)
                CancelPropertyEdit();

            e.Handled = true;
            return true;
        }

        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (ctrl && e.GetKeyCode() == 'Z')
        {
            if (AnimatorComponent* animator = GetAnimator())
                UndoGraphEdit(*animator);
            e.Handled = true;
            return true;
        }
        if (ctrl && e.GetKeyCode() == 'Y')
        {
            if (AnimatorComponent* animator = GetAnimator())
                RedoGraphEdit(*animator);
            e.Handled = true;
            return true;
        }
        if (ctrl && e.GetKeyCode() == 'C')
        {
            CopySelectedPropertyKeys();
            e.Handled = true;
            return true;
        }
        if (ctrl && e.GetKeyCode() == 'V')
        {
            PastePropertyKeysAtTimeline();
            e.Handled = true;
            return true;
        }
        if (e.GetKeyCode() == VK_LEFT || e.GetKeyCode() == VK_RIGHT)
        {
            MoveSelectedPropertyKeys(e.GetKeyCode() == VK_LEFT ? -0.033333f : 0.033333f);
            e.Handled = true;
            return true;
        }
        if (e.GetKeyCode() == 'I')
        {
            CycleSelectedPropertyInterpolation();
            e.Handled = true;
            return true;
        }

        if (e.GetKeyCode() == 0x2E)
        {
            if (AnimatorComponent* animator = GetAnimator())
            {
                auto* layer = GetActiveLayer(*animator);
                if (m_GraphViewMode == GraphViewMode::BlendTree && layer && m_SelectedStateIndex >= 0 && m_SelectedStateIndex < (int)layer->States.size())
                {
                    auto& children = layer->States[m_SelectedStateIndex].Tree.Children;
                    if (m_SelectedBlendChildIndex >= 0 && m_SelectedBlendChildIndex < (int)children.size())
                    {
                        children.erase(children.begin() + m_SelectedBlendChildIndex);
                        m_SelectedBlendChildIndex = std::clamp(m_SelectedBlendChildIndex, -1, (int)children.size() - 1);
                        CommitGraphEdit(*animator);
                    }
                }
                else if (layer && m_SelectedTransitionIndex >= 0 && m_SelectedTransitionIndex < (int)layer->Transitions.size())
                {
                    layer->Transitions.erase(layer->Transitions.begin() + m_SelectedTransitionIndex);
                    m_SelectedTransitionIndex = -1;
                    layer->SelectedTransitionIndex = -1;
                    m_SelectedStateIndices.clear();
                    CommitGraphEdit(*animator);
                }
                else if (layer && m_SelectedStateIndex >= 0 && m_SelectedStateIndex < (int)layer->States.size() &&
                    layer->States[m_SelectedStateIndex].Motion == AnimatorComponent::State::MotionType::PropertyClip &&
                    m_SelectedPropertyTrackIndex >= 0 && m_SelectedPropertyKeyIndex >= 0)
                {
                    DeleteSelectedPropertyKey();
                }
                else
                {
                    DeleteSelectedState();
                }
            }
            e.Handled = true;
            return true;
        }
        return false;
    }

    bool AnimatorGraphPanel::OnTextInput(TextInputEvent& e)
    {
        if (!IsVisible() || !Widget::IsKeyboardFocusOwner(this))
            return false;

        const char c = e.GetCharacter();
        if (m_EditingStateNameIndex >= 0)
        {
            if (c >= 32 && c < 127 && m_StateNameEditBuffer.size() < 48)
                m_StateNameEditBuffer.push_back(c);
            e.Handled = true;
            return true;
        }

        if (m_EditingParameterIndex >= 0)
        {
            if ((std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ' ') && m_ParameterEditBuffer.size() < 32)
                m_ParameterEditBuffer.push_back(c);
            e.Handled = true;
            return true;
        }

        if (m_EditingBlendField != BlendTreeValueField::None)
        {
            if (IsNumericEditCharacter(c) && m_BlendValueEditBuffer.size() < 16)
                m_BlendValueEditBuffer.push_back(c);
            e.Handled = true;
            return true;
        }

        if (m_EditingPropertyField != PropertyEditField::None)
        {
            if (m_EditingPropertyField == PropertyEditField::TargetPath)
            {
                if (c >= 32 && c < 127 && m_PropertyEditBuffer.size() < 96)
                    m_PropertyEditBuffer.push_back(c);
            }
            else if (IsNumericEditCharacter(c) && m_PropertyEditBuffer.size() < 24)
            {
                m_PropertyEditBuffer.push_back(c);
            }
            e.Handled = true;
            return true;
        }

        return false;
    }

    void AnimatorGraphPanel::DrawToolbar(float x, float y, float)
    {
        struct ButtonDef { const char* Label; float W; };
        const ButtonDef buttons[] =
        {
            { "Add State", 92.0f }, { "Entry", 62.0f }, { "Replace Clip", 106.0f },
            { "Edit Tree", 82.0f }, { "Property", 82.0f }, { "Graph", 66.0f },
            { "Preview", 72.0f }, { "Auto", 56.0f }, { "Delete", 68.0f }
        };

        float bx = x;
        for (int i = 0; i < 9; ++i)
        {
            AnimatorComponent* animator = GetAnimator();
            const auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
            const bool selectedPropertyState = layer && m_SelectedStateIndex >= 0 && m_SelectedStateIndex < (int)layer->States.size() &&
                layer->States[m_SelectedStateIndex].Motion == AnimatorComponent::State::MotionType::PropertyClip;
            const bool danger = i == 8;
            const bool activeView = (i == 3 && m_GraphViewMode == GraphViewMode::BlendTree) ||
                (i == 4 && selectedPropertyState) ||
                (i == 5 && m_GraphViewMode == GraphViewMode::StateMachine);
            const bool hover = IsPointInRect(m_LastMouseX, m_LastMouseY, bx, y, buttons[i].W, 25.0f);
            DirectX::XMFLOAT4 fill = activeView ? DirectX::XMFLOAT4{ 0.18f, 0.31f, 0.48f, 1.0f } :
                (danger ? DirectX::XMFLOAT4{ 0.24f, 0.10f, 0.12f, 1.0f } : DirectX::XMFLOAT4{ 0.125f, 0.130f, 0.145f, 1.0f });
            if (hover)
                fill = danger ? DirectX::XMFLOAT4{ 0.34f, 0.13f, 0.16f, 1.0f } : DirectX::XMFLOAT4{ 0.18f, 0.22f, 0.30f, 1.0f };
            DirectX::XMFLOAT4 stroke = danger ? DirectX::XMFLOAT4{ 0.55f, 0.18f, 0.22f, 1.0f } : (activeView ? AccentBlue : PanelStroke);
            UIRenderer::DrawRectFilled(bx, y, buttons[i].W, 25.0f, fill);
            DrawBorder(bx, y, buttons[i].W, 25.0f, stroke);
            UIRenderer::DrawString(buttons[i].Label, bx + 8.0f, y + 18.0f, TextStrong);
            bx += buttons[i].W + 7.0f;
        }

        if (AnimatorComponent* animator = GetAnimator())
        {
            const auto* layer = GetActiveLayer(*animator);
            const std::string info = (!layer || layer->States.empty()) ? "No states" : ("States " + std::to_string(layer->States.size()));
            UIRenderer::DrawString(info, bx + 10.0f, y + 18.0f, TextMuted);
        }
    }

    void AnimatorGraphPanel::DrawSidebar(float x, float y, float w, float h)
    {
        UIRenderer::PushClipRect(x, y, w, h);
        UIRenderer::DrawRectFilled(x, y, w, h, SidebarColor);
        UIRenderer::DrawRectFilled(x + w - 1.0f, y, 1.0f, h, PanelStroke);

        auto drawTab = [&](const char* label, float tx, bool active)
        {
            const bool hover = IsPointInRect(m_LastMouseX, m_LastMouseY, tx, y + 8.0f, 92.0f, 26.0f);
            DirectX::XMFLOAT4 fill = active ? DirectX::XMFLOAT4{ 0.18f, 0.25f, 0.34f, 1.0f } : DirectX::XMFLOAT4{ 0.12f, 0.125f, 0.14f, 1.0f };
            if (hover && !active)
                fill = { 0.16f, 0.17f, 0.19f, 1.0f };
            UIRenderer::DrawRectFilled(tx, y + 8.0f, 92.0f, 26.0f, fill);
            DrawBorder(tx, y + 8.0f, 92.0f, 26.0f, active ? AccentBlue : PanelStroke);
            UIRenderer::DrawString(label, tx + 10.0f, y + 26.0f, active ? TextStrong : TextMuted);
        };
        drawTab("Layers", x + 10.0f, m_SidebarPage == SidebarPage::Layers);
        drawTab("Parameters", x + 106.0f, m_SidebarPage == SidebarPage::Parameters);

        if (AnimatorComponent* animator = GetAnimator())
        {
            EnsureAnimatorLayers(*animator);
            if (m_SidebarPage == SidebarPage::Layers)
            {
                UIRenderer::DrawString("Layers", x + 18.0f, y + 62.0f, TextStrong);
                const bool addLayerHover = IsPointInRect(m_LastMouseX, m_LastMouseY, x + w - 42.0f, y + 45.0f, 24.0f, 22.0f);
                UIRenderer::DrawRectFilled(x + w - 42.0f, y + 45.0f, 24.0f, 22.0f, addLayerHover ? DirectX::XMFLOAT4{ 0.18f, 0.25f, 0.34f, 1.0f } : DirectX::XMFLOAT4{ 0.13f, 0.135f, 0.145f, 1.0f });
                DrawBorder(x + w - 42.0f, y + 45.0f, 24.0f, 22.0f, addLayerHover ? AccentBlue : PanelStroke);
                UIRenderer::DrawString("+", x + w - 35.0f, y + 62.0f, TextStrong);

                float layerY = y + 74.0f;
                for (int i = 0; i < (int)animator->Layers.size(); ++i)
                {
                    const auto& layer = animator->Layers[i];
                    const bool active = i == animator->ActiveLayerIndex;
                    const bool hover = IsPointInRect(m_LastMouseX, m_LastMouseY, x + 12.0f, layerY, w - 25.0f, 27.0f);
                    DirectX::XMFLOAT4 fill = active ? DirectX::XMFLOAT4{ 0.18f, 0.31f, 0.48f, 1.0f } : DirectX::XMFLOAT4{ 0.13f, 0.135f, 0.145f, 1.0f };
                    if (hover && !active)
                        fill = { 0.17f, 0.18f, 0.20f, 1.0f };
                    UIRenderer::DrawRectFilled(x + 12.0f, layerY, w - 25.0f, 27.0f, fill);
                    UIRenderer::DrawString(FitText(layer.Name, w - 98.0f), x + 22.0f, layerY + 19.0f, active ? TextStrong : TextMuted);
                    UIRenderer::DrawString(std::to_string((int)(layer.Weight * 100.0f)) + "%", x + w - 76.0f, layerY + 19.0f, TextMuted);
                    UIRenderer::DrawString("*", x + w - 30.0f, layerY + 19.0f, active ? AccentGreen : TextMuted);
                    layerY += 30.0f;
                }

                auto& layer = animator->Layers[animator->ActiveLayerIndex];
                const float settingsY = layerY + 8.0f;
                UIRenderer::DrawRectFilled(x + 10.0f, settingsY, w - 22.0f, 118.0f, { 0.070f, 0.074f, 0.082f, 1.0f });
                DrawBorder(x + 10.0f, settingsY, w - 22.0f, 118.0f, PanelStroke);
                UIRenderer::DrawString("Layer Settings", x + 20.0f, settingsY + 22.0f, TextStrong);
                UIRenderer::DrawString("Weight", x + 20.0f, settingsY + 48.0f, TextMuted);
                UIRenderer::DrawRectFilled(x + 86.0f, settingsY + 33.0f, 92.0f, 18.0f, { 0.10f, 0.105f, 0.115f, 1.0f });
                UIRenderer::DrawRectFilled(x + 86.0f, settingsY + 33.0f, 92.0f * std::clamp(layer.Weight, 0.0f, 1.0f), 18.0f, { 0.22f, 0.40f, 0.62f, 1.0f });
                UIRenderer::DrawString("-", x + w - 63.0f, settingsY + 49.0f, TextStrong);
                UIRenderer::DrawString("+", x + w - 37.0f, settingsY + 49.0f, TextStrong);
                UIRenderer::DrawString(std::string("Blend: ") + LayerBlendModeName(layer.Blending), x + 20.0f, settingsY + 74.0f, TextMuted);
                UIRenderer::DrawString(layer.IKPass ? "[v] IK Pass" : "[ ] IK Pass", x + 20.0f, settingsY + 99.0f, layer.IKPass ? AccentGreen : TextMuted);

                float rowY = settingsY + 138.0f;
                UIRenderer::DrawString("States", x + 18.0f, rowY - 10.0f, TextStrong);
                for (int i = 0; i < (int)layer.States.size(); ++i)
                {
                    const bool active = i == layer.ActiveStateIndex;
                    const bool hover = IsPointInRect(m_LastMouseX, m_LastMouseY, x + 12.0f, rowY, w - 25.0f, 27.0f);
                    DirectX::XMFLOAT4 fill = active ? DirectX::XMFLOAT4{ 0.18f, 0.31f, 0.48f, 1.0f } : DirectX::XMFLOAT4{ 0.13f, 0.135f, 0.145f, 1.0f };
                    if (hover && !active)
                        fill = { 0.17f, 0.18f, 0.20f, 1.0f };
                    UIRenderer::DrawRectFilled(x + 12.0f, rowY, w - 25.0f, 27.0f, fill);
                    UIRenderer::DrawString(FitText(layer.States[i].Name, w - 92.0f), x + 22.0f, rowY + 19.0f, active ? TextStrong : TextMuted);
                    if (i == layer.EntryStateIndex)
                        UIRenderer::DrawString("Entry", x + w - 58.0f, rowY + 19.0f, AccentGreen);
                    rowY += 30.0f;
                }

                if (m_SelectedStateIndex >= 0 && m_SelectedStateIndex < (int)layer.States.size())
                {
                    const auto& state = layer.States[m_SelectedStateIndex];
                    const float propY = y + h - 390.0f;
                    UIRenderer::DrawRectFilled(x + 10.0f, propY, w - 22.0f, 376.0f, { 0.070f, 0.074f, 0.082f, 1.0f });
                    DrawBorder(x + 10.0f, propY, w - 22.0f, 376.0f, PanelStroke);
                    UIRenderer::DrawString("Selected State", x + 20.0f, propY + 24.0f, TextStrong);
                    const bool editingStateName = m_EditingStateNameIndex == m_SelectedStateIndex;
                    UIRenderer::DrawString("Name", x + 20.0f, propY + 50.0f, TextMuted);
                    UIRenderer::DrawRectFilled(x + 68.0f, propY + 31.0f, w - 100.0f, 23.0f,
                        editingStateName ? DirectX::XMFLOAT4{ 0.12f, 0.14f, 0.18f, 1.0f } : DirectX::XMFLOAT4{ 0.10f, 0.105f, 0.115f, 1.0f });
                    DrawBorder(x + 68.0f, propY + 31.0f, w - 100.0f, 23.0f, editingStateName ? AccentBlue : PanelStroke);
                    UIRenderer::DrawString(FitText(editingStateName ? m_StateNameEditBuffer : state.Name, w - 116.0f), x + 76.0f, propY + 49.0f, TextStrong);
                    UIRenderer::DrawString(MotionLabel(state), x + 20.0f, propY + 76.0f, TextMuted);
                    UIRenderer::DrawRectFilled(x + 20.0f, propY + 88.0f, 15.0f, 15.0f, { 0.10f, 0.105f, 0.115f, 1.0f });
                    DrawBorder(x + 20.0f, propY + 88.0f, 15.0f, 15.0f, state.Loop ? AccentGreen : PanelStroke);
                    if (state.Loop)
                        UIRenderer::DrawString("v", x + 24.0f, propY + 102.0f, AccentGreen);
                    UIRenderer::DrawString("Loop", x + 42.0f, propY + 104.0f, state.Loop ? AccentGreen : TextMuted);
                    // Write Defaults는 별도 상태가 아니라, 선택한 상태 안의 옵션이다.
                    // Unity처럼 한 State를 고른 뒤 체크박스로 켜고 끄는 흐름을 유지한다.
                    UIRenderer::DrawRectFilled(x + 20.0f, propY + 114.0f, 15.0f, 15.0f, { 0.10f, 0.105f, 0.115f, 1.0f });
                    DrawBorder(x + 20.0f, propY + 114.0f, 15.0f, 15.0f, state.WriteDefaults ? AccentGreen : PanelStroke);
                    if (state.WriteDefaults)
                        UIRenderer::DrawString("v", x + 24.0f, propY + 128.0f, AccentGreen);
                    UIRenderer::DrawString("Write Defaults", x + 42.0f, propY + 130.0f, state.WriteDefaults ? AccentGreen : TextMuted);
                    UIRenderer::DrawString(("Speed: " + std::to_string(state.Speed)).substr(0, 13), x + 20.0f, propY + 156.0f, TextMuted);
                    UIRenderer::DrawRectFilled(x + w - 72.0f, propY + 138.0f, 24.0f, 20.0f, { 0.12f, 0.125f, 0.14f, 1.0f });
                    UIRenderer::DrawRectFilled(x + w - 44.0f, propY + 138.0f, 24.0f, 20.0f, { 0.12f, 0.125f, 0.14f, 1.0f });
                    UIRenderer::DrawString("-", x + w - 64.0f, propY + 154.0f, TextStrong);
                    UIRenderer::DrawString("+", x + w - 37.0f, propY + 154.0f, TextStrong);

                    auto drawCheck = [&](const char* label, bool checked, float localY)
                        {
                            UIRenderer::DrawRectFilled(x + 20.0f, propY + localY, 15.0f, 15.0f, { 0.10f, 0.105f, 0.115f, 1.0f });
                            DrawBorder(x + 20.0f, propY + localY, 15.0f, 15.0f, checked ? AccentGreen : PanelStroke);
                            if (checked)
                                UIRenderer::DrawString("v", x + 24.0f, propY + localY + 14.0f, AccentGreen);
                            UIRenderer::DrawString(label, x + 42.0f, propY + localY + 16.0f, checked ? TextStrong : TextMuted);
                        };

                    UIRenderer::DrawString("Root Motion Import", x + 20.0f, propY + 184.0f, TextStrong);
                    UIRenderer::DrawString(FitText("Root Bone: " + animator->HumanoidRootBone, w - 132.0f), x + 20.0f, propY + 208.0f, TextMuted);
                    const bool autoRootHover = IsPointInRect(m_LastMouseX, m_LastMouseY, x + w - 84.0f, propY + 190.0f, 62.0f, 22.0f);
                    UIRenderer::DrawRectFilled(x + w - 84.0f, propY + 190.0f, 62.0f, 22.0f,
                        autoRootHover ? DirectX::XMFLOAT4{ 0.18f, 0.25f, 0.34f, 1.0f } : DirectX::XMFLOAT4{ 0.12f, 0.125f, 0.14f, 1.0f });
                    DrawBorder(x + w - 84.0f, propY + 190.0f, 62.0f, 22.0f, autoRootHover ? AccentBlue : PanelStroke);
                    UIRenderer::DrawString("Auto", x + w - 67.0f, propY + 207.0f, TextStrong);
                    // 루트모션 관련 옵션은 State 안의 Import Settings로 보관한다.
                    // 같은 클립이라도 State마다 이동을 굽거나 잠그는 정책이 다를 수 있기 때문이다.
                    drawCheck("Apply Root Motion", state.ApplyRootMotion, 224.0f);
                    drawCheck("Bake Root Transform", state.ImportSettings.BakeRootTransform, 250.0f);
                    drawCheck("Lock Root XZ", state.ImportSettings.LockRootPositionXZ, 276.0f);
                    drawCheck("Lock Root Y", state.ImportSettings.LockRootPositionY, 302.0f);
                    drawCheck("Lock Root Rotation", state.ImportSettings.LockRootRotation, 328.0f);
                    const DirectX::XMFLOAT4 statusColor = animator->RuntimeRootMotionRootMissing ? AccentRed : TextMuted;
                    if (state.Motion == AnimatorComponent::State::MotionType::BlendTree)
                        UIRenderer::DrawString(FitText("Replace Clip: whole state / Edit Tree: child clips", w - 44.0f), x + 20.0f, propY + 364.0f, AccentOrange);
                    else if (!m_StateEditMessage.empty())
                        UIRenderer::DrawString(FitText(m_StateEditMessage, w - 44.0f), x + 20.0f, propY + 364.0f, AccentOrange);
                    else
                        UIRenderer::DrawString(animator->RuntimeRootMotionRootMissing ? "Runtime: root channel missing" : "Runtime: root ready", x + 20.0f, propY + 364.0f, statusColor);
                }
            }
            else
            {
                UIRenderer::DrawString("Parameters", x + 18.0f, y + 64.0f, TextStrong);
                UIRenderer::DrawString("+ Float   + Bool   + Trigger", x + 18.0f, y + 92.0f, AccentBlue);
                if (!m_ParameterEditMessage.empty())
                    UIRenderer::DrawString(FitText(m_ParameterEditMessage, w - 36.0f), x + 18.0f, y + 112.0f, AccentOrange);

                float rowY = y + 118.0f;
                if (animator->Parameters.empty())
                    UIRenderer::DrawString("No parameters yet.", x + 18.0f, rowY + 18.0f, TextMuted);
                for (int i = 0; i < (int)animator->Parameters.size(); ++i)
                {
                    const auto& parameter = animator->Parameters[i];
                    UIRenderer::DrawRectFilled(x + 12.0f, rowY, w - 25.0f, 27.0f, { 0.13f, 0.135f, 0.145f, 1.0f });
                    if (m_EditingParameterIndex == i)
                    {
                        UIRenderer::DrawRectFilled(x + 18.0f, rowY + 4.0f, w - 105.0f, 19.0f, { 0.09f, 0.15f, 0.22f, 1.0f });
                        UIRenderer::DrawString(m_ParameterEditBuffer + "_", x + 22.0f, rowY + 19.0f, TextStrong);
                    }
                    else
                    {
                        UIRenderer::DrawString(parameter.Name, x + 22.0f, rowY + 19.0f, TextStrong);
                    }
                    UIRenderer::DrawString(ParameterTypeName(parameter.ParamType), x + w - 76.0f, rowY + 19.0f, TextMuted);
                    rowY += 30.0f;
                }
            }

            DrawTransitionInspector(x + 10.0f, y + h - 364.0f, w - 22.0f, 350.0f);
        }
        UIRenderer::PopClipRect();
    }

    void AnimatorGraphPanel::DrawGraph(float x, float y, float w, float h)
    {
        if (m_GraphViewMode == GraphViewMode::BlendTree)
        {
            m_TimelineX = x;
            m_TimelineY = y + h;
            m_TimelineW = w;
            m_TimelineH = 0.0f;
            DrawBlendTreeEditor(x, y, w, h);
            return;
        }

        AnimatorComponent* animatorForTimeline = GetAnimator();
        auto* layerForTimeline = animatorForTimeline ? GetActiveLayer(*animatorForTimeline) : nullptr;
        const bool showTimeline = layerForTimeline && m_SelectedStateIndex >= 0 && m_SelectedStateIndex < (int)layerForTimeline->States.size();
        const bool propertyTimeline = showTimeline &&
            layerForTimeline->States[m_SelectedStateIndex].Motion == AnimatorComponent::State::MotionType::PropertyClip;
        const float timelineH = showTimeline ? (propertyTimeline ? 238.0f : 148.0f) : 0.0f;
        const float graphH = (std::max)(40.0f, h - timelineH);

        m_TimelineX = x;
        m_TimelineY = y + graphH;
        m_TimelineW = w;
        m_TimelineH = timelineH;

        UIRenderer::PushClipRect(x, y, w, graphH);
        DrawGrid(x, y, w, graphH);

        DirectX::XMFLOAT2 entry = GraphToScreen({ 120.0f, 180.0f });
        DirectX::XMFLOAT2 any = GraphToScreen({ 120.0f, 320.0f });
        DirectX::XMFLOAT2 exit = GraphToScreen({ 120.0f, 460.0f });

        auto drawSmallNode = [&](const char* name, DirectX::XMFLOAT2 p, DirectX::XMFLOAT4 accent)
        {
            UIRenderer::DrawRectFilled(p.x, p.y, 112.0f, 36.0f, { 0.13f, 0.14f, 0.15f, 1.0f });
            UIRenderer::DrawRectFilled(p.x, p.y, 4.0f, 36.0f, accent);
            DrawBorder(p.x, p.y, 112.0f, 36.0f, accent);
            UIRenderer::DrawString(name, p.x + 14.0f, p.y + 24.0f, TextStrong);
        };

        drawSmallNode("Entry", entry, AccentGreen);
        drawSmallNode("Any State", any, AccentTeal);
        drawSmallNode("Exit", exit, AccentRed);

        if (AnimatorComponent* animator = GetAnimator())
        {
            const auto* layer = GetActiveLayer(*animator);
            if (layer && !layer->States.empty())
            {
                int entryIndex = std::clamp(layer->EntryStateIndex, 0, (int)layer->States.size() - 1);
                StateNodeRect entryState = GetStateRect(entryIndex);
                DrawNodeConnection({ entry.x + 112.0f, entry.y + 18.0f }, { entryState.X, entryState.Y + entryState.H * 0.5f }, AccentGreen);
            }

            if (!layer)
            {
                UIRenderer::PopClipRect();
                return;
            }

            for (int i = 0; i < (int)layer->Transitions.size(); ++i)
                DrawTransitionArrow(i, layer->Transitions[i], i == m_SelectedTransitionIndex || i == layer->SelectedTransitionIndex);

            if (m_IsCreatingTransition &&
                m_TransitionSourceStateIndex >= 0 &&
                m_TransitionSourceStateIndex < (int)layer->States.size())
            {
                const StateNodeRect sourceRect = GetStateRect(m_TransitionSourceStateIndex);
                DirectX::XMFLOAT2 from = { sourceRect.X + sourceRect.W, sourceRect.Y + sourceRect.H * 0.5f };
                DirectX::XMFLOAT2 to = { m_LastMouseX, m_LastMouseY };
                const int hoverState = GetStateAt(m_LastMouseX, m_LastMouseY);
                if (hoverState >= 0 && hoverState < (int)layer->States.size() && hoverState != m_TransitionSourceStateIndex)
                {
                    const StateNodeRect targetRect = GetStateRect(hoverState);
                    to = { targetRect.X, targetRect.Y + targetRect.H * 0.5f };
                }

                // Transition 생성 중에는 아직 데이터에 저장하지 않고 임시 선만 그린다.
                // source를 정한 뒤 target을 클릭하는 방식이어야 사용자가 의도한 방향(A -> B)을 잃지 않는다.
                DrawLine(from, to, AccentGreen, 2.0f);
                DrawArrowHead(from, to, AccentGreen);
                UIRenderer::DrawString("Click target state, Esc/right click to cancel", from.x + 10.0f, from.y - 10.0f, AccentGreen);
            }

            for (int i = 0; i < (int)layer->States.size(); ++i)
            {
                StateNodeRect r = GetStateRect(i);
                const bool selected = IsStateSelected(i);
                const bool hover = IsPointInRect(m_LastMouseX, m_LastMouseY, r.X, r.Y, r.W, r.H);
                const bool entryState = i == layer->EntryStateIndex;
                DirectX::XMFLOAT4 accent = entryState ? AccentOrange : AccentBlue;
                UIRenderer::DrawRectFilled(r.X + 4.0f, r.Y + 5.0f, r.W, r.H, { 0.010f, 0.012f, 0.016f, 0.82f });
                DirectX::XMFLOAT4 nodeFill = selected ? DirectX::XMFLOAT4{ 0.15f, 0.18f, 0.22f, 1.0f } : DirectX::XMFLOAT4{ 0.12f, 0.125f, 0.14f, 1.0f };
                if (hover && !selected)
                    nodeFill = { 0.16f, 0.17f, 0.19f, 1.0f };
                UIRenderer::DrawRectFilled(r.X, r.Y, r.W, r.H, nodeFill);
                UIRenderer::DrawRectFilled(r.X, r.Y, r.W, 24.0f, { 0.16f, 0.165f, 0.18f, 1.0f });
                UIRenderer::DrawRectFilled(r.X, r.Y, 4.0f, r.H, accent);
                DrawBorder(r.X, r.Y, r.W, r.H, selected ? accent : PanelStroke, selected ? 2.0f : 1.0f);
                // 노드 이름은 저장 데이터 그대로 두고, 화면에 그릴 때만 폭에 맞춰 줄인다.
                // 긴 클립 이름이 노드 밖으로 삐져나가면 연결선과 다른 노드를 가려 편집성이 떨어진다.
                UIRenderer::DrawString(FitText(layer->States[i].Name, r.W - 96.0f), r.X + 14.0f, r.Y + 18.0f, TextStrong);
                UIRenderer::DrawString(entryState ? "ENTRY" : "STATE", r.X + r.W - 74.0f, r.Y + 18.0f, accent);
                    UIRenderer::DrawString(MotionLabel(layer->States[i]), r.X + 14.0f, r.Y + 47.0f, TextMuted);

                const float flagY = r.Y + 62.0f;
                auto drawFlag = [&](float fx, const char* label, bool enabled)
                {
                    UIRenderer::DrawRectFilled(fx, flagY - 11.0f, 12.0f, 12.0f, { 0.085f, 0.090f, 0.100f, 1.0f });
                    DrawBorder(fx, flagY - 11.0f, 12.0f, 12.0f, enabled ? AccentGreen : PanelStroke);
                    if (enabled)
                        UIRenderer::DrawString("v", fx + 3.0f, flagY + 1.0f, AccentGreen);
                    UIRenderer::DrawString(label, fx + 17.0f, flagY + 1.0f, enabled ? AccentGreen : TextMuted);
                };
                drawFlag(r.X + 14.0f, "Loop", layer->States[i].Loop);
                drawFlag(r.X + 86.0f, "Write Def.", layer->States[i].WriteDefaults);
            }

            if (m_IsBoxSelecting)
            {
                const float sx = (std::min)(m_BoxSelectStart.x, m_BoxSelectEnd.x);
                const float sy = (std::min)(m_BoxSelectStart.y, m_BoxSelectEnd.y);
                const float sw = std::abs(m_BoxSelectEnd.x - m_BoxSelectStart.x);
                const float sh = std::abs(m_BoxSelectEnd.y - m_BoxSelectStart.y);
                // 드래그 선택 영역은 실제 선택 결과를 예측하게 해 주는 편집 피드백이다.
                // 그래프 클립 안에서만 그려 다른 패널 위로 삐져나가지 않게 한다.
                UIRenderer::DrawRectFilled(sx, sy, sw, sh, { 0.28f, 0.52f, 0.90f, 0.18f });
                DrawBorder(sx, sy, sw, sh, { 0.46f, 0.68f, 1.0f, 0.95f }, 1.0f);
            }
        }

        UIRenderer::PopClipRect();

        if (showTimeline)
            DrawTimeline(m_TimelineX, m_TimelineY, m_TimelineW, m_TimelineH);
    }

    void AnimatorGraphPanel::DrawBlendTreeEditor(float x, float y, float w, float h)
    {
        UIRenderer::PushClipRect(x, y, w, h);
        DrawGrid(x, y, w, h);

        AnimatorComponent* animator = GetAnimator();
        auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer || m_SelectedStateIndex < 0 || m_SelectedStateIndex >= (int)layer->States.size())
        {
            UIRenderer::DrawString("Select a State, then press Tree.", x + 24.0f, y + 42.0f, TextMuted);
            UIRenderer::PopClipRect();
            return;
        }

        auto& state = layer->States[m_SelectedStateIndex];
        const bool isTree = state.Motion == AnimatorComponent::State::MotionType::BlendTree;
        UIRenderer::DrawRectFilled(x, y, w, 72.0f, { 0.070f, 0.074f, 0.083f, 0.96f });
        DrawBorder(x, y, w, 72.0f, PanelStroke);
        UIRenderer::DrawString("Blend Tree", x + 18.0f, y + 24.0f, TextStrong);
        UIRenderer::DrawString(FitText(state.Name, w - 380.0f), x + 116.0f, y + 24.0f, TextMuted);

        auto drawButton = [&](const char* text, float bx, float by, float bw, bool active, DirectX::XMFLOAT4 accent = AccentBlue)
        {
            const bool hover = IsPointInRect(m_LastMouseX, m_LastMouseY, bx, by, bw, 24.0f);
            DirectX::XMFLOAT4 fill = active ? DirectX::XMFLOAT4{ 0.18f, 0.31f, 0.48f, 1.0f } : DirectX::XMFLOAT4{ 0.12f, 0.125f, 0.14f, 1.0f };
            if (hover && !active)
                fill = { 0.16f, 0.17f, 0.19f, 1.0f };
            UIRenderer::DrawRectFilled(bx, by, bw, 24.0f, fill);
            DrawBorder(bx, by, bw, 24.0f, active ? accent : PanelStroke);
            UIRenderer::DrawString(text, bx + 8.0f, by + 18.0f, active ? TextStrong : TextMuted);
        };

        drawButton("< Graph", x + w - 102.0f, y + 10.0f, 82.0f, false);
        drawButton("Direct", x + 18.0f, y + 42.0f, 62.0f, isTree && state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::Direct);
        drawButton("1D", x + 86.0f, y + 42.0f, 42.0f, isTree && state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::OneD);
        drawButton("2D", x + 134.0f, y + 42.0f, 42.0f, isTree && state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::TwoD);
        drawButton("2D+2", x + 182.0f, y + 42.0f, 58.0f, isTree && state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::TwoDFreeform);
        drawButton("+ Clip", x + 252.0f, y + 42.0f, 62.0f, false, AccentGreen);
        drawButton("Replace", x + 320.0f, y + 42.0f, 72.0f, m_SelectedBlendChildIndex >= 0);
        drawButton("Remove", x + 398.0f, y + 42.0f, 72.0f, m_SelectedBlendChildIndex >= 0, AccentRed);
        drawButton("Auto Layout", x + 478.0f, y + 42.0f, 98.0f, false);

        if (!isTree)
        {
            UIRenderer::DrawString("This State is a Clip. Press Tree to convert it.", x + 24.0f, y + 118.0f, TextMuted);
            UIRenderer::PopClipRect();
            return;
        }

        const float canvasX = x + 22.0f;
        const float canvasY = y + 98.0f;
        const float canvasW = (std::max)(160.0f, w - 44.0f);
        const float canvasH = (std::max)(120.0f, h - 122.0f);
        UIRenderer::DrawRectFilled(canvasX, canvasY, canvasW, canvasH, { 0.035f, 0.038f, 0.044f, 0.92f });
        DrawBorder(canvasX, canvasY, canvasW, canvasH, PanelStroke);

        const bool needsParamX = state.Tree.TreeType != AnimatorComponent::State::BlendTree::Type::Direct;
        const bool needsParamY = state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::TwoD ||
            state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::TwoDFreeform;
        const bool paramXValid = !needsParamX || IsFloatParameter(*animator, state.Tree.ParameterX);
        const bool paramYValid = !needsParamY || IsFloatParameter(*animator, state.Tree.ParameterY);
        const std::string paramX = state.Tree.ParameterX.empty() ? "(none)" : state.Tree.ParameterX;
        const std::string paramY = state.Tree.ParameterY.empty() ? "(none)" : state.Tree.ParameterY;
        UIRenderer::DrawString(std::string("Type: ") + BlendTreeTypeName(state.Tree.TreeType), canvasX + 14.0f, canvasY + 24.0f, TextStrong);
        UIRenderer::DrawString("Param X: " + FitText(paramX, 160.0f), canvasX + 136.0f, canvasY + 24.0f, paramXValid ? (needsParamX ? AccentBlue : TextMuted) : AccentOrange);
        if (state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::TwoD ||
            state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::TwoDFreeform)
            UIRenderer::DrawString("Param Y: " + FitText(paramY, 160.0f), canvasX + 326.0f, canvasY + 24.0f, paramYValid ? AccentBlue : AccentOrange);

        if (!paramXValid || !paramYValid)
        {
            const std::string warning = !paramXValid
                ? "Param X needs a Float parameter."
                : "Param Y needs a Float parameter.";
            UIRenderer::DrawString(warning, canvasX + canvasW - 250.0f, canvasY + 24.0f, AccentOrange);
        }
        else if (!m_BlendTreeMessage.empty())
        {
            UIRenderer::DrawString(FitText(m_BlendTreeMessage, 240.0f), canvasX + canvasW - 250.0f, canvasY + 24.0f, AccentOrange);
        }

        const auto clips = InspectSourceClips(*animator);
        auto clipName = [&clips](int clipIndex)
        {
            if (clipIndex >= 0 && clipIndex < (int)clips.size())
                return clips[clipIndex].Name;
            return std::string("Clip ") + std::to_string(clipIndex);
        };

        if (state.Tree.Children.empty())
        {
            UIRenderer::DrawString("No child motions. Use + Clip or right click in this view.", canvasX + 20.0f, canvasY + 64.0f, TextMuted);
            UIRenderer::PopClipRect();
            return;
        }

        m_BlendTreeChildScrollY = std::clamp(m_BlendTreeChildScrollY, 0.0f, GetBlendTreeMaxScroll(state, canvasH));

        if (state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::Direct)
        {
            const float listTop = canvasY + 52.0f;
            const float listBottom = canvasY + canvasH - 84.0f;
            float rowY = canvasY + 58.0f - m_BlendTreeChildScrollY;
            for (int i = 0; i < (int)state.Tree.Children.size(); ++i)
            {
                const auto& child = state.Tree.Children[i];
                if (rowY + 34.0f < listTop || rowY > listBottom)
                {
                    rowY += 40.0f;
                    continue;
                }

                const bool selected = i == m_SelectedBlendChildIndex;
                const bool hover = IsPointInRect(m_LastMouseX, m_LastMouseY, canvasX + 14.0f, rowY, canvasW - 28.0f, 34.0f);
                DirectX::XMFLOAT4 fill = selected ? DirectX::XMFLOAT4{ 0.16f, 0.24f, 0.34f, 1.0f } : DirectX::XMFLOAT4{ 0.10f, 0.105f, 0.118f, 1.0f };
                if (hover && !selected)
                    fill = { 0.14f, 0.15f, 0.17f, 1.0f };
                UIRenderer::DrawRectFilled(canvasX + 14.0f, rowY, canvasW - 28.0f, 34.0f, fill);
                DrawBorder(canvasX + 14.0f, rowY, canvasW - 28.0f, 34.0f, selected ? AccentBlue : PanelStroke);
                UIRenderer::DrawString(FitText(clipName(child.ClipIndex), 220.0f), canvasX + 26.0f, rowY + 23.0f, TextStrong);
                const float barX = canvasX + 260.0f;
                const float barW = (std::max)(80.0f, canvasW - 420.0f);
                UIRenderer::DrawRectFilled(barX, rowY + 9.0f, barW, 14.0f, { 0.055f, 0.060f, 0.070f, 1.0f });
                UIRenderer::DrawRectFilled(barX, rowY + 9.0f, barW * std::clamp(child.Weight, 0.0f, 1.0f), 14.0f, AccentBlue);
                UIRenderer::DrawString("Weight " + FormatBlendValue(child.Weight), barX + barW + 12.0f, rowY + 23.0f, TextMuted);
                UIRenderer::DrawString("-", canvasX + canvasW - 64.0f, rowY + 23.0f, TextStrong);
                UIRenderer::DrawString("+", canvasX + canvasW - 32.0f, rowY + 23.0f, TextStrong);
                rowY += 40.0f;
            }

            const float maxScroll = GetBlendTreeMaxScroll(state, canvasH);
            if (maxScroll > 0.0f)
            {
                const float scrollTrackH = (std::max)(20.0f, listBottom - listTop);
                const float thumbH = (std::max)(26.0f, scrollTrackH * (scrollTrackH / (scrollTrackH + maxScroll)));
                const float thumbY = listTop + (scrollTrackH - thumbH) * (m_BlendTreeChildScrollY / maxScroll);
                UIRenderer::DrawRectFilled(canvasX + canvasW - 10.0f, listTop, 4.0f, scrollTrackH, { 0.11f, 0.12f, 0.14f, 1.0f });
                UIRenderer::DrawRectFilled(canvasX + canvasW - 10.0f, thumbY, 4.0f, thumbH, { 0.42f, 0.44f, 0.48f, 1.0f });
            }
        }
        else
        {
            const float axisX = canvasX + 64.0f;
            const float axisY = canvasY + 76.0f;
            const float axisW = (std::max)(80.0f, canvasW - 128.0f);
            const float axisH = (std::max)(60.0f, canvasH - 134.0f);
            if (state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::OneD)
            {
                const float lineY = axisY + axisH * 0.5f;
                DrawLine({ axisX, lineY }, { axisX + axisW, lineY }, AccentBlue, 2.0f);
                UIRenderer::DrawString("-1", axisX - 8.0f, lineY + 28.0f, TextMuted);
                UIRenderer::DrawString("+1", axisX + axisW - 8.0f, lineY + 28.0f, TextMuted);
                for (int i = 0; i < (int)state.Tree.Children.size(); ++i)
                {
                    const auto& child = state.Tree.Children[i];
                    const float ratio = (std::clamp(child.Threshold, -1.0f, 1.0f) + 1.0f) * 0.5f;
                    const float px = axisX + axisW * ratio;
                    const bool selected = i == m_SelectedBlendChildIndex;
                    UIRenderer::DrawRectFilled(px - 7.0f, lineY - 18.0f, 14.0f, 36.0f, selected ? AccentOrange : AccentTeal);
                    UIRenderer::DrawString(FitText(clipName(child.ClipIndex), 104.0f), px - 44.0f, lineY - 26.0f, selected ? TextStrong : TextMuted);
                    UIRenderer::DrawString("T " + FormatBlendValue(child.Threshold), px - 24.0f, lineY + 43.0f, TextMuted);
                }
            }
            else
            {
                DrawLine({ axisX, axisY + axisH * 0.5f }, { axisX + axisW, axisY + axisH * 0.5f }, GridMajor, 1.0f);
                DrawLine({ axisX + axisW * 0.5f, axisY }, { axisX + axisW * 0.5f, axisY + axisH }, GridMajor, 1.0f);
                if (state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::TwoDFreeform)
                {
                    DrawLine({ axisX, axisY }, { axisX + axisW, axisY + axisH }, GridFine, 1.0f);
                    DrawLine({ axisX + axisW, axisY }, { axisX, axisY + axisH }, GridFine, 1.0f);
                    UIRenderer::DrawString("2D + 2 Axis", axisX + 8.0f, axisY + 18.0f, TextMuted);
                }
                for (int i = 0; i < (int)state.Tree.Children.size(); ++i)
                {
                    const auto& child = state.Tree.Children[i];
                    const float px = axisX + (std::clamp(child.Position.x, -1.0f, 1.0f) + 1.0f) * 0.5f * axisW;
                    const float py = axisY + (1.0f - (std::clamp(child.Position.y, -1.0f, 1.0f) + 1.0f) * 0.5f) * axisH;
                    const bool selected = i == m_SelectedBlendChildIndex;
                    UIRenderer::DrawRectFilled(px - 10.0f, py - 10.0f, 20.0f, 20.0f, selected ? AccentOrange : AccentTeal);
                    DrawBorder(px - 10.0f, py - 10.0f, 20.0f, 20.0f, selected ? TextStrong : PanelStroke);
                    UIRenderer::DrawString(FitText(clipName(child.ClipIndex), 118.0f), px + 14.0f, py + 5.0f, selected ? TextStrong : TextMuted);
                }
            }
        }

        {
            const float previewX = canvasX + canvasW - 220.0f;
            const float previewY = canvasY + 42.0f;
            const float previewW = 194.0f;
            const float previewH = 92.0f;
            if (previewX > canvasX + 420.0f)
            {
                UIRenderer::DrawRectFilled(previewX, previewY, previewW, previewH, { 0.055f, 0.060f, 0.070f, 0.92f });
                DrawBorder(previewX, previewY, previewW, previewH, PanelStroke);
                UIRenderer::DrawString("Preview Mix", previewX + 10.0f, previewY + 20.0f, TextStrong);

                std::vector<float> weights(state.Tree.Children.size(), 0.0f);
                if (state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::Direct)
                {
                    float sum = 0.0f;
                    for (const auto& child : state.Tree.Children)
                        sum += (std::max)(0.0f, child.Weight);
                    for (int i = 0; i < (int)state.Tree.Children.size(); ++i)
                        weights[i] = sum > 0.0f ? (std::max)(0.0f, state.Tree.Children[i].Weight) / sum : 0.0f;
                }
                else if (state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::OneD)
                {
                    const float value = GetFloatParameterValue(*animator, state.Tree.ParameterX);
                    int nearest = 0;
                    float nearestDistance = (std::numeric_limits<float>::max)();
                    for (int i = 0; i < (int)state.Tree.Children.size(); ++i)
                    {
                        const float distance = std::abs(value - state.Tree.Children[i].Threshold);
                        if (distance < nearestDistance)
                        {
                            nearestDistance = distance;
                            nearest = i;
                        }
                    }
                    weights[nearest] = 1.0f;
                }
                else
                {
                    const float px = GetFloatParameterValue(*animator, state.Tree.ParameterX);
                    const float py = GetFloatParameterValue(*animator, state.Tree.ParameterY);
                    float sum = 0.0f;
                    for (int i = 0; i < (int)state.Tree.Children.size(); ++i)
                    {
                        const float dx = px - state.Tree.Children[i].Position.x;
                        const float dy = py - state.Tree.Children[i].Position.y;
                        weights[i] = 1.0f / ((std::sqrt)(dx * dx + dy * dy) + 0.001f);
                        sum += weights[i];
                    }
                    for (float& weight : weights)
                        weight = sum > 0.0f ? weight / sum : 0.0f;
                }

                for (int i = 0; i < (int)weights.size() && i < 3; ++i)
                {
                    const float rowY = previewY + 34.0f + (float)i * 17.0f;
                    UIRenderer::DrawString(FitText(clipName(state.Tree.Children[i].ClipIndex), 78.0f), previewX + 10.0f, rowY + 11.0f, TextMuted);
                    UIRenderer::DrawRectFilled(previewX + 92.0f, rowY, 82.0f, 9.0f, { 0.10f, 0.105f, 0.118f, 1.0f });
                    UIRenderer::DrawRectFilled(previewX + 92.0f, rowY, 82.0f * std::clamp(weights[i], 0.0f, 1.0f), 9.0f, AccentGreen);
                }
            }
        }

        if (m_SelectedBlendChildIndex >= 0 && m_SelectedBlendChildIndex < (int)state.Tree.Children.size())
        {
            auto& child = state.Tree.Children[m_SelectedBlendChildIndex];
            const float detailX = canvasX + 14.0f;
            const float detailY = canvasY + canvasH - 72.0f;
            const float detailW = canvasW - 28.0f;
            UIRenderer::DrawRectFilled(detailX, detailY, detailW, 58.0f, { 0.070f, 0.074f, 0.084f, 0.96f });
            DrawBorder(detailX, detailY, detailW, 58.0f, PanelStroke);
            UIRenderer::DrawString("Selected Motion", detailX + 12.0f, detailY + 20.0f, TextStrong);
            UIRenderer::DrawRectFilled(detailX + 128.0f, detailY + 8.0f, 210.0f, 22.0f, { 0.095f, 0.100f, 0.112f, 1.0f });
            DrawBorder(detailX + 128.0f, detailY + 8.0f, 210.0f, 22.0f, PanelStroke);
            UIRenderer::DrawString(FitText(clipName(child.ClipIndex), 188.0f), detailX + 138.0f, detailY + 25.0f, TextStrong);
            UIRenderer::DrawString("Replace", detailX + 350.0f, detailY + 25.0f, AccentBlue);

            const float valueX = detailX + 432.0f;
            auto drawValueEditor = [&](const std::string& label, float value, float sx, BlendTreeValueField field)
            {
                const bool editing = m_EditingBlendField == field && m_EditingBlendChildIndex == m_SelectedBlendChildIndex;
                UIRenderer::DrawString(label, sx, detailY + 25.0f, TextMuted);
                UIRenderer::DrawRectFilled(sx + 58.0f, detailY + 8.0f, 58.0f, 22.0f, editing ? DirectX::XMFLOAT4{ 0.13f, 0.19f, 0.27f, 1.0f } : DirectX::XMFLOAT4{ 0.075f, 0.080f, 0.092f, 1.0f });
                DrawBorder(sx + 58.0f, detailY + 8.0f, 58.0f, 22.0f, editing ? AccentBlue : PanelStroke);
                UIRenderer::DrawString(editing ? m_BlendValueEditBuffer : FormatBlendValue(value), sx + 64.0f, detailY + 25.0f, TextStrong);
                UIRenderer::DrawRectFilled(sx + 122.0f, detailY + 8.0f, 22.0f, 22.0f, { 0.12f, 0.125f, 0.14f, 1.0f });
                UIRenderer::DrawRectFilled(sx + 148.0f, detailY + 8.0f, 22.0f, 22.0f, { 0.12f, 0.125f, 0.14f, 1.0f });
                UIRenderer::DrawString("-", sx + 129.0f, detailY + 25.0f, TextStrong);
                UIRenderer::DrawString("+", sx + 155.0f, detailY + 25.0f, TextStrong);
            };

            if (state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::Direct)
                drawValueEditor("Weight", child.Weight, valueX, BlendTreeValueField::DirectWeight);
            else if (state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::OneD)
                drawValueEditor("Threshold", child.Threshold, valueX, BlendTreeValueField::Threshold);
            else
            {
                drawValueEditor("X", child.Position.x, valueX, BlendTreeValueField::PositionX);
                drawValueEditor("Y", child.Position.y, valueX + 190.0f, BlendTreeValueField::PositionY);
            }
        }

        // Blend Tree 편집 화면은 State 하나의 내부 Motion을 다룬다.
        // State Machine 노드와 같은 캔버스를 쓰지만, 여기서 바꾸는 값은 자식 클립의 Weight/Threshold/Position이다.
        UIRenderer::PopClipRect();
    }

    void AnimatorGraphPanel::DrawTimeline(float x, float y, float w, float h)
    {
        AnimatorComponent* animator = GetAnimator();
        auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer || m_SelectedStateIndex < 0 || m_SelectedStateIndex >= (int)layer->States.size() || h <= 0.0f)
            return;

        auto& state = layer->States[m_SelectedStateIndex];
        const float rawDuration = (std::max)(0.05f, GetSelectedStateRawDurationSeconds(*animator, state));
        const float duration = (std::max)(0.05f, GetSelectedStateDurationSeconds(*animator, state));
        const float trackX = x + 18.0f;
        const float trackY = y + 48.0f;
        const float trackW = (std::max)(48.0f, w - 36.0f);
        const float clampedTime = std::clamp(layer->StateTime, 0.0f, duration);

        UIRenderer::PushClipRect(x, y, w, h);
        UIRenderer::DrawRectFilled(x, y, w, h, { 0.070f, 0.074f, 0.083f, 1.0f });
        DrawBorder(x, y, w, h, PanelStroke);
        UIRenderer::DrawString("Timeline", x + 14.0f, y + 24.0f, TextStrong);
        UIRenderer::DrawString(FitText(state.Name, w - 290.0f), x + 96.0f, y + 24.0f, TextMuted);
        UIRenderer::DrawString(("Time " + std::to_string(clampedTime)).substr(0, 11), x + w - 204.0f, y + 24.0f, TextMuted);
        UIRenderer::DrawString(("Length " + std::to_string(duration)).substr(0, 13), x + w - 108.0f, y + 24.0f, TextMuted);

        UIRenderer::DrawRectFilled(trackX, trackY, trackW, 16.0f, { 0.105f, 0.112f, 0.125f, 1.0f });
        DrawBorder(trackX, trackY, trackW, 16.0f, PanelStroke);
        if (state.ImportSettings.UseCustomRange)
        {
            const float start = std::clamp(state.ImportSettings.StartSeconds, 0.0f, rawDuration);
            const float end = std::clamp(state.ImportSettings.EndSeconds <= 0.0f ? rawDuration : state.ImportSettings.EndSeconds, start, rawDuration);
            const float rangeX = trackX + trackW * (start / rawDuration);
            const float rangeW = trackW * ((end - start) / rawDuration);
            UIRenderer::DrawRectFilled(rangeX, trackY + 3.0f, (std::max)(2.0f, rangeW), 10.0f, { 0.30f, 0.56f, 0.72f, 1.0f });
        }
        const int ticks = 8;
        for (int i = 0; i <= ticks; ++i)
        {
            const float tx = trackX + trackW * ((float)i / (float)ticks);
            UIRenderer::DrawRectFilled(tx, trackY - 5.0f, 1.0f, 26.0f, i == 0 || i == ticks ? GridMajor : GridFine);
        }

        for (int i = 0; i < (int)state.Events.size(); ++i)
        {
            const auto& event = state.Events[i];
            const float ex = trackX + trackW * (std::clamp(event.TimeSeconds, 0.0f, duration) / duration);
            const bool selected = i == state.SelectedEventIndex;
            UIRenderer::DrawRectFilled(ex - 3.0f, trackY - 10.0f, 6.0f, 36.0f, selected ? AccentOrange : AccentTeal);
            UIRenderer::DrawString(FitText(event.FunctionName, 86.0f), ex + 5.0f, trackY + 39.0f, selected ? TextStrong : TextMuted);
        }

        const float rangeStart = state.ImportSettings.UseCustomRange
            ? std::clamp(state.ImportSettings.StartSeconds, 0.0f, rawDuration)
            : 0.0f;
        const float playSourceTime = rangeStart + clampedTime;
        const float playRatio = state.Motion == AnimatorComponent::State::MotionType::PropertyClip
            ? (clampedTime / duration)
            : (playSourceTime / rawDuration);
        const float playX = trackX + trackW * std::clamp(playRatio, 0.0f, 1.0f);
        UIRenderer::DrawRectFilled(playX - 1.0f, trackY - 14.0f, 2.0f, 46.0f, AccentBlue);

        if (state.Motion == AnimatorComponent::State::MotionType::PropertyClip)
        {
            const float toolY = y + 74.0f;
            auto drawPropButton = [&](const char* label, float bx, float by, float bw)
            {
                const bool hover = IsPointInRect(m_LastMouseX, m_LastMouseY, bx, by, bw, 22.0f);
                UIRenderer::DrawRectFilled(bx, by, bw, 22.0f, hover ? DirectX::XMFLOAT4{ 0.18f, 0.25f, 0.34f, 1.0f } : DirectX::XMFLOAT4{ 0.12f, 0.125f, 0.14f, 1.0f });
                DrawBorder(bx, by, bw, 22.0f, PanelStroke);
                UIRenderer::DrawString(label, bx + 8.0f, by + 17.0f, TextStrong);
            };
            drawPropButton("+ Pos", x + 14.0f, toolY, 58.0f);
            drawPropButton("+ Rot", x + 78.0f, toolY, 58.0f);
            drawPropButton("+ Scale", x + 142.0f, toolY, 72.0f);
            drawPropButton("+ Active", x + 220.0f, toolY, 78.0f);
            drawPropButton("+ Mat", x + 304.0f, toolY, 64.0f);
            drawPropButton("+ Intensity", x + 374.0f, toolY, 92.0f);
            drawPropButton("+ Cam", x + 472.0f, toolY, 66.0f);
            drawPropButton("+ Script", x + 544.0f, toolY, 78.0f);
            drawPropButton("+ Vol", x + 628.0f, toolY, 58.0f);
            drawPropButton("+ Pitch", x + 692.0f, toolY, 70.0f);
            drawPropButton("+ Play", x + 768.0f, toolY, 64.0f);

            const float rowsY = y + 104.0f;
            const int visibleRows = (std::max)(1, (int)((h - 142.0f) / 24.0f));
            if (state.PropertyTracks.empty())
                UIRenderer::DrawString("No property tracks. Add Transform/Material/Light/Camera/Audio keys from this object.", x + 18.0f, rowsY + 18.0f, TextMuted);
            for (int i = 0; i < (int)state.PropertyTracks.size() && i < visibleRows; ++i)
            {
                const auto& track = state.PropertyTracks[i];
                const float rowY = rowsY + (float)i * 24.0f;
                const bool selected = i == m_SelectedPropertyTrackIndex;
                const bool hover = IsPointInRect(m_LastMouseX, m_LastMouseY, x + 14.0f, rowY, w - 28.0f, 21.0f);
                DirectX::XMFLOAT4 fill = selected ? DirectX::XMFLOAT4{ 0.16f, 0.25f, 0.36f, 1.0f } : DirectX::XMFLOAT4{ 0.10f, 0.105f, 0.118f, 1.0f };
                if (hover && !selected)
                    fill = { 0.14f, 0.15f, 0.17f, 1.0f };
                UIRenderer::DrawRectFilled(x + 14.0f, rowY, w - 28.0f, 21.0f, fill);
                UIRenderer::DrawString(FitText(PropertyTrackLabel(track), 230.0f), x + 24.0f, rowY + 16.0f, TextStrong);
                UIRenderer::DrawString(PropertyTrackTypeName(track.Type), x + 264.0f, rowY + 16.0f, TextMuted);
                for (int k = 0; k < (int)track.Keys.size(); ++k)
                {
                    const float kx = trackX + trackW * std::clamp(track.Keys[k].TimeSeconds / duration, 0.0f, 1.0f);
                    const bool keySelected = selected && (k == m_SelectedPropertyKeyIndex || track.Keys[k].Selected);
                    UIRenderer::DrawRectFilled(kx - 3.0f, rowY + 4.0f, 6.0f, 13.0f, keySelected ? AccentOrange : AccentBlue);
                }
            }

            const float buttonY = y + h - 28.0f;
            drawPropButton("+ Key", x + 14.0f, buttonY, 70.0f);
            drawPropButton("- Key", x + 90.0f, buttonY, 70.0f);
            drawPropButton("Copy", x + 166.0f, buttonY, 56.0f);
            drawPropButton("Paste", x + 228.0f, buttonY, 62.0f);
            drawPropButton("Target", x + 296.0f, buttonY, 70.0f);

            if (m_SelectedPropertyTrackIndex >= 0 && m_SelectedPropertyTrackIndex < (int)state.PropertyTracks.size())
            {
                const auto& track = state.PropertyTracks[m_SelectedPropertyTrackIndex];
                const std::string targetText = m_EditingPropertyField == PropertyEditField::TargetPath
                    ? m_PropertyEditBuffer
                    : (track.EntityPath.empty() ? std::string(".") : track.EntityPath);
                UIRenderer::DrawString(FitText("Target " + targetText, 170.0f), x + 374.0f, buttonY + 17.0f, TextMuted);

                if (m_SelectedPropertyKeyIndex >= 0 && m_SelectedPropertyKeyIndex < (int)track.Keys.size())
                {
                    const auto& key = track.Keys[m_SelectedPropertyKeyIndex];
                    const float detailY = y + h - 54.0f;
                    auto drawEditButton = [&](const std::string& label, float bx, float bw)
                    {
                        const bool hover = IsPointInRect(m_LastMouseX, m_LastMouseY, bx, detailY, bw, 22.0f);
                        UIRenderer::DrawRectFilled(bx, detailY, bw, 22.0f, hover ? DirectX::XMFLOAT4{ 0.18f, 0.25f, 0.34f, 1.0f } : DirectX::XMFLOAT4{ 0.11f, 0.115f, 0.128f, 1.0f });
                        DrawBorder(bx, detailY, bw, 22.0f, PanelStroke);
                        UIRenderer::DrawString(FitText(label, bw - 10.0f), bx + 6.0f, detailY + 17.0f, TextStrong);
                    };
                    const std::string timeText = m_EditingPropertyField == PropertyEditField::KeyTime ? m_PropertyEditBuffer : FormatPropertyValue(key.TimeSeconds);
                    const std::string xText = m_EditingPropertyField == PropertyEditField::ValueX ? m_PropertyEditBuffer : FormatPropertyValue(key.Value.x);
                    const std::string yText = m_EditingPropertyField == PropertyEditField::ValueY ? m_PropertyEditBuffer : FormatPropertyValue(key.Value.y);
                    const std::string zText = m_EditingPropertyField == PropertyEditField::ValueZ ? m_PropertyEditBuffer : FormatPropertyValue(key.Value.z);
                    const std::string wText = m_EditingPropertyField == PropertyEditField::ValueW ? m_PropertyEditBuffer : FormatPropertyValue(key.Value.w);
                    drawEditButton("T " + timeText, x + 14.0f, 80.0f);
                    drawEditButton("X " + xText, x + 100.0f, 74.0f);
                    if (track.Type == AnimatorComponent::State::PropertyTrack::ValueType::Float3 ||
                        track.Type == AnimatorComponent::State::PropertyTrack::ValueType::Float4)
                    {
                        drawEditButton("Y " + yText, x + 180.0f, 74.0f);
                        drawEditButton("Z " + zText, x + 260.0f, 74.0f);
                    }
                    if (track.Type == AnimatorComponent::State::PropertyTrack::ValueType::Float4)
                        drawEditButton("W " + wText, x + 340.0f, 74.0f);
                    drawEditButton(PropertyInterpolationName(key.Interp), x + 420.0f, 86.0f);
                }
            }
            UIRenderer::PopClipRect();
            return;
        }

        const float importY = y + 74.0f;
        const bool rangeHover = IsPointInRect(m_LastMouseX, m_LastMouseY, x + 14.0f, importY, 96.0f, 22.0f);
        UIRenderer::DrawRectFilled(x + 14.0f, importY, 96.0f, 22.0f, state.ImportSettings.UseCustomRange ? DirectX::XMFLOAT4{ 0.16f, 0.24f, 0.34f, 1.0f } : DirectX::XMFLOAT4{ 0.12f, 0.125f, 0.14f, 1.0f });
        if (rangeHover)
            UIRenderer::DrawRectFilled(x + 14.0f, importY, 96.0f, 22.0f, { 0.18f, 0.25f, 0.34f, 1.0f });
        DrawBorder(x + 14.0f, importY, 96.0f, 22.0f, state.ImportSettings.UseCustomRange ? AccentBlue : PanelStroke);
        UIRenderer::DrawString(state.ImportSettings.UseCustomRange ? "Range On" : "Range Off", x + 24.0f, importY + 17.0f, TextStrong);

        auto drawNudge = [&](float bx, const char* text)
        {
            const bool hover = IsPointInRect(m_LastMouseX, m_LastMouseY, bx, importY, 22.0f, 22.0f);
            UIRenderer::DrawRectFilled(bx, importY, 22.0f, 22.0f, hover ? DirectX::XMFLOAT4{ 0.18f, 0.22f, 0.30f, 1.0f } : DirectX::XMFLOAT4{ 0.12f, 0.125f, 0.14f, 1.0f });
            DrawBorder(bx, importY, 22.0f, 22.0f, PanelStroke);
            UIRenderer::DrawString(text, bx + 7.0f, importY + 17.0f, TextStrong);
        };
        drawNudge(x + 124.0f, "-");
        drawNudge(x + 150.0f, "+");
        UIRenderer::DrawString(("Start " + std::to_string(std::clamp(state.ImportSettings.StartSeconds, 0.0f, rawDuration))).substr(0, 12), x + 180.0f, importY + 17.0f, TextMuted);
        drawNudge(x + 286.0f, "-");
        drawNudge(x + 312.0f, "+");
        const float endSeconds = std::clamp(state.ImportSettings.EndSeconds <= 0.0f ? rawDuration : state.ImportSettings.EndSeconds, 0.0f, rawDuration);
        UIRenderer::DrawString(("End " + std::to_string(endSeconds)).substr(0, 10), x + 342.0f, importY + 17.0f, TextMuted);
        UIRenderer::DrawRectFilled(x + 430.0f, importY, 94.0f, 22.0f, state.ImportSettings.LoopPose ? DirectX::XMFLOAT4{ 0.16f, 0.24f, 0.34f, 1.0f } : DirectX::XMFLOAT4{ 0.12f, 0.125f, 0.14f, 1.0f });
        DrawBorder(x + 430.0f, importY, 94.0f, 22.0f, state.ImportSettings.LoopPose ? AccentBlue : PanelStroke);
        UIRenderer::DrawString(state.ImportSettings.LoopPose ? "Loop Pose" : "Pose Off", x + 440.0f, importY + 17.0f, TextStrong);

        const float buttonY = y + h - 28.0f;
        UIRenderer::DrawRectFilled(x + 14.0f, buttonY, 86.0f, 22.0f, { 0.13f, 0.15f, 0.18f, 1.0f });
        DrawBorder(x + 14.0f, buttonY, 86.0f, 22.0f, PanelStroke);
        UIRenderer::DrawString("+ Event", x + 24.0f, buttonY + 17.0f, TextStrong);
        UIRenderer::DrawRectFilled(x + 106.0f, buttonY, 92.0f, 22.0f, { 0.22f, 0.10f, 0.12f, 1.0f });
        DrawBorder(x + 106.0f, buttonY, 92.0f, 22.0f, PanelStroke);
        UIRenderer::DrawString("- Event", x + 118.0f, buttonY + 17.0f, TextStrong);

        if (state.SelectedEventIndex >= 0 && state.SelectedEventIndex < (int)state.Events.size())
        {
            const auto& event = state.Events[state.SelectedEventIndex];
            UIRenderer::DrawString("Selected: " + event.FunctionName, x + 214.0f, buttonY + 17.0f, TextMuted);
        }
        else
        {
            UIRenderer::DrawString("Click track to scrub. Click marker to select event.", x + 214.0f, buttonY + 17.0f, TextMuted);
        }
        UIRenderer::PopClipRect();
    }

    void AnimatorGraphPanel::DrawGrid(float x, float y, float w, float h) const
    {
        UIRenderer::DrawRectFilled(x, y, w, h, CanvasColor);
        const float fineStep = 32.0f * m_Zoom;
        const float majorStep = 128.0f * m_Zoom;
        const float fineStartX = x + std::fmod(m_ViewOffsetX * m_Zoom, fineStep);
        const float fineStartY = y + std::fmod(m_ViewOffsetY * m_Zoom, fineStep);
        for (float gx = fineStartX; gx < x + w; gx += fineStep)
            UIRenderer::DrawRectFilled(gx, y, 1.0f, h, GridFine);
        for (float gy = fineStartY; gy < y + h; gy += fineStep)
            UIRenderer::DrawRectFilled(x, gy, w, 1.0f, GridFine);
        for (float gx = x + std::fmod(m_ViewOffsetX * m_Zoom, majorStep); gx < x + w; gx += majorStep)
            UIRenderer::DrawRectFilled(gx, y, 1.0f, h, GridMajor);
        for (float gy = y + std::fmod(m_ViewOffsetY * m_Zoom, majorStep); gy < y + h; gy += majorStep)
            UIRenderer::DrawRectFilled(x, gy, w, 1.0f, GridMajor);
    }

    void AnimatorGraphPanel::DrawNodeConnection(DirectX::XMFLOAT2 from, DirectX::XMFLOAT2 to, const DirectX::XMFLOAT4& color) const
    {
        // Animator Graph의 연결선은 경로 자체가 의미다.
        // 꺾은 선은 노드 배치가 복잡할 때 흐름을 헷갈리게 하므로 Unity처럼 직선 화살표로 통일한다.
        DrawLine(from, to, color, 2.0f);
        DrawArrowHead(from, to, color);
    }

    void AnimatorGraphPanel::DrawTransitionArrow(int transitionIndex, const AnimatorComponent::Transition& transition, bool selected) const
    {
        const AnimatorComponent* animator = GetAnimator();
        const auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer ||
            !IsValidAnimatorStateEndpoint(*layer, transition.FromStateIndex) ||
            !IsValidAnimatorStateEndpoint(*layer, transition.ToStateIndex))
            return;

        auto endpoint = [this](int stateIndex, bool source)
        {
            if (stateIndex >= 0)
            {
                StateNodeRect r = GetStateRect(stateIndex);
                return source
                    ? DirectX::XMFLOAT2{ r.X + r.W, r.Y + r.H * 0.5f }
                    : DirectX::XMFLOAT2{ r.X, r.Y + r.H * 0.5f };
            }

            DirectX::XMFLOAT2 p = GraphToScreen(GetSpecialAnimatorNodeGraphPosition(stateIndex));
            return source
                ? DirectX::XMFLOAT2{ p.x + 112.0f, p.y + 18.0f }
                : DirectX::XMFLOAT2{ p.x, p.y + 18.0f };
        };

        DirectX::XMFLOAT2 from = endpoint(transition.FromStateIndex, true);
        DirectX::XMFLOAT2 to = endpoint(transition.ToStateIndex, false);
        if (transition.FromStateIndex >= 0 && transition.ToStateIndex >= 0 && to.x < from.x)
        {
            const StateNodeRect fromRect = GetStateRect(transition.FromStateIndex);
            const StateNodeRect toRect = GetStateRect(transition.ToStateIndex);
            from = { fromRect.X + fromRect.W * 0.5f, fromRect.Y + fromRect.H };
            to = { toRect.X + toRect.W * 0.5f, toRect.Y };
        }

        const auto issues = ValidateTransition(*animator, *layer, transition);
        const DirectX::XMFLOAT4 color = !issues.empty()
            ? AccentOrange
            : (selected ? DirectX::XMFLOAT4{ 0.88f, 0.92f, 1.0f, 1.0f } : DirectX::XMFLOAT4{ 0.58f, 0.64f, 0.72f, 1.0f });
        // Animator 전이는 방향성이 중요하다. 곡선 대신 직선+화살촉으로 그려 Unity Animator처럼 흐름을 바로 읽게 한다.
        DrawLine(from, to, color, selected ? 3.0f : 2.0f);
        DrawArrowHead(from, to, color);

        if (!transition.Conditions.empty() || !issues.empty())
        {
            const float labelW = !issues.empty() ? 92.0f : 84.0f;
            const float labelX = (from.x + to.x) * 0.5f - labelW * 0.5f;
            const float labelY = (from.y + to.y) * 0.5f - 11.0f;
            UIRenderer::DrawRectFilled(labelX, labelY, labelW, 21.0f, { 0.05f, 0.055f, 0.065f, 0.92f });
            DrawBorder(labelX, labelY, labelW, 21.0f, !issues.empty() ? AccentOrange : PanelStroke);
            UIRenderer::DrawString(!issues.empty() ? "needs fix" : "conditions", labelX + 8.0f, labelY + 16.0f, !issues.empty() ? AccentOrange : TextMuted);
        }
    }

    void AnimatorGraphPanel::DrawTransitionInspector(float x, float y, float w, float h)
    {
        AnimatorComponent* animator = GetAnimator();
        auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer || m_SelectedTransitionIndex < 0 || m_SelectedTransitionIndex >= (int)layer->Transitions.size())
            return;

        auto& transition = layer->Transitions[m_SelectedTransitionIndex];
        if (!IsValidAnimatorStateEndpoint(*layer, transition.FromStateIndex) ||
            !IsValidAnimatorStateEndpoint(*layer, transition.ToStateIndex))
            return;

        UIRenderer::DrawRectFilled(x, y, w, h, { 0.070f, 0.074f, 0.082f, 1.0f });
        DrawBorder(x, y, w, h, AccentBlue);
        UIRenderer::DrawString("Transition", x + 10.0f, y + 22.0f, TextStrong);
        UIRenderer::DrawString("From: " + FitText(GetAnimatorStateLabel(*layer, transition.FromStateIndex), w - 22.0f), x + 10.0f, y + 46.0f, TextMuted);
        UIRenderer::DrawString("To: " + FitText(GetAnimatorStateLabel(*layer, transition.ToStateIndex), w - 22.0f), x + 10.0f, y + 68.0f, TextMuted);

        const auto issues = ValidateTransition(*animator, *layer, transition);
        const std::string issueText = issues.empty() ? "Issues: none" : "Issues: " + FitText(issues.front(), w - 88.0f);
        UIRenderer::DrawString(issueText, x + 10.0f, y + 90.0f, issues.empty() ? AccentGreen : AccentOrange);

        auto drawButton = [](const std::string& text, float bx, float by, float bw, const DirectX::XMFLOAT4& color)
        {
            UIRenderer::DrawRectFilled(bx, by, bw, 22.0f, color);
            DrawBorder(bx, by, bw, 22.0f, PanelStroke);
            UIRenderer::DrawString(text, bx + 8.0f, by + 17.0f, TextStrong);
        };

        drawButton(transition.HasExitTime ? "Exit Time: On" : "Exit Time: Off", x + 10.0f, y + 104.0f, w - 20.0f, transition.HasExitTime ? DirectX::XMFLOAT4{ 0.12f, 0.20f, 0.14f, 1.0f } : DirectX::XMFLOAT4{ 0.12f, 0.125f, 0.14f, 1.0f });
        drawButton(transition.CanInterrupt ? "Interrupt: On" : "Interrupt: Off", x + 10.0f, y + 130.0f, w - 20.0f, transition.CanInterrupt ? DirectX::XMFLOAT4{ 0.12f, 0.17f, 0.22f, 1.0f } : DirectX::XMFLOAT4{ 0.12f, 0.125f, 0.14f, 1.0f });

        UIRenderer::DrawString("Priority", x + 10.0f, y + 177.0f, TextMuted);
        drawButton("-", x + w - 78.0f, y + 160.0f, 24.0f, { 0.12f, 0.125f, 0.14f, 1.0f });
        UIRenderer::DrawString(std::to_string(transition.Priority), x + w - 48.0f, y + 177.0f, TextStrong);
        drawButton("+", x + w - 24.0f, y + 160.0f, 24.0f, { 0.12f, 0.125f, 0.14f, 1.0f });

        UIRenderer::DrawString(("Blend " + std::to_string(transition.BlendTime)).substr(0, 12), x + 10.0f, y + 203.0f, TextMuted);
        drawButton("-", x + w - 78.0f, y + 186.0f, 24.0f, { 0.12f, 0.125f, 0.14f, 1.0f });
        drawButton("+", x + w - 24.0f, y + 186.0f, 24.0f, { 0.12f, 0.125f, 0.14f, 1.0f });

        drawButton("+ Condition", x + 10.0f, y + 214.0f, 104.0f, { 0.12f, 0.15f, 0.19f, 1.0f });
        drawButton("- Last", x + 118.0f, y + 214.0f, 72.0f, { 0.16f, 0.10f, 0.11f, 1.0f });
        drawButton("Sort", x + w - 56.0f, y + 214.0f, 46.0f, { 0.13f, 0.14f, 0.16f, 1.0f });

        UIRenderer::DrawString("Conditions", x + 10.0f, y + 260.0f, TextStrong);
        const int maxRows = (std::max)(0, (int)((h - 266.0f) / 22.0f));
        if (transition.Conditions.empty())
            UIRenderer::DrawString("(none)", x + 92.0f, y + 260.0f, TextMuted);
        for (int i = 0; i < (int)transition.Conditions.size() && i < maxRows; ++i)
        {
            const auto& condition = transition.Conditions[i];
            const auto* parameter = FindAnimatorParameter(*animator, condition.ParameterName);
            const float rowY = y + 268.0f + (float)i * 22.0f;
            const DirectX::XMFLOAT4 rowColor = parameter ? DirectX::XMFLOAT4{ 0.10f, 0.105f, 0.118f, 1.0f } : DirectX::XMFLOAT4{ 0.18f, 0.09f, 0.08f, 1.0f };
            UIRenderer::DrawRectFilled(x + 10.0f, rowY, w - 20.0f, 20.0f, rowColor);
            const std::string value = parameter && parameter->ParamType == AnimatorComponent::Parameter::Type::Float
                ? (" " + std::to_string(condition.FloatValue)).substr(0, 7)
                : "";
            // 조건은 위에서 아래로 모두 AND로 묶인다.
            // 한 줄에 Parameter, 비교 방식, 기준값을 같이 보여 줘야 전이가 언제 실행되는지 바로 확인된다.
            UIRenderer::DrawString(FitText(condition.ParameterName + " " + ConditionModeName(condition.Mode) + value, w - 128.0f), x + 16.0f, rowY + 16.0f, parameter ? TextStrong : AccentOrange);
            UIRenderer::DrawString("P", x + w - 94.0f, rowY + 16.0f, AccentBlue);
            UIRenderer::DrawString("M", x + w - 72.0f, rowY + 16.0f, AccentTeal);
            UIRenderer::DrawString("-", x + w - 49.0f, rowY + 16.0f, AccentGreen);
            UIRenderer::DrawString("+", x + w - 29.0f, rowY + 16.0f, AccentGreen);
            UIRenderer::DrawString("X", x + w - 12.0f, rowY + 16.0f, AccentRed);
        }
    }

    void AnimatorGraphPanel::DrawContextMenu()
    {
        if (!m_IsContextMenuOpen)
            return;

        const bool canBeginTransition = m_ContextMenuMode == ContextMenuMode::ReplaceState && m_ContextStateIndex >= 0;
        if (AnimatorComponent* animator = GetAnimator())
        {
            const auto* layer = GetActiveLayer(*animator);
            const bool validContextState = layer && m_ContextStateIndex < (int)layer->States.size();
            if (!validContextState)
                return;
        }
        const float itemH = 24.0f;
        const float menuW = 230.0f;
        float menuH = 8.0f;
        if (m_ContextMenuMode == ContextMenuMode::Transition)
            menuH += 4.0f * itemH;
        else
            menuH += 2.0f * itemH + 8.0f + (canBeginTransition ? itemH : 0.0f);

        const float menuX = (std::min)(m_ContextMenuX, m_CalculatedPos.x + m_CalculatedSize.x - menuW - 4.0f);
        const float menuY = (std::min)(m_ContextMenuY, m_CalculatedPos.y + m_CalculatedSize.y - menuH - 4.0f);
        UIRenderer::DrawRectFilled(menuX, menuY, menuW, menuH, { 0.115f, 0.118f, 0.130f, 0.98f });
        DrawBorder(menuX, menuY, menuW, menuH, PanelStroke);

        if (m_ContextMenuMode == ContextMenuMode::Transition)
        {
            const char* items[] = { "Toggle Exit Time", "Add Condition", "Sort By Priority", "Delete Transition" };
            for (int i = 0; i < 4; ++i)
                UIRenderer::DrawString(items[i], menuX + 10.0f, menuY + 22.0f + (float)i * itemH, i == 3 ? DirectX::XMFLOAT4{ 0.95f, 0.58f, 0.60f, 1.0f } : TextStrong);
            return;
        }

        UIRenderer::DrawString(m_ContextMenuMode == ContextMenuMode::ReplaceState ? "State Actions" : "Graph Actions", menuX + 10.0f, menuY + 22.0f, TextMuted);
        float clipStartY = menuY + 30.0f;
        if (canBeginTransition)
        {
            if (AnimatorComponent* animator = GetAnimator())
            {
                const auto* layer = GetActiveLayer(*animator);
                const std::string label = layer ? "Make Transition from " + FitText(layer->States[m_ContextStateIndex].Name, menuW - 28.0f) : "Make Transition";
                UIRenderer::DrawString(label, menuX + 10.0f, clipStartY + 18.0f, AccentGreen);
                clipStartY += itemH;
            }
        }
        UIRenderer::DrawString(m_ContextMenuMode == ContextMenuMode::ReplaceState ? "Select Replacement Clip..." : "Select Clip For New State...", menuX + 10.0f, clipStartY + 18.0f, TextStrong);
        UIRenderer::DrawString("Clips open in picker.", menuX + 10.0f, clipStartY + itemH + 18.0f, TextMuted);
    }

    void AnimatorGraphPanel::DrawClipPicker()
    {
        if (!m_IsClipPickerOpen)
            return;

        const float pickerW = (std::min)(520.0f, m_CalculatedSize.x - 48.0f);
        const float pickerH = (std::min)(420.0f, m_CalculatedSize.y - 72.0f);
        const float pickerX = m_CalculatedPos.x + (m_CalculatedSize.x - pickerW) * 0.5f;
        const float pickerY = m_CalculatedPos.y + (m_CalculatedSize.y - pickerH) * 0.5f;

        UIRenderer::DrawRectFilled(m_CalculatedPos.x, m_CalculatedPos.y, m_CalculatedSize.x, m_CalculatedSize.y, { 0.0f, 0.0f, 0.0f, 0.38f });
        UIRenderer::DrawRectFilled(pickerX, pickerY, pickerW, pickerH, { 0.105f, 0.110f, 0.122f, 0.98f });
        DrawBorder(pickerX, pickerY, pickerW, pickerH, AccentBlue, 1.0f);
        UIRenderer::DrawRectFilled(pickerX, pickerY, pickerW, 34.0f, { 0.14f, 0.15f, 0.17f, 1.0f });
        const bool replacing = m_ClipPickerMode == ContextMenuMode::ReplaceState || m_ClipPickerMode == ContextMenuMode::BlendTreeReplaceChild;
        UIRenderer::DrawString(replacing ? "Select Replacement Clip" : "Select Animation Clip", pickerX + 14.0f, pickerY + 23.0f, TextStrong);
        UIRenderer::DrawRectFilled(pickerX + pickerW - 74.0f, pickerY + 7.0f, 58.0f, 21.0f, { 0.20f, 0.20f, 0.22f, 1.0f });
        UIRenderer::DrawString("Cancel", pickerX + pickerW - 64.0f, pickerY + 23.0f, TextMuted);

        if (!m_ClipPickerMessage.empty())
            UIRenderer::DrawString(m_ClipPickerMessage, pickerX + 14.0f, pickerY + 58.0f, TextMuted);

        // 긴 Clip 목록만 별도 클립을 씌운다. Pop하면 바깥 Animator 창 클립이 그대로 복구된다.
        UIRenderer::PushClipRect(pickerX + 10.0f, pickerY + 68.0f, pickerW - 20.0f, pickerH - 78.0f);
        float rowY = pickerY + 72.0f;
        if (m_ClipPickerClips.empty())
        {
            UIRenderer::DrawString("No animation clips found. Assign a model/FBX source first.", pickerX + 16.0f, rowY + 20.0f, TextMuted);
        }
        for (int i = 0; i < (int)m_ClipPickerClips.size(); ++i)
        {
            const bool hover = IsPointInRect(m_LastMouseX, m_LastMouseY, pickerX + 12.0f, rowY, pickerW - 24.0f, 30.0f);
            UIRenderer::DrawRectFilled(pickerX + 12.0f, rowY, pickerW - 24.0f, 30.0f, hover ? DirectX::XMFLOAT4{ 0.18f, 0.28f, 0.42f, 1.0f } : DirectX::XMFLOAT4{ 0.13f, 0.135f, 0.15f, 1.0f });
            UIRenderer::DrawString(std::to_string(i) + ". " + m_ClipPickerClips[i].Name, pickerX + 24.0f, rowY + 21.0f, TextStrong);
            rowY += 34.0f;
        }
        UIRenderer::PopClipRect();
    }

    bool AnimatorGraphPanel::HandleToolbarClick(float mouseX, float mouseY)
    {
        const float y = m_CalculatedPos.y + m_TitleContentTop + 4.0f;
        float x = m_CalculatedPos.x + 8.0f;
        const float widths[] = { 92.0f, 62.0f, 106.0f, 82.0f, 82.0f, 66.0f, 72.0f, 56.0f, 68.0f };
        for (int i = 0; i < 9; ++i)
        {
            if (IsPointInRect(mouseX, mouseY, x, y, widths[i], 25.0f))
            {
                AnimatorComponent* animator = GetAnimator();
                if (!animator)
                    return true;
                auto* layer = GetActiveLayer(*animator);
                if (!layer)
                    return true;

                if (i == 0)
                    AddEmptyState(
                        { 360.0f + (float)(layer->States.size() % 4) * 60.0f, 180.0f + (float)(layer->States.size() % 5) * 46.0f });
                else if (i == 1 && m_SelectedStateIndex >= 0) layer->EntryStateIndex = m_SelectedStateIndex;
                else if (i == 2 && m_SelectedStateIndex >= 0 && m_SelectedStateIndex < (int)layer->States.size())
                {
                    OpenClipPicker(ContextMenuMode::ReplaceState, m_SelectedStateIndex, layer->States[m_SelectedStateIndex].GraphPosition);
                }
                else if (i == 3 && m_SelectedStateIndex >= 0 && m_SelectedStateIndex < (int)layer->States.size())
                {
                    if (EnsureSelectedBlendTree(*animator))
                    {
                        m_GraphViewMode = GraphViewMode::BlendTree;
                        CommitGraphEdit(*animator);
                    }
                }
                else if (i == 4)
                {
                    if (EnsureSelectedPropertyClip(*animator))
                    {
                        m_GraphViewMode = GraphViewMode::StateMachine;
                        CommitGraphEdit(*animator);
                    }
                }
                else if (i == 5)
                {
                    m_GraphViewMode = GraphViewMode::StateMachine;
                    m_IsDraggingBlendChild = false;
                    m_DraggingBlendChildIndex = -1;
                }
                else if (i == 6)
                {
                    animator->PreviewInEdit = true;
                    animator->IsPlaying = !animator->IsPlaying;
                    if (!animator->IsPlaying)
                        animator->AnimPlayer.StopAnimation();
                }
                else if (i == 7) animator->AutoPlay = !animator->AutoPlay;
                else if (i == 8)
                {
                    if (m_SelectedTransitionIndex >= 0 && m_SelectedTransitionIndex < (int)layer->Transitions.size())
                    {
                        layer->Transitions.erase(layer->Transitions.begin() + m_SelectedTransitionIndex);
                        m_SelectedTransitionIndex = -1;
                        layer->SelectedTransitionIndex = -1;
                    }
                    else
                    {
                        DeleteSelectedState();
                    }
                }
                ClampAnimatorSelection(*animator);
                CommitGraphEdit(*animator);
                return true;
            }
            x += widths[i] + 7.0f;
        }
        return false;
    }

    bool AnimatorGraphPanel::HandleSidebarClick(float mouseX, float mouseY)
    {
        const float x = m_CalculatedPos.x;
        const float y = m_CalculatedPos.y + m_TitleContentTop + m_ToolbarHeight;
        if (!IsPointInRect(mouseX, mouseY, x, y, m_SidebarWidth, m_CalculatedSize.y - m_TitleContentTop - m_ToolbarHeight))
            return false;

        if (IsPointInRect(mouseX, mouseY, x + 10.0f, y + 8.0f, 92.0f, 26.0f))
            m_SidebarPage = SidebarPage::Layers;
        else if (IsPointInRect(mouseX, mouseY, x + 106.0f, y + 8.0f, 92.0f, 26.0f))
            m_SidebarPage = SidebarPage::Parameters;
        if (AnimatorComponent* animator = GetAnimator())
        {
            EnsureAnimatorLayers(*animator);
            auto& activeLayer = animator->Layers[animator->ActiveLayerIndex];
            if (m_SelectedTransitionIndex >= 0 && m_SelectedTransitionIndex < (int)activeLayer.Transitions.size())
            {
                const float panelX = x + 10.0f;
                const float panelW = m_SidebarWidth - 22.0f;
                const float propY = y + (m_CalculatedSize.y - m_TitleContentTop - m_ToolbarHeight) - 364.0f;
                auto& transition = activeLayer.Transitions[m_SelectedTransitionIndex];

                if (IsPointInRect(mouseX, mouseY, panelX + 10.0f, propY + 104.0f, panelW - 20.0f, 22.0f))
                {
                    transition.HasExitTime = !transition.HasExitTime;
                    CommitGraphEdit(*animator);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, panelX + 10.0f, propY + 130.0f, panelW - 20.0f, 22.0f))
                {
                    transition.CanInterrupt = !transition.CanInterrupt;
                    CommitGraphEdit(*animator);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, panelX + panelW - 78.0f, propY + 160.0f, 24.0f, 22.0f))
                {
                    transition.Priority -= 1;
                    CommitGraphEdit(*animator);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, panelX + panelW - 24.0f, propY + 160.0f, 24.0f, 22.0f))
                {
                    transition.Priority += 1;
                    CommitGraphEdit(*animator);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, panelX + panelW - 78.0f, propY + 186.0f, 24.0f, 22.0f))
                {
                    transition.BlendTime = (std::max)(0.0f, transition.BlendTime - 0.05f);
                    CommitGraphEdit(*animator);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, panelX + panelW - 24.0f, propY + 186.0f, 24.0f, 22.0f))
                {
                    transition.BlendTime += 0.05f;
                    CommitGraphEdit(*animator);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, panelX + 10.0f, propY + 214.0f, 104.0f, 22.0f))
                {
                    transition.Conditions.push_back(MakeDefaultTransitionCondition(*animator));
                    CommitGraphEdit(*animator);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, panelX + 118.0f, propY + 214.0f, 72.0f, 22.0f))
                {
                    if (!transition.Conditions.empty())
                    {
                        transition.Conditions.pop_back();
                        CommitGraphEdit(*animator);
                    }
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, panelX + panelW - 56.0f, propY + 214.0f, 46.0f, 22.0f))
                {
                    auto selected = transition;
                    std::stable_sort(activeLayer.Transitions.begin(), activeLayer.Transitions.end(),
                        [](const AnimatorComponent::Transition& a, const AnimatorComponent::Transition& b)
                        {
                            return a.Priority < b.Priority;
                        });
                    auto it = std::find_if(activeLayer.Transitions.begin(), activeLayer.Transitions.end(), [&selected](const AnimatorComponent::Transition& item)
                    {
                        return item.FromStateIndex == selected.FromStateIndex &&
                            item.ToStateIndex == selected.ToStateIndex &&
                            item.Priority == selected.Priority &&
                            item.Conditions.size() == selected.Conditions.size();
                    });
                    m_SelectedTransitionIndex = it == activeLayer.Transitions.end() ? -1 : static_cast<int>(std::distance(activeLayer.Transitions.begin(), it));
                    activeLayer.SelectedTransitionIndex = m_SelectedTransitionIndex;
                    CommitGraphEdit(*animator);
                    return true;
                }

                const float conditionStartY = propY + 268.0f;
                const int maxVisibleRows = (std::max)(0, (int)((350.0f - 266.0f) / 22.0f));
                for (int i = 0; i < (int)transition.Conditions.size() && i < maxVisibleRows; ++i)
                {
                    const float rowY = conditionStartY + (float)i * 22.0f;
                    if (!IsPointInRect(mouseX, mouseY, panelX + 10.0f, rowY, panelW - 20.0f, 20.0f))
                        continue;

                    auto& condition = transition.Conditions[i];
                    if (IsPointInRect(mouseX, mouseY, panelX + panelW - 98.0f, rowY, 22.0f, 20.0f))
                        CycleConditionParameter(*animator, condition);
                    else if (IsPointInRect(mouseX, mouseY, panelX + panelW - 76.0f, rowY, 22.0f, 20.0f))
                    {
                        const auto* parameter = FindAnimatorParameter(*animator, condition.ParameterName);
                        condition.Mode = NextConditionModeForParameter(condition.Mode, parameter ? parameter->ParamType : AnimatorComponent::Parameter::Type::Float);
                    }
                    else if (IsPointInRect(mouseX, mouseY, panelX + panelW - 54.0f, rowY, 18.0f, 20.0f))
                    {
                        const auto* parameter = FindAnimatorParameter(*animator, condition.ParameterName);
                        if (parameter && parameter->ParamType == AnimatorComponent::Parameter::Type::Float)
                            condition.FloatValue -= 0.1f;
                        else
                            condition.BoolValue = !condition.BoolValue;
                    }
                    else if (IsPointInRect(mouseX, mouseY, panelX + panelW - 34.0f, rowY, 18.0f, 20.0f))
                    {
                        const auto* parameter = FindAnimatorParameter(*animator, condition.ParameterName);
                        if (parameter && parameter->ParamType == AnimatorComponent::Parameter::Type::Float)
                            condition.FloatValue += 0.1f;
                        else
                            condition.BoolValue = !condition.BoolValue;
                    }
                    else if (IsPointInRect(mouseX, mouseY, panelX + panelW - 16.0f, rowY, 16.0f, 20.0f))
                        transition.Conditions.erase(transition.Conditions.begin() + i);
                    else
                        CycleConditionParameter(*animator, condition);

                    CommitGraphEdit(*animator);
                    return true;
                }
            }
        }
        if (m_SidebarPage == SidebarPage::Parameters)
        {
            if (IsPointInRect(mouseX, mouseY, x + 18.0f, y + 73.0f, 58.0f, 22.0f))
            {
                AddParameter(AnimatorComponent::Parameter::Type::Float);
                return true;
            }
            else if (IsPointInRect(mouseX, mouseY, x + 82.0f, y + 73.0f, 52.0f, 22.0f))
            {
                AddParameter(AnimatorComponent::Parameter::Type::Bool);
                return true;
            }
            else if (IsPointInRect(mouseX, mouseY, x + 140.0f, y + 73.0f, 82.0f, 22.0f))
            {
                AddParameter(AnimatorComponent::Parameter::Type::Trigger);
                return true;
            }
            else if (AnimatorComponent* animator = GetAnimator())
            {
                float rowY = y + 118.0f;
                for (int i = 0; i < (int)animator->Parameters.size(); ++i)
                {
                    if (IsPointInRect(mouseX, mouseY, x + 12.0f, rowY, m_SidebarWidth - 25.0f, 27.0f))
                    {
                        BeginParameterRename(i);
                        return true;
                    }
                    rowY += 30.0f;
                }
            }
        }
        else if (AnimatorComponent* animator = GetAnimator())
        {
            EnsureAnimatorLayers(*animator);
            if (IsPointInRect(mouseX, mouseY, x + m_SidebarWidth - 42.0f, y + 45.0f, 24.0f, 22.0f))
            {
                AnimatorComponent::Layer layer;
                layer.Name = "New Layer " + std::to_string(animator->Layers.size());
                layer.Weight = 1.0f;
                layer.ActiveStateIndex = -1;
                layer.EntryStateIndex = -1;
                layer.SelectedTransitionIndex = -1;
                animator->Layers.push_back(layer);
                animator->ActiveLayerIndex = (int)animator->Layers.size() - 1;
                m_SelectedStateIndex = -1;
                m_SelectedTransitionIndex = -1;
                m_SelectedStateIndices.clear();
                CommitGraphEdit(*animator);
                return true;
            }

            float layerY = y + 74.0f;
            for (int i = 0; i < (int)animator->Layers.size(); ++i)
            {
                if (IsPointInRect(mouseX, mouseY, x + 12.0f, layerY, m_SidebarWidth - 25.0f, 27.0f))
                {
                    animator->ActiveLayerIndex = i;
                    auto* selectedLayer = GetActiveLayer(*animator);
                    m_SelectedStateIndex = selectedLayer ? selectedLayer->ActiveStateIndex : -1;
                    m_SelectedTransitionIndex = selectedLayer ? selectedLayer->SelectedTransitionIndex : -1;
                    m_SelectedStateIndices.clear();
                    if (m_SelectedStateIndex >= 0)
                        m_SelectedStateIndices.push_back(m_SelectedStateIndex);
                    return true;
                }
                layerY += 30.0f;
            }

            auto& activeLayer = animator->Layers[animator->ActiveLayerIndex];
            const float settingsY = layerY + 8.0f;
            if (IsPointInRect(mouseX, mouseY, x + m_SidebarWidth - 72.0f, settingsY + 32.0f, 24.0f, 20.0f))
            {
                activeLayer.Weight = std::clamp(activeLayer.Weight - 0.1f, 0.0f, 1.0f);
                CommitGraphEdit(*animator);
                return true;
            }
            if (IsPointInRect(mouseX, mouseY, x + m_SidebarWidth - 44.0f, settingsY + 32.0f, 24.0f, 20.0f))
            {
                activeLayer.Weight = std::clamp(activeLayer.Weight + 0.1f, 0.0f, 1.0f);
                CommitGraphEdit(*animator);
                return true;
            }
            if (IsPointInRect(mouseX, mouseY, x + 10.0f, settingsY + 54.0f, m_SidebarWidth - 22.0f, 24.0f))
            {
                activeLayer.Blending = activeLayer.Blending == AnimatorComponent::Layer::BlendMode::Override
                    ? AnimatorComponent::Layer::BlendMode::Additive
                    : AnimatorComponent::Layer::BlendMode::Override;
                CommitGraphEdit(*animator);
                return true;
            }
            if (IsPointInRect(mouseX, mouseY, x + 10.0f, settingsY + 80.0f, m_SidebarWidth - 22.0f, 24.0f))
            {
                activeLayer.IKPass = !activeLayer.IKPass;
                CommitGraphEdit(*animator);
                return true;
            }

            if (m_SelectedTransitionIndex >= 0 && m_SelectedTransitionIndex < (int)activeLayer.Transitions.size())
            {
                const float panelX = x + 10.0f;
                const float panelW = m_SidebarWidth - 22.0f;
                const float propY = y + (m_CalculatedSize.y - m_TitleContentTop - m_ToolbarHeight) - 364.0f;
                auto& transition = activeLayer.Transitions[m_SelectedTransitionIndex];
                if (IsPointInRect(mouseX, mouseY, panelX + 10.0f, propY + 104.0f, panelW - 20.0f, 22.0f))
                {
                    transition.HasExitTime = !transition.HasExitTime;
                    CommitGraphEdit(*animator);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, panelX + 10.0f, propY + 130.0f, panelW - 20.0f, 22.0f))
                {
                    transition.CanInterrupt = !transition.CanInterrupt;
                    CommitGraphEdit(*animator);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, panelX + panelW - 78.0f, propY + 160.0f, 24.0f, 22.0f))
                {
                    transition.Priority -= 1;
                    CommitGraphEdit(*animator);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, panelX + panelW - 24.0f, propY + 160.0f, 24.0f, 22.0f))
                {
                    transition.Priority += 1;
                    CommitGraphEdit(*animator);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, panelX + panelW - 78.0f, propY + 186.0f, 24.0f, 22.0f))
                {
                    transition.BlendTime = (std::max)(0.0f, transition.BlendTime - 0.05f);
                    CommitGraphEdit(*animator);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, panelX + panelW - 24.0f, propY + 186.0f, 24.0f, 22.0f))
                {
                    transition.BlendTime += 0.05f;
                    CommitGraphEdit(*animator);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, panelX + 10.0f, propY + 214.0f, 104.0f, 22.0f))
                {
                    transition.Conditions.push_back(MakeDefaultTransitionCondition(*animator));
                    CommitGraphEdit(*animator);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, panelX + 118.0f, propY + 214.0f, 72.0f, 22.0f))
                {
                    if (!transition.Conditions.empty())
                    {
                        transition.Conditions.pop_back();
                        CommitGraphEdit(*animator);
                    }
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, panelX + panelW - 56.0f, propY + 214.0f, 46.0f, 22.0f))
                {
                    auto selected = transition;
                    std::stable_sort(activeLayer.Transitions.begin(), activeLayer.Transitions.end(),
                        [](const AnimatorComponent::Transition& a, const AnimatorComponent::Transition& b)
                        {
                            return a.Priority < b.Priority;
                        });
                    auto it = std::find_if(activeLayer.Transitions.begin(), activeLayer.Transitions.end(), [&selected](const AnimatorComponent::Transition& item)
                    {
                        return item.FromStateIndex == selected.FromStateIndex &&
                            item.ToStateIndex == selected.ToStateIndex &&
                            item.Priority == selected.Priority &&
                            item.Conditions.size() == selected.Conditions.size();
                    });
                    m_SelectedTransitionIndex = it == activeLayer.Transitions.end() ? -1 : static_cast<int>(std::distance(activeLayer.Transitions.begin(), it));
                    activeLayer.SelectedTransitionIndex = m_SelectedTransitionIndex;
                    CommitGraphEdit(*animator);
                    return true;
                }

                const float conditionStartY = propY + 268.0f;
                const int maxVisibleRows = (std::max)(0, (int)((350.0f - 266.0f) / 22.0f));
                for (int i = 0; i < (int)transition.Conditions.size() && i < maxVisibleRows; ++i)
                {
                    const float rowY = conditionStartY + (float)i * 22.0f;
                    if (!IsPointInRect(mouseX, mouseY, panelX + 10.0f, rowY, panelW - 20.0f, 20.0f))
                        continue;

                    auto& condition = transition.Conditions[i];
                    if (IsPointInRect(mouseX, mouseY, panelX + panelW - 98.0f, rowY, 22.0f, 20.0f))
                    {
                        CycleConditionParameter(*animator, condition);
                    }
                    else if (IsPointInRect(mouseX, mouseY, panelX + panelW - 76.0f, rowY, 22.0f, 20.0f))
                    {
                        const auto* parameter = FindAnimatorParameter(*animator, condition.ParameterName);
                        condition.Mode = NextConditionModeForParameter(condition.Mode, parameter ? parameter->ParamType : AnimatorComponent::Parameter::Type::Float);
                    }
                    else if (IsPointInRect(mouseX, mouseY, panelX + panelW - 54.0f, rowY, 18.0f, 20.0f))
                    {
                        const auto* parameter = FindAnimatorParameter(*animator, condition.ParameterName);
                        if (parameter && parameter->ParamType == AnimatorComponent::Parameter::Type::Float)
                            condition.FloatValue -= 0.1f;
                        else
                            condition.BoolValue = !condition.BoolValue;
                    }
                    else if (IsPointInRect(mouseX, mouseY, panelX + panelW - 34.0f, rowY, 18.0f, 20.0f))
                    {
                        const auto* parameter = FindAnimatorParameter(*animator, condition.ParameterName);
                        if (parameter && parameter->ParamType == AnimatorComponent::Parameter::Type::Float)
                            condition.FloatValue += 0.1f;
                        else
                            condition.BoolValue = !condition.BoolValue;
                    }
                    else if (IsPointInRect(mouseX, mouseY, panelX + panelW - 16.0f, rowY, 16.0f, 20.0f))
                    {
                        transition.Conditions.erase(transition.Conditions.begin() + i);
                    }
                    else
                    {
                        CycleConditionParameter(*animator, condition);
                    }

                    CommitGraphEdit(*animator);
                    return true;
                }
            }

            if (m_SidebarPage == SidebarPage::Layers && m_SelectedStateIndex >= 0 && m_SelectedStateIndex < (int)activeLayer.States.size())
            {
                const float propY = y + (m_CalculatedSize.y - m_TitleContentTop - m_ToolbarHeight) - 390.0f;
                auto& state = activeLayer.States[m_SelectedStateIndex];
                if (IsPointInRect(mouseX, mouseY, x + 68.0f, propY + 31.0f, m_SidebarWidth - 100.0f, 23.0f))
                {
                    BeginStateRename(m_SelectedStateIndex);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, x + 10.0f, propY + 84.0f, m_SidebarWidth - 22.0f, 24.0f))
                {
                    state.Loop = !state.Loop;
                    animator->Loop = state.Loop;
                    CommitGraphEdit(*animator);
                    ResetRuntime(*animator);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, x + 10.0f, propY + 110.0f, m_SidebarWidth - 22.0f, 24.0f))
                {
                    state.WriteDefaults = !state.WriteDefaults;
                    animator->AnimPlayer.SetWriteDefaults(state.WriteDefaults);
                    CommitGraphEdit(*animator);
                    ResetRuntime(*animator);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, x + m_SidebarWidth - 72.0f, propY + 138.0f, 24.0f, 20.0f))
                {
                    state.Speed = (std::max)(0.0f, state.Speed - 0.1f);
                    animator->Speed = state.Speed;
                    CommitGraphEdit(*animator);
                    ResetRuntime(*animator);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, x + m_SidebarWidth - 44.0f, propY + 138.0f, 24.0f, 20.0f))
                {
                    state.Speed += 0.1f;
                    animator->Speed = state.Speed;
                    CommitGraphEdit(*animator);
                    ResetRuntime(*animator);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, x + m_SidebarWidth - 84.0f, propY + 190.0f, 62.0f, 22.0f))
                {
                    // 자동 탐색은 후보를 "추측"하지만, 저장은 실제 FBX 노드/채널 이름으로 한다.
                    // 그래야 Mixamo처럼 네임스페이스가 붙은 루트 본도 Root Motion 샘플링에서 빠지지 않는다.
                    animator->HumanoidRootBone = FindBestRootBoneCandidateForState(m_TargetEntity, *animator, state);
                    CommitGraphEdit(*animator);
                    ResetRuntime(*animator);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, x + 10.0f, propY + 220.0f, m_SidebarWidth - 22.0f, 24.0f))
                {
                    state.ApplyRootMotion = !state.ApplyRootMotion;
                    animator->ApplyRootMotion = state.ApplyRootMotion;
                    CommitGraphEdit(*animator);
                    ResetRuntime(*animator);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, x + 10.0f, propY + 246.0f, m_SidebarWidth - 22.0f, 24.0f))
                {
                    state.ImportSettings.BakeRootTransform = !state.ImportSettings.BakeRootTransform;
                    CommitGraphEdit(*animator);
                    ResetRuntime(*animator);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, x + 10.0f, propY + 272.0f, m_SidebarWidth - 22.0f, 24.0f))
                {
                    state.ImportSettings.LockRootPositionXZ = !state.ImportSettings.LockRootPositionXZ;
                    CommitGraphEdit(*animator);
                    ResetRuntime(*animator);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, x + 10.0f, propY + 298.0f, m_SidebarWidth - 22.0f, 24.0f))
                {
                    state.ImportSettings.LockRootPositionY = !state.ImportSettings.LockRootPositionY;
                    CommitGraphEdit(*animator);
                    ResetRuntime(*animator);
                    return true;
                }
                if (IsPointInRect(mouseX, mouseY, x + 10.0f, propY + 324.0f, m_SidebarWidth - 22.0f, 24.0f))
                {
                    state.ImportSettings.LockRootRotation = !state.ImportSettings.LockRootRotation;
                    CommitGraphEdit(*animator);
                    ResetRuntime(*animator);
                    return true;
                }
            }

            float rowY = settingsY + 138.0f;
            for (int i = 0; i < (int)activeLayer.States.size(); ++i)
            {
                if (IsPointInRect(mouseX, mouseY, x + 12.0f, rowY, m_SidebarWidth - 25.0f, 27.0f))
                {
                    SelectOnlyState(*animator, i);
                    SyncBaseLayerToLegacyGraph(*animator);
                    ResetRuntime(*animator);
                    break;
                }
                rowY += 30.0f;
            }
        }

        return true;
    }

    bool AnimatorGraphPanel::HandleBlendTreeClick(float mouseX, float mouseY)
    {
        const float x = m_CalculatedPos.x + m_SidebarWidth;
        const float y = m_CalculatedPos.y + m_TitleContentTop + m_ToolbarHeight;
        const float w = m_CalculatedSize.x - m_SidebarWidth;
        const float h = m_CalculatedSize.y - m_TitleContentTop - m_ToolbarHeight;
        if (!IsPointInRect(mouseX, mouseY, x, y, w, h))
            return false;

        AnimatorComponent* animator = GetAnimator();
        auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer || m_SelectedStateIndex < 0 || m_SelectedStateIndex >= (int)layer->States.size())
            return true;

        auto& state = layer->States[m_SelectedStateIndex];
        if (IsPointInRect(mouseX, mouseY, x + w - 102.0f, y + 10.0f, 82.0f, 24.0f))
        {
            m_GraphViewMode = GraphViewMode::StateMachine;
            return true;
        }

        if (!EnsureSelectedBlendTree(*animator))
            return true;

        auto setType = [&](AnimatorComponent::State::BlendTree::Type type)
        {
            if (state.Tree.TreeType != type)
            {
                state.Tree.TreeType = type;
                if (state.Tree.ParameterX.empty())
                    state.Tree.ParameterX = PickFirstFloatParameter(*animator);
                if ((type == AnimatorComponent::State::BlendTree::Type::TwoD ||
                    type == AnimatorComponent::State::BlendTree::Type::TwoDFreeform) && state.Tree.ParameterY.empty())
                    state.Tree.ParameterY = PickFirstFloatParameter(*animator);
                AutoLayoutBlendTreeChildren(state);
                CancelBlendTreeValueEdit();
                m_BlendTreeChildScrollY = 0.0f;
                CommitGraphEdit(*animator);
            }
        };

        if (IsPointInRect(mouseX, mouseY, x + 18.0f, y + 42.0f, 62.0f, 24.0f)) { setType(AnimatorComponent::State::BlendTree::Type::Direct); return true; }
        if (IsPointInRect(mouseX, mouseY, x + 86.0f, y + 42.0f, 42.0f, 24.0f)) { setType(AnimatorComponent::State::BlendTree::Type::OneD); return true; }
        if (IsPointInRect(mouseX, mouseY, x + 134.0f, y + 42.0f, 42.0f, 24.0f)) { setType(AnimatorComponent::State::BlendTree::Type::TwoD); return true; }
        if (IsPointInRect(mouseX, mouseY, x + 182.0f, y + 42.0f, 58.0f, 24.0f)) { setType(AnimatorComponent::State::BlendTree::Type::TwoDFreeform); return true; }
        if (IsPointInRect(mouseX, mouseY, x + 252.0f, y + 42.0f, 62.0f, 24.0f))
        {
            OpenClipPicker(ContextMenuMode::BlendTreeAddChild, m_SelectedStateIndex, { 0.0f, 0.0f });
            return true;
        }
        if (IsPointInRect(mouseX, mouseY, x + 320.0f, y + 42.0f, 72.0f, 24.0f) && m_SelectedBlendChildIndex >= 0)
        {
            m_ClipPickerBlendChildIndex = m_SelectedBlendChildIndex;
            OpenClipPicker(ContextMenuMode::BlendTreeReplaceChild, m_SelectedStateIndex, { 0.0f, 0.0f });
            return true;
        }
        if (IsPointInRect(mouseX, mouseY, x + 398.0f, y + 42.0f, 72.0f, 24.0f) && m_SelectedBlendChildIndex >= 0 && m_SelectedBlendChildIndex < (int)state.Tree.Children.size())
        {
            state.Tree.Children.erase(state.Tree.Children.begin() + m_SelectedBlendChildIndex);
            m_SelectedBlendChildIndex = std::clamp(m_SelectedBlendChildIndex, -1, (int)state.Tree.Children.size() - 1);
            CancelBlendTreeValueEdit();
            CommitGraphEdit(*animator);
            return true;
        }
        if (IsPointInRect(mouseX, mouseY, x + 478.0f, y + 42.0f, 98.0f, 24.0f))
        {
            AutoLayoutBlendTreeChildren(state);
            CommitGraphEdit(*animator);
            return true;
        }

        const float canvasX = x + 22.0f;
        const float canvasY = y + 98.0f;
        const float canvasW = (std::max)(160.0f, w - 44.0f);
        const float canvasH = (std::max)(120.0f, h - 122.0f);
        if (!IsPointInRect(mouseX, mouseY, canvasX, canvasY, canvasW, canvasH))
            return true;

        if (IsPointInRect(mouseX, mouseY, canvasX + 136.0f, canvasY + 3.0f, 180.0f, 24.0f))
        {
            CycleBlendTreeParameter(*animator, true);
            CommitGraphEdit(*animator);
            return true;
        }
        if ((state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::TwoD ||
            state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::TwoDFreeform) &&
            IsPointInRect(mouseX, mouseY, canvasX + 326.0f, canvasY + 3.0f, 180.0f, 24.0f))
        {
            CycleBlendTreeParameter(*animator, false);
            CommitGraphEdit(*animator);
            return true;
        }

        if (m_SelectedBlendChildIndex >= 0 && m_SelectedBlendChildIndex < (int)state.Tree.Children.size())
        {
            auto& child = state.Tree.Children[m_SelectedBlendChildIndex];
            const float detailX = canvasX + 14.0f;
            const float detailY = canvasY + canvasH - 72.0f;
            const float detailW = canvasW - 28.0f;
            if (IsPointInRect(mouseX, mouseY, detailX, detailY, detailW, 58.0f))
            {
                if (IsPointInRect(mouseX, mouseY, detailX + 128.0f, detailY + 8.0f, 282.0f, 22.0f))
                {
                    m_ClipPickerBlendChildIndex = m_SelectedBlendChildIndex;
                    OpenClipPicker(ContextMenuMode::BlendTreeReplaceChild, m_SelectedStateIndex, { 0.0f, 0.0f });
                    return true;
                }

                const float step = 0.05f;
                auto handleValueEditor = [&](float sx, float& value, float minValue, float maxValue, BlendTreeValueField field)
                {
                    if (IsPointInRect(mouseX, mouseY, sx + 58.0f, detailY + 8.0f, 58.0f, 22.0f))
                    {
                        BeginBlendTreeValueEdit(field, m_SelectedBlendChildIndex, value);
                        return true;
                    }
                    if (IsPointInRect(mouseX, mouseY, sx + 122.0f, detailY + 8.0f, 22.0f, 22.0f))
                    {
                        value = std::clamp(value - step, minValue, maxValue);
                        CommitGraphEdit(*animator);
                        return true;
                    }
                    if (IsPointInRect(mouseX, mouseY, sx + 148.0f, detailY + 8.0f, 22.0f, 22.0f))
                    {
                        value = std::clamp(value + step, minValue, maxValue);
                        CommitGraphEdit(*animator);
                        return true;
                    }
                    return false;
                };

                const float valueX = detailX + 432.0f;
                if (state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::Direct)
                {
                    if (handleValueEditor(valueX, child.Weight, 0.0f, 1.0f, BlendTreeValueField::DirectWeight))
                        return true;
                }
                else if (state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::OneD)
                {
                    if (handleValueEditor(valueX, child.Threshold, -1.0f, 1.0f, BlendTreeValueField::Threshold))
                        return true;
                }
                else
                {
                    if (handleValueEditor(valueX, child.Position.x, -1.0f, 1.0f, BlendTreeValueField::PositionX))
                        return true;
                    if (handleValueEditor(valueX + 190.0f, child.Position.y, -1.0f, 1.0f, BlendTreeValueField::PositionY))
                        return true;
                }
                return true;
            }
        }

        if (state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::Direct)
        {
            const float listTop = canvasY + 52.0f;
            const float listBottom = canvasY + canvasH - 84.0f;
            float rowY = canvasY + 58.0f - m_BlendTreeChildScrollY;
            for (int i = 0; i < (int)state.Tree.Children.size(); ++i)
            {
                if (rowY + 34.0f < listTop || rowY > listBottom)
                {
                    rowY += 40.0f;
                    continue;
                }

                if (IsPointInRect(mouseX, mouseY, canvasX + 14.0f, rowY, canvasW - 28.0f, 34.0f))
                {
                    m_SelectedBlendChildIndex = i;
                    if (IsPointInRect(mouseX, mouseY, canvasX + canvasW - 70.0f, rowY, 28.0f, 34.0f))
                    {
                        state.Tree.Children[i].Weight = std::clamp(state.Tree.Children[i].Weight - 0.1f, 0.0f, 1.0f);
                        CommitGraphEdit(*animator);
                    }
                    else if (IsPointInRect(mouseX, mouseY, canvasX + canvasW - 38.0f, rowY, 28.0f, 34.0f))
                    {
                        state.Tree.Children[i].Weight = std::clamp(state.Tree.Children[i].Weight + 0.1f, 0.0f, 1.0f);
                        CommitGraphEdit(*animator);
                    }
                    return true;
                }
                rowY += 40.0f;
            }
            return true;
        }

        const float axisX = canvasX + 64.0f;
        const float axisY = canvasY + 76.0f;
        const float axisW = (std::max)(80.0f, canvasW - 128.0f);
        const float axisH = (std::max)(60.0f, canvasH - 134.0f);
        for (int i = (int)state.Tree.Children.size() - 1; i >= 0; --i)
        {
            float px = axisX;
            float py = axisY + axisH * 0.5f;
            if (state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::OneD)
                px = axisX + (std::clamp(state.Tree.Children[i].Threshold, -1.0f, 1.0f) + 1.0f) * 0.5f * axisW;
            else
            {
                px = axisX + (std::clamp(state.Tree.Children[i].Position.x, -1.0f, 1.0f) + 1.0f) * 0.5f * axisW;
                py = axisY + (1.0f - (std::clamp(state.Tree.Children[i].Position.y, -1.0f, 1.0f) + 1.0f) * 0.5f) * axisH;
            }

            if (IsPointInRect(mouseX, mouseY, px - 14.0f, py - 18.0f, 28.0f, 36.0f))
            {
                m_SelectedBlendChildIndex = i;
                m_DraggingBlendChildIndex = i;
                m_IsDraggingBlendChild = true;
                Widget::BeginMouseInteraction(this);
                return true;
            }
        }

        m_SelectedBlendChildIndex = -1;
        return true;
    }

    bool AnimatorGraphPanel::HandleTimelineClick(float mouseX, float mouseY)
    {
        if (m_TimelineH <= 0.0f || !IsPointInRect(mouseX, mouseY, m_TimelineX, m_TimelineY, m_TimelineW, m_TimelineH))
            return false;

        AnimatorComponent* animator = GetAnimator();
        auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer || m_SelectedStateIndex < 0 || m_SelectedStateIndex >= (int)layer->States.size())
            return true;

        auto& state = layer->States[m_SelectedStateIndex];
        const float rawDuration = (std::max)(0.05f, GetSelectedStateRawDurationSeconds(*animator, state));
        const float duration = (std::max)(0.05f, GetSelectedStateDurationSeconds(*animator, state));
        const float trackX = m_TimelineX + 18.0f;
        const float trackY = m_TimelineY + 48.0f;
        const float trackW = (std::max)(48.0f, m_TimelineW - 36.0f);
        const float importY = m_TimelineY + 74.0f;
        const float buttonY = m_TimelineY + m_TimelineH - 28.0f;

        if (state.Motion == AnimatorComponent::State::MotionType::PropertyClip)
        {
            auto hit = [&](float bx, float by, float bw, float bh)
            {
                return IsPointInRect(mouseX, mouseY, bx, by, bw, bh);
            };
            if (hit(m_TimelineX + 14.0f, importY, 58.0f, 22.0f)) return AddPropertyTrack(PropertyTrackPreset::TransformPosition);
            if (hit(m_TimelineX + 78.0f, importY, 58.0f, 22.0f)) return AddPropertyTrack(PropertyTrackPreset::TransformRotation);
            if (hit(m_TimelineX + 142.0f, importY, 72.0f, 22.0f)) return AddPropertyTrack(PropertyTrackPreset::TransformScale);
            if (hit(m_TimelineX + 220.0f, importY, 78.0f, 22.0f)) return AddPropertyTrack(PropertyTrackPreset::ActiveSelf);
            if (hit(m_TimelineX + 304.0f, importY, 64.0f, 22.0f)) return AddPropertyTrack(PropertyTrackPreset::MaterialColor);
            if (hit(m_TimelineX + 374.0f, importY, 92.0f, 22.0f)) return AddPropertyTrack(PropertyTrackPreset::LightIntensity);
            if (hit(m_TimelineX + 472.0f, importY, 66.0f, 22.0f)) return AddPropertyTrack(PropertyTrackPreset::CameraFOV);
            if (hit(m_TimelineX + 544.0f, importY, 78.0f, 22.0f)) return AddPropertyTrack(PropertyTrackPreset::ScriptEnabled);
            if (hit(m_TimelineX + 628.0f, importY, 58.0f, 22.0f)) return AddPropertyTrack(PropertyTrackPreset::AudioVolume);
            if (hit(m_TimelineX + 692.0f, importY, 70.0f, 22.0f)) return AddPropertyTrack(PropertyTrackPreset::AudioPitch);
            if (hit(m_TimelineX + 768.0f, importY, 64.0f, 22.0f)) return AddPropertyTrack(PropertyTrackPreset::AudioPlayTrigger);
            if (hit(m_TimelineX + 14.0f, buttonY, 70.0f, 22.0f)) return AddPropertyKeyAtTimeline();
            if (hit(m_TimelineX + 90.0f, buttonY, 70.0f, 22.0f)) return DeleteSelectedPropertyKey();
            if (hit(m_TimelineX + 166.0f, buttonY, 56.0f, 22.0f)) return CopySelectedPropertyKeys();
            if (hit(m_TimelineX + 228.0f, buttonY, 62.0f, 22.0f)) return PastePropertyKeysAtTimeline();
            if (hit(m_TimelineX + 296.0f, buttonY, 70.0f, 22.0f)) return BeginPropertyEdit(PropertyEditField::TargetPath);

            const float detailY = m_TimelineY + m_TimelineH - 54.0f;
            if (m_SelectedPropertyTrackIndex >= 0 && m_SelectedPropertyTrackIndex < (int)state.PropertyTracks.size() &&
                m_SelectedPropertyKeyIndex >= 0 && m_SelectedPropertyKeyIndex < (int)state.PropertyTracks[m_SelectedPropertyTrackIndex].Keys.size())
            {
                const auto& selectedTrack = state.PropertyTracks[m_SelectedPropertyTrackIndex];
                if (hit(m_TimelineX + 14.0f, detailY, 80.0f, 22.0f)) return BeginPropertyEdit(PropertyEditField::KeyTime);
                if (hit(m_TimelineX + 100.0f, detailY, 74.0f, 22.0f)) return BeginPropertyEdit(PropertyEditField::ValueX);
                if ((selectedTrack.Type == AnimatorComponent::State::PropertyTrack::ValueType::Float3 ||
                    selectedTrack.Type == AnimatorComponent::State::PropertyTrack::ValueType::Float4) &&
                    hit(m_TimelineX + 180.0f, detailY, 74.0f, 22.0f)) return BeginPropertyEdit(PropertyEditField::ValueY);
                if ((selectedTrack.Type == AnimatorComponent::State::PropertyTrack::ValueType::Float3 ||
                    selectedTrack.Type == AnimatorComponent::State::PropertyTrack::ValueType::Float4) &&
                    hit(m_TimelineX + 260.0f, detailY, 74.0f, 22.0f)) return BeginPropertyEdit(PropertyEditField::ValueZ);
                if (selectedTrack.Type == AnimatorComponent::State::PropertyTrack::ValueType::Float4 &&
                    hit(m_TimelineX + 340.0f, detailY, 74.0f, 22.0f)) return BeginPropertyEdit(PropertyEditField::ValueW);
                if (hit(m_TimelineX + 420.0f, detailY, 86.0f, 22.0f)) return CycleSelectedPropertyInterpolation();
            }

            const float rowsY = m_TimelineY + 104.0f;
            const int visibleRows = (std::max)(1, (int)((m_TimelineH - 142.0f) / 24.0f));
            for (int i = 0; i < (int)state.PropertyTracks.size() && i < visibleRows; ++i)
            {
                const float rowY = rowsY + (float)i * 24.0f;
                if (!hit(m_TimelineX + 14.0f, rowY, m_TimelineW - 28.0f, 21.0f))
                    continue;

                m_SelectedPropertyTrackIndex = i;
                m_SelectedPropertyKeyIndex = -1;
                auto& track = state.PropertyTracks[i];
                for (int k = 0; k < (int)track.Keys.size(); ++k)
                {
                    const float kx = trackX + trackW * std::clamp(track.Keys[k].TimeSeconds / duration, 0.0f, 1.0f);
                    if (std::abs(mouseX - kx) <= 8.0f)
                    {
                        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                        if (!shift)
                        {
                            for (auto& key : state.PropertyTracks[i].Keys)
                                key.Selected = false;
                        }
                        m_SelectedPropertyKeyIndex = k;
                        state.PropertyTracks[i].Keys[k].Selected = shift ? !state.PropertyTracks[i].Keys[k].Selected : true;
                        layer->StateTime = std::clamp(track.Keys[k].TimeSeconds, 0.0f, duration);
                        ApplyPropertyClipPreview(state, layer->StateTime);
                        break;
                    }
                }
                SyncBaseLayerToLegacyGraph(*animator);
                return true;
            }

            if (IsPointInRect(mouseX, mouseY, trackX, trackY - 16.0f, trackW, 48.0f))
            {
                const float ratio = std::clamp((mouseX - trackX) / trackW, 0.0f, 1.0f);
                layer->StateTime = ratio * duration;
                ApplyPropertyClipPreview(state, layer->StateTime);
                m_IsScrubbingTimeline = true;
                Widget::BeginMouseInteraction(this);
                return true;
            }
            return true;
        }

        const float step = (std::max)(1.0f / 30.0f, rawDuration * 0.01f);
        auto ensureRange = [&]()
        {
            if (!state.ImportSettings.UseCustomRange)
            {
                state.ImportSettings.UseCustomRange = true;
                state.ImportSettings.StartSeconds = 0.0f;
                state.ImportSettings.EndSeconds = rawDuration;
            }
            state.ImportSettings.StartSeconds = std::clamp(state.ImportSettings.StartSeconds, 0.0f, rawDuration);
            state.ImportSettings.EndSeconds = std::clamp(state.ImportSettings.EndSeconds <= 0.0f ? rawDuration : state.ImportSettings.EndSeconds, state.ImportSettings.StartSeconds, rawDuration);
        };

        if (IsPointInRect(mouseX, mouseY, m_TimelineX + 14.0f, importY, 96.0f, 22.0f))
        {
            state.ImportSettings.UseCustomRange = !state.ImportSettings.UseCustomRange;
            if (state.ImportSettings.UseCustomRange)
            {
                state.ImportSettings.StartSeconds = 0.0f;
                state.ImportSettings.EndSeconds = rawDuration;
            }
            CommitGraphEdit(*animator);
            ResetRuntime(*animator);
            return true;
        }
        if (IsPointInRect(mouseX, mouseY, m_TimelineX + 124.0f, importY, 22.0f, 22.0f))
        {
            ensureRange();
            state.ImportSettings.StartSeconds = std::clamp(state.ImportSettings.StartSeconds - step, 0.0f, state.ImportSettings.EndSeconds);
            CommitGraphEdit(*animator);
            ResetRuntime(*animator);
            return true;
        }
        if (IsPointInRect(mouseX, mouseY, m_TimelineX + 150.0f, importY, 22.0f, 22.0f))
        {
            ensureRange();
            state.ImportSettings.StartSeconds = std::clamp(state.ImportSettings.StartSeconds + step, 0.0f, state.ImportSettings.EndSeconds);
            CommitGraphEdit(*animator);
            ResetRuntime(*animator);
            return true;
        }
        if (IsPointInRect(mouseX, mouseY, m_TimelineX + 286.0f, importY, 22.0f, 22.0f))
        {
            ensureRange();
            state.ImportSettings.EndSeconds = std::clamp(state.ImportSettings.EndSeconds - step, state.ImportSettings.StartSeconds, rawDuration);
            CommitGraphEdit(*animator);
            ResetRuntime(*animator);
            return true;
        }
        if (IsPointInRect(mouseX, mouseY, m_TimelineX + 312.0f, importY, 22.0f, 22.0f))
        {
            ensureRange();
            state.ImportSettings.EndSeconds = std::clamp(state.ImportSettings.EndSeconds + step, state.ImportSettings.StartSeconds, rawDuration);
            CommitGraphEdit(*animator);
            ResetRuntime(*animator);
            return true;
        }
        if (IsPointInRect(mouseX, mouseY, m_TimelineX + 430.0f, importY, 94.0f, 22.0f))
        {
            state.ImportSettings.LoopPose = !state.ImportSettings.LoopPose;
            CommitGraphEdit(*animator);
            ResetRuntime(*animator);
            return true;
        }

        if (IsPointInRect(mouseX, mouseY, m_TimelineX + 14.0f, buttonY, 86.0f, 22.0f))
        {
            AnimatorComponent::State::AnimationEvent event;
            event.TimeSeconds = std::clamp(layer->StateTime, 0.0f, duration);
            event.FunctionName = "OnAnimationEvent";
            event.StringArgument = state.Name;
            state.Events.push_back(event);
            state.SelectedEventIndex = (int)state.Events.size() - 1;
            CommitGraphEdit(*animator);
            return true;
        }

        if (IsPointInRect(mouseX, mouseY, m_TimelineX + 106.0f, buttonY, 92.0f, 22.0f))
        {
            if (state.SelectedEventIndex >= 0 && state.SelectedEventIndex < (int)state.Events.size())
            {
                state.Events.erase(state.Events.begin() + state.SelectedEventIndex);
                state.SelectedEventIndex = std::clamp(state.SelectedEventIndex, -1, (int)state.Events.size() - 1);
                CommitGraphEdit(*animator);
            }
            return true;
        }

        for (int i = 0; i < (int)state.Events.size(); ++i)
        {
            const float markerX = trackX + trackW * (std::clamp(state.Events[i].TimeSeconds, 0.0f, duration) / duration);
            if (IsPointInRect(mouseX, mouseY, markerX - 8.0f, trackY - 14.0f, 16.0f, 44.0f))
            {
                state.SelectedEventIndex = i;
                SyncBaseLayerToLegacyGraph(*animator);
                return true;
            }
        }

        if (IsPointInRect(mouseX, mouseY, trackX, trackY - 16.0f, trackW, 48.0f))
        {
            const float ratio = std::clamp((mouseX - trackX) / trackW, 0.0f, 1.0f);
            const float sourceTime = ratio * rawDuration;
            const float rangeStart = state.ImportSettings.UseCustomRange
                ? std::clamp(state.ImportSettings.StartSeconds, 0.0f, rawDuration)
                : 0.0f;
            ApplyTimelinePreview(*animator, state, std::clamp(sourceTime - rangeStart, 0.0f, duration));
            m_IsScrubbingTimeline = true;
            Widget::BeginMouseInteraction(this);
            return true;
        }

        return true;
    }

    bool AnimatorGraphPanel::HandleContextMenuClick(float mouseX, float mouseY)
    {
        if (!m_IsContextMenuOpen)
            return false;

        const auto* animator = GetAnimator();
        const auto* readLayer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !readLayer)
            return false;

        const bool canBeginTransition = m_ContextMenuMode == ContextMenuMode::ReplaceState &&
            m_ContextStateIndex >= 0 &&
            m_ContextStateIndex < (int)readLayer->States.size();
        const float itemH = 24.0f;
        const float menuW = 230.0f;
        float menuH = 8.0f;
        if (m_ContextMenuMode == ContextMenuMode::Transition)
            menuH += 4.0f * itemH;
        else
            menuH += 2.0f * itemH + 8.0f + (canBeginTransition ? itemH : 0.0f);

        const float menuX = (std::min)(m_ContextMenuX, m_CalculatedPos.x + m_CalculatedSize.x - menuW - 4.0f);
        const float menuY = (std::min)(m_ContextMenuY, m_CalculatedPos.y + m_CalculatedSize.y - menuH - 4.0f);
        if (!IsPointInRect(mouseX, mouseY, menuX, menuY, menuW, menuH))
            return false;

        AnimatorComponent* mutableAnimator = GetAnimator();
        if (!mutableAnimator)
            return false;
        auto* layer = GetActiveLayer(*mutableAnimator);
        if (!layer)
            return false;

        if (m_ContextMenuMode == ContextMenuMode::Transition)
        {
            if (m_ContextTransitionIndex >= 0 && m_ContextTransitionIndex < (int)layer->Transitions.size())
            {
                auto& transition = layer->Transitions[m_ContextTransitionIndex];
                const int item = (int)((mouseY - menuY - 4.0f) / itemH);
                if (item == 0)
                    transition.HasExitTime = !transition.HasExitTime;
                else if (item == 1)
                {
                    transition.Conditions.push_back(MakeDefaultTransitionCondition(*mutableAnimator));
                }
                else if (item == 2)
                {
                    std::stable_sort(layer->Transitions.begin(), layer->Transitions.end(),
                        [](const AnimatorComponent::Transition& a, const AnimatorComponent::Transition& b)
                        {
                            return a.Priority < b.Priority;
                        });
                    m_ContextTransitionIndex = std::clamp(m_ContextTransitionIndex, -1, (int)layer->Transitions.size() - 1);
                    m_SelectedTransitionIndex = m_ContextTransitionIndex;
                    layer->SelectedTransitionIndex = m_SelectedTransitionIndex;
                }
                else if (item == 3)
                {
                    layer->Transitions.erase(layer->Transitions.begin() + m_ContextTransitionIndex);
                    m_SelectedTransitionIndex = -1;
                    layer->SelectedTransitionIndex = -1;
                }
                CommitGraphEdit(*mutableAnimator);
            }
            CloseContextMenu();
            return true;
        }

        if (canBeginTransition && IsPointInRect(mouseX, mouseY, menuX, menuY + 30.0f, menuW, itemH))
        {
            BeginTransitionCreation(m_ContextStateIndex);
            CloseContextMenu();
            return true;
        }

        const float clipStartY = menuY + 30.0f + (canBeginTransition ? itemH : 0.0f);
        if (IsPointInRect(mouseX, mouseY, menuX, clipStartY, menuW, itemH))
            OpenClipPicker(m_ContextMenuMode, m_ContextStateIndex, m_ContextGraphPosition);
        CloseContextMenu();
        return true;
    }

    bool AnimatorGraphPanel::HandleClipPickerClick(float mouseX, float mouseY)
    {
        if (!m_IsClipPickerOpen)
            return false;

        const float pickerW = (std::min)(520.0f, m_CalculatedSize.x - 48.0f);
        const float pickerH = (std::min)(420.0f, m_CalculatedSize.y - 72.0f);
        const float pickerX = m_CalculatedPos.x + (m_CalculatedSize.x - pickerW) * 0.5f;
        const float pickerY = m_CalculatedPos.y + (m_CalculatedSize.y - pickerH) * 0.5f;

        if (IsPointInRect(mouseX, mouseY, pickerX + pickerW - 74.0f, pickerY + 7.0f, 58.0f, 21.0f) ||
            !IsPointInRect(mouseX, mouseY, pickerX, pickerY, pickerW, pickerH))
        {
            CloseClipPicker();
            return true;
        }

        const float listY = pickerY + 72.0f;
        const int clipIndex = (int)((mouseY - listY) / 34.0f);
        if (clipIndex >= 0 && clipIndex < (int)m_ClipPickerClips.size())
        {
            const int sourceClipIndex = (int)m_ClipPickerClips[clipIndex].Index;
            const std::string sourceClipName = m_ClipPickerClips[clipIndex].Name;
            if (m_ClipPickerMode == ContextMenuMode::ReplaceState)
                ReplaceStateClip(m_ClipPickerStateIndex, sourceClipIndex, sourceClipName);
            else if (m_ClipPickerMode == ContextMenuMode::BlendTreeReplaceChild)
                ReplaceBlendTreeChild(m_ClipPickerStateIndex, m_ClipPickerBlendChildIndex, sourceClipIndex, sourceClipName);
            else if (m_ClipPickerMode == ContextMenuMode::BlendTreeAddChild)
                AddBlendTreeChild(sourceClipIndex, sourceClipName);
            else
                AddStateFromClipIndex(sourceClipIndex, sourceClipName, m_ClipPickerGraphPosition);
            CloseClipPicker();
        }

        return true;
    }

    void AnimatorGraphPanel::OpenContextMenu(ContextMenuMode mode, float mouseX, float mouseY, int stateIndex, int transitionIndex)
    {
        if (mode != ContextMenuMode::ReplaceState)
            m_ContextSourceStateIndex = -1;
        m_IsContextMenuOpen = true;
        m_ContextMenuMode = mode;
        m_ContextMenuX = mouseX;
        m_ContextMenuY = mouseY;
        m_ContextStateIndex = stateIndex;
        m_ContextTransitionIndex = transitionIndex;
        m_ContextGraphPosition = ScreenToGraph(mouseX, mouseY);
        m_ContextClips.clear();
        if (mode == ContextMenuMode::AddState || mode == ContextMenuMode::ReplaceState)
        {
            // 실제 클립 목록은 선택 브라우저를 열 때만 읽는다.
            // 우클릭 메뉴는 가벼운 명령 목록만 담당해서 빈 Animator에서도 클릭이 먹통처럼 보이지 않게 한다.
        }
    }

    void AnimatorGraphPanel::CloseContextMenu()
    {
        m_IsContextMenuOpen = false;
        m_ContextMenuMode = ContextMenuMode::None;
        m_ContextStateIndex = -1;
        m_ContextSourceStateIndex = -1;
        m_ContextTransitionIndex = -1;
        m_ContextClips.clear();
    }

    void AnimatorGraphPanel::BeginTransitionCreation(int sourceStateIndex)
    {
        AnimatorComponent* animator = GetAnimator();
        auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer || sourceStateIndex < 0 || sourceStateIndex >= (int)layer->States.size())
            return;

        // Transition 편집은 두 단계 입력이다.
        // 1) source state에서 Make Transition을 누른다.
        // 2) 마우스를 따라오는 선을 target state에 클릭해 연결한다.
        // 이렇게 해야 이전 선택 상태나 마지막 생성 state에 끌려가지 않고, 사용자가 지정한 A -> B 방향이 그대로 저장된다.
        m_IsCreatingTransition = true;
        m_TransitionSourceStateIndex = sourceStateIndex;
        SelectOnlyState(*animator, sourceStateIndex);
        m_SelectedTransitionIndex = -1;
        layer->SelectedTransitionIndex = -1;
        Widget::SetKeyboardFocus(this);
    }

    void AnimatorGraphPanel::CancelTransitionCreation()
    {
        m_IsCreatingTransition = false;
        m_TransitionSourceStateIndex = -1;
    }

    void AnimatorGraphPanel::OpenClipPicker(ContextMenuMode mode, int stateIndex, const DirectX::XMFLOAT2& graphPosition)
    {
        AnimatorComponent* animator = GetAnimator();
        if (!animator)
            return;

        m_IsClipPickerOpen = true;
        m_ClipPickerMode = mode;
        m_ClipPickerStateIndex = stateIndex;
        m_ClipPickerGraphPosition = graphPosition;
        m_ClipPickerClips = InspectSourceClips(*animator);
        m_ClipPickerMessage.clear();
        if (m_ClipPickerClips.empty())
            m_ClipPickerMessage = "Animator has no usable clip source.";
        Widget::SetKeyboardFocus(this);
    }

    void AnimatorGraphPanel::CloseClipPicker()
    {
        m_IsClipPickerOpen = false;
        m_ClipPickerMode = ContextMenuMode::None;
        m_ClipPickerStateIndex = -1;
        m_ClipPickerBlendChildIndex = -1;
        m_ClipPickerClips.clear();
        m_ClipPickerMessage.clear();
    }

    void AnimatorGraphPanel::AddStateFromSelectedClip()
    {
        AnimatorComponent* animator = GetAnimator();
        if (!animator)
            return;
        auto* layer = GetActiveLayer(*animator);
        if (!layer)
            return;

        AddStateFromClipIndex((std::max)(0, animator->SelectedClipIndex), animator->SelectedClipName,
            { 360.0f + (float)(layer->States.size() % 4) * 60.0f, 180.0f + (float)(layer->States.size() % 5) * 46.0f });
    }

    void AnimatorGraphPanel::AddEmptyState(const DirectX::XMFLOAT2& graphPosition)
    {
        AnimatorComponent* animator = GetAnimator();
        if (!animator)
            return;
        auto* layer = GetActiveLayer(*animator);
        if (!layer)
            return;

        auto makeUniqueName = [&layer]()
        {
            const std::string base = "New State";
            int suffix = 1;
            std::string candidate = base;
            while (std::any_of(layer->States.begin(), layer->States.end(), [&candidate](const AnimatorComponent::State& state)
            {
                return state.Name == candidate;
            }))
            {
                candidate = base + " " + std::to_string(++suffix);
            }
            return candidate;
        };

        AnimatorComponent::State state;
        state.Name = makeUniqueName();
        state.Motion = AnimatorComponent::State::MotionType::Clip;
        state.ClipIndex = -1;
        state.ImportSettings.DisplayName = state.Name;
        state.GraphPosition = graphPosition;
        layer->States.push_back(state);
        if (layer->EntryStateIndex < 0)
            layer->EntryStateIndex = 0;
        layer->ActiveStateIndex = static_cast<int>(layer->States.size()) - 1;
        m_SelectedStateIndex = layer->ActiveStateIndex;
        m_SelectedTransitionIndex = -1;
        layer->SelectedTransitionIndex = -1;
        m_SelectedStateIndices.clear();
        m_SelectedStateIndices.push_back(m_SelectedStateIndex);
        m_StateEditMessage = "Empty State created. Click Name to rename, Replace Clip to assign motion.";
        CommitGraphEdit(*animator);
        ResetRuntime(*animator);
    }

    void AnimatorGraphPanel::AddStateFromClipIndex(int clipIndex, const std::string& clipName, const DirectX::XMFLOAT2& graphPosition)
    {
        AnimatorComponent* animator = GetAnimator();
        if (!animator)
            return;
        auto* layer = GetActiveLayer(*animator);
        if (!layer)
            return;

        AnimatorComponent::State state;
        state.Name = clipName.empty() ? ("State " + std::to_string(layer->States.size() + 1)) : clipName;
        state.ImportSettings.DisplayName = state.Name;
        state.ClipIndex = (std::max)(0, clipIndex);
        state.GraphPosition = graphPosition;
        layer->States.push_back(state);
        if (layer->EntryStateIndex < 0)
            layer->EntryStateIndex = 0;
        layer->ActiveStateIndex = (int)layer->States.size() - 1;
        m_SelectedStateIndex = layer->ActiveStateIndex;
        m_SelectedTransitionIndex = -1;
        layer->SelectedTransitionIndex = -1;
        m_SelectedStateIndices.clear();
        m_SelectedStateIndices.push_back(m_SelectedStateIndex);
        CommitGraphEdit(*animator);
        ResetRuntime(*animator);
    }

    void AnimatorGraphPanel::ReplaceStateClip(int stateIndex, int clipIndex, const std::string& clipName)
    {
        AnimatorComponent* animator = GetAnimator();
        auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer || stateIndex < 0 || stateIndex >= (int)layer->States.size())
            return;

        auto& state = layer->States[stateIndex];
        state.Motion = AnimatorComponent::State::MotionType::Clip;
        state.ClipIndex = (std::max)(0, clipIndex);
        if (!clipName.empty())
        {
            state.Name = clipName;
            state.ImportSettings.DisplayName = clipName;
        }
        animator->SelectedClipIndex = state.ClipIndex;
        animator->SelectedClipName = clipName;
        SelectOnlyState(*animator, stateIndex);
        CommitGraphEdit(*animator);
        ResetRuntime(*animator);
    }

    bool AnimatorGraphPanel::EnsureSelectedPropertyClip(AnimatorComponent& animator)
    {
        auto* layer = GetActiveLayer(animator);
        if (!layer)
            return false;

        if (m_SelectedStateIndex < 0 || m_SelectedStateIndex >= (int)layer->States.size())
        {
            AnimatorComponent::State state;
            state.Name = "Property Clip";
            state.Motion = AnimatorComponent::State::MotionType::PropertyClip;
            state.ClipIndex = -1;
            state.GraphPosition = { 360.0f + (float)(layer->States.size() % 4) * 60.0f, 180.0f + (float)(layer->States.size() % 5) * 46.0f };
            layer->States.push_back(state);
            if (layer->EntryStateIndex < 0)
                layer->EntryStateIndex = 0;
            layer->ActiveStateIndex = (int)layer->States.size() - 1;
            m_SelectedStateIndex = layer->ActiveStateIndex;
            m_SelectedTransitionIndex = -1;
            m_SelectedPropertyTrackIndex = -1;
            m_SelectedPropertyKeyIndex = -1;
            layer->SelectedTransitionIndex = -1;
            m_SelectedStateIndices.clear();
            m_SelectedStateIndices.push_back(m_SelectedStateIndex);
        }

        auto& state = layer->States[m_SelectedStateIndex];
        if (state.Motion != AnimatorComponent::State::MotionType::PropertyClip)
        {
            state.Motion = AnimatorComponent::State::MotionType::PropertyClip;
            state.Name = state.Name.empty() ? "Property Clip" : state.Name;
            state.ClipIndex = -1;
            state.Tree.Children.clear();
            state.Events.clear();
            state.SelectedEventIndex = -1;
            m_SelectedPropertyTrackIndex = -1;
            m_SelectedPropertyKeyIndex = -1;
        }
        return true;
    }

    bool AnimatorGraphPanel::AddPropertyTrack(PropertyTrackPreset preset)
    {
        AnimatorComponent* animator = GetAnimator();
        auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer || !EnsureSelectedPropertyClip(*animator))
            return false;

        auto& state = layer->States[m_SelectedStateIndex];
        AnimatorComponent::State::PropertyTrack track;
        track.EntityPath = ".";
        track.Type = AnimatorComponent::State::PropertyTrack::ValueType::Float;

        switch (preset)
        {
            case PropertyTrackPreset::TransformPosition:
                track.ComponentName = "Transform"; track.PropertyName = "Position";
                track.Type = AnimatorComponent::State::PropertyTrack::ValueType::Float3;
                break;
            case PropertyTrackPreset::TransformRotation:
                track.ComponentName = "Transform"; track.PropertyName = "Rotation";
                track.Type = AnimatorComponent::State::PropertyTrack::ValueType::Float3;
                break;
            case PropertyTrackPreset::TransformScale:
                track.ComponentName = "Transform"; track.PropertyName = "Scale";
                track.Type = AnimatorComponent::State::PropertyTrack::ValueType::Float3;
                break;
            case PropertyTrackPreset::ActiveSelf:
                track.ComponentName = "Active"; track.PropertyName = "Self";
                track.Type = AnimatorComponent::State::PropertyTrack::ValueType::Bool;
                break;
            case PropertyTrackPreset::MaterialColor:
                track.ComponentName = "Material"; track.PropertyName = "AlbedoColor";
                track.Type = AnimatorComponent::State::PropertyTrack::ValueType::Float4;
                break;
            case PropertyTrackPreset::LightIntensity:
                track.ComponentName = "Light"; track.PropertyName = "Intensity";
                break;
            case PropertyTrackPreset::LightColor:
                track.ComponentName = "Light"; track.PropertyName = "Color";
                track.Type = AnimatorComponent::State::PropertyTrack::ValueType::Float3;
                break;
            case PropertyTrackPreset::CameraFOV:
                track.ComponentName = "Camera"; track.PropertyName = "FOV";
                break;
            case PropertyTrackPreset::ScriptEnabled:
                track.ComponentName = "Script"; track.PropertyName = "Enabled";
                track.Type = AnimatorComponent::State::PropertyTrack::ValueType::Bool;
                break;
            case PropertyTrackPreset::AudioVolume:
                track.ComponentName = "Audio"; track.PropertyName = "Volume";
                break;
            case PropertyTrackPreset::AudioPitch:
                track.ComponentName = "Audio"; track.PropertyName = "Pitch";
                break;
            case PropertyTrackPreset::AudioPlayTrigger:
                track.ComponentName = "Audio"; track.PropertyName = "PlayTrigger";
                track.Type = AnimatorComponent::State::PropertyTrack::ValueType::Bool;
                break;
        }

        if (track.ComponentName == "Audio" && m_TargetEntity && !m_TargetEntity.HasComponent<AudioComponent>())
            m_TargetEntity.AddComponent<AudioComponent>();

        const auto duplicate = std::find_if(state.PropertyTracks.begin(), state.PropertyTracks.end(), [&track](const auto& existing)
        {
            return existing.EntityPath == track.EntityPath &&
                existing.ComponentName == track.ComponentName &&
                existing.PropertyName == track.PropertyName;
        });
        if (duplicate != state.PropertyTracks.end())
        {
            m_SelectedPropertyTrackIndex = (int)std::distance(state.PropertyTracks.begin(), duplicate);
            return true;
        }

        AnimatorComponent::State::PropertyKey key;
        key.TimeSeconds = 0.0f;
        key.Value = CapturePropertyValue(track);
        key.Interp = track.Type == AnimatorComponent::State::PropertyTrack::ValueType::Bool
            ? AnimatorComponent::State::PropertyKey::Interpolation::Constant
            : AnimatorComponent::State::PropertyKey::Interpolation::Linear;
        key.Selected = true;
        track.Keys.push_back(key);
        state.PropertyTracks.push_back(track);
        m_SelectedPropertyTrackIndex = (int)state.PropertyTracks.size() - 1;
        m_SelectedPropertyKeyIndex = 0;
        CommitGraphEdit(*animator);
        return true;
    }

    bool AnimatorGraphPanel::AddPropertyKeyAtTimeline()
    {
        AnimatorComponent* animator = GetAnimator();
        auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer || m_SelectedStateIndex < 0 || m_SelectedStateIndex >= (int)layer->States.size())
            return false;

        auto& state = layer->States[m_SelectedStateIndex];
        if (state.Motion != AnimatorComponent::State::MotionType::PropertyClip ||
            m_SelectedPropertyTrackIndex < 0 || m_SelectedPropertyTrackIndex >= (int)state.PropertyTracks.size())
            return false;

        auto& track = state.PropertyTracks[m_SelectedPropertyTrackIndex];
        AnimatorComponent::State::PropertyKey key;
        key.TimeSeconds = std::clamp(layer->StateTime, 0.0f, GetSelectedStateDurationSeconds(*animator, state));
        key.Value = CapturePropertyValue(track);
        if (track.ComponentName == "Audio" && track.PropertyName == "PlayTrigger")
            key.Value = { 1.0f, 0.0f, 0.0f, 0.0f };
        key.Interp = track.Type == AnimatorComponent::State::PropertyTrack::ValueType::Bool
            ? AnimatorComponent::State::PropertyKey::Interpolation::Constant
            : AnimatorComponent::State::PropertyKey::Interpolation::Linear;
        for (auto& existingKey : track.Keys)
            existingKey.Selected = false;
        key.Selected = true;
        track.Keys.push_back(key);
        std::stable_sort(track.Keys.begin(), track.Keys.end(), [](const auto& a, const auto& b) { return a.TimeSeconds < b.TimeSeconds; });
        for (int i = 0; i < (int)track.Keys.size(); ++i)
        {
            if (std::abs(track.Keys[i].TimeSeconds - key.TimeSeconds) < 0.0001f)
            {
                m_SelectedPropertyKeyIndex = i;
                break;
            }
        }
        CommitGraphEdit(*animator);
        return true;
    }

    bool AnimatorGraphPanel::DeleteSelectedPropertyKey()
    {
        AnimatorComponent* animator = GetAnimator();
        auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer || m_SelectedStateIndex < 0 || m_SelectedStateIndex >= (int)layer->States.size())
            return false;

        auto& state = layer->States[m_SelectedStateIndex];
        if (state.Motion != AnimatorComponent::State::MotionType::PropertyClip ||
            m_SelectedPropertyTrackIndex < 0 || m_SelectedPropertyTrackIndex >= (int)state.PropertyTracks.size())
            return false;

        auto& track = state.PropertyTracks[m_SelectedPropertyTrackIndex];
        if (m_SelectedPropertyKeyIndex < 0 || m_SelectedPropertyKeyIndex >= (int)track.Keys.size())
            return false;

        track.Keys[m_SelectedPropertyKeyIndex].Selected = true;
        track.Keys.erase(
            std::remove_if(track.Keys.begin(), track.Keys.end(), [](const auto& key)
                {
                    return key.Selected;
                }),
            track.Keys.end());
        m_SelectedPropertyKeyIndex = std::clamp(m_SelectedPropertyKeyIndex, -1, (int)track.Keys.size() - 1);
        if (track.Keys.empty())
        {
            state.PropertyTracks.erase(state.PropertyTracks.begin() + m_SelectedPropertyTrackIndex);
            m_SelectedPropertyTrackIndex = std::clamp(m_SelectedPropertyTrackIndex, -1, (int)state.PropertyTracks.size() - 1);
            m_SelectedPropertyKeyIndex = -1;
        }
        CommitGraphEdit(*animator);
        return true;
    }

    bool AnimatorGraphPanel::CopySelectedPropertyKeys()
    {
        AnimatorComponent* animator = GetAnimator();
        auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer || m_SelectedStateIndex < 0 || m_SelectedStateIndex >= (int)layer->States.size())
            return false;

        const auto& state = layer->States[m_SelectedStateIndex];
        if (state.Motion != AnimatorComponent::State::MotionType::PropertyClip ||
            m_SelectedPropertyTrackIndex < 0 || m_SelectedPropertyTrackIndex >= (int)state.PropertyTracks.size())
            return false;

        const auto& track = state.PropertyTracks[m_SelectedPropertyTrackIndex];
        m_CopiedPropertyKeys.clear();
        for (const auto& key : track.Keys)
        {
            if (key.Selected)
                m_CopiedPropertyKeys.push_back(key);
        }
        if (m_CopiedPropertyKeys.empty() && m_SelectedPropertyKeyIndex >= 0 && m_SelectedPropertyKeyIndex < (int)track.Keys.size())
            m_CopiedPropertyKeys.push_back(track.Keys[m_SelectedPropertyKeyIndex]);
        return !m_CopiedPropertyKeys.empty();
    }

    bool AnimatorGraphPanel::PastePropertyKeysAtTimeline()
    {
        AnimatorComponent* animator = GetAnimator();
        auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer || m_CopiedPropertyKeys.empty() ||
            m_SelectedStateIndex < 0 || m_SelectedStateIndex >= (int)layer->States.size())
            return false;

        auto& state = layer->States[m_SelectedStateIndex];
        if (state.Motion != AnimatorComponent::State::MotionType::PropertyClip ||
            m_SelectedPropertyTrackIndex < 0 || m_SelectedPropertyTrackIndex >= (int)state.PropertyTracks.size())
            return false;

        auto& track = state.PropertyTracks[m_SelectedPropertyTrackIndex];
        const float duration = GetSelectedStateDurationSeconds(*animator, state);
        float firstTime = m_CopiedPropertyKeys.front().TimeSeconds;
        for (const auto& key : m_CopiedPropertyKeys)
            firstTime = (std::min)(firstTime, key.TimeSeconds);

        for (auto& key : track.Keys)
            key.Selected = false;

        const float pasteTime = std::clamp(layer->StateTime, 0.0f, duration);
        for (auto copied : m_CopiedPropertyKeys)
        {
            copied.TimeSeconds = std::clamp(pasteTime + (copied.TimeSeconds - firstTime), 0.0f, duration);
            copied.Selected = true;
            track.Keys.push_back(copied);
        }

        std::stable_sort(track.Keys.begin(), track.Keys.end(), [](const auto& a, const auto& b) { return a.TimeSeconds < b.TimeSeconds; });
        m_SelectedPropertyKeyIndex = -1;
        for (int i = 0; i < (int)track.Keys.size(); ++i)
        {
            if (track.Keys[i].Selected)
            {
                m_SelectedPropertyKeyIndex = i;
                break;
            }
        }
        CommitGraphEdit(*animator);
        ApplyPropertyClipPreview(state, layer->StateTime);
        return true;
    }

    bool AnimatorGraphPanel::MoveSelectedPropertyKeys(float deltaSeconds)
    {
        AnimatorComponent* animator = GetAnimator();
        auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer || m_SelectedStateIndex < 0 || m_SelectedStateIndex >= (int)layer->States.size())
            return false;

        auto& state = layer->States[m_SelectedStateIndex];
        if (state.Motion != AnimatorComponent::State::MotionType::PropertyClip ||
            m_SelectedPropertyTrackIndex < 0 || m_SelectedPropertyTrackIndex >= (int)state.PropertyTracks.size())
            return false;

        auto& track = state.PropertyTracks[m_SelectedPropertyTrackIndex];
        if (m_SelectedPropertyKeyIndex >= 0 && m_SelectedPropertyKeyIndex < (int)track.Keys.size())
            track.Keys[m_SelectedPropertyKeyIndex].Selected = true;

        const float duration = GetSelectedStateDurationSeconds(*animator, state);
        bool moved = false;
        for (auto& key : track.Keys)
        {
            if (!key.Selected)
                continue;
            key.TimeSeconds = std::clamp(key.TimeSeconds + deltaSeconds, 0.0f, duration);
            moved = true;
        }
        if (!moved)
            return false;

        std::stable_sort(track.Keys.begin(), track.Keys.end(), [](const auto& a, const auto& b) { return a.TimeSeconds < b.TimeSeconds; });
        m_SelectedPropertyKeyIndex = -1;
        for (int i = 0; i < (int)track.Keys.size(); ++i)
        {
            if (track.Keys[i].Selected)
            {
                m_SelectedPropertyKeyIndex = i;
                break;
            }
        }
        CommitGraphEdit(*animator);
        ApplyPropertyClipPreview(state, layer->StateTime);
        return true;
    }

    bool AnimatorGraphPanel::CycleSelectedPropertyInterpolation()
    {
        AnimatorComponent* animator = GetAnimator();
        auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer || m_SelectedStateIndex < 0 || m_SelectedStateIndex >= (int)layer->States.size())
            return false;

        auto& state = layer->States[m_SelectedStateIndex];
        if (state.Motion != AnimatorComponent::State::MotionType::PropertyClip ||
            m_SelectedPropertyTrackIndex < 0 || m_SelectedPropertyTrackIndex >= (int)state.PropertyTracks.size())
            return false;

        auto& track = state.PropertyTracks[m_SelectedPropertyTrackIndex];
        if (m_SelectedPropertyKeyIndex < 0 || m_SelectedPropertyKeyIndex >= (int)track.Keys.size())
            return false;

        const auto next = NextPropertyInterpolation(track.Keys[m_SelectedPropertyKeyIndex].Interp);
        bool changed = false;
        for (auto& key : track.Keys)
        {
            if (key.Selected || &key == &track.Keys[m_SelectedPropertyKeyIndex])
            {
                key.Interp = next;
                changed = true;
            }
        }
        if (!changed)
            return false;

        CommitGraphEdit(*animator);
        ApplyPropertyClipPreview(state, layer->StateTime);
        return true;
    }

    bool AnimatorGraphPanel::BeginPropertyEdit(PropertyEditField field)
    {
        AnimatorComponent* animator = GetAnimator();
        auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer || m_SelectedStateIndex < 0 || m_SelectedStateIndex >= (int)layer->States.size())
            return false;

        auto& state = layer->States[m_SelectedStateIndex];
        if (state.Motion != AnimatorComponent::State::MotionType::PropertyClip ||
            m_SelectedPropertyTrackIndex < 0 || m_SelectedPropertyTrackIndex >= (int)state.PropertyTracks.size())
            return false;

        const auto& track = state.PropertyTracks[m_SelectedPropertyTrackIndex];
        if (field == PropertyEditField::TargetPath)
        {
            m_EditingPropertyField = field;
            m_PropertyEditBuffer = track.EntityPath.empty() ? "." : track.EntityPath;
            Widget::SetKeyboardFocus(this);
            return true;
        }

        if (m_SelectedPropertyKeyIndex < 0 || m_SelectedPropertyKeyIndex >= (int)track.Keys.size())
            return false;

        const auto& key = track.Keys[m_SelectedPropertyKeyIndex];
        m_EditingPropertyField = field;
        switch (field)
        {
            case PropertyEditField::KeyTime: m_PropertyEditBuffer = FormatPropertyValue(key.TimeSeconds); break;
            case PropertyEditField::ValueX: m_PropertyEditBuffer = FormatPropertyValue(key.Value.x); break;
            case PropertyEditField::ValueY: m_PropertyEditBuffer = FormatPropertyValue(key.Value.y); break;
            case PropertyEditField::ValueZ: m_PropertyEditBuffer = FormatPropertyValue(key.Value.z); break;
            case PropertyEditField::ValueW: m_PropertyEditBuffer = FormatPropertyValue(key.Value.w); break;
            default: break;
        }
        Widget::SetKeyboardFocus(this);
        return true;
    }

    bool AnimatorGraphPanel::CommitPropertyEdit()
    {
        AnimatorComponent* animator = GetAnimator();
        auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer || m_EditingPropertyField == PropertyEditField::None ||
            m_SelectedStateIndex < 0 || m_SelectedStateIndex >= (int)layer->States.size())
            return false;

        auto& state = layer->States[m_SelectedStateIndex];
        if (state.Motion != AnimatorComponent::State::MotionType::PropertyClip ||
            m_SelectedPropertyTrackIndex < 0 || m_SelectedPropertyTrackIndex >= (int)state.PropertyTracks.size())
            return false;

        auto& track = state.PropertyTracks[m_SelectedPropertyTrackIndex];
        if (m_EditingPropertyField == PropertyEditField::TargetPath)
        {
            track.EntityPath = m_PropertyEditBuffer.empty() ? "." : m_PropertyEditBuffer;
            CommitGraphEdit(*animator);
            CancelPropertyEdit();
            return true;
        }

        if (m_SelectedPropertyKeyIndex < 0 || m_SelectedPropertyKeyIndex >= (int)track.Keys.size())
            return false;

        char* end = nullptr;
        const float value = std::strtof(m_PropertyEditBuffer.c_str(), &end);
        if (end == m_PropertyEditBuffer.c_str())
            return false;

        auto& key = track.Keys[m_SelectedPropertyKeyIndex];
        switch (m_EditingPropertyField)
        {
            case PropertyEditField::KeyTime:
                key.TimeSeconds = std::clamp(value, 0.0f, GetSelectedStateDurationSeconds(*animator, state));
                break;
            case PropertyEditField::ValueX: key.Value.x = value; break;
            case PropertyEditField::ValueY: key.Value.y = value; break;
            case PropertyEditField::ValueZ: key.Value.z = value; break;
            case PropertyEditField::ValueW: key.Value.w = value; break;
            default: break;
        }

        key.Selected = true;
        std::stable_sort(track.Keys.begin(), track.Keys.end(), [](const auto& a, const auto& b) { return a.TimeSeconds < b.TimeSeconds; });
        for (int i = 0; i < (int)track.Keys.size(); ++i)
        {
            if (track.Keys[i].Selected)
            {
                m_SelectedPropertyKeyIndex = i;
                break;
            }
        }
        CommitGraphEdit(*animator);
        ApplyPropertyClipPreview(state, layer->StateTime);
        CancelPropertyEdit();
        return true;
    }

    void AnimatorGraphPanel::CancelPropertyEdit()
    {
        m_EditingPropertyField = PropertyEditField::None;
        m_PropertyEditBuffer.clear();
    }

    DirectX::XMFLOAT4 AnimatorGraphPanel::CapturePropertyValue(const AnimatorComponent::State::PropertyTrack& track) const
    {
        Entity target = m_TargetEntity;
        if (!track.EntityPath.empty() && track.EntityPath != "." && m_TargetEntity.GetScene())
            target = m_TargetEntity.GetScene()->FindEntityByName(track.EntityPath);
        if (!target)
            return {};

        if (track.ComponentName == "Transform" && target.HasComponent<TransformComponent>())
        {
            const auto& transform = target.GetComponent<TransformComponent>();
            if (track.PropertyName == "Position")
                return { transform.Translation.x, transform.Translation.y, transform.Translation.z, 0.0f };
            if (track.PropertyName == "Rotation")
                return { transform.Rotation.x, transform.Rotation.y, transform.Rotation.z, 0.0f };
            if (track.PropertyName == "Scale")
                return { transform.Scale.x, transform.Scale.y, transform.Scale.z, 0.0f };
        }
        if (track.ComponentName == "Active")
        {
            const bool active = !target.HasComponent<ActiveComponent>() || target.GetComponent<ActiveComponent>().ActiveSelf;
            return { active ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
        }
        if (track.ComponentName == "Material" && target.HasComponent<MeshComponent>())
        {
            auto& mesh = target.GetComponent<MeshComponent>();
            return mesh.Material ? mesh.Material->AlbedoColor : mesh.BaseColor;
        }
        if (track.ComponentName == "Light" && target.HasComponent<LightComponent>())
        {
            const auto& light = target.GetComponent<LightComponent>();
            if (track.PropertyName == "Intensity")
                return { light.Intensity, 0.0f, 0.0f, 0.0f };
            if (track.PropertyName == "Color")
                return { light.LightColor.x, light.LightColor.y, light.LightColor.z, 0.0f };
        }
        if (track.ComponentName == "Camera" && target.HasComponent<CameraComponent>())
            return { target.GetComponent<CameraComponent>().FOV, 0.0f, 0.0f, 0.0f };
        if (track.ComponentName == "Script" && target.HasComponent<ScriptComponent>())
            return { target.GetComponent<ScriptComponent>().Enabled ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
        if (track.ComponentName == "Audio" && target.HasComponent<AudioComponent>())
        {
            const auto& audio = target.GetComponent<AudioComponent>();
            if (track.PropertyName == "Volume")
                return { audio.Volume, 0.0f, 0.0f, 0.0f };
            if (track.PropertyName == "Pitch")
                return { audio.Pitch, 0.0f, 0.0f, 0.0f };
            if (track.PropertyName == "PlayTrigger")
                return { 0.0f, 0.0f, 0.0f, 0.0f };
        }
        return {};
    }

    void AnimatorGraphPanel::ApplyPropertyTrackPreview(const AnimatorComponent::State::PropertyTrack& track, float timeSeconds)
    {
        Entity target = m_TargetEntity;
        if (!track.EntityPath.empty() && track.EntityPath != "." && m_TargetEntity.GetScene())
            target = m_TargetEntity.GetScene()->FindEntityByName(track.EntityPath);
        if (!target || track.Keys.empty())
            return;

        const DirectX::XMFLOAT4 value = EvaluatePropertyKeysForEditor(track.Keys, timeSeconds);
        if (track.ComponentName == "Transform" && target.HasComponent<TransformComponent>())
        {
            auto& transform = target.GetComponent<TransformComponent>();
            if (track.PropertyName == "Position")
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
        if (track.ComponentName == "Material" && target.HasComponent<MeshComponent>())
        {
            auto& mesh = target.GetComponent<MeshComponent>();
            if (mesh.Material)
                mesh.Material->AlbedoColor = value;
            else
                mesh.BaseColor = value;
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
        if (track.ComponentName == "Camera" && target.HasComponent<CameraComponent>())
            target.GetComponent<CameraComponent>().FOV = value.x;
        if (track.ComponentName == "Script" && target.HasComponent<ScriptComponent>())
            target.GetComponent<ScriptComponent>().Enabled = value.x >= 0.5f;
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
                audio.RuntimePlayRequested = audio.RuntimePlayRequested || risingEdge;
                audio.RuntimeLastPlaySignal = value.x;
            }
        }
    }

    void AnimatorGraphPanel::ApplyPropertyClipPreview(const AnimatorComponent::State& state, float timeSeconds)
    {
        for (const auto& track : state.PropertyTracks)
            ApplyPropertyTrackPreview(track, timeSeconds);
    }

    bool AnimatorGraphPanel::EnsureSelectedBlendTree(AnimatorComponent& animator)
    {
        auto* layer = GetActiveLayer(animator);
        if (!layer || m_SelectedStateIndex < 0 || m_SelectedStateIndex >= (int)layer->States.size())
            return false;

        auto& state = layer->States[m_SelectedStateIndex];
        if (state.Motion != AnimatorComponent::State::MotionType::BlendTree)
        {
            // 기존 Clip State를 Blend Tree로 바꿀 때 기존 클립을 첫 자식으로 넣는다.
            // 이렇게 해야 사용자가 Tree 버튼을 눌러도 현재 애니메이션 선택이 사라지지 않는다.
            AnimatorComponent::State::BlendTreeChild firstChild;
            firstChild.ClipIndex = state.ClipIndex;
            firstChild.Weight = 1.0f;
            state.Tree.Children.clear();
            state.Tree.Children.push_back(firstChild);
            state.Motion = AnimatorComponent::State::MotionType::BlendTree;
            state.Tree.TreeType = AnimatorComponent::State::BlendTree::Type::Direct;
            state.Tree.ParameterX = PickFirstFloatParameter(animator);
            state.Tree.ParameterY = state.Tree.ParameterX;
            m_SelectedBlendChildIndex = 0;
        }
        else if (state.Tree.Children.empty() && state.ClipIndex >= 0)
        {
            AnimatorComponent::State::BlendTreeChild child;
            child.ClipIndex = state.ClipIndex;
            child.Weight = 1.0f;
            state.Tree.Children.push_back(child);
            m_SelectedBlendChildIndex = 0;
        }
        return true;
    }

    void AnimatorGraphPanel::AddBlendTreeChild(int clipIndex, const std::string& clipName)
    {
        AnimatorComponent* animator = GetAnimator();
        auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer || m_SelectedStateIndex < 0 || m_SelectedStateIndex >= (int)layer->States.size())
            return;
        if (!EnsureSelectedBlendTree(*animator))
            return;

        auto& state = layer->States[m_SelectedStateIndex];
        AnimatorComponent::State::BlendTreeChild child;
        child.ClipIndex = (std::max)(0, clipIndex);
        child.Weight = 1.0f;
        child.Threshold = (float)state.Tree.Children.size() * 0.25f;
        child.Position = { child.Threshold, 0.0f };
        state.Tree.Children.push_back(child);
        m_SelectedBlendChildIndex = (int)state.Tree.Children.size() - 1;
        AutoLayoutBlendTreeChildren(state);
        if (!clipName.empty() && state.Name == "State")
            state.Name = "Blend " + clipName;
        CommitGraphEdit(*animator);
        ResetRuntime(*animator);
    }

    void AnimatorGraphPanel::ReplaceBlendTreeChild(int stateIndex, int childIndex, int clipIndex, const std::string& clipName)
    {
        AnimatorComponent* animator = GetAnimator();
        auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer || stateIndex < 0 || stateIndex >= (int)layer->States.size())
            return;

        auto& state = layer->States[stateIndex];
        if (state.Motion != AnimatorComponent::State::MotionType::BlendTree || childIndex < 0 || childIndex >= (int)state.Tree.Children.size())
            return;

        state.Tree.Children[childIndex].ClipIndex = (std::max)(0, clipIndex);
        m_SelectedBlendChildIndex = childIndex;
        if (!clipName.empty() && state.Tree.Children.size() == 1)
            state.Name = "Blend " + clipName;
        CommitGraphEdit(*animator);
        ResetRuntime(*animator);
    }

    void AnimatorGraphPanel::AutoLayoutBlendTreeChildren(AnimatorComponent::State& state) const
    {
        const int count = (int)state.Tree.Children.size();
        if (count <= 0)
            return;

        if (state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::Direct)
            return;

        if (state.Tree.TreeType == AnimatorComponent::State::BlendTree::Type::OneD)
        {
            for (int i = 0; i < count; ++i)
            {
                const float ratio = count == 1 ? 0.5f : (float)i / (float)(count - 1);
                state.Tree.Children[i].Threshold = ratio * 2.0f - 1.0f;
                state.Tree.Children[i].Position = { state.Tree.Children[i].Threshold, 0.0f };
            }
            return;
        }

        const float radius = 0.72f;
        for (int i = 0; i < count; ++i)
        {
            const float angle = count == 1 ? 0.0f : (6.2831853f * (float)i / (float)count);
            state.Tree.Children[i].Position = { std::cos(angle) * radius, std::sin(angle) * radius };
            state.Tree.Children[i].Threshold = state.Tree.Children[i].Position.x;
        }
    }

    void AnimatorGraphPanel::CycleBlendTreeParameter(AnimatorComponent& animator, bool parameterX)
    {
        auto* layer = GetActiveLayer(animator);
        if (!layer || m_SelectedStateIndex < 0 || m_SelectedStateIndex >= (int)layer->States.size())
            return;
        auto& parameterName = parameterX ? layer->States[m_SelectedStateIndex].Tree.ParameterX : layer->States[m_SelectedStateIndex].Tree.ParameterY;

        std::vector<std::string> names;
        for (const auto& parameter : animator.Parameters)
        {
            if (parameter.ParamType == AnimatorComponent::Parameter::Type::Float)
                names.push_back(parameter.Name);
        }
        if (names.empty())
        {
            parameterName.clear();
            return;
        }

        int index = -1;
        for (int i = 0; i < (int)names.size(); ++i)
        {
            if (names[i] == parameterName)
            {
                index = i;
                break;
            }
        }
        parameterName = names[(index + 1) % (int)names.size()];
    }

    void AnimatorGraphPanel::BeginBlendTreeValueEdit(BlendTreeValueField field, int childIndex, float currentValue)
    {
        // 숫자 편집은 child index와 field를 같이 잡아 둔다.
        // 입력 중 선택이 바뀌어도 처음 누른 값만 바꾸기 위해서다.
        m_EditingBlendField = field;
        m_EditingBlendChildIndex = childIndex;
        m_BlendValueEditBuffer = FormatBlendValue(currentValue);
        Widget::SetKeyboardFocus(this);
    }

    bool AnimatorGraphPanel::CommitBlendTreeValueEdit()
    {
        if (m_EditingBlendField == BlendTreeValueField::None)
            return false;

        char* end = nullptr;
        const float value = std::strtof(m_BlendValueEditBuffer.c_str(), &end);
        if (end == m_BlendValueEditBuffer.c_str())
        {
            CancelBlendTreeValueEdit();
            return false;
        }

        AnimatorComponent* animator = GetAnimator();
        const bool applied = animator && ApplyBlendTreeValueEdit(*animator, m_EditingBlendField, m_EditingBlendChildIndex, value);
        if (applied)
            CommitGraphEdit(*animator);

        CancelBlendTreeValueEdit();
        return applied;
    }

    void AnimatorGraphPanel::CancelBlendTreeValueEdit()
    {
        m_EditingBlendField = BlendTreeValueField::None;
        m_EditingBlendChildIndex = -1;
        m_BlendValueEditBuffer.clear();
    }

    bool AnimatorGraphPanel::ApplyBlendTreeValueEdit(AnimatorComponent& animator, BlendTreeValueField field, int childIndex, float value)
    {
        auto* layer = GetActiveLayer(animator);
        if (!layer || m_SelectedStateIndex < 0 || m_SelectedStateIndex >= (int)layer->States.size())
            return false;

        auto& state = layer->States[m_SelectedStateIndex];
        if (state.Motion != AnimatorComponent::State::MotionType::BlendTree ||
            childIndex < 0 || childIndex >= (int)state.Tree.Children.size())
            return false;

        auto& child = state.Tree.Children[childIndex];
        switch (field)
        {
            case BlendTreeValueField::DirectWeight:
                child.Weight = std::clamp(value, 0.0f, 1.0f);
                return true;
            case BlendTreeValueField::Threshold:
                child.Threshold = std::clamp(value, -1.0f, 1.0f);
                child.Position.x = child.Threshold;
                return true;
            case BlendTreeValueField::PositionX:
                child.Position.x = std::clamp(value, -1.0f, 1.0f);
                child.Threshold = child.Position.x;
                return true;
            case BlendTreeValueField::PositionY:
                child.Position.y = std::clamp(value, -1.0f, 1.0f);
                return true;
            default:
                return false;
        }
    }

    bool AnimatorGraphPanel::TryPickClipFromDroppedAsset(const std::string& filepath, int& outClipIndex, std::string& outClipName) const
    {
        const AnimatorComponent* animator = GetAnimator();
        if (!animator || animator->SourcePath.empty())
            return false;

        std::error_code ecA;
        std::error_code ecB;
        const std::filesystem::path dropped = std::filesystem::weakly_canonical(filepath, ecA);
        const std::filesystem::path source = std::filesystem::weakly_canonical(animator->SourcePath, ecB);
        if (ecA || ecB || dropped != source)
            return false;

        const auto clips = InspectSourceClips(*animator);
        if (clips.empty())
            return false;

        outClipIndex = (int)clips.front().Index;
        outClipName = clips.front().Name;
        return true;
    }

    float AnimatorGraphPanel::GetBlendTreeMaxScroll(const AnimatorComponent::State& state, float canvasH) const
    {
        if (state.Motion != AnimatorComponent::State::MotionType::BlendTree ||
            state.Tree.TreeType != AnimatorComponent::State::BlendTree::Type::Direct)
            return 0.0f;

        const float visibleH = (std::max)(0.0f, canvasH - 136.0f);
        const float contentH = 58.0f + (float)state.Tree.Children.size() * 40.0f;
        return (std::max)(0.0f, contentH - visibleH);
    }

    std::string AnimatorGraphPanel::GetClipDisplayName(const AnimatorComponent& animator, int clipIndex) const
    {
        const auto clips = InspectSourceClips(animator);
        if (clipIndex >= 0 && clipIndex < (int)clips.size())
            return clips[clipIndex].Name;
        return "Clip " + std::to_string(clipIndex);
    }

    void AnimatorGraphPanel::AddTransition(int fromStateIndex, int toStateIndex)
    {
        AnimatorComponent* animator = GetAnimator();
        auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || fromStateIndex < 0 || toStateIndex < 0 || fromStateIndex == toStateIndex ||
            !layer || fromStateIndex >= (int)layer->States.size() || toStateIndex >= (int)layer->States.size())
            return;

        auto exists = std::find_if(layer->Transitions.begin(), layer->Transitions.end(), [fromStateIndex, toStateIndex](const AnimatorComponent::Transition& transition)
        {
            return transition.FromStateIndex == fromStateIndex && transition.ToStateIndex == toStateIndex;
        });
        if (exists != layer->Transitions.end())
            return;

        AnimatorComponent::Transition transition;
        transition.FromStateIndex = fromStateIndex;
        transition.ToStateIndex = toStateIndex;
        layer->Transitions.push_back(transition);
        m_SelectedTransitionIndex = (int)layer->Transitions.size() - 1;
        layer->SelectedTransitionIndex = m_SelectedTransitionIndex;
        m_SelectedStateIndices.clear();
        CommitGraphEdit(*animator);
    }

    void AnimatorGraphPanel::DeleteSelectedState()
    {
        AnimatorComponent* animator = GetAnimator();
        auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer || m_SelectedStateIndex < 0 || m_SelectedStateIndex >= (int)layer->States.size())
            return;

        std::vector<int> removedIndices = m_SelectedStateIndices;
        if (removedIndices.empty() || !IsStateSelected(m_SelectedStateIndex))
            removedIndices.push_back(m_SelectedStateIndex);
        removedIndices.erase(
            std::remove_if(removedIndices.begin(), removedIndices.end(), [layer](int index)
            {
                return index < 0 || index >= (int)layer->States.size();
            }),
            removedIndices.end());
        std::sort(removedIndices.begin(), removedIndices.end());
        removedIndices.erase(std::unique(removedIndices.begin(), removedIndices.end()), removedIndices.end());
        if (removedIndices.empty())
            return;

        auto isRemoved = [&removedIndices](int index)
        {
            return std::binary_search(removedIndices.begin(), removedIndices.end(), index);
        };

        layer->Transitions.erase(
            std::remove_if(layer->Transitions.begin(), layer->Transitions.end(), [&isRemoved](const AnimatorComponent::Transition& transition)
            {
                return isRemoved(transition.FromStateIndex) || isRemoved(transition.ToStateIndex);
            }),
            layer->Transitions.end());

        for (auto it = removedIndices.rbegin(); it != removedIndices.rend(); ++it)
            layer->States.erase(layer->States.begin() + *it);

        for (auto& transition : layer->Transitions)
        {
            // 상태를 여러 개 지우면 뒤쪽 인덱스가 앞으로 당겨진다.
            // 전이는 상태 번호만 들고 있으므로, 삭제된 상태보다 앞에 남은 개수만큼 다시 보정한다.
            const int fromShift = (int)std::count_if(removedIndices.begin(), removedIndices.end(), [&transition](int removed) { return removed < transition.FromStateIndex; });
            const int toShift = (int)std::count_if(removedIndices.begin(), removedIndices.end(), [&transition](int removed) { return removed < transition.ToStateIndex; });
            transition.FromStateIndex -= fromShift;
            transition.ToStateIndex -= toShift;
        }
        m_SelectedTransitionIndex = -1;
        layer->SelectedTransitionIndex = -1;
        m_SelectedStateIndices.clear();

        if (layer->States.empty())
        {
            layer->ActiveStateIndex = -1;
            layer->EntryStateIndex = -1;
            m_SelectedStateIndex = -1;
        }
        else
        {
            layer->ActiveStateIndex = std::clamp(m_SelectedStateIndex, 0, (int)layer->States.size() - 1);
            layer->EntryStateIndex = std::clamp(layer->EntryStateIndex, 0, (int)layer->States.size() - 1);
            m_SelectedStateIndex = layer->ActiveStateIndex;
            m_SelectedStateIndices.push_back(m_SelectedStateIndex);
        }
        CommitGraphEdit(*animator);
        ResetRuntime(*animator);
    }

    void AnimatorGraphPanel::SelectOnlyState(AnimatorComponent& animator, int stateIndex)
    {
        auto* layer = GetActiveLayer(animator);
        if (!layer || stateIndex < 0 || stateIndex >= (int)layer->States.size())
        {
            ClearStateSelection(animator);
            return;
        }

        m_SelectedStateIndices.clear();
        m_SelectedStateIndices.push_back(stateIndex);
        m_SelectedStateIndex = stateIndex;
        m_SelectedTransitionIndex = -1;
        m_SelectedBlendChildIndex = -1;
        m_SelectedPropertyTrackIndex = -1;
        m_SelectedPropertyKeyIndex = -1;
        CancelStateRename();
        layer->ActiveStateIndex = stateIndex;
        layer->SelectedTransitionIndex = -1;
        SyncBaseLayerToLegacyGraph(animator);
    }

    void AnimatorGraphPanel::ClearStateSelection(AnimatorComponent& animator)
    {
        auto* layer = GetActiveLayer(animator);
        m_SelectedStateIndices.clear();
        m_SelectedStateIndex = -1;
        m_SelectedTransitionIndex = -1;
        m_SelectedBlendChildIndex = -1;
        m_SelectedPropertyTrackIndex = -1;
        m_SelectedPropertyKeyIndex = -1;
        CancelStateRename();
        if (layer)
            layer->SelectedTransitionIndex = -1;
        SyncBaseLayerToLegacyGraph(animator);
    }

    void AnimatorGraphPanel::SelectStatesInBox(AnimatorComponent& animator)
    {
        auto* layer = GetActiveLayer(animator);
        if (!layer)
            return;

        const float minX = (std::min)(m_BoxSelectStart.x, m_BoxSelectEnd.x);
        const float minY = (std::min)(m_BoxSelectStart.y, m_BoxSelectEnd.y);
        const float maxX = (std::max)(m_BoxSelectStart.x, m_BoxSelectEnd.x);
        const float maxY = (std::max)(m_BoxSelectStart.y, m_BoxSelectEnd.y);

        m_SelectedStateIndices.clear();
        for (int i = 0; i < (int)layer->States.size(); ++i)
        {
            const StateNodeRect r = GetStateRect(i);
            const bool intersects = r.X <= maxX && r.X + r.W >= minX && r.Y <= maxY && r.Y + r.H >= minY;
            if (intersects)
                m_SelectedStateIndices.push_back(i);
        }

        if (m_SelectedStateIndices.empty())
        {
            // 선택이 비어도 재생용 ActiveStateIndex는 유지한다.
            // 빈 공간을 클릭했을 뿐인데 런타임 상태까지 사라지면 Preview/Play가 멈춘다.
            m_SelectedStateIndex = -1;
        }
        else
        {
            // 다중 선택에서도 대표 상태 하나는 유지한다.
            // Inspector/사이드바는 대표 상태를 보여 주고, 그래프는 선택 목록 전체를 강조한다.
            m_SelectedStateIndex = m_SelectedStateIndices.back();
            layer->ActiveStateIndex = m_SelectedStateIndex;
        }
        layer->SelectedTransitionIndex = -1;
        m_SelectedTransitionIndex = -1;
        SyncBaseLayerToLegacyGraph(animator);
    }

    bool AnimatorGraphPanel::IsStateSelected(int stateIndex) const
    {
        return std::find(m_SelectedStateIndices.begin(), m_SelectedStateIndices.end(), stateIndex) != m_SelectedStateIndices.end();
    }

    void AnimatorGraphPanel::AddParameter(AnimatorComponent::Parameter::Type type)
    {
        AnimatorComponent* animator = GetAnimator();
        if (!animator)
            return;

        AnimatorComponent::Parameter parameter;
        parameter.ParamType = type;
        parameter.Name = std::string(ParameterTypeName(type)) + " " + std::to_string(animator->Parameters.size() + 1);
        animator->Parameters.push_back(parameter);
        m_ParameterEditMessage = "Added " + parameter.Name + ". Click a parameter row to rename.";
        CommitGraphEdit(*animator);
    }

    bool AnimatorGraphPanel::BeginParameterRename(int parameterIndex)
    {
        AnimatorComponent* animator = GetAnimator();
        if (!animator || parameterIndex < 0 || parameterIndex >= (int)animator->Parameters.size())
            return false;

        m_EditingParameterIndex = parameterIndex;
        m_ParameterEditBuffer = animator->Parameters[parameterIndex].Name;
        m_ParameterEditMessage = "Enter to apply, Esc to cancel.";
        Widget::SetKeyboardFocus(this);
        return true;
    }

    bool AnimatorGraphPanel::CommitParameterRename()
    {
        AnimatorComponent* animator = GetAnimator();
        if (!animator || m_EditingParameterIndex < 0 || m_EditingParameterIndex >= (int)animator->Parameters.size())
            return false;

        const std::string newName = TrimName(m_ParameterEditBuffer);
        if (!IsValidParameterName(*animator, newName, m_EditingParameterIndex))
        {
            m_ParameterEditMessage = "Invalid or duplicate parameter name.";
            return false;
        }

        const std::string oldName = animator->Parameters[m_EditingParameterIndex].Name;
        animator->Parameters[m_EditingParameterIndex].Name = newName;
        RenameTransitionParameterReferences(*animator, oldName, newName);
        CommitGraphEdit(*animator);
        m_ParameterEditMessage = "Renamed parameter to " + newName + ".";
        CancelParameterRename();
        return true;
    }

    void AnimatorGraphPanel::CancelParameterRename()
    {
        m_EditingParameterIndex = -1;
        m_ParameterEditBuffer.clear();
    }

    bool AnimatorGraphPanel::IsValidParameterName(const AnimatorComponent& animator, const std::string& name, int editingIndex) const
    {
        if (name.empty() || name == "Entry" || name == "Exit" || name == "Any State")
            return false;
        for (int i = 0; i < (int)animator.Parameters.size(); ++i)
        {
            if (i != editingIndex && animator.Parameters[i].Name == name)
                return false;
        }
        return true;
    }

    bool AnimatorGraphPanel::BeginStateRename(int stateIndex)
    {
        AnimatorComponent* animator = GetAnimator();
        auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer || stateIndex < 0 || stateIndex >= (int)layer->States.size())
            return false;

        m_EditingStateNameIndex = stateIndex;
        m_StateNameEditBuffer = layer->States[stateIndex].Name;
        m_StateEditMessage = "Enter to apply, Esc to cancel.";
        Widget::SetKeyboardFocus(this);
        return true;
    }

    bool AnimatorGraphPanel::CommitStateRename()
    {
        AnimatorComponent* animator = GetAnimator();
        auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer || m_EditingStateNameIndex < 0 || m_EditingStateNameIndex >= (int)layer->States.size())
            return false;

        const std::string newName = TrimName(m_StateNameEditBuffer);
        if (!IsValidStateName(*layer, newName, m_EditingStateNameIndex))
        {
            m_StateEditMessage = "Invalid or duplicate state name.";
            return false;
        }

        auto& state = layer->States[m_EditingStateNameIndex];
        state.Name = newName;
        if (state.ImportSettings.DisplayName.empty())
            state.ImportSettings.DisplayName = newName;
        CommitGraphEdit(*animator);
        m_StateEditMessage = "Renamed state to " + newName + ".";
        CancelStateRename();
        return true;
    }

    void AnimatorGraphPanel::CancelStateRename()
    {
        m_EditingStateNameIndex = -1;
        m_StateNameEditBuffer.clear();
    }

    bool AnimatorGraphPanel::IsValidStateName(const AnimatorComponent::Layer& layer, const std::string& name, int editingIndex) const
    {
        if (name.empty() || name == "Entry" || name == "Exit" || name == "Any State")
            return false;
        for (int i = 0; i < (int)layer.States.size(); ++i)
        {
            if (i != editingIndex && layer.States[i].Name == name)
                return false;
        }
        return true;
    }

    void AnimatorGraphPanel::RenameTransitionParameterReferences(AnimatorComponent& animator, const std::string& oldName, const std::string& newName) const
    {
        // 조건은 파라미터 이름을 직접 들고 있다. 이름을 바꾸면 Transition 조건도 함께 바꿔야 저장 후 깨지지 않는다.
        for (auto& transition : animator.Transitions)
        {
            for (auto& condition : transition.Conditions)
            {
                if (condition.ParameterName == oldName)
                    condition.ParameterName = newName;
            }
        }
        for (auto& layer : animator.Layers)
        {
            for (auto& transition : layer.Transitions)
            {
                for (auto& condition : transition.Conditions)
                {
                    if (condition.ParameterName == oldName)
                        condition.ParameterName = newName;
                }
            }
        }
    }

    void AnimatorGraphPanel::ClampAnimatorSelection(AnimatorComponent& animator) const
    {
        EnsureAnimatorLayers(animator);
        SyncBaseLayerToLegacyGraph(animator);
    }

    void AnimatorGraphPanel::ResetRuntime(AnimatorComponent& animator) const
    {
        ClearAnimatorRuntimeCache(animator);
    }

    void AnimatorGraphPanel::PropagateSharedControllerEdit(AnimatorComponent& editedAnimator) const
    {
        std::filesystem::path controllerPath = ResolveControllerPath(editedAnimator);
        if (controllerPath.empty() || !m_TargetEntity || !m_TargetEntity.GetScene())
            return;

        const std::string sourceKey = NormalizeControllerPathKey(controllerPath);
        if (sourceKey.empty())
            return;

        auto view = m_TargetEntity.GetScene()->GetRegistry().view<AnimatorComponent>();
        for (auto entityID : view)
        {
            AnimatorComponent& otherAnimator = view.get<AnimatorComponent>(entityID);
            if (&otherAnimator == &editedAnimator)
                continue;

            const std::filesystem::path otherPath = ResolveControllerPath(otherAnimator);
            if (otherPath.empty() || NormalizeControllerPathKey(otherPath) != sourceKey)
                continue;

            // Controller는 여러 오브젝트가 공유하는 원본 에셋이다.
            // 한 그래프 창에서 저장한 뒤 같은 Controller를 이미 들고 있던 Animator도 재로드해야
            // Unity처럼 "에셋 하나 수정 = 모든 참조자 갱신" 규칙이 유지된다.
            AnimatorControllerAsset::LoadFromFile(controllerPath, otherAnimator);
            ClearAnimatorRuntimeCache(otherAnimator);
            ClampAnimatorSelection(otherAnimator);
        }
    }

    void AnimatorGraphPanel::CaptureCommittedAnimator(const AnimatorComponent& animator)
    {
        m_LastCommittedAnimator = animator;
        ClearAnimatorRuntimeCache(m_LastCommittedAnimator);
        m_HasCommittedAnimator = true;
    }

    void AnimatorGraphPanel::CommitGraphEdit(AnimatorComponent& animator)
    {
        // Undo는 바뀐 뒤의 값이 아니라, 바뀌기 직전의 확정 상태를 되돌린다.
        // 그래서 마지막 확정 스냅샷을 Undo 스택에 넣고 현재 값을 새 기준점으로 갱신한다.
        if (m_HasCommittedAnimator)
            m_UndoStack.push_back(m_LastCommittedAnimator);
        m_RedoStack.clear();
        CommitAnimatorGraphChange(animator);
        PropagateSharedControllerEdit(animator);
        CaptureCommittedAnimator(animator);
    }

    bool AnimatorGraphPanel::UndoGraphEdit(AnimatorComponent& animator)
    {
        if (m_UndoStack.empty())
            return false;

        AnimatorComponent redoSnapshot = animator;
        ClearAnimatorRuntimeCache(redoSnapshot);
        m_RedoStack.push_back(redoSnapshot);
        animator = m_UndoStack.back();
        m_UndoStack.pop_back();
        ClearAnimatorRuntimeCache(animator);
        CommitAnimatorGraphChange(animator);
        PropagateSharedControllerEdit(animator);
        ResetRuntime(animator);
        CaptureCommittedAnimator(animator);
        ClampAnimatorSelection(animator);
        return true;
    }

    bool AnimatorGraphPanel::RedoGraphEdit(AnimatorComponent& animator)
    {
        if (m_RedoStack.empty())
            return false;

        AnimatorComponent undoSnapshot = animator;
        ClearAnimatorRuntimeCache(undoSnapshot);
        m_UndoStack.push_back(undoSnapshot);
        animator = m_RedoStack.back();
        m_RedoStack.pop_back();
        ClearAnimatorRuntimeCache(animator);
        CommitAnimatorGraphChange(animator);
        PropagateSharedControllerEdit(animator);
        ResetRuntime(animator);
        CaptureCommittedAnimator(animator);
        ClampAnimatorSelection(animator);
        return true;
    }

    float AnimatorGraphPanel::GetSelectedStateRawDurationSeconds(const AnimatorComponent& animator, const AnimatorComponent::State& state)
    {
        const std::string key = animator.SourceAssetGuid + "|" + animator.SourcePath + "|" + std::to_string(state.ClipIndex);
        if (m_TimelineClipKey == key && m_TimelineClipDurationSeconds > 0.0f)
            return m_TimelineClipDurationSeconds;

        m_TimelineClipKey = key;
        m_TimelineClipDurationSeconds = 1.0f;
        const auto clips = InspectSourceClips(animator);
        auto clipIt = std::find_if(clips.begin(), clips.end(), [&state](const AnimationClipInfo& clip)
        {
            return (int)clip.Index == state.ClipIndex;
        });
        if (clipIt != clips.end() && clipIt->TicksPerSecond > 0.0f)
            m_TimelineClipDurationSeconds = (std::max)(0.05f, clipIt->DurationTicks / clipIt->TicksPerSecond);
        return m_TimelineClipDurationSeconds;
    }

    float AnimatorGraphPanel::GetSelectedStateDurationSeconds(const AnimatorComponent& animator, const AnimatorComponent::State& state)
    {
        if (state.Motion == AnimatorComponent::State::MotionType::PropertyClip)
        {
            float duration = 1.0f;
            for (const auto& track : state.PropertyTracks)
            {
                for (const auto& key : track.Keys)
                    duration = (std::max)(duration, key.TimeSeconds);
            }
            return (std::max)(0.05f, duration);
        }

        const float rawDuration = GetSelectedStateRawDurationSeconds(animator, state);
        if (state.Motion == AnimatorComponent::State::MotionType::Clip && state.ClipIndex < 0)
            return 1.0f;
        if (!state.ImportSettings.UseCustomRange)
            return rawDuration;

        const float start = std::clamp(state.ImportSettings.StartSeconds, 0.0f, rawDuration);
        const float end = std::clamp(state.ImportSettings.EndSeconds <= 0.0f ? rawDuration : state.ImportSettings.EndSeconds, start, rawDuration);
        // State 시간은 Import Range 안에서 흐르는 "부분 클립 시간"이다.
        // 원본 클립 길이를 그대로 쓰면 잘라낸 구간 이후까지 재생되어 런타임 샘플 시간과 타임라인이 어긋난다.
        return (std::max)(0.05f, end - start);
    }

    void AnimatorGraphPanel::ApplyTimelinePreview(AnimatorComponent& animator, AnimatorComponent::State& state, float timeSeconds)
    {
        auto* layer = GetActiveLayer(animator);
        if (!layer)
            return;

        const float duration = (std::max)(0.05f, GetSelectedStateDurationSeconds(animator, state));
        layer->ActiveStateIndex = m_SelectedStateIndex;
        layer->StateTime = std::clamp(timeSeconds, 0.0f, duration);
        layer->PreviousLoopTime = layer->StateTime;
        layer->FiredEventIndices.clear();
        animator.PreviewInEdit = true;
        animator.ActiveStateIndex = layer->ActiveStateIndex;
        if (state.Motion == AnimatorComponent::State::MotionType::PropertyClip)
        {
            ApplyPropertyClipPreview(state, layer->StateTime);
            SyncBaseLayerToLegacyGraph(animator);
            return;
        }
        if (state.Motion == AnimatorComponent::State::MotionType::Clip && state.ClipIndex < 0)
        {
            animator.RuntimeClip.reset();
            animator.RuntimeClipKey.clear();
            animator.AnimPlayer.StopAnimation();
            SyncBaseLayerToLegacyGraph(animator);
            return;
        }
        animator.SelectedClipIndex = state.ClipIndex;
        animator.SelectedClipName = state.Name;
        SyncBaseLayerToLegacyGraph(animator);

        // 스크럽은 현재 State의 시간만 바꾼 뒤 같은 클립을 0초 업데이트로 평가한다.
        // 자동 재생을 켜지 않기 때문에 타임라인을 놓은 뒤에도 클립이 혼자 계속 흐르지 않는다.
        std::filesystem::path sourcePath = animator.SourcePath;
        if (!animator.SourceAssetGuid.empty())
        {
            std::filesystem::path guidPath = AssetDatabase::GetPathFromGuid(animator.SourceAssetGuid);
            if (!guidPath.empty())
                sourcePath = guidPath;
        }
        if (!animator.RuntimeClip && !sourcePath.empty())
            animator.RuntimeClip = AnimationClip::LoadShared(sourcePath.string(), (uint32_t)(std::max)(0, state.ClipIndex));
        if (!animator.RuntimeClip)
            return;

        animator.AnimPlayer.PlayAnimation(animator.RuntimeClip, false);
        animator.AnimPlayer.SetCurrentTime(layer->StateTime * animator.RuntimeClip->GetTicksPerSecond());

        Entity current = m_TargetEntity;
        while (current && current.HasComponent<RelationshipComponent>() && !current.HasComponent<ModelComponent>())
        {
            entt::entity parentID = current.GetComponent<RelationshipComponent>().Parent;
            if (parentID == entt::null)
                break;
            current = { parentID, current.GetScene() };
        }
        if (current && current.HasComponent<ModelComponent>())
        {
            auto& modelComponent = current.GetComponent<ModelComponent>();
            if (modelComponent.TargetModel)
            {
                const auto& nodeMap = modelComponent.NodePathEntityMap.empty() ? modelComponent.NodeEntityMap : modelComponent.NodePathEntityMap;
                animator.AnimPlayer.Update(0.0f, modelComponent.TargetModel.get(), current.GetScene(), &nodeMap);
            }
        }
    }

    AnimatorGraphPanel::StateNodeRect AnimatorGraphPanel::GetStateRect(int stateIndex) const
    {
        const AnimatorComponent* animator = GetAnimator();
        const auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer || stateIndex < 0 || stateIndex >= (int)layer->States.size())
            return {};

        DirectX::XMFLOAT2 screen = GraphToScreen(layer->States[stateIndex].GraphPosition);
        // 줌은 노드 간 거리만이 아니라 노드 자체 크기에도 반영되어야 한다.
        // 최소값은 클릭 가능한 크기만 보장하고, 실제 확대/축소 느낌은 유지한다.
        const float screenW = (std::max)(118.0f, m_NodeWidth * m_Zoom);
        const float screenH = (std::max)(48.0f, m_NodeHeight * m_Zoom);
        return { stateIndex, screen.x, screen.y, screenW, screenH };
    }

    int AnimatorGraphPanel::GetStateAt(float mouseX, float mouseY) const
    {
        const AnimatorComponent* animator = GetAnimator();
        const auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer)
            return -1;

        for (int i = (int)layer->States.size() - 1; i >= 0; --i)
        {
            StateNodeRect r = GetStateRect(i);
            if (IsPointInRect(mouseX, mouseY, r.X, r.Y, r.W, r.H))
                return i;
        }
        return -1;
    }

    int AnimatorGraphPanel::GetTransitionAt(float mouseX, float mouseY) const
    {
        const AnimatorComponent* animator = GetAnimator();
        const auto* layer = animator ? GetActiveLayer(*animator) : nullptr;
        if (!animator || !layer)
            return -1;

        for (int i = (int)layer->Transitions.size() - 1; i >= 0; --i)
        {
            const auto& transition = layer->Transitions[i];
            if (!IsValidAnimatorStateEndpoint(*layer, transition.FromStateIndex) ||
                !IsValidAnimatorStateEndpoint(*layer, transition.ToStateIndex))
                continue;

            auto endpoint = [this](int stateIndex, bool source)
            {
                if (stateIndex >= 0)
                {
                    const StateNodeRect r = GetStateRect(stateIndex);
                    return source
                        ? DirectX::XMFLOAT2{ r.X + r.W, r.Y + r.H * 0.5f }
                        : DirectX::XMFLOAT2{ r.X, r.Y + r.H * 0.5f };
                }

                DirectX::XMFLOAT2 p = GraphToScreen(GetSpecialAnimatorNodeGraphPosition(stateIndex));
                return source
                    ? DirectX::XMFLOAT2{ p.x + 112.0f, p.y + 18.0f }
                    : DirectX::XMFLOAT2{ p.x, p.y + 18.0f };
            };

            DirectX::XMFLOAT2 from = endpoint(transition.FromStateIndex, true);
            DirectX::XMFLOAT2 to = endpoint(transition.ToStateIndex, false);
            if (transition.FromStateIndex >= 0 && transition.ToStateIndex >= 0 && to.x < from.x)
            {
                const StateNodeRect fromRect = GetStateRect(transition.FromStateIndex);
                const StateNodeRect toRect = GetStateRect(transition.ToStateIndex);
                from = { fromRect.X + fromRect.W * 0.5f, fromRect.Y + fromRect.H };
                to = { toRect.X + toRect.W * 0.5f, toRect.Y };
            }
            if (DistancePointToSegment(mouseX, mouseY, from, to) <= 8.0f)
                return i;
        }
        return -1;
    }

    std::vector<AnimationClipInfo> AnimatorGraphPanel::InspectSourceClips(const AnimatorComponent& animator) const
    {
        std::filesystem::path sourcePath = animator.SourcePath;
        if (!animator.SourceAssetGuid.empty())
        {
            std::filesystem::path guidPath = AssetDatabase::GetPathFromGuid(animator.SourceAssetGuid);
            if (!guidPath.empty())
                sourcePath = guidPath;
        }
        if (sourcePath.empty())
        {
            const auto* layer = GetActiveLayer(animator);
            if (layer && m_SelectedStateIndex >= 0 && m_SelectedStateIndex < (int)layer->States.size())
            {
                const auto& state = layer->States[m_SelectedStateIndex];
                sourcePath = state.MotionPath;
                if (!state.MotionAssetGuid.empty())
                {
                    std::filesystem::path guidPath = AssetDatabase::GetPathFromGuid(state.MotionAssetGuid);
                    if (!guidPath.empty())
                        sourcePath = guidPath;
                }
            }
        }

        return sourcePath.empty() ? std::vector<AnimationClipInfo>{} : AnimationClip::InspectClips(sourcePath.string());
    }

    DirectX::XMFLOAT2 AnimatorGraphPanel::GraphToScreen(const DirectX::XMFLOAT2& graphPosition) const
    {
        const float graphX = m_CalculatedPos.x + m_SidebarWidth;
        const float graphY = m_CalculatedPos.y + m_TitleContentTop + m_ToolbarHeight;
        return { graphX + (graphPosition.x + m_ViewOffsetX) * m_Zoom, graphY + (graphPosition.y + m_ViewOffsetY) * m_Zoom };
    }

    DirectX::XMFLOAT2 AnimatorGraphPanel::ScreenToGraph(float screenX, float screenY) const
    {
        const float graphX = m_CalculatedPos.x + m_SidebarWidth;
        const float graphY = m_CalculatedPos.y + m_TitleContentTop + m_ToolbarHeight;
        return { (screenX - graphX) / m_Zoom - m_ViewOffsetX, (screenY - graphY) / m_Zoom - m_ViewOffsetY };
    }

    bool AnimatorGraphPanel::IsPointInRect(float mouseX, float mouseY, float x, float y, float w, float h)
    {
        return mouseX >= x && mouseX <= x + w && mouseY >= y && mouseY <= y + h;
    }
}
