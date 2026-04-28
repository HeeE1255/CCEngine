#pragma once
#include "MeshFactory.h"

namespace CCEngine
{
    std::shared_ptr<Mesh> MeshFactory::CreateCube()
    {
        std::vector<Vertex3D> vertices = {
                        // 1. [앞면] - Normal: (0, 0, -1)
                        { {-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f} }, // 0
                        { { 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f} }, // 1
                        { { 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f} }, // 2
                        { {-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f} }, // 3

                        // 2. [뒷면] - Normal: (0, 0, 1)
                        { { 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f} },  // 4
                        { {-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f} },  // 5
                        { {-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f} },  // 6
                        { { 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f} },  // 7

                        // 3. [윗면] - Normal: (0, 1, 0)
                        { {-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f} },  // 8
                        { { 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f} },  // 9
                        { { 0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f} },  // 10
                        { {-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f} },  // 11

                        // 4. [아랫면] - Normal: (0, -1, 0)
                        { {-0.5f, -0.5f,  0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f} }, // 12
                        { { 0.5f, -0.5f,  0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f} }, // 13
                        { { 0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f} }, // 14
                        { {-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f} }, // 15

                        // 5. [우측면] - Normal: (1, 0, 0)
                        { { 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f} },  // 16
                        { { 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f} },  // 17
                        { { 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f} },  // 18
                        { { 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f} },  // 19

                        // 6. [좌측면] - Normal: (-1, 0, 0)
                        { {-0.5f, -0.5f,  0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f} }, // 20
                        { {-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f} }, // 21
                        { {-0.5f,  0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f} }, // 22
                        { {-0.5f,  0.5f,  0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f} }  // 23
        };

        std::vector<uint32_t> indices = {
            0,  3,  2,  2,  1,  0, // 앞면
            4,  7,  6,  6,  5,  4, // 뒷면
            8, 11, 10, 10,  9,  8, // 윗면
            12, 15, 14, 14, 13, 12, // 아랫면
            16, 19, 18, 18, 17, 16, // 우측면
            20, 23, 22, 22, 21, 20  // 좌측면
        };

        return std::make_shared<Mesh>(vertices, indices);
    }

    std::shared_ptr<Mesh> MeshFactory::CreatePlane(float width, float height)
    {
        float w2 = width * 0.5f;
        float h2 = height * 0.5f;

        std::vector<Vertex3D> vertices = {
            { {-w2, 0.0f, -h2}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f} }, // 좌하단
            { { w2, 0.0f, -h2}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f} }, // 우하단
            { { w2, 0.0f,  h2}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f} }, // 우상단
            { {-w2, 0.0f,  h2}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f} }  // 좌상단
        };

        std::vector<uint32_t> indices = { 0, 2, 1, 0, 3, 2 }; // 시계 방향

        return std::make_shared<Mesh>(vertices, indices);
    }

    std::shared_ptr<Mesh> MeshFactory::CreateSphere(float radius, uint32_t sliceCount, uint32_t stackCount)
    {
        std::vector<Vertex3D> vertices;
        std::vector<uint32_t> indices;

        // 1. 정점 생성
        for (uint32_t i = 0; i <= stackCount; ++i)
        {
            float phi = DirectX::XM_PI * (float)i / stackCount;
            for (uint32_t j = 0; j <= sliceCount; ++j)
            {
                float theta = 2.0f * DirectX::XM_PI * (float)j / sliceCount;

                Vertex3D v;
                // 좌표 계산 (구면 좌표계 -> 직교 좌표계)
                v.Position.x = radius * sinf(phi) * cosf(theta);
                v.Position.y = radius * cosf(phi);
                v.Position.z = radius * sinf(phi) * sinf(theta);

                // 법선 벡터 (원점이 중심이므로 위치 벡터를 정규화하면 법선이 됨)
                DirectX::XMVECTOR pos = DirectX::XMLoadFloat3(&v.Position);
                DirectX::XMStoreFloat3(&v.Normal, DirectX::XMVector3Normalize(pos));

                // UV 좌표
                v.TexCoord.x = (float)j / sliceCount;
                v.TexCoord.y = (float)i / stackCount;

                vertices.push_back(v);
            }
        }

        // 2. 인덱스 생성 (인덱스 그리드 연결)
        for (uint32_t i = 0; i < stackCount; ++i)
        {
            for (uint32_t j = 0; j < sliceCount; ++j)
            {
                uint32_t first = (i * (sliceCount + 1)) + j;
                uint32_t second = first + sliceCount + 1;

                indices.push_back(first);
                indices.push_back(first + 1);
                indices.push_back(second);

                indices.push_back(second);
                indices.push_back(first + 1);
                indices.push_back(second + 1);
            }
        }

        return std::make_shared<Mesh>(vertices, indices);
    }
}