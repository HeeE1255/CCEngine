#include "UI/InspectorPanel.h"

#include "Animation/Animator.h"
#include "Animation/AnimatorControllerAsset.h"
#include "Application.h"
#include "Core/AssetDatabase.h"
#include "Core/ConsoleLog.h"
#include "Scene/Components.h"
#include "UI/AnimatorInspectorWidgets.h"
#include "UI/Button.h"
#include "UI/InspectorItem.h"
#include "UI/TextInput.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace CCEngine
{
    namespace UI
    {
        namespace
        {
            Widget* FindVisibleDescendantByName(Widget* widget, const std::string& name)
            {
                if (!widget || !widget->IsVisible())
                    return nullptr;
                if (widget->GetName() == name)
                    return widget;
                for (Widget* child : widget->GetChildren())
                {
                    if (Widget* found = FindVisibleDescendantByName(child, name))
                        return found;
                }
                return nullptr;
            }
            AnimatorComponent::Layer* GetInspectorAnimatorLayer(AnimatorComponent& animator, int layerIndex)
            {
                if (animator.Layers.empty())
                    animator.Layers.push_back(AnimatorComponent::Layer{});
                layerIndex = std::clamp(layerIndex, 0, (int)animator.Layers.size() - 1);
                animator.ActiveLayerIndex = layerIndex;
                return &animator.Layers[layerIndex];
            }

            void SyncInspectorBaseLayerToRuntimeFields(AnimatorComponent& animator)
            {
                if (animator.Layers.empty())
                    return;

                const auto& baseLayer = animator.Layers[0];
                // 런타임 호환 필드는 아직 Base Layer를 읽는 코드가 남아 있다.
                // Inspector에서 State를 수정할 때도 이 복사본을 맞춰야 그래프와 재생 결과가 엇갈리지 않는다.
                animator.States = baseLayer.States;
                animator.Transitions = baseLayer.Transitions;
                animator.ActiveStateIndex = baseLayer.ActiveStateIndex;
                animator.EntryStateIndex = baseLayer.EntryStateIndex;
                animator.SelectedTransitionIndex = baseLayer.SelectedTransitionIndex;
            }

            std::filesystem::path ResolveInspectorControllerPath(const AnimatorComponent& animator)
            {
                if (!animator.ControllerAssetGuid.empty())
                {
                    std::filesystem::path guidPath = AssetDatabase::GetPathFromGuid(animator.ControllerAssetGuid);
                    if (!guidPath.empty())
                        return guidPath;
                }
                return animator.ControllerPath;
            }

            void ResetInspectorAnimatorRuntime(AnimatorComponent& animator)
            {
                animator.RuntimeClip.reset();
                animator.RuntimeClipKey.clear();
                animator.AnimPlayer.StopAnimation();
                animator.IsPlaying = false;
                for (auto& layer : animator.Layers)
                {
                    layer.PreviousRuntimeClip.reset();
                    layer.PreviousStateIndex = -1;
                    layer.BlendElapsed = 0.0f;
                    layer.BlendDuration = 0.0f;
                    layer.FiredEventIndices.clear();
                }
            }

            bool SaveInspectorAnimatorController(AnimatorComponent& animator)
            {
                SyncInspectorBaseLayerToRuntimeFields(animator);
                std::filesystem::path controllerPath = ResolveInspectorControllerPath(animator);
                if (controllerPath.empty())
                    return false;

                AnimatorControllerAsset::Normalize(animator);
                const bool saved = AnimatorControllerAsset::SaveToFile(controllerPath, animator);
                if (saved)
                    AssetDatabase::EnsureMetaFile(controllerPath);
                return saved;
            }

            std::filesystem::path ResolveInspectorAnimationSourcePath(const AnimatorComponent& animator, const AnimatorComponent::State* state)
            {
                std::filesystem::path sourcePath;
                if (state)
                {
                    sourcePath = state->MotionPath;
                    if (!state->MotionAssetGuid.empty())
                    {
                        std::filesystem::path guidPath = AssetDatabase::GetPathFromGuid(state->MotionAssetGuid);
                        if (!guidPath.empty())
                            sourcePath = guidPath;
                    }
                    if (!sourcePath.empty() && std::filesystem::exists(sourcePath))
                        return sourcePath;
                }

                sourcePath = animator.SourcePath;
                if (!animator.SourceAssetGuid.empty())
                {
                    std::filesystem::path guidPath = AssetDatabase::GetPathFromGuid(animator.SourceAssetGuid);
                    if (!guidPath.empty())
                        sourcePath = guidPath;
                }

                return sourcePath;
            }

            const char* InspectorMotionTypeName(AnimatorComponent::State::MotionType type)
            {
                switch (type)
                {
                    case AnimatorComponent::State::MotionType::BlendTree: return "Blend Tree";
                    case AnimatorComponent::State::MotionType::PropertyClip: return "Property Clip";
                    default: return "Animation Clip";
                }
            }

            const char* InspectorConditionModeName(AnimatorComponent::TransitionCondition::CompareMode mode)
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

            const AnimatorComponent::Parameter* FindInspectorAnimatorParameter(const AnimatorComponent& animator, const std::string& name)
            {
                auto it = std::find_if(animator.Parameters.begin(), animator.Parameters.end(),
                    [&](const AnimatorComponent::Parameter& parameter) { return parameter.Name == name; });
                return it == animator.Parameters.end() ? nullptr : &(*it);
            }

            AnimatorComponent::TransitionCondition::CompareMode NextInspectorConditionMode(
                AnimatorComponent::TransitionCondition::CompareMode mode,
                AnimatorComponent::Parameter::Type parameterType)
            {
                if (parameterType == AnimatorComponent::Parameter::Type::Bool ||
                    parameterType == AnimatorComponent::Parameter::Type::Trigger)
                {
                    return mode == AnimatorComponent::TransitionCondition::CompareMode::If
                        ? AnimatorComponent::TransitionCondition::CompareMode::IfNot
                        : AnimatorComponent::TransitionCondition::CompareMode::If;
                }

                const int next = (static_cast<int>(mode) + 1) % 6;
                return static_cast<AnimatorComponent::TransitionCondition::CompareMode>(next);
            }

            void CycleInspectorConditionParameter(AnimatorComponent& animator, AnimatorComponent::TransitionCondition& condition)
            {
                if (animator.Parameters.empty())
                {
                    condition.ParameterName.clear();
                    return;
                }

                int current = -1;
                for (int i = 0; i < (int)animator.Parameters.size(); ++i)
                {
                    if (animator.Parameters[i].Name == condition.ParameterName)
                    {
                        current = i;
                        break;
                    }
                }

                const auto& nextParameter = animator.Parameters[(current + 1) % animator.Parameters.size()];
                condition.ParameterName = nextParameter.Name;
                condition.Mode = nextParameter.ParamType == AnimatorComponent::Parameter::Type::Float
                    ? AnimatorComponent::TransitionCondition::CompareMode::Greater
                    : AnimatorComponent::TransitionCondition::CompareMode::If;
            }

            AnimatorComponent::TransitionCondition MakeInspectorDefaultCondition(const AnimatorComponent& animator)
            {
                AnimatorComponent::TransitionCondition condition;
                if (!animator.Parameters.empty())
                {
                    condition.ParameterName = animator.Parameters.front().Name;
                    condition.Mode = animator.Parameters.front().ParamType == AnimatorComponent::Parameter::Type::Float
                        ? AnimatorComponent::TransitionCondition::CompareMode::Greater
                        : AnimatorComponent::TransitionCondition::CompareMode::If;
                }
                return condition;
            }

            std::string InspectorAnimatorEndpointLabel(const AnimatorComponent::Layer& layer, int stateIndex)
            {
                if (stateIndex == AnimatorComponent::Transition::AnyStateIndex)
                    return "Any State";
                if (stateIndex == AnimatorComponent::Transition::ExitStateIndex)
                    return "Exit";
                if (stateIndex >= 0 && stateIndex < (int)layer.States.size())
                    return layer.States[stateIndex].Name;
                return "Invalid";
            }

            std::string FormatInspectorFloat(float value, int precision = 2)
            {
                std::ostringstream ss;
                ss << std::fixed << std::setprecision(precision) << value;
                return ss.str();
            }
        }


        bool InspectorPanel::ClearSelectedAnimatorStateClip()
        {
            if (!m_HasSelectedAnimatorState || !m_AnimatorStateClipSlotSelected || !m_SelectedEntity || !m_SelectedEntity.HasComponent<AnimatorComponent>())
                return false;

            auto& animator = m_SelectedEntity.GetComponent<AnimatorComponent>();
            AnimatorComponent::Layer* layer = GetInspectorAnimatorLayer(animator, m_SelectedAnimatorLayerIndex);
            if (!layer || m_SelectedAnimatorStateIndex < 0 || m_SelectedAnimatorStateIndex >= (int)layer->States.size())
                return false;

            auto& state = layer->States[m_SelectedAnimatorStateIndex];
            state.Motion = AnimatorComponent::State::MotionType::Clip;
            state.ClipIndex = -1;
            state.MotionPath.clear();
            state.MotionAssetGuid.clear();
            state.Tree.Children.clear();
            animator.SelectedClipIndex = -1;
            animator.SelectedClipName.clear();

            ResetInspectorAnimatorRuntime(animator);
            const bool saved = SaveInspectorAnimatorController(animator);
            if (saved && m_OnAssetChanged)
                m_OnAssetChanged(ResolveInspectorControllerPath(animator), "animatorcontroller");
            RequestRebuild();
            return true;
        }

        void InspectorPanel::BuildAnimatorStateInspector()
        {
            auto invalid = [this](const std::string& message)
            {
                auto* item = new UI::InspectorItem("AnimatorStateInvalid", "Animator State");
                AddChild(item);
                auto* text = new UI::Button("AnimatorStateInvalidText", message);
                text->SetNormalColor({ 0.22f, 0.10f, 0.10f, 1.0f });
                text->SetHoverColor({ 0.22f, 0.10f, 0.10f, 1.0f });
                item->AddChild(text);
                auto* back = new UI::Button("AnimatorStateBack", "Back To Object Inspector");
                back->SetOnClick([this]()
                    {
                        m_HasSelectedAnimatorState = false;
                        m_HasSelectedAnimatorTransition = false;
                        m_SelectedAnimatorLayerIndex = -1;
                        m_SelectedAnimatorStateIndex = -1;
                        m_SelectedAnimatorTransitionIndex = -1;
                        RebuildInspector();
                    });
                item->AddChild(back);
            };

            if (!m_SelectedEntity || !m_SelectedEntity.HasComponent<AnimatorComponent>())
            {
                invalid("Animator component missing.");
                return;
            }

            auto& animator = m_SelectedEntity.GetComponent<AnimatorComponent>();
            AnimatorComponent::Layer* layer = GetInspectorAnimatorLayer(animator, m_SelectedAnimatorLayerIndex);
            if (!layer || m_SelectedAnimatorStateIndex < 0 || m_SelectedAnimatorStateIndex >= (int)layer->States.size())
            {
                invalid("Selected State is missing.");
                return;
            }

            auto& state = layer->States[m_SelectedAnimatorStateIndex];
            layer->ActiveStateIndex = m_SelectedAnimatorStateIndex;
            animator.ActiveLayerIndex = std::clamp(m_SelectedAnimatorLayerIndex, 0, (int)animator.Layers.size() - 1);

            const Entity entity = m_SelectedEntity;
            const int layerIndex = m_SelectedAnimatorLayerIndex;
            const int stateIndex = m_SelectedAnimatorStateIndex;
            std::function<void(const std::function<void(AnimatorComponent&, AnimatorComponent::Layer&, AnimatorComponent::State&)>&)> commitStateEdit;
            commitStateEdit = [this, entity, layerIndex, stateIndex](const std::function<void(AnimatorComponent&, AnimatorComponent::Layer&, AnimatorComponent::State&)>& edit) mutable
                {
                    Entity editableEntity = entity;
                    if (!editableEntity || !editableEntity.HasComponent<AnimatorComponent>())
                        return;

                    auto& currentAnimator = editableEntity.GetComponent<AnimatorComponent>();
                    AnimatorComponent::Layer* currentLayer = GetInspectorAnimatorLayer(currentAnimator, layerIndex);
                    if (!currentLayer || stateIndex < 0 || stateIndex >= (int)currentLayer->States.size())
                        return;

                    edit(currentAnimator, *currentLayer, currentLayer->States[stateIndex]);
                    currentLayer->ActiveStateIndex = stateIndex;
                    currentAnimator.ActiveLayerIndex = std::clamp(layerIndex, 0, (int)currentAnimator.Layers.size() - 1);
                    ResetInspectorAnimatorRuntime(currentAnimator);
                    const bool saved = SaveInspectorAnimatorController(currentAnimator);
                    if (saved && m_OnAssetChanged)
                        m_OnAssetChanged(ResolveInspectorControllerPath(currentAnimator), "animatorcontroller");
                    RequestRebuild();
                };

            auto* item = new UI::InspectorItem("AnimatorStateItem", "Animator State");
            item->SetAnchorMin(0.0f, 0.0f);
            item->SetAnchorMax(1.0f, 0.0f);
            AddChild(item);

            auto addSection = [&](const std::string& label)
            {
                item->AddChild(new AnimatorStateInspectorRow("AnimatorStateSection" + label, label, "", AnimatorStateInspectorRow::Kind::Section));
            };
            auto addField = [&](const std::string& name, const std::string& label, const std::string& value, std::function<void()> onClick = {})
            {
                auto* row = new AnimatorStateInspectorRow(name, label, value, AnimatorStateInspectorRow::Kind::Field);
                if (onClick)
                    row->SetOnClick(std::move(onClick));
                item->AddChild(row);
                return row;
            };
            auto addToggle = [&](const std::string& name, const std::string& label, bool checked, std::function<void()> onClick)
            {
                auto* row = new AnimatorStateInspectorRow(name, label, "", AnimatorStateInspectorRow::Kind::Toggle);
                row->SetChecked(checked);
                row->SetOnClick(std::move(onClick));
                item->AddChild(row);
                return row;
            };
            auto addAction = [&](const std::string& name, const std::string& label, const std::string& value, std::function<void()> onClick)
            {
                auto* row = new AnimatorStateInspectorRow(name, label, value, AnimatorStateInspectorRow::Kind::Action);
                row->SetOnClick(std::move(onClick));
                item->AddChild(row);
                return row;
            };
            auto addStepper = [&](const std::string& name, const std::string& label, const std::string& value, std::function<void()> onMinus, std::function<void()> onPlus)
            {
                auto* row = new AnimatorStateInspectorRow(name, label, value, AnimatorStateInspectorRow::Kind::Stepper);
                row->SetOnMinus(std::move(onMinus));
                row->SetOnPlus(std::move(onPlus));
                item->AddChild(row);
                return row;
            };

            addSection("State");
            const std::string objectName = m_SelectedEntity.HasComponent<TagComponent>()
                ? m_SelectedEntity.GetComponent<TagComponent>().Tag
                : std::string("Object");
            addField("AnimatorStateOwner", "Object", objectName);
            addField("AnimatorStateLayer", "Layer", layer->Name);
            addField("AnimatorStateController", "Controller", ResolveInspectorControllerPath(animator).filename().string());

            auto* nameInput = new UI::TextInput("AnimatorStateName", "State Name");
            nameInput->SetText(state.Name, false);
            nameInput->SetOnTextChanged([commitStateEdit](const std::string& text)
                {
                    commitStateEdit([&](AnimatorComponent&, AnimatorComponent::Layer&, AnimatorComponent::State& editableState)
                        {
                            editableState.Name = text.empty() ? "State" : text;
                            if (editableState.ImportSettings.DisplayName.empty())
                                editableState.ImportSettings.DisplayName = editableState.Name;
                        });
                });
            item->AddChild(nameInput);

            const std::filesystem::path sourcePath = ResolveInspectorAnimationSourcePath(animator, &state);
            const std::vector<AnimationClipInfo> clips = sourcePath.empty()
                ? std::vector<AnimationClipInfo>{}
                : AnimationClip::InspectClips(sourcePath.string());

            std::string clipName = "None";
            if (state.Motion == AnimatorComponent::State::MotionType::Clip && state.ClipIndex >= 0)
            {
                clipName = "Clip " + std::to_string(state.ClipIndex);
                for (const auto& clip : clips)
                {
                    if ((int)clip.Index == state.ClipIndex)
                    {
                        clipName = clip.Name;
                        break;
                    }
                }
            }

            addSection("Motion");

            auto selectClipSlot = [this]()
                {
                    m_AnimatorStateClipSlotSelected = true;
                    Widget::SetKeyboardFocus(this);
                    if (Widget* clipSlot = FindVisibleDescendantByName(this, "AnimatorStateClipSlot"))
                    {
                        if (auto* row = dynamic_cast<AnimatorStateInspectorRow*>(clipSlot))
                            row->SetSelected(true);
                    }
                };

            // Motion/Source/Clip은 서로 다른 값처럼 보여도 같은 Motion 슬롯을 설명한다.
            // 어느 행을 눌러도 Backspace 대상이 되게 해야 Inspector의 넓은 행 전체가 일관되게 동작한다.
            addField("AnimatorStateMotion", "Motion", InspectorMotionTypeName(state.Motion), selectClipSlot);
            addField("AnimatorStateMotionSource", "Source", sourcePath.empty() ? "None" : sourcePath.filename().string(), selectClipSlot);

            auto openClipSelection = [this, selectClipSlot]()
                {
                    selectClipSlot();
                    if (m_OnAnimatorClipPickRequested)
                        m_OnAnimatorClipPickRequested(m_SelectedEntity, m_SelectedAnimatorLayerIndex, m_SelectedAnimatorStateIndex);
                    else
                        ConsoleLog::Warning("Animator clip picker is not connected to Asset Browser.");
                    RequestRebuild();
                };

            addAction("AnimatorStateSelectClipFile", "", "Select Clip File", openClipSelection);

            const bool hasClip = state.Motion == AnimatorComponent::State::MotionType::Clip && state.ClipIndex >= 0;
            auto* clipSlot = new AnimatorStateInspectorRow("AnimatorStateClipSlot", "Clip", clipName, AnimatorStateInspectorRow::Kind::Field);
            clipSlot->SetOnClick([this, hasClip, openClipSelection, clipSlot]()
                {
                    const auto now = std::chrono::steady_clock::now();
                    const bool doubleClick = hasClip &&
                        m_AnimatorStateClipSlotSelected &&
                        std::chrono::duration_cast<std::chrono::milliseconds>(now - m_LastAnimatorClipSlotClickTime).count() <= 350;

                    m_AnimatorStateClipSlotSelected = true;
                    m_LastAnimatorClipSlotClickTime = now;
                    Widget::SetKeyboardFocus(this);
                    clipSlot->SetSelected(true);

                    // 첫 클릭에서 Inspector를 리빌드하면 같은 row가 사라져 더블클릭 판정이 끊길 수 있다.
                    // 그래서 클립이 있는 슬롯은 같은 위젯 안에서 선택 표시만 갱신하고, 두 번째 클릭에서 picker를 연다.
                    if (!hasClip || doubleClick)
                        openClipSelection();
                });
            clipSlot->SetSelected(m_AnimatorStateClipSlotSelected);
            clipSlot->SetWarning(!hasClip);
            clipSlot->SetOnIconClick(openClipSelection);
            item->AddChild(clipSlot);

            addToggle("AnimatorStateUseAvailableClips", "Use Available Clips", m_ShowAnimatorAvailableClips, [this]()
                {
                    m_ShowAnimatorAvailableClips = !m_ShowAnimatorAvailableClips;
                    RequestRebuild();
                });

            if (m_ShowAnimatorAvailableClips && !clips.empty())
            {
                addSection("Available Clips");
                for (const auto& clip : clips)
                {
                    const std::string rowName = "AnimatorStateClipChoice" + std::to_string(clip.Index);
                    const bool activeClip = (int)clip.Index == state.ClipIndex;
                    auto* row = addField(rowName, activeClip ? "Current" : "Clip", clip.Name.empty() ? ("Clip " + std::to_string(clip.Index)) : clip.Name,
                        [this, commitStateEdit, clip, sourcePath]()
                        {
                            m_AnimatorStateClipSlotSelected = true;
                            Widget::SetKeyboardFocus(this);
                            commitStateEdit([&](AnimatorComponent& editableAnimator, AnimatorComponent::Layer&, AnimatorComponent::State& editableState)
                                {
                                    editableState.Motion = AnimatorComponent::State::MotionType::Clip;
                                    editableState.ClipIndex = (int)clip.Index;
                                    editableState.MotionPath = sourcePath.string();
                                    editableState.MotionAssetGuid = AssetDatabase::GetGuidFromPath(sourcePath);
                                    editableState.Tree.Children.clear();
                                    editableAnimator.SelectedClipIndex = editableState.ClipIndex;
                                    editableAnimator.SelectedClipName = clip.Name;
                                });
                        });
                    // 이 목록은 현재 소스 파일 안의 클립 후보를 보여주는 곳이다.
                    // 선택 상태 파란색은 실제로 사용자가 클릭한 Object Field에만 쓰고,
                    // 현재 재생 클립은 "Current" 라벨로만 표시해 좌표/선택 오해를 줄인다.
                }
            }
            else if (m_ShowAnimatorAvailableClips)
            {
                auto* noClips = addField("AnimatorStateNoClips", "Clip Source", "No source clips found");
                noClips->SetWarning(true);
            }

            addAction("AnimatorStateRemoveClip", "", "Remove Clip", [this, commitStateEdit]()
                {
                    m_AnimatorStateClipSlotSelected = false;
                    commitStateEdit([](AnimatorComponent& editableAnimator, AnimatorComponent::Layer&, AnimatorComponent::State& editableState)
                        {
                            editableAnimator.SelectedClipIndex = -1;
                            editableAnimator.SelectedClipName.clear();
                            editableState.Motion = AnimatorComponent::State::MotionType::Clip;
                            editableState.ClipIndex = -1;
                            editableState.MotionPath.clear();
                            editableState.MotionAssetGuid.clear();
                            editableState.Tree.Children.clear();
                        });
                });

            addStepper("AnimatorStateSpeed", "Speed", std::to_string((int)std::round(state.Speed * 100.0f)) + "%",
                [commitStateEdit]()
                {
                    commitStateEdit([](AnimatorComponent&, AnimatorComponent::Layer&, AnimatorComponent::State& editableState)
                        {
                            editableState.Speed = std::clamp(editableState.Speed - 0.1f, 0.0f, 8.0f);
                        });
                },
                [commitStateEdit]()
                {
                    commitStateEdit([](AnimatorComponent&, AnimatorComponent::Layer&, AnimatorComponent::State& editableState)
                        {
                            editableState.Speed = std::clamp(editableState.Speed + 0.1f, 0.0f, 8.0f);
                        });
                });

            addToggle("AnimatorStateLoop", "Loop", state.Loop, [commitStateEdit]()
                {
                    commitStateEdit([](AnimatorComponent&, AnimatorComponent::Layer&, AnimatorComponent::State& editableState)
                        {
                            editableState.Loop = !editableState.Loop;
                        });
                });

            addToggle("AnimatorStateWriteDefaults", "Write Defaults", state.WriteDefaults, [commitStateEdit]()
                {
                    commitStateEdit([](AnimatorComponent&, AnimatorComponent::Layer&, AnimatorComponent::State& editableState)
                        {
                            editableState.WriteDefaults = !editableState.WriteDefaults;
                        });
                });

            addSection("Root Motion");
            auto* rootBoneInput = new UI::TextInput("AnimatorStateRootBone", "Root Bone");
            rootBoneInput->SetText(animator.HumanoidRootBone.empty() ? "Hips" : animator.HumanoidRootBone, false);
            rootBoneInput->SetOnTextChanged([commitStateEdit](const std::string& text)
                {
                    commitStateEdit([&](AnimatorComponent& editableAnimator, AnimatorComponent::Layer&, AnimatorComponent::State&)
                        {
                            editableAnimator.HumanoidRootBone = text.empty() ? "Hips" : text;
                        });
                });
            item->AddChild(rootBoneInput);

            addAction("AnimatorStateRootAuto", "Root Bone", "Auto: Hips", [commitStateEdit]()
                {
                    commitStateEdit([](AnimatorComponent& editableAnimator, AnimatorComponent::Layer&, AnimatorComponent::State&)
                        {
                            editableAnimator.HumanoidRootBone = "Hips";
                        });
                });

            addToggle("AnimatorStateApplyRoot", "Apply Root Motion", state.ApplyRootMotion, [commitStateEdit]()
                {
                    commitStateEdit([](AnimatorComponent& editableAnimator, AnimatorComponent::Layer&, AnimatorComponent::State& editableState)
                        {
                            editableState.ApplyRootMotion = !editableState.ApplyRootMotion;
                            editableAnimator.ApplyRootMotion = editableState.ApplyRootMotion;
                        });
                });

            addToggle("AnimatorStateBakeRoot", "Bake Root Transform", state.ImportSettings.BakeRootTransform, [commitStateEdit]()
                {
                    commitStateEdit([](AnimatorComponent&, AnimatorComponent::Layer&, AnimatorComponent::State& editableState)
                        {
                            editableState.ImportSettings.BakeRootTransform = !editableState.ImportSettings.BakeRootTransform;
                        });
                });

            addToggle("AnimatorStateLockRootXZ", "Lock Root XZ", state.ImportSettings.LockRootPositionXZ, [commitStateEdit]()
                {
                    commitStateEdit([](AnimatorComponent&, AnimatorComponent::Layer&, AnimatorComponent::State& editableState)
                        {
                            editableState.ImportSettings.LockRootPositionXZ = !editableState.ImportSettings.LockRootPositionXZ;
                        });
                });

            addToggle("AnimatorStateLockRootY", "Lock Root Y", state.ImportSettings.LockRootPositionY, [commitStateEdit]()
                {
                    commitStateEdit([](AnimatorComponent&, AnimatorComponent::Layer&, AnimatorComponent::State& editableState)
                        {
                            editableState.ImportSettings.LockRootPositionY = !editableState.ImportSettings.LockRootPositionY;
                        });
                });

            addToggle("AnimatorStateLockRootRotation", "Lock Root Rotation", state.ImportSettings.LockRootRotation, [commitStateEdit]()
                {
                    commitStateEdit([](AnimatorComponent&, AnimatorComponent::Layer&, AnimatorComponent::State& editableState)
                        {
                            editableState.ImportSettings.LockRootRotation = !editableState.ImportSettings.LockRootRotation;
                        });
                });

            addSection("Inspector");
            addAction("AnimatorStateBackToObject", "", "Back To Object Inspector", [this]()
                {
                    m_HasSelectedAnimatorState = false;
                    m_HasSelectedAnimatorTransition = false;
                    m_SelectedAnimatorLayerIndex = -1;
                    m_SelectedAnimatorStateIndex = -1;
                    m_SelectedAnimatorTransitionIndex = -1;
                    RebuildInspector();
                });

            auto& window = CCEngine::Application::Get()->GetWindow();
            UpdateLayout({ 0.0f, 0.0f }, { (float)window.GetWidth(), (float)window.GetHeight() });
        }

        void InspectorPanel::BuildAnimatorTransitionInspector()
        {
            auto invalid = [this](const std::string& message)
            {
                auto* item = new UI::InspectorItem("AnimatorTransitionInvalid", "Animator Transition");
                AddChild(item);
                auto* text = new UI::Button("AnimatorTransitionInvalidText", message);
                text->SetNormalColor({ 0.22f, 0.10f, 0.10f, 1.0f });
                text->SetHoverColor({ 0.22f, 0.10f, 0.10f, 1.0f });
                item->AddChild(text);
                auto* back = new UI::Button("AnimatorTransitionBack", "Back To Object Inspector");
                back->SetOnClick([this]()
                    {
                        m_HasSelectedAnimatorState = false;
                        m_HasSelectedAnimatorTransition = false;
                        m_SelectedAnimatorLayerIndex = -1;
                        m_SelectedAnimatorStateIndex = -1;
                        m_SelectedAnimatorTransitionIndex = -1;
                        RebuildInspector();
                    });
                item->AddChild(back);
            };

            if (!m_SelectedEntity || !m_SelectedEntity.HasComponent<AnimatorComponent>())
            {
                invalid("Animator component missing.");
                return;
            }

            auto& animator = m_SelectedEntity.GetComponent<AnimatorComponent>();
            AnimatorComponent::Layer* layer = GetInspectorAnimatorLayer(animator, m_SelectedAnimatorLayerIndex);
            if (!layer || m_SelectedAnimatorTransitionIndex < 0 || m_SelectedAnimatorTransitionIndex >= (int)layer->Transitions.size())
            {
                invalid("Selected transition no longer exists.");
                return;
            }

            auto& transition = layer->Transitions[m_SelectedAnimatorTransitionIndex];
            const Entity entity = m_SelectedEntity;
            const int layerIndex = m_SelectedAnimatorLayerIndex;
            const int transitionIndex = m_SelectedAnimatorTransitionIndex;
            std::function<void(const std::function<void(AnimatorComponent&, AnimatorComponent::Layer&, AnimatorComponent::Transition&)>&)> commitTransitionEdit;
            commitTransitionEdit = [this, entity, layerIndex, transitionIndex](const std::function<void(AnimatorComponent&, AnimatorComponent::Layer&, AnimatorComponent::Transition&)>& edit) mutable
                {
                    Entity editableEntity = entity;
                    if (!editableEntity || !editableEntity.HasComponent<AnimatorComponent>())
                        return;

                    auto& currentAnimator = editableEntity.GetComponent<AnimatorComponent>();
                    AnimatorComponent::Layer* currentLayer = GetInspectorAnimatorLayer(currentAnimator, layerIndex);
                    if (!currentLayer || transitionIndex < 0 || transitionIndex >= (int)currentLayer->Transitions.size())
                        return;

                    currentLayer->SelectedTransitionIndex = transitionIndex;
                    edit(currentAnimator, *currentLayer, currentLayer->Transitions[transitionIndex]);
                    currentLayer->SelectedTransitionIndex = std::clamp(currentLayer->SelectedTransitionIndex, -1, (int)currentLayer->Transitions.size() - 1);
                    currentAnimator.ActiveLayerIndex = std::clamp(layerIndex, 0, (int)currentAnimator.Layers.size() - 1);
                    ResetInspectorAnimatorRuntime(currentAnimator);
                    const bool saved = SaveInspectorAnimatorController(currentAnimator);
                    if (saved && m_OnAssetChanged)
                        m_OnAssetChanged(ResolveInspectorControllerPath(currentAnimator), "animatorcontroller");
                    m_SelectedAnimatorTransitionIndex = std::clamp(currentLayer->SelectedTransitionIndex, -1, (int)currentLayer->Transitions.size() - 1);
                    RequestRebuild();
                };

            auto* item = new UI::InspectorItem("AnimatorTransitionItem", "Animator Transition");
            item->SetAnchorMin(0.0f, 0.0f);
            item->SetAnchorMax(1.0f, 0.0f);
            AddChild(item);

            auto addSection = [&](const std::string& label)
            {
                item->AddChild(new AnimatorStateInspectorRow("AnimatorTransitionSection" + label, label, "", AnimatorStateInspectorRow::Kind::Section));
            };
            auto addField = [&](const std::string& name, const std::string& label, const std::string& value, std::function<void()> onClick = {})
            {
                auto* row = new AnimatorStateInspectorRow(name, label, value, AnimatorStateInspectorRow::Kind::Field);
                if (onClick)
                    row->SetOnClick(std::move(onClick));
                item->AddChild(row);
                return row;
            };
            auto addToggle = [&](const std::string& name, const std::string& label, bool checked, std::function<void()> onClick)
            {
                auto* row = new AnimatorStateInspectorRow(name, label, "", AnimatorStateInspectorRow::Kind::Toggle);
                row->SetChecked(checked);
                row->SetOnClick(std::move(onClick));
                item->AddChild(row);
                return row;
            };
            auto addAction = [&](const std::string& name, const std::string& label, const std::string& value, std::function<void()> onClick)
            {
                auto* row = new AnimatorStateInspectorRow(name, label, value, AnimatorStateInspectorRow::Kind::Action);
                row->SetOnClick(std::move(onClick));
                item->AddChild(row);
                return row;
            };
            auto addStepper = [&](const std::string& name, const std::string& label, const std::string& value, std::function<void()> onMinus, std::function<void()> onPlus)
            {
                auto* row = new AnimatorStateInspectorRow(name, label, value, AnimatorStateInspectorRow::Kind::Stepper);
                row->SetOnMinus(std::move(onMinus));
                row->SetOnPlus(std::move(onPlus));
                item->AddChild(row);
                return row;
            };

            const std::string fromLabel = InspectorAnimatorEndpointLabel(*layer, transition.FromStateIndex);
            const std::string toLabel = InspectorAnimatorEndpointLabel(*layer, transition.ToStateIndex);
            addSection("Transition");
            addField("AnimatorTransitionName", "Transition", fromLabel + " -> " + toLabel);
            addField("AnimatorTransitionLayer", "Layer", layer->Name);
            addToggle("AnimatorTransitionExitTime", "Has Exit Time", transition.HasExitTime, [commitTransitionEdit]()
                {
                    commitTransitionEdit([](AnimatorComponent&, AnimatorComponent::Layer&, AnimatorComponent::Transition& editableTransition)
                        {
                            editableTransition.HasExitTime = !editableTransition.HasExitTime;
                        });
                });
            addStepper("AnimatorTransitionExitValue", "Exit Time", FormatInspectorFloat(transition.ExitTime, 2),
                [commitTransitionEdit]()
                {
                    commitTransitionEdit([](AnimatorComponent&, AnimatorComponent::Layer&, AnimatorComponent::Transition& editableTransition)
                        {
                            editableTransition.ExitTime = std::clamp(editableTransition.ExitTime - 0.05f, 0.0f, 1.0f);
                        });
                },
                [commitTransitionEdit]()
                {
                    commitTransitionEdit([](AnimatorComponent&, AnimatorComponent::Layer&, AnimatorComponent::Transition& editableTransition)
                        {
                            editableTransition.ExitTime = std::clamp(editableTransition.ExitTime + 0.05f, 0.0f, 1.0f);
                        });
                });

            addSection("Settings");
            addToggle("AnimatorTransitionInterrupt", "Can Interrupt", transition.CanInterrupt, [commitTransitionEdit]()
                {
                    commitTransitionEdit([](AnimatorComponent&, AnimatorComponent::Layer&, AnimatorComponent::Transition& editableTransition)
                        {
                            editableTransition.CanInterrupt = !editableTransition.CanInterrupt;
                        });
                });
            addStepper("AnimatorTransitionPriority", "Priority", std::to_string(transition.Priority),
                [commitTransitionEdit]()
                {
                    commitTransitionEdit([](AnimatorComponent&, AnimatorComponent::Layer&, AnimatorComponent::Transition& editableTransition)
                        {
                            editableTransition.Priority -= 1;
                        });
                },
                [commitTransitionEdit]()
                {
                    commitTransitionEdit([](AnimatorComponent&, AnimatorComponent::Layer&, AnimatorComponent::Transition& editableTransition)
                        {
                            editableTransition.Priority += 1;
                        });
                });
            addStepper("AnimatorTransitionBlend", "Blend Time", FormatInspectorFloat(transition.BlendTime, 2) + "s",
                [commitTransitionEdit]()
                {
                    commitTransitionEdit([](AnimatorComponent&, AnimatorComponent::Layer&, AnimatorComponent::Transition& editableTransition)
                        {
                            editableTransition.BlendTime = (std::max)(0.0f, editableTransition.BlendTime - 0.05f);
                        });
                },
                [commitTransitionEdit]()
                {
                    commitTransitionEdit([](AnimatorComponent&, AnimatorComponent::Layer&, AnimatorComponent::Transition& editableTransition)
                        {
                            editableTransition.BlendTime += 0.05f;
                        });
                });
            addSection("Blend Preview");
            item->AddChild(new AnimatorTransitionBlendPreview(
                "AnimatorTransitionBlendPreview",
                fromLabel,
                toLabel,
                transition.HasExitTime,
                transition.ExitTime,
                transition.BlendTime));

            addSection("Conditions");
            if (transition.Conditions.empty())
            {
                auto* warning = addField("AnimatorTransitionNoConditions", "List", "No conditions");
                if (!transition.HasExitTime)
                    warning->SetWarning(true);
            }

            for (int i = 0; i < (int)transition.Conditions.size(); ++i)
            {
                const auto& condition = transition.Conditions[i];
                const auto* parameter = FindInspectorAnimatorParameter(animator, condition.ParameterName);
                const std::string value = parameter && parameter->ParamType == AnimatorComponent::Parameter::Type::Float
                    ? (" " + FormatInspectorFloat(condition.FloatValue, 1))
                    : "";
                auto* conditionRow = addField("AnimatorTransitionCondition" + std::to_string(i), "Condition " + std::to_string(i + 1),
                    condition.ParameterName + " " + InspectorConditionModeName(condition.Mode) + value,
                    [commitTransitionEdit, i]()
                    {
                        commitTransitionEdit([i](AnimatorComponent& editableAnimator, AnimatorComponent::Layer&, AnimatorComponent::Transition& editableTransition)
                            {
                                if (i >= 0 && i < (int)editableTransition.Conditions.size())
                                    CycleInspectorConditionParameter(editableAnimator, editableTransition.Conditions[i]);
                            });
                    });
                if (!parameter)
                    conditionRow->SetWarning(true);

                addAction("AnimatorTransitionConditionMode" + std::to_string(i), "Mode", InspectorConditionModeName(condition.Mode), [commitTransitionEdit, i]()
                    {
                        commitTransitionEdit([i](AnimatorComponent& editableAnimator, AnimatorComponent::Layer&, AnimatorComponent::Transition& editableTransition)
                            {
                                if (i < 0 || i >= (int)editableTransition.Conditions.size())
                                    return;
                                auto& editableCondition = editableTransition.Conditions[i];
                                const auto* editableParameter = FindInspectorAnimatorParameter(editableAnimator, editableCondition.ParameterName);
                                editableCondition.Mode = NextInspectorConditionMode(editableCondition.Mode,
                                    editableParameter ? editableParameter->ParamType : AnimatorComponent::Parameter::Type::Float);
                            });
                    });
                addStepper("AnimatorTransitionConditionValue" + std::to_string(i), "Value", parameter && parameter->ParamType == AnimatorComponent::Parameter::Type::Float ? FormatInspectorFloat(condition.FloatValue, 1) : (condition.BoolValue ? "True" : "False"),
                    [commitTransitionEdit, i]()
                    {
                        commitTransitionEdit([i](AnimatorComponent& editableAnimator, AnimatorComponent::Layer&, AnimatorComponent::Transition& editableTransition)
                            {
                                if (i < 0 || i >= (int)editableTransition.Conditions.size())
                                    return;
                                auto& editableCondition = editableTransition.Conditions[i];
                                const auto* editableParameter = FindInspectorAnimatorParameter(editableAnimator, editableCondition.ParameterName);
                                if (editableParameter && editableParameter->ParamType == AnimatorComponent::Parameter::Type::Float)
                                    editableCondition.FloatValue -= 0.1f;
                                else
                                    editableCondition.BoolValue = !editableCondition.BoolValue;
                            });
                    },
                    [commitTransitionEdit, i]()
                    {
                        commitTransitionEdit([i](AnimatorComponent& editableAnimator, AnimatorComponent::Layer&, AnimatorComponent::Transition& editableTransition)
                            {
                                if (i < 0 || i >= (int)editableTransition.Conditions.size())
                                    return;
                                auto& editableCondition = editableTransition.Conditions[i];
                                const auto* editableParameter = FindInspectorAnimatorParameter(editableAnimator, editableCondition.ParameterName);
                                if (editableParameter && editableParameter->ParamType == AnimatorComponent::Parameter::Type::Float)
                                    editableCondition.FloatValue += 0.1f;
                                else
                                    editableCondition.BoolValue = !editableCondition.BoolValue;
                            });
                    });
                addAction("AnimatorTransitionConditionRemove" + std::to_string(i), "", "Remove Condition", [commitTransitionEdit, i]()
                    {
                        commitTransitionEdit([i](AnimatorComponent&, AnimatorComponent::Layer&, AnimatorComponent::Transition& editableTransition)
                            {
                                if (i >= 0 && i < (int)editableTransition.Conditions.size())
                                    editableTransition.Conditions.erase(editableTransition.Conditions.begin() + i);
                            });
                    });
            }

            addAction("AnimatorTransitionAddCondition", "", "+ Add Condition", [commitTransitionEdit]()
                {
                    commitTransitionEdit([](AnimatorComponent& editableAnimator, AnimatorComponent::Layer&, AnimatorComponent::Transition& editableTransition)
                        {
                            editableTransition.Conditions.push_back(MakeInspectorDefaultCondition(editableAnimator));
                        });
                });
            addAction("AnimatorTransitionSort", "", "Sort Transitions By Priority", [commitTransitionEdit, transitionIndex]()
                {
                    commitTransitionEdit([transitionIndex](AnimatorComponent&, AnimatorComponent::Layer& editableLayer, AnimatorComponent::Transition& editableTransition)
                        {
                            auto selected = editableTransition;
                            std::stable_sort(editableLayer.Transitions.begin(), editableLayer.Transitions.end(),
                                [](const AnimatorComponent::Transition& a, const AnimatorComponent::Transition& b)
                                {
                                    return a.Priority < b.Priority;
                                });
                            auto it = std::find_if(editableLayer.Transitions.begin(), editableLayer.Transitions.end(), [&selected](const AnimatorComponent::Transition& item)
                                {
                                    return item.FromStateIndex == selected.FromStateIndex &&
                                        item.ToStateIndex == selected.ToStateIndex &&
                                        item.Priority == selected.Priority &&
                                        item.Conditions.size() == selected.Conditions.size();
                                });
                            editableLayer.SelectedTransitionIndex = it == editableLayer.Transitions.end()
                                ? std::clamp(transitionIndex, -1, (int)editableLayer.Transitions.size() - 1)
                                : static_cast<int>(std::distance(editableLayer.Transitions.begin(), it));
                        });
                });

            addSection("Inspector");
            addAction("AnimatorTransitionBackToObject", "", "Back To Object Inspector", [this]()
                {
                    m_HasSelectedAnimatorState = false;
                    m_HasSelectedAnimatorTransition = false;
                    m_SelectedAnimatorLayerIndex = -1;
                    m_SelectedAnimatorStateIndex = -1;
                    m_SelectedAnimatorTransitionIndex = -1;
                    RebuildInspector();
                });

            auto& window = CCEngine::Application::Get()->GetWindow();
            UpdateLayout({ 0.0f, 0.0f }, { (float)window.GetWidth(), (float)window.GetHeight() });
        }

    }
}
