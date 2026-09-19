#include "Animation/AnimatorControllerAsset.h"

#include "json.hpp"

#include <algorithm>
#include <fstream>

namespace CCEngine
{
    namespace
    {
        nlohmann::json Float2ToJson(const DirectX::XMFLOAT2& value)
        {
            return { value.x, value.y };
        }

        nlohmann::json Float4ToJson(const DirectX::XMFLOAT4& value)
        {
            return { value.x, value.y, value.z, value.w };
        }

        DirectX::XMFLOAT2 JsonToFloat2(const nlohmann::json& data)
        {
            return { data[0].get<float>(), data[1].get<float>() };
        }

        DirectX::XMFLOAT4 JsonToFloat4(const nlohmann::json& data)
        {
            return { data[0].get<float>(), data[1].get<float>(), data[2].get<float>(), data[3].get<float>() };
        }

        nlohmann::json PropertyTracksToJson(const AnimatorComponent::State& state)
        {
            nlohmann::json tracks = nlohmann::json::array();
            for (const auto& track : state.PropertyTracks)
            {
                nlohmann::json keys = nlohmann::json::array();
                for (const auto& key : track.Keys)
                    keys.push_back({
                        { "TimeSeconds", key.TimeSeconds },
                        { "Value", Float4ToJson(key.Value) },
                        { "Interpolation", static_cast<int>(key.Interp) }
                        });

                tracks.push_back({
                    { "EntityPath", track.EntityPath },
                    { "ComponentName", track.ComponentName },
                    { "PropertyName", track.PropertyName },
                    { "Type", static_cast<int>(track.Type) },
                    { "Keys", keys }
                    });
            }
            return tracks;
        }

        void LoadPropertyTracks(const nlohmann::json& stateData, AnimatorComponent::State& state)
        {
            state.PropertyTracks.clear();
            if (!stateData.contains("PropertyTracks") || !stateData["PropertyTracks"].is_array())
                return;

            for (const auto& trackData : stateData["PropertyTracks"])
            {
                AnimatorComponent::State::PropertyTrack track;
                track.EntityPath = trackData.value("EntityPath", "");
                track.ComponentName = trackData.value("ComponentName", "");
                track.PropertyName = trackData.value("PropertyName", "");
                track.Type = static_cast<AnimatorComponent::State::PropertyTrack::ValueType>(trackData.value("Type", 0));
                if (trackData.contains("Keys") && trackData["Keys"].is_array())
                {
                    for (const auto& keyData : trackData["Keys"])
                    {
                        AnimatorComponent::State::PropertyKey key;
                        key.TimeSeconds = keyData.value("TimeSeconds", 0.0f);
                        if (keyData.contains("Value") && keyData["Value"].is_array())
                            key.Value = JsonToFloat4(keyData["Value"]);
                        key.Interp = static_cast<AnimatorComponent::State::PropertyKey::Interpolation>(keyData.value("Interpolation", 1));
                        track.Keys.push_back(key);
                    }
                }
                state.PropertyTracks.push_back(track);
            }
        }

        nlohmann::json EventsToJson(const AnimatorComponent::State& state)
        {
            nlohmann::json events = nlohmann::json::array();
            for (const auto& event : state.Events)
            {
                events.push_back({
                    { "TimeSeconds", event.TimeSeconds },
                    { "FunctionName", event.FunctionName },
                    { "StringArgument", event.StringArgument }
                    });
            }
            return events;
        }

        void LoadEvents(const nlohmann::json& stateData, AnimatorComponent::State& state)
        {
            state.Events.clear();
            if (!stateData.contains("Events") || !stateData["Events"].is_array())
                return;

            for (const auto& eventData : stateData["Events"])
            {
                AnimatorComponent::State::AnimationEvent event;
                event.TimeSeconds = eventData.value("TimeSeconds", 0.0f);
                event.FunctionName = eventData.value("FunctionName", "OnAnimationEvent");
                event.StringArgument = eventData.value("StringArgument", "");
                state.Events.push_back(event);
            }
            state.SelectedEventIndex = std::clamp(state.SelectedEventIndex, -1, static_cast<int>(state.Events.size()) - 1);
        }

