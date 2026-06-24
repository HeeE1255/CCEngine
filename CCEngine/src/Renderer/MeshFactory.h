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
        static std::shared_ptr<Mesh> CreateQuad(float width = 1.0f, float height = 1.0f);
        // 바닥 제작용 Plane은 화면/이미지용 Quad와 구분되도록 기본 크기를 10 x 10으로 생성한다.
        static std::shared_ptr<Mesh> CreatePlane(float width = 10.0f, float height = 10.0f);
        static std::shared_ptr<Mesh> CreateSphere(float radius = 0.5f, uint32_t sliceCount = 30, uint32_t stackCount = 30);
        static std::shared_ptr<Mesh> CreateCapsule(float radius = 0.5f, float cylinderHeight = 1.0f, uint32_t radialSegments = 24, uint32_t hemisphereSegments = 8);
        static std::shared_ptr<Mesh> CreateCylinder(float radius = 0.5f, float height = 1.0f, uint32_t radialSegments = 24);
        static std::shared_ptr<Mesh> CreateTorus(float majorRadius = 0.75f, float minorRadius = 0.25f, uint32_t radialSegments = 32, uint32_t tubularSegments = 16);
    };
}
