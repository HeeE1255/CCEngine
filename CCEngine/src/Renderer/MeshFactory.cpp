#pragma once
#include "MeshFactory.h"

namespace CCEngine
{
    std::shared_ptr<Mesh> MeshFactory::CreateCube()
    {
        std::vector<Vertex3D> vertices = {
                        // 1. 앞면 - 법선: (0, 0, -1)
                        { {-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f} }, // 0
                        { { 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f} }, // 1
                        { { 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f} }, // 2
                        { {-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f} }, // 3

                        // 2. 뒷면 - 법선: (0, 0, 1)
                        { { 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f} },  // 4
                        { {-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f} },  // 5
                        { {-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f} },  // 6
                        { { 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f} },  // 7

                        // 3. 윗면 - 법선: (0, 1, 0)
                        { {-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f} },  // 8
                        { { 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f} },  // 9
                        { { 0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f} },  // 10
                        { {-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f} },  // 11

                        // 4. 아랫면 - 법선: (0, -1, 0)
                        { {-0.5f, -0.5f,  0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f} }, // 12
                        { { 0.5f, -0.5f,  0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f} }, // 13
                        { { 0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f} }, // 14
                        { {-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f} }, // 15

                        // 5. 우측면 - 법선: (1, 0, 0)
                        { { 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f} },  // 16
                        { { 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f} },  // 17
                        { { 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f} },  // 18
                        { { 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f} },  // 19

                        // 6. 좌측면 - 법선: (-1, 0, 0)
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
    std::shared_ptr<Mesh> MeshFactory::CreateTorus(float majorRadius, float minorRadius, uint32_t radialSegments, uint32_t tubularSegments)
    {
        std::vector<Vertex3D> vertices;
        std::vector<uint32_t> indices;

        // 1. 정점 생성
        for (uint32_t i = 0; i <= radialSegments; ++i)
        {
            float u = (float)i / radialSegments * DirectX::XM_2PI; // 큰 원의 각도
            float cosU = cosf(u);
            float sinU = sinf(u);

            for (uint32_t j = 0; j <= tubularSegments; ++j)
            {
                float v = (float)j / tubularSegments * DirectX::XM_2PI; // 튜브 단면의 각도
                float cosV = cosf(v);
                float sinV = sinf(v);

                DirectX::XMFLOAT3 pos;
                pos.x = (majorRadius + minorRadius * cosV) * cosU;
                pos.y = (majorRadius + minorRadius * cosV) * sinU;
                pos.z = minorRadius * sinV;

                // 법선이 필요하면 중심점에서 정점 방향으로 계산합니다.
                Vertex3D vertex;
                vertex.Position = pos;

                vertices.push_back(vertex);
            }
        }

        // 2. 삼각형 인덱스 생성
        for (uint32_t i = 0; i < radialSegments; ++i)
        {
            for (uint32_t j = 0; j < tubularSegments; ++j)
            {
                uint32_t a = i * (tubularSegments + 1) + j;
                uint32_t b = a + tubularSegments + 1;
                uint32_t c = a + 1;
                uint32_t d = b + 1;

                // 삼각형 1
                indices.push_back(a);
                indices.push_back(b);
                indices.push_back(c);

                // 삼각형 2
                indices.push_back(c);
                indices.push_back(b);
                indices.push_back(d);
            }
        }

        return std::make_shared<Mesh>(vertices, indices);
    }
}
