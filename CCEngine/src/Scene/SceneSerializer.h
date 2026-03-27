#pragma once
#include "Core.h"
#include "Scene/Scene.h"
#include <string>
#include <memory>

namespace CCEngine
{
    class CC_API SceneSerializer
    {
    public:
        // 어떤 씬을 저장/불러오기 할지 타겟지정
        SceneSerializer(Scene* scene);

        // 씬을 텍스트 파일(JSON)로 저장
        bool Serialize(const std::string& filepath);

        // JSON 에서 씬 복원
        bool Deserialize(const std::string& filepath);

    private:
        Scene* m_Scene;
    };
}