        nlohmann::json StateToJson(const AnimatorComponent::State& state)
        {
            nlohmann::json blendTreeChildren = nlohmann::json::array();
            for (const auto& child : state.Tree.Children)
            {
                blendTreeChildren.push_back({
                    { "ClipIndex", child.ClipIndex },
                    { "Threshold", child.Threshold },
                    { "Position", Float2ToJson(child.Position) },
                    { "Weight", child.Weight }
                    });
            }

            return {
                { "Name", state.Name },
                { "Motion", static_cast<int>(state.Motion) },
                { "ClipIndex", state.ClipIndex },
                { "MotionGuid", state.MotionAssetGuid },
                { "MotionPath", state.MotionPath },
                { "Loop", state.Loop },
                { "Speed", state.Speed },
                { "ApplyRootMotion", state.ApplyRootMotion },
                { "BlendTree", {
                    { "Type", static_cast<int>(state.Tree.TreeType) },
                    { "ParameterX", state.Tree.ParameterX },
                    { "ParameterY", state.Tree.ParameterY },
                    { "Children", blendTreeChildren }
                } },
                { "ImportSettings", {
                    { "DisplayName", state.ImportSettings.DisplayName },
                    { "UseCustomRange", state.ImportSettings.UseCustomRange },
                    { "StartSeconds", state.ImportSettings.StartSeconds },
                    { "EndSeconds", state.ImportSettings.EndSeconds },
                    { "LoopPose", state.ImportSettings.LoopPose },
                    { "BakeRootTransform", state.ImportSettings.BakeRootTransform },
                    { "LockRootPositionXZ", state.ImportSettings.LockRootPositionXZ },
                    { "LockRootPositionY", state.ImportSettings.LockRootPositionY },
                    { "LockRootRotation", state.ImportSettings.LockRootRotation }
                } },
                { "PropertyTracks", PropertyTracksToJson(state) },
                { "GraphPosition", Float2ToJson(state.GraphPosition) },
                { "WriteDefaults", state.WriteDefaults },
                { "Events", EventsToJson(state) }
            };
        }

        AnimatorComponent::State JsonToState(const nlohmann::json& stateData)
        {
            AnimatorComponent::State state;
            state.Name = stateData.value("Name", "State");
            state.Motion = static_cast<AnimatorComponent::State::MotionType>(stateData.value("Motion", 0));
            state.ClipIndex = stateData.value("ClipIndex", 0);
            state.MotionAssetGuid = stateData.value("MotionGuid", "");
            state.MotionPath = stateData.value("MotionPath", "");
            state.Loop = stateData.value("Loop", true);
            state.Speed = stateData.value("Speed", 1.0f);
            state.ApplyRootMotion = stateData.value("ApplyRootMotion", false);
            if (stateData.contains("BlendTree") && stateData["BlendTree"].is_object())
            {
                const auto& treeData = stateData["BlendTree"];
                state.Tree.TreeType = static_cast<AnimatorComponent::State::BlendTree::Type>(treeData.value("Type", 0));
                state.Tree.ParameterX = treeData.value("ParameterX", "");
                state.Tree.ParameterY = treeData.value("ParameterY", "");
                if (treeData.contains("Children") && treeData["Children"].is_array())
                {
                    for (const auto& childData : treeData["Children"])
                    {
                        AnimatorComponent::State::BlendTreeChild child;
                        child.ClipIndex = childData.value("ClipIndex", 0);
                        child.Threshold = childData.value("Threshold", 0.0f);
                        if (childData.contains("Position") && childData["Position"].is_array())
                            child.Position = JsonToFloat2(childData["Position"]);
                        child.Weight = childData.value("Weight", 1.0f);
                        state.Tree.Children.push_back(child);
                    }
                }
            }
            if (stateData.contains("ImportSettings") && stateData["ImportSettings"].is_object())
            {
                const auto& importData = stateData["ImportSettings"];
                state.ImportSettings.DisplayName = importData.value("DisplayName", "");
                state.ImportSettings.UseCustomRange = importData.value("UseCustomRange", false);
                state.ImportSettings.StartSeconds = importData.value("StartSeconds", 0.0f);
                state.ImportSettings.EndSeconds = importData.value("EndSeconds", 0.0f);
                state.ImportSettings.LoopPose = importData.value("LoopPose", false);
                state.ImportSettings.BakeRootTransform = importData.value("BakeRootTransform", false);
                state.ImportSettings.LockRootPositionXZ = importData.value("LockRootPositionXZ", false);
                state.ImportSettings.LockRootPositionY = importData.value("LockRootPositionY", false);
                state.ImportSettings.LockRootRotation = importData.value("LockRootRotation", false);
            }
            LoadPropertyTracks(stateData, state);
            if (stateData.contains("GraphPosition") && stateData["GraphPosition"].is_array())
                state.GraphPosition = JsonToFloat2(stateData["GraphPosition"]);
            state.WriteDefaults = stateData.value("WriteDefaults", true);
            LoadEvents(stateData, state);
            return state;
        }

