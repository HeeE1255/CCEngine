#include "Editor/SceneHierarchyPanel.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include <imgui.h>
#include <vector>


namespace CCEngine
{
    SceneHierarchyPanel::SceneHierarchyPanel(Scene* context)
    {
        SetContext(context);
    }

    void SceneHierarchyPanel::SetContext(Scene* context)
    {
        m_Context = context;
        m_SelectionContext = {}; // 씬이 바뀌면 선택된 것도 초기화
    }

    void SceneHierarchyPanel::OnImGuiRender()
    {
        //ImGui::ShowDemoWindow(); // ImGui의 기능들을 테스트해 볼 수 있는 데모 창을 띄우는 함수

        // ==========================================
        // 1. Scene Hierarchy (계층 구조) 창
        // ==========================================
        //
        ImGuiCond windowFlags = m_ResetLayoutFlag ? ImGuiCond_Always : ImGuiCond_FirstUseEver;

        ImGui::SetNextWindowPos(ImVec2(0.0f, 20.0f), windowFlags);
        ImGui::SetNextWindowSize(ImVec2(300.0f, 700.0f), windowFlags);
        
        ImGui::Begin("Scene Hierarchy");

        if (m_Context)
        {
            // nTT 뷰어에서 엔티티 ID만 싹 다 긁어서 vector로
            auto view = m_Context->m_Registry.view<TagComponent>();
            std::vector<entt::entity> entities(view.begin(), view.end());

            // ImGuiListClipper는 긴 리스트를 효율적으로 그릴 때 사용하는 도구
            ImGuiListClipper clipper;
            clipper.Begin((int)entities.size()); 


            while (clipper.Step()) // 화면에 그려야 할 시작/끝 인덱스를 계산
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                {
                    Entity entity{ entities[i], m_Context };
                    DrawEntityNode(entity); // Hierarchy에 보이는것만 그리기 (30~40개)
                }
            }
            clipper.End();

            // 빈 공간을 좌클릭하면 선택 해제!
            if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
            {
                m_SelectionContext = {};
            }
            
        }
        ImGui::End();

        // ==========================================
        // 2. Properties (속성 / 인스펙터) 창
        // ==========================================
        ImGui::SetNextWindowPos(ImVec2(980.0f, 20.0f), windowFlags);
        ImGui::SetNextWindowSize(ImVec2(300.0f, 700.0f), windowFlags);

        ImGui::Begin("Properties");

        if (m_SelectionContext) // 뭔가 선택된 엔티티가 있다면?
        {
            DrawComponents(m_SelectionContext);
        }

        ImGui::End();

        m_ResetLayoutFlag = false;
    }

    void SceneHierarchyPanel::DrawEntityNode(Entity entity)
    {
        auto& tag = entity.GetComponent<TagComponent>().Tag;

        // ImGui 트리 노드 설정 (선택됐으면 파란색 배경 표시)
        ImGuiTreeNodeFlags flags = ((m_SelectionContext == entity) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
        flags |= ImGuiTreeNodeFlags_SpanAvailWidth;

        // 엔티티의 ID를 고유 식별자로 사용해서 트리를 그립니다.
        bool opened = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entity, flags, tag.c_str());

        // 마우스로 이 항목을 클릭했다면? -> 선택된 엔티티로 지정!
        if (ImGui::IsItemClicked())
        {
            m_SelectionContext = entity;
        }

        if (opened)
        {
            // 나중에 자식(Child) 엔티티가 생기면 여기서 재귀 호출을 합니다. 지금은 닫아만 둡니다.
            ImGui::TreePop();
        }
    }

    void SceneHierarchyPanel::DrawComponents(Entity entity)
    {
        // 1. Tag (이름) 표시
        if (entity.HasComponent<TagComponent>())
        {
            auto& tag = entity.GetComponent<TagComponent>().Tag;

            // ImGui 텍스트 박스로 이름을 수정할 수 있게 만듭니다!
            char buffer[256];
            memset(buffer, 0, sizeof(buffer));
            strcpy_s(buffer, sizeof(buffer), tag.c_str());

            if (ImGui::InputText("Tag", buffer, sizeof(buffer)))
            {
                tag = std::string(buffer);
            }
        }

        ImGui::Separator(); // 가로줄 긋기

        // 2. Transform (위치, 회전, 크기)
        if (entity.HasComponent<TransformComponent>())
        {
            if (ImGui::TreeNodeEx((void*)typeid(TransformComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, "Transform"))
            {
                auto& tc = entity.GetComponent<TransformComponent>();

                // DirectXMath의 XMFLOAT3는 그냥 .x의 주소를 넘기면 완벽하게 작동합니다!
                ImGui::DragFloat3("Position", &tc.Translation.x, 0.1f);
                ImGui::DragFloat3("Rotation", &tc.Rotation.x, 0.1f); // (일단 라디안/디그리 변환 없이 직관적으로 연결)
                ImGui::DragFloat3("Scale", &tc.Scale.x, 0.1f);

                ImGui::TreePop();
            }
        }


        // 3. Sprite Renderer(색상)
        if (entity.HasComponent<SpriteRendererComponent>())
        {
            if (ImGui::TreeNodeEx((void*)typeid(SpriteRendererComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, "Sprite Renderer"))
            {
                auto& src = entity.GetComponent<SpriteRendererComponent>();

                // XMFLOAT4는 r, g, b, a 대신 x, y, z, w를 씁니다. 첫 번째인 x 주소를 넘깁니다.
                ImGui::ColorEdit4("Color", &src.Color.x);

                ImGui::TreePop();
            }
        }

        // 4. Wave Component (임시)
        if (entity.HasComponent<WaveComponent>())
        {
            if (ImGui::TreeNodeEx((void*)typeid(WaveComponent).hash_code(), ImGuiTreeNodeFlags_DefaultOpen, "Wave Component"))
            {
                auto& wave = entity.GetComponent<WaveComponent>();

                ImGui::DragFloat("Start X", &wave.StartX, 0.1f);
                ImGui::DragFloat("Start Y", &wave.StartY, 0.1f);
                ImGui::DragFloat("Time Offset", &wave.TimeOffset, 0.1f);

                ImGui::TreePop();
            }
        }

    }
}