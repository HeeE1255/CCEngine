#pragma once
#include "Renderer/Mesh.h"
#include <memory>

namespace CCEngine
{
    class CC_API MeshFactory
    {
    public:
        // 기본 도형의 Mesh 객체를 생성해서 반환
        static std::shared_ptr<Mesh> CreateCube();
        static std::shared_ptr<Mesh> CreatePlane(float width = 1.0f, float height = 1.0f);
        static std::shared_ptr<Mesh> CreateSphere(float radius = 0.5f, uint32_t sliceCount = 30, uint32_t stackCount = 30);
    };
}