        nlohmann::json TransitionToJson(const AnimatorComponent::Transition& transition)
        {
            nlohmann::json data = {
                { "FromStateIndex", transition.FromStateIndex },
                { "ToStateIndex", transition.ToStateIndex },
                { "HasExitTime", transition.HasExitTime },
                { "ExitTime", transition.ExitTime },
                { "BlendTime", transition.BlendTime },
                { "CanInterrupt", transition.CanInterrupt },
                { "Priority", transition.Priority },
                { "ExitTargetStateIndex", transition.ExitTargetStateIndex },
                { "Conditions", nlohmann::json::array() }
            };

            for (const auto& condition : transition.Conditions)
            {
                data["Conditions"].push_back({
                    { "ParameterName", condition.ParameterName },
                    { "Mode", static_cast<int>(condition.Mode) },
                    { "FloatValue", condition.FloatValue },
                    { "BoolValue", condition.BoolValue }
                    });
            }
            return data;
        }

        AnimatorComponent::Transition JsonToTransition(const nlohmann::json& transitionData)
        {
            AnimatorComponent::Transition transition;
            transition.FromStateIndex = transitionData.value("FromStateIndex", -1);
            transition.ToStateIndex = transitionData.value("ToStateIndex", -1);
            transition.HasExitTime = transitionData.value("HasExitTime", false);
            transition.ExitTime = transitionData.value("ExitTime", 1.0f);
            transition.BlendTime = transitionData.value("BlendTime", 0.15f);
            transition.CanInterrupt = transitionData.value("CanInterrupt", true);
            transition.Priority = transitionData.value("Priority", 0);
            transition.ExitTargetStateIndex = transitionData.value("ExitTargetStateIndex", -1);
            if (transitionData.contains("Conditions") && transitionData["Conditions"].is_array())
            {
                for (const auto& conditionData : transitionData["Conditions"])
                {
                    AnimatorComponent::TransitionCondition condition;
                    condition.ParameterName = conditionData.value("ParameterName", "");
                    condition.Mode = static_cast<AnimatorComponent::TransitionCondition::CompareMode>(conditionData.value("Mode", 0));
                    condition.FloatValue = conditionData.value("FloatValue", 0.0f);
                    condition.BoolValue = conditionData.value("BoolValue", true);
                    transition.Conditions.push_back(condition);
                }
            }
            return transition;
        }
    }

