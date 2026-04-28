#include "Scene/SceneSerializer.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"


// ==========================================
// 매크로 충돌을 피하기 위해 json.hpp를 먼저
// ==========================================
#include "json.hpp"
#include <fstream>
#include <iostream>

//#include "Core/MemoryMacro.h" //직렬화에선 사용안하기

namespace CCEngine
{
    SceneSerializer::SceneSerializer(Scene* scene)
        : m_Scene(scene)
    {
    }

    bool SceneSerializer::Serialize(const std::string& filepath)
    {
        nlohmann::json sceneData;
        sceneData["Scene"] = "Untitled Scene";

        // 엔티티들을 담을 배열
        nlohmann::json entitiesArray = nlohmann::json::array();

        // 씬 안의 모든 엔티티를 순회하며 데이터를 추출
        m_Scene->m_Registry.view<TransformComponent>().each([&](auto entityID, auto& transform)
            {
                Entity entity = { entityID, m_Scene };
                if (!entity) return;

                nlohmann::json entityData;

                // 1. Tag 컴포넌트 저장
                if (entity.HasComponent<TagComponent>())
                {
                    entityData["TagComponent"]["Tag"] = entity.GetComponent<TagComponent>().Tag;
                }

                // 2. Transform 컴포넌트 저장
                if (entity.HasComponent<TransformComponent>())
                {
                    auto& tc = entity.GetComponent<TransformComponent>();
                    entityData["TransformComponent"]["Translation"] = { tc.Translation.x, tc.Translation.y, tc.Translation.z };
                    entityData["TransformComponent"]["Rotation"] = { tc.Rotation.x, tc.Rotation.y, tc.Rotation.z };
                    entityData["TransformComponent"]["Scale"] = { tc.Scale.x, tc.Scale.y, tc.Scale.z };
                }

                // 3. SpriteRenderer 컴포넌트 저장
                if (entity.HasComponent<SpriteRendererComponent>())
                {
                    auto& sprite = entity.GetComponent<SpriteRendererComponent>();
                    entityData["SpriteRendererComponent"]["Color"] = { sprite.Color.x, sprite.Color.y, sprite.Color.z, sprite.Color.w };
                }

                //// 4. Wave 컴포넌트 저장 (임시)
                //if (entity.HasComponent<WaveComponent>())
                //{
                //    auto& wave = entity.GetComponent<WaveComponent>();
                //    entityData["WaveComponent"]["StartX"] = wave.StartX;
                //    entityData["WaveComponent"]["StartY"] = wave.StartY;
                //    entityData["WaveComponent"]["TimeOffset"] = wave.TimeOffset;
                //}

                // 배열에 완성된 엔티티 데이터를 추가!
                entitiesArray.push_back(entityData);
            });

        sceneData["Entities"] = entitiesArray;

        // 파일 출력 (dump(4)는 들여쓰기를 4칸 가독성++)
        std::ofstream fout(filepath);
        if (fout.is_open())
        {
            fout << sceneData.dump(4);
            fout.close();
            return true;
        }

        return false;
    }

    bool SceneSerializer::Deserialize(const std::string& filepath)
    {
        // 1. 파일 열기
        std::ifstream stream(filepath);
        if (!stream.is_open())
        {
            return false;
        }

        // 2. JSON 파싱 (스트림에서 데이터 뽑아오기)
        nlohmann::json data;
        
        // 스트림(파일)에서 데이터를 뽑아서(>>) json 객체(data)에 파싱해 넣어라는 오버로딩 연산자
        // 파싱이 실패하면 예외가 터지므로, 여기까지 왔으면 일단 성공적으로 파싱된 것
        stream >> data; 

        // 3. 파일이 제대로 된 씬 데이터인지 확인
        if (!data.contains("Entities"))
        {
            return false;
        }

        // ==========================================
        // 기존 씬에 있던 모든 엔티티를 클리어
        // ==========================================
        m_Scene->m_Registry.clear();

        // 4. 배열에 있는 엔티티들을 하나씩 생성
        auto entities = data["Entities"];
        for (auto& entityData : entities)
        {
            // Tag 읽어오기 (없으면 기본값)
            std::string tag = "Untitled Entity";
            if (entityData.contains("TagComponent"))
                tag = entityData["TagComponent"]["Tag"];

            // 빈 엔티티 생성! (CreateEntity 안에서 Tag와 Transform은 기본으로..)
            Entity deserializedEntity = m_Scene->CreateEntity(tag);

            // Transform 복구
            if (entityData.contains("TransformComponent"))
            {
                auto& tc = deserializedEntity.GetComponent<TransformComponent>();
                auto& transformData = entityData["TransformComponent"];

                tc.Translation = { transformData["Translation"][0], transformData["Translation"][1], transformData["Translation"][2] };
                tc.Rotation = { transformData["Rotation"][0], transformData["Rotation"][1], transformData["Rotation"][2] };
                tc.Scale = { transformData["Scale"][0], transformData["Scale"][1], transformData["Scale"][2] };
            }

            // SpriteRenderer 복구
            if (entityData.contains("SpriteRendererComponent"))
            {
                auto& spriteData = entityData["SpriteRendererComponent"];
                auto& src = deserializedEntity.AddComponent<SpriteRendererComponent>();
                src.Color = { spriteData["Color"][0], spriteData["Color"][1], spriteData["Color"][2], spriteData["Color"][3] };
            }

            //// Wave 복구
            //if (entityData.contains("WaveComponent"))
            //{
            //    auto& waveData = entityData["WaveComponent"];
            //    auto& wc = deserializedEntity.AddComponent<WaveComponent>();
            //    wc.StartX = waveData["StartX"];
            //    wc.StartY = waveData["StartY"];
            //    wc.TimeOffset = waveData["TimeOffset"];
            //}
        }

        return true;
    }
}