#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Renderer/Renderer2D.h"

namespace CCEngine
{
    Scene::Scene()
    {
    }

    Scene::~Scene()
    {
    }

    Entity Scene::CreateEntity(const std::string& name)
    {
        // EnTT에게 새로운 ID를 하나 발급
        Entity entity = { m_Registry.create(), this };

        // Transform과 Tag 컴포넌트는 모든 오브젝트의 기본
        entity.AddComponent<TransformComponent>();
        auto& tag = entity.AddComponent<TagComponent>();
        tag.Tag = name.empty() ? "Entity" : name;

        return entity;
    }

    void Scene::OnUpdate(float deltaTime)
    {
        // 파도 스크립트 업데이트 (임시) ////////////////////////////////////////////////////
        auto waveView = m_Registry.view<TransformComponent, WaveComponent>();
        waveView.each([deltaTime](auto entity, auto& transform, auto& wave)
            {
                wave.TimeOffset += deltaTime;
                // std::sin 호출은 어쩔 수 없지만, 메모리 접근 속도가 빛의 속도입니다.
                transform.Translation.y = wave.StartY + std::sin(wave.StartX * 2.0f + wave.TimeOffset * 5.0f) * 0.2f;
            }
        );
        ////////////////////////////////////////////////////////////////////////////////////

        // 네이티브 스크립트 실행 (렌더링보다 먼저 로직부터)
        m_Registry.view<NativeScriptComponent>().each([=](auto entityID, auto& nsc)
            {
                // 스크립트 인스턴스가 아직 안 만들어졌다면 최초 1회 생성 (OnCreate)
                if (!nsc.Instance)
                {
                    nsc.Instance = nsc.InstantiateScript();
                    nsc.Instance->m_Entity = Entity{ entityID, this }; // "넌 이 엔티티의 스크립트야" 라고 알려줌
                    nsc.Instance->OnCreate();
                }

                // 매 프레임 업데이트 (OnUpdate)
                nsc.Instance->OnUpdate(deltaTime);
            }
        );

        // 카메라 세팅 (나중에 씬 매니저에서 관리할 때는 카메라도 씬의 구성 요소로 만들면 됩니다)
        auto view = m_Registry.view<TransformComponent, SpriteRendererComponent>();
        view.each([](auto entityID, auto& transform, auto& sprite)
            {
                DirectX::XMFLOAT2 size = { transform.Scale.x, transform.Scale.y };
                Renderer2D::DrawQuad(transform.Translation, size, sprite.Color);
            }
        );

    }
}