    bool AnimatorControllerAsset::SaveToFile(const std::filesystem::path& path, const AnimatorComponent& animator)
    {
        if (path.empty())
            return false;

        nlohmann::json data;
        data["Version"] = 2;
        data["Name"] = path.stem().string();
        data["SourceGuid"] = animator.SourceAssetGuid;
        data["SourcePath"] = animator.SourcePath;
        data["SelectedClipIndex"] = animator.SelectedClipIndex;
        data["SelectedClipName"] = animator.SelectedClipName;
        data["AutoPlay"] = animator.AutoPlay;
        data["Loop"] = animator.Loop;
        data["Speed"] = animator.Speed;
        data["ApplyRootMotion"] = animator.ApplyRootMotion;
        data["AvatarGuid"] = animator.AvatarGuid;
        data["AvatarPath"] = animator.AvatarPath;
        data["SourceAvatarGuid"] = animator.SourceAvatarGuid;
        data["SourceAvatarPath"] = animator.SourceAvatarPath;
        data["HumanoidRootBone"] = animator.HumanoidRootBone;
        data["RetargetToHumanoid"] = animator.RetargetToHumanoid;
        data["ActiveLayerIndex"] = animator.ActiveLayerIndex;
        data["Parameters"] = nlohmann::json::array();
        data["Layers"] = nlohmann::json::array();

        for (const auto& parameter : animator.Parameters)
        {
            data["Parameters"].push_back({
                { "Name", parameter.Name },
                { "Type", static_cast<int>(parameter.ParamType) },
                { "FloatValue", parameter.FloatValue },
                { "BoolValue", parameter.BoolValue }
                });
        }

        for (const auto& layer : animator.Layers)
        {
            nlohmann::json layerData = {
                { "Name", layer.Name },
                { "Weight", layer.Weight },
                { "MaskRootBone", layer.MaskRootBone },
                { "MaskBoneNames", layer.MaskBoneNames },
                { "Blending", static_cast<int>(layer.Blending) },
                { "Sync", layer.Sync },
                { "IKPass", layer.IKPass },
                { "Visible", layer.Visible },
                { "ActiveStateIndex", layer.ActiveStateIndex },
                { "EntryStateIndex", layer.EntryStateIndex },
                { "SelectedTransitionIndex", layer.SelectedTransitionIndex },
                { "EntryNodePosition", Float2ToJson(layer.EntryNodePosition) },
                { "AnyStateNodePosition", Float2ToJson(layer.AnyStateNodePosition) },
                { "ExitNodePosition", Float2ToJson(layer.ExitNodePosition) },
                { "States", nlohmann::json::array() },
                { "Transitions", nlohmann::json::array() }
            };

            // 컨트롤러 파일은 여러 오브젝트가 공유하는 상태머신 원본이다.
            // 그래서 재생 중에 변하는 시간값은 빼고, 편집자가 만든 그래프 구조만 저장한다.
            for (const auto& state : layer.States)
                layerData["States"].push_back(StateToJson(state));
            for (const auto& transition : layer.Transitions)
                layerData["Transitions"].push_back(TransitionToJson(transition));
            data["Layers"].push_back(layerData);
        }

        data["States"] = nlohmann::json::array();
        for (const auto& state : animator.States)
            data["States"].push_back(StateToJson(state));
        data["Transitions"] = nlohmann::json::array();
        for (const auto& transition : animator.Transitions)
            data["Transitions"].push_back(TransitionToJson(transition));

        std::ofstream file(path);
        if (!file.is_open())
            return false;
        file << data.dump(4);
        return true;
    }

    bool AnimatorControllerAsset::LoadFromFile(const std::filesystem::path& path, AnimatorComponent& animator)
    {
        if (path.empty())
            return false;

        std::ifstream file(path);
        if (!file.is_open())
            return false;

        nlohmann::json data;
        try
        {
            file >> data;
        }
        catch (...)
        {
            return false;
        }

        const std::string controllerGuid = animator.ControllerAssetGuid;
        const std::string controllerPath = animator.ControllerPath;

        animator.SourceAssetGuid = data.value("SourceGuid", animator.SourceAssetGuid);
        animator.SourcePath = data.value("SourcePath", animator.SourcePath);
        animator.SelectedClipIndex = data.value("SelectedClipIndex", 0);
        animator.SelectedClipName = data.value("SelectedClipName", "");
        animator.AutoPlay = data.value("AutoPlay", true);
        animator.Loop = data.value("Loop", true);
        animator.Speed = data.value("Speed", 1.0f);
        animator.ApplyRootMotion = data.value("ApplyRootMotion", false);
        animator.AvatarGuid = data.value("AvatarGuid", "");
        animator.AvatarPath = data.value("AvatarPath", "");
        animator.SourceAvatarGuid = data.value("SourceAvatarGuid", "");
        animator.SourceAvatarPath = data.value("SourceAvatarPath", "");
        animator.HumanoidRootBone = data.value("HumanoidRootBone", "Hips");
        animator.RetargetToHumanoid = data.value("RetargetToHumanoid", false);
        animator.ActiveLayerIndex = data.value("ActiveLayerIndex", 0);

        animator.Parameters.clear();
        if (data.contains("Parameters") && data["Parameters"].is_array())
        {
            for (const auto& parameterData : data["Parameters"])
            {
                AnimatorComponent::Parameter parameter;
                parameter.Name = parameterData.value("Name", "Parameter");
                parameter.ParamType = static_cast<AnimatorComponent::Parameter::Type>(parameterData.value("Type", 0));
                parameter.FloatValue = parameterData.value("FloatValue", 0.0f);
                parameter.BoolValue = parameterData.value("BoolValue", false);
                animator.Parameters.push_back(parameter);
            }
        }

        animator.Layers.clear();
        if (data.contains("Layers") && data["Layers"].is_array())
        {
            for (const auto& layerData : data["Layers"])
            {
                AnimatorComponent::Layer layer;
                layer.Name = layerData.value("Name", "Layer");
                layer.Weight = layerData.value("Weight", 1.0f);
                layer.MaskRootBone = layerData.value("MaskRootBone", "");
                layer.MaskBoneNames = layerData.value("MaskBoneNames", std::vector<std::string>{});
                layer.Blending = static_cast<AnimatorComponent::Layer::BlendMode>(layerData.value("Blending", 0));
                layer.Sync = layerData.value("Sync", false);
                layer.IKPass = layerData.value("IKPass", false);
                layer.Visible = layerData.value("Visible", true);
                layer.ActiveStateIndex = layerData.value("ActiveStateIndex", -1);
                layer.EntryStateIndex = layerData.value("EntryStateIndex", layer.ActiveStateIndex);
                layer.SelectedTransitionIndex = layerData.value("SelectedTransitionIndex", -1);
                if (layerData.contains("EntryNodePosition") && layerData["EntryNodePosition"].is_array())
                    layer.EntryNodePosition = JsonToFloat2(layerData["EntryNodePosition"]);
                if (layerData.contains("AnyStateNodePosition") && layerData["AnyStateNodePosition"].is_array())
                    layer.AnyStateNodePosition = JsonToFloat2(layerData["AnyStateNodePosition"]);
                if (layerData.contains("ExitNodePosition") && layerData["ExitNodePosition"].is_array())
                    layer.ExitNodePosition = JsonToFloat2(layerData["ExitNodePosition"]);

                if (layerData.contains("States") && layerData["States"].is_array())
                {
                    for (const auto& stateData : layerData["States"])
                        layer.States.push_back(JsonToState(stateData));
                }
                if (layerData.contains("Transitions") && layerData["Transitions"].is_array())
                {
                    for (const auto& transitionData : layerData["Transitions"])
                        layer.Transitions.push_back(JsonToTransition(transitionData));
                }
                animator.Layers.push_back(layer);
            }
        }

        animator.States.clear();
        if (data.contains("States") && data["States"].is_array())
        {
            for (const auto& stateData : data["States"])
                animator.States.push_back(JsonToState(stateData));
        }

        animator.Transitions.clear();
        if (data.contains("Transitions") && data["Transitions"].is_array())
        {
            for (const auto& transitionData : data["Transitions"])
                animator.Transitions.push_back(JsonToTransition(transitionData));
        }

        animator.ControllerAssetGuid = controllerGuid;
        animator.ControllerPath = controllerPath.empty() ? path.string() : controllerPath;
        Normalize(animator);
        ResetRuntimeState(animator);
        return true;
    }

    void AnimatorControllerAsset::Normalize(AnimatorComponent& animator)
    {
        if (animator.Layers.empty())
            animator.Layers.push_back({});

        animator.ActiveLayerIndex = std::clamp(animator.ActiveLayerIndex, 0, static_cast<int>(animator.Layers.size()) - 1);

        if (!animator.Layers.empty() && animator.Layers[0].States.empty() && !animator.States.empty())
        {
            // 예전 컨트롤러는 레이어 없이 상태만 저장했다.
            // 로드할 때 Base Layer로 올려 두면 구버전 파일도 새 그래프 편집기에 맞춰진다.
            animator.Layers[0].States = animator.States;
            animator.Layers[0].Transitions = animator.Transitions;
            animator.Layers[0].ActiveStateIndex = animator.ActiveStateIndex;
            animator.Layers[0].EntryStateIndex = animator.EntryStateIndex;
            animator.Layers[0].SelectedTransitionIndex = animator.SelectedTransitionIndex;
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

            layer.ActiveStateIndex = std::clamp(layer.ActiveStateIndex, 0, static_cast<int>(layer.States.size()) - 1);
            layer.EntryStateIndex = std::clamp(layer.EntryStateIndex, 0, static_cast<int>(layer.States.size()) - 1);
            layer.Transitions.erase(
                std::remove_if(layer.Transitions.begin(), layer.Transitions.end(), [&layer](const AnimatorComponent::Transition& transition)
                {
                    const bool validFrom = transition.FromStateIndex == AnimatorComponent::Transition::AnyStateIndex ||
                        (transition.FromStateIndex >= 0 && transition.FromStateIndex < static_cast<int>(layer.States.size()));
                    const bool validTo = transition.ToStateIndex == AnimatorComponent::Transition::ExitStateIndex ||
                        (transition.ToStateIndex >= 0 && transition.ToStateIndex < static_cast<int>(layer.States.size()));
                    return !validFrom || !validTo;
                }),
                layer.Transitions.end());
            for (auto& transition : layer.Transitions)
            {
                if (transition.ExitTargetStateIndex >= static_cast<int>(layer.States.size()))
                    transition.ExitTargetStateIndex = -1;
            }
            layer.SelectedTransitionIndex = std::clamp(layer.SelectedTransitionIndex, -1, static_cast<int>(layer.Transitions.size()) - 1);
        }

        SyncBaseLayerToLegacyGraph(animator);
    }

    void AnimatorControllerAsset::SyncBaseLayerToLegacyGraph(AnimatorComponent& animator)
    {
        if (animator.Layers.empty())
            return;

        const auto& baseLayer = animator.Layers[0];
        animator.States = baseLayer.States;
        animator.Transitions = baseLayer.Transitions;
        animator.ActiveStateIndex = baseLayer.ActiveStateIndex;
        animator.EntryStateIndex = baseLayer.EntryStateIndex;
        animator.SelectedTransitionIndex = baseLayer.SelectedTransitionIndex;
    }

    void AnimatorControllerAsset::ResetRuntimeState(AnimatorComponent& animator)
    {
        animator.IsPlaying = false;
        animator.RuntimeClip.reset();
        animator.RuntimeClipKey.clear();
        animator.RuntimePlaybackLayerIndex = -1;
        animator.AnimPlayer.StopAnimation();
        for (auto& layer : animator.Layers)
        {
            layer.PreviousStateIndex = -1;
            layer.StateTime = 0.0f;
            layer.PreviousLoopTime = 0.0f;
            layer.PreviousStateTime = 0.0f;
            layer.BlendElapsed = 0.0f;
            layer.BlendDuration = 0.0f;
            layer.FiredEventIndices.clear();
            layer.PreviousRuntimeClip.reset();
            layer.Exited = false;
        }
    }
}
