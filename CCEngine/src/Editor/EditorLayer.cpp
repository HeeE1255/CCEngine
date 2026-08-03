#include "EditorLayer.h"
#include "Renderer/Renderer.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/Renderer3D.h"
#include "Renderer/UIRenderer.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/MeshFactory.h"
#include "Renderer/Texture.h"
#include "Renderer/Font.h"
#include "Renderer/RendererHandle.h"
#include "Editor/EditorQATestRunner.h"
#include "Scene/Components.h"
#include "Scene/PrefabSerializer.h"
#include "Scene/SceneSerializer.h"
#include "Scripting/ScriptCompiler.h"
#include "Utils/PlatformUtils.h"
#include "Utils/MathUtils.h"
#include "Renderer/ModelImporter.h"
#include "Application.h"
#include "UI/HierarchyItem.h"
#include "UI/InspectorPanel.h"
#include "UI/InspectorRegistry.h"
#include "UI/InspectorItem.h"
#include "UI/InspectorUtils.h"
#include "UI/KeyBindingPickerPanel.h"
#include "Core/AssetDatabase.h"
#include <windows.h>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Events/Event.h"
#include "Events/MouseEvent.h"

namespace CCEngine {
    namespace
    {
        // 에디터 프레임 멈춤을 다시 추적할 때만 true로 바꾼다.
        // 기본값은 꺼 둬서 평상시 콘솔 로그와 단계 기록이 프레임을 건드리지 않게 한다.
        constexpr bool kEnableEditorHitchProfiler = false;

        struct EditorHitchStage
        {
            const char* Name = "";
            double Milliseconds = 0.0;
        };

        double ToMilliseconds(std::chrono::steady_clock::duration duration)
        {
            return std::chrono::duration<double, std::milli>(duration).count();
        }

        void AddEditorHitchStage(std::vector<EditorHitchStage>& stages, const char* name, std::chrono::steady_clock::time_point startedAt)
        {
            if (!kEnableEditorHitchProfiler)
                return;

            stages.push_back({ name, ToMilliseconds(std::chrono::steady_clock::now() - startedAt) });
        }

        void ReportEditorHitch(std::chrono::steady_clock::time_point frameStartedAt, const std::vector<EditorHitchStage>& stages)
        {
            if (!kEnableEditorHitchProfiler)
                return;

            double totalMs = ToMilliseconds(std::chrono::steady_clock::now() - frameStartedAt);
            double worstStageMs = 0.0;
            for (const EditorHitchStage& stage : stages)
                worstStageMs = (std::max)(worstStageMs, stage.Milliseconds);

            if (totalMs < 16.0 && worstStageMs < 6.0)
                return;

            static auto s_LastReportTime = std::chrono::steady_clock::now() - std::chrono::seconds(2);
            auto now = std::chrono::steady_clock::now();
            if (now - s_LastReportTime < std::chrono::seconds(1))
                return;
            s_LastReportTime = now;

            // 에디터 히치는 대부분 특정 관리 작업이 프레임 안에 끼어들 때 생긴다.
            // 단계별 시간을 남겨 두면 감으로 고치지 않고 병목을 바로 좁힐 수 있다.
            std::ostringstream stream;
            stream << "[Hitch] EditorLayer total=" << std::fixed << std::setprecision(2) << totalMs << "ms";
            for (const EditorHitchStage& stage : stages)
                stream << " | " << stage.Name << "=" << std::fixed << std::setprecision(2) << stage.Milliseconds << "ms";
            stream << '\n';

            OutputDebugStringA(stream.str().c_str());
            std::cout << stream.str();
        }

        std::shared_ptr<Mesh> CreateDefaultMeshForType(MeshComponent::MeshType type)
        {
            switch (type)
            {
                case MeshComponent::MeshType::Cube: return MeshFactory::CreateCube();
                case MeshComponent::MeshType::Sphere: return MeshFactory::CreateSphere();
                case MeshComponent::MeshType::Capsule: return MeshFactory::CreateCapsule();
                case MeshComponent::MeshType::Cylinder: return MeshFactory::CreateCylinder();
                case MeshComponent::MeshType::Plane: return MeshFactory::CreatePlane();
                case MeshComponent::MeshType::Quad: return MeshFactory::CreateQuad();
                case MeshComponent::MeshType::Torus: return MeshFactory::CreateTorus();
                default: return nullptr;
            }
        }

        std::string MakeSafeAssetFileName(const std::string& name)
        {
            std::string result;
            result.reserve(name.size());

            for (char c : name)
            {
                unsigned char uc = static_cast<unsigned char>(c);
                if (std::isalnum(uc) || c == '_' || c == '-' || c == ' ')
                    result.push_back(c);
                else
                    result.push_back('_');
            }

            while (!result.empty() && result.back() == ' ')
                result.pop_back();

            return result.empty() ? "Prefab" : result;
        }

        std::filesystem::path MakeUniquePrefabPath(const std::filesystem::path& directory, const std::string& baseName)
        {
            std::filesystem::path safeBase = MakeSafeAssetFileName(baseName);
            std::filesystem::path candidate = directory / (safeBase.string() + ".ccprefab");

            int index = 1;
            while (std::filesystem::exists(candidate))
            {
                candidate = directory / (safeBase.string() + " " + std::to_string(index) + ".ccprefab");
                ++index;
            }

            return candidate;
        }

        std::string ToLowerPathString(const std::filesystem::path& path)
        {
            std::string text = path.lexically_normal().generic_string();
            std::transform(text.begin(), text.end(), text.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        std::filesystem::path MakeAbsoluteNormalizedPath(const std::filesystem::path& path)
        {
            std::error_code ec;
            std::filesystem::path absolutePath = std::filesystem::absolute(path, ec);
            if (ec)
                absolutePath = path;
            return absolutePath.lexically_normal();
        }

        bool IsPathUnderDirectory(const std::filesystem::path& path, const std::filesystem::path& directory)
        {
            std::string pathText = ToLowerPathString(MakeAbsoluteNormalizedPath(path));
            std::string directoryText = ToLowerPathString(MakeAbsoluteNormalizedPath(directory));

            if (pathText == directoryText)
                return true;
            if (!directoryText.empty() && directoryText.back() != '/')
                directoryText.push_back('/');

            return pathText.rfind(directoryText, 0) == 0;
        }

        bool HasExtension(const std::filesystem::path& path, const std::string& expectedExtension)
        {
            std::string extension = path.extension().generic_string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return extension == expectedExtension;
        }

        bool IsIgnoredAssetWatcherPath(const std::filesystem::path& path)
        {
            std::filesystem::path assetsRoot = std::filesystem::current_path() / "assets";
            std::filesystem::path scriptBuildDirectory = assetsRoot / "Scripts" / "Build";

            // meta는 AssetDatabase가 에셋을 정리하면서 직접 쓰는 sidecar 파일이다.
            // 이것까지 다시 변경으로 처리하면 "스캔 -> meta 저장 -> 감지 -> 스캔" 루프가 생긴다.
            if (HasExtension(path, ".meta"))
                return true;

            // 스크립트 런타임 DLL/PDB는 에디터가 만들어내는 산출물이다.
            // 이 파일들까지 에셋 변경으로 처리하면 아무 입력이 없어도 전체 assets 스캔이 반복된다.
            if (IsPathUnderDirectory(path, scriptBuildDirectory))
                return true;

            return false;
        }

        DirectX::XMMATRIX GetEditorLocalTransform(Entity entity)
        {
            auto& tc = entity.GetComponent<TransformComponent>();
            return DirectX::XMMatrixScaling(tc.Scale.x, tc.Scale.y, tc.Scale.z) *
                DirectX::XMMatrixRotationQuaternion(DirectX::XMLoadFloat4(&tc.QuaternionRotation)) *
                DirectX::XMMatrixTranslation(tc.Translation.x, tc.Translation.y, tc.Translation.z);
        }

        DirectX::XMMATRIX GetEditorWorldTransform(Entity entity)
        {
            DirectX::XMMATRIX transform = GetEditorLocalTransform(entity);

            if (entity.HasComponent<RelationshipComponent>())
            {
                entt::entity parentID = entity.GetComponent<RelationshipComponent>().Parent;
                if (parentID != entt::null)
                    transform = transform * GetEditorWorldTransform(Entity{ parentID, entity.GetScene() });
            }

            return transform;
        }

        DirectX::XMFLOAT3 GetEditorWorldPosition(Entity entity)
        {
            DirectX::XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
            if (!entity || !entity.HasComponent<TransformComponent>())
                return position;

            DirectX::XMStoreFloat3(&position, GetEditorWorldTransform(entity).r[3]);
            return position;
        }

        void AccumulateFrameBounds(Entity entity, DirectX::XMVECTOR& minPoint, DirectX::XMVECTOR& maxPoint, bool& hasPoint)
        {
            if (!entity || !entity.HasComponent<TransformComponent>())
                return;

            DirectX::XMFLOAT3 worldPosition = GetEditorWorldPosition(entity);
            DirectX::XMVECTOR point = DirectX::XMLoadFloat3(&worldPosition);

            if (!hasPoint)
            {
                minPoint = point;
                maxPoint = point;
                hasPoint = true;
            }
            else
            {
                minPoint = DirectX::XMVectorMin(minPoint, point);
                maxPoint = DirectX::XMVectorMax(maxPoint, point);
            }

            if (!entity.HasComponent<RelationshipComponent>())
                return;

            for (entt::entity childID : entity.GetComponent<RelationshipComponent>().Children)
                AccumulateFrameBounds(Entity{ childID, entity.GetScene() }, minPoint, maxPoint, hasPoint);
        }

        DirectX::XMFLOAT3 TransformPoint(const DirectX::XMFLOAT3& point, DirectX::XMMATRIX transform)
        {
            DirectX::XMFLOAT3 result;
            DirectX::XMStoreFloat3(&result, DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&point), transform));
            return result;
        }

        void DrawWorldDebugLine(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, float thickness, const DirectX::XMFLOAT4& color)
        {
            float dx = b.x - a.x;
            float dy = b.y - a.y;
            float length = std::sqrt(dx * dx + dy * dy);
            if (length <= 0.0001f)
                return;

            DirectX::XMFLOAT3 center = {
                (a.x + b.x) * 0.5f,
                (a.y + b.y) * 0.5f,
                (a.z + b.z) * 0.5f
            };

            float angle = std::atan2(dy, dx);
            auto drawLineQuad = [&](float drawThickness, const DirectX::XMFLOAT4& drawColor)
                {
                    DirectX::XMMATRIX transform =
                        DirectX::XMMatrixScaling(length, drawThickness, 1.0f) *
                        DirectX::XMMatrixRotationZ(angle) *
                        DirectX::XMMatrixTranslation(center.x, center.y, center.z + 0.035f);
                    Renderer2D::DrawQuad(transform, drawColor, -1);
                };

            // 밝은 선만 그리면 같은 밝기의 메쉬 위에서 콜라이더 경계가 사라져 보인다.
            // 어두운 받침선을 먼저 깔고 실제 색을 얹어 상용 에디터의 outline처럼 읽히게 한다.
            drawLineQuad(thickness * 1.9f, { 0.02f, 0.02f, 0.02f, 0.82f });
            drawLineQuad(thickness, color);
        }

        void DrawWorldDebugLine3D(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, const DirectX::XMFLOAT3& cameraPosition, float thickness, const DirectX::XMFLOAT4& color)
        {
            DirectX::XMVECTOR start = DirectX::XMLoadFloat3(&a);
            DirectX::XMVECTOR end = DirectX::XMLoadFloat3(&b);
            DirectX::XMVECTOR line = DirectX::XMVectorSubtract(end, start);
            float length = DirectX::XMVectorGetX(DirectX::XMVector3Length(line));
            if (length <= 0.0001f)
                return;

            DirectX::XMVECTOR center = DirectX::XMVectorScale(DirectX::XMVectorAdd(start, end), 0.5f);
            DirectX::XMVECTOR lineAxis = DirectX::XMVector3Normalize(line);
            DirectX::XMVECTOR camera = DirectX::XMLoadFloat3(&cameraPosition);
            DirectX::XMVECTOR toCamera = DirectX::XMVectorSubtract(camera, center);
            if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(toCamera)) <= 0.0001f)
                toCamera = DirectX::XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f);

            DirectX::XMVECTOR side = DirectX::XMVector3Cross(lineAxis, toCamera);
            if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(side)) <= 0.0001f)
                side = DirectX::XMVector3Cross(lineAxis, DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
            if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(side)) <= 0.0001f)
                side = DirectX::XMVector3Cross(lineAxis, DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
            side = DirectX::XMVector3Normalize(side);

            DirectX::XMVECTOR normal = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(side, lineAxis));
            DirectX::XMVECTOR xAxis = DirectX::XMVectorScale(lineAxis, length);
            DirectX::XMVECTOR yAxis = DirectX::XMVectorScale(side, thickness);
            DirectX::XMVECTOR zAxis = DirectX::XMVectorScale(normal, thickness);

            DirectX::XMFLOAT3 x, y, z, c;
            DirectX::XMStoreFloat3(&x, xAxis);
            DirectX::XMStoreFloat3(&y, yAxis);
            DirectX::XMStoreFloat3(&z, zAxis);
            DirectX::XMStoreFloat3(&c, center);

            auto drawBillboardLine = [&](float drawThickness, const DirectX::XMFLOAT4& drawColor)
                {
                    DirectX::XMVECTOR drawYAxis = DirectX::XMVectorScale(side, drawThickness);
                    DirectX::XMFLOAT3 drawY;
                    DirectX::XMStoreFloat3(&drawY, drawYAxis);

                    // 3D 와이어는 선분마다 카메라를 향하는 얇은 사각형으로 그린다.
                    // 별도 라인 렌더러가 없더라도 카메라 회전에서 두께가 납작해지지 않는다.
                    DirectX::XMMATRIX transform(
                        x.x, x.y, x.z, 0.0f,
                        drawY.x, drawY.y, drawY.z, 0.0f,
                        z.x, z.y, z.z, 0.0f,
                        c.x, c.y, c.z, 1.0f);
                    Renderer2D::DrawQuad(transform, drawColor, -1);
                };

            drawBillboardLine(thickness * 2.0f, { 0.02f, 0.02f, 0.02f, 0.78f });
            drawBillboardLine(thickness, color);
        }

        void DrawColliderBoxOutline(DirectX::XMMATRIX entityWorld, const BoxCollider2DComponent& collider, const DirectX::XMFLOAT4& color)
        {
            DirectX::XMMATRIX colliderLocal =
                DirectX::XMMatrixScaling(collider.Size.x, collider.Size.y, 1.0f) *
                DirectX::XMMatrixTranslation(collider.Offset.x, collider.Offset.y, 0.0f);
            DirectX::XMMATRIX colliderWorld = colliderLocal * entityWorld;

            DirectX::XMFLOAT3 corners[4] =
            {
                TransformPoint({ -0.5f, -0.5f, 0.0f }, colliderWorld),
                TransformPoint({  0.5f, -0.5f, 0.0f }, colliderWorld),
                TransformPoint({  0.5f,  0.5f, 0.0f }, colliderWorld),
                TransformPoint({ -0.5f,  0.5f, 0.0f }, colliderWorld)
            };

            constexpr float lineThickness = 0.04f;
            DrawWorldDebugLine(corners[0], corners[1], lineThickness, color);
            DrawWorldDebugLine(corners[1], corners[2], lineThickness, color);
            DrawWorldDebugLine(corners[2], corners[3], lineThickness, color);
            DrawWorldDebugLine(corners[3], corners[0], lineThickness, color);
        }

        void DrawWireBox3D(DirectX::XMMATRIX colliderWorld, const DirectX::XMFLOAT3& cameraPosition, const DirectX::XMFLOAT4& color)
        {
            DirectX::XMFLOAT3 corners[8] =
            {
                TransformPoint({ -0.5f, -0.5f, -0.5f }, colliderWorld),
                TransformPoint({  0.5f, -0.5f, -0.5f }, colliderWorld),
                TransformPoint({  0.5f,  0.5f, -0.5f }, colliderWorld),
                TransformPoint({ -0.5f,  0.5f, -0.5f }, colliderWorld),
                TransformPoint({ -0.5f, -0.5f,  0.5f }, colliderWorld),
                TransformPoint({  0.5f, -0.5f,  0.5f }, colliderWorld),
                TransformPoint({  0.5f,  0.5f,  0.5f }, colliderWorld),
                TransformPoint({ -0.5f,  0.5f,  0.5f }, colliderWorld)
            };

            constexpr std::array<std::pair<int, int>, 12> edges =
            {
                std::pair<int, int>{0, 1}, {1, 2}, {2, 3}, {3, 0},
                {4, 5}, {5, 6}, {6, 7}, {7, 4},
                {0, 4}, {1, 5}, {2, 6}, {3, 7}
            };

            constexpr float lineThickness = 0.035f;
            for (const auto& edge : edges)
                DrawWorldDebugLine3D(corners[edge.first], corners[edge.second], cameraPosition, lineThickness, color);
        }

        void DrawMeshColliderWire3D(const std::shared_ptr<Mesh>& mesh, DirectX::XMMATRIX meshWorld, const DirectX::XMFLOAT3& cameraPosition, const DirectX::XMFLOAT4& color)
        {
            if (!mesh)
                return;

            const std::vector<Vertex3D>& vertices = mesh->GetVertices();
            const std::vector<uint32_t>& indices = mesh->GetIndices();
            if (vertices.empty() || indices.size() < 3)
                return;

            constexpr float lineThickness = 0.006f;
            constexpr size_t maxDebugEdges = 6000;
            std::unordered_set<uint64_t> drawnEdges;
            drawnEdges.reserve((std::min)(indices.size(), maxDebugEdges * 2));

            auto makeEdgeKey = [](uint32_t a, uint32_t b)
                {
                    uint32_t lo = (std::min)(a, b);
                    uint32_t hi = (std::max)(a, b);
                    return (uint64_t)lo << 32 | (uint64_t)hi;
                };

            auto drawEdge = [&](uint32_t a, uint32_t b)
                {
                    if (a >= vertices.size() || b >= vertices.size())
                        return;

                    uint64_t key = makeEdgeKey(a, b);
                    if (!drawnEdges.insert(key).second)
                        return;

                    DirectX::XMFLOAT3 start = TransformPoint(vertices[a].Position, meshWorld);
                    DirectX::XMFLOAT3 end = TransformPoint(vertices[b].Position, meshWorld);
                    DrawWorldDebugLine3D(start, end, cameraPosition, lineThickness, color);
                };

            // 메시 와이어는 렌더 메쉬 정점을 그대로 사용한다.
            // collider.Size는 bounds 표시용 값이므로 여기에 곱하면 실제 모델과 선 위치가 어긋난다.
            // 같은 edge는 한 번만 그리고, 너무 큰 메시에서는 상한을 둬 씬 조작 프레임을 지킨다.
            for (size_t i = 0; i + 2 < indices.size() && drawnEdges.size() < maxDebugEdges; i += 3)
            {
                uint32_t a = indices[i + 0];
                uint32_t b = indices[i + 1];
                uint32_t c = indices[i + 2];
                drawEdge(a, b);
                drawEdge(b, c);
                drawEdge(c, a);
            }
        }

        void DrawBoxCollider3DOutline(DirectX::XMMATRIX entityWorld, const BoxCollider3DComponent& collider, const DirectX::XMFLOAT3& cameraPosition, const DirectX::XMFLOAT4& color)
        {
            DirectX::XMMATRIX colliderWorld =
                DirectX::XMMatrixScaling(collider.Size.x, collider.Size.y, collider.Size.z) *
                DirectX::XMMatrixTranslation(collider.Offset.x, collider.Offset.y, collider.Offset.z) *
                entityWorld;
            DrawWireBox3D(colliderWorld, cameraPosition, color);
        }

        void DrawSphereCollider3DOutline(DirectX::XMMATRIX entityWorld, const SphereCollider3DComponent& collider, const DirectX::XMFLOAT3& cameraPosition, const DirectX::XMFLOAT4& color)
        {
            DirectX::XMMATRIX colliderWorld =
                DirectX::XMMatrixScaling(collider.Radius, collider.Radius, collider.Radius) *
                DirectX::XMMatrixTranslation(collider.Offset.x, collider.Offset.y, collider.Offset.z) *
                entityWorld;

            constexpr int segments = 36;
            constexpr float pi = 3.1415926535f;
            constexpr float lineThickness = 0.03f;

            for (int plane = 0; plane < 3; ++plane)
            {
                DirectX::XMFLOAT3 previous{};
                for (int i = 0; i <= segments; ++i)
                {
                    float angle = (float)i / (float)segments * pi * 2.0f;
                    float c = std::cos(angle);
                    float s = std::sin(angle);
                    DirectX::XMFLOAT3 local =
                        plane == 0 ? DirectX::XMFLOAT3{ c, s, 0.0f } :
                        plane == 1 ? DirectX::XMFLOAT3{ c, 0.0f, s } :
                                     DirectX::XMFLOAT3{ 0.0f, c, s };
                    DirectX::XMFLOAT3 current = TransformPoint(local, colliderWorld);
                    if (i > 0)
                        DrawWorldDebugLine3D(previous, current, cameraPosition, lineThickness, color);
                    previous = current;
                }
            }
        }

        void DrawCylinderCollider3DOutline(DirectX::XMMATRIX entityWorld, const CylinderCollider3DComponent& collider, const DirectX::XMFLOAT3& cameraPosition, const DirectX::XMFLOAT4& color)
        {
            DirectX::XMMATRIX colliderWorld =
                DirectX::XMMatrixTranslation(collider.Offset.x, collider.Offset.y, collider.Offset.z) *
                entityWorld;

            constexpr int segments = 36;
            constexpr float pi = 3.1415926535f;
            constexpr float lineThickness = 0.03f;
            const float halfHeight = collider.Height * 0.5f;

            DirectX::XMFLOAT3 previousTop{};
            DirectX::XMFLOAT3 previousBottom{};
            for (int i = 0; i <= segments; ++i)
            {
                float angle = (float)i / (float)segments * pi * 2.0f;
                float x = std::cos(angle) * collider.Radius;
                float z = std::sin(angle) * collider.Radius;
                DirectX::XMFLOAT3 top = TransformPoint({ x, halfHeight, z }, colliderWorld);
                DirectX::XMFLOAT3 bottom = TransformPoint({ x, -halfHeight, z }, colliderWorld);
                if (i > 0)
                {
                    DrawWorldDebugLine3D(previousTop, top, cameraPosition, lineThickness, color);
                    DrawWorldDebugLine3D(previousBottom, bottom, cameraPosition, lineThickness, color);
                }
                if (i % 9 == 0)
                    DrawWorldDebugLine3D(top, bottom, cameraPosition, lineThickness, color);
                previousTop = top;
                previousBottom = bottom;
            }
        }

        DirectX::XMFLOAT4 GetRigidbodyDebugColor(Rigidbody2DComponent::BodyType type)
        {
            switch (type)
            {
                case Rigidbody2DComponent::BodyType::Dynamic: return { 0.35f, 0.55f, 1.0f, 0.9f };
                case Rigidbody2DComponent::BodyType::Kinematic: return { 0.75f, 0.45f, 1.0f, 0.9f };
                case Rigidbody2DComponent::BodyType::Static:
                default: return { 0.72f, 0.72f, 0.72f, 0.9f };
            }
        }
    }

    EditorLayer::EditorLayer()
        : Layer("EditorLayer"), m_Camera(45.0f, 1280.0f / 720.0f, 0.1f, 100.0f)
    {
        m_GizmoSystem.Init(); // 기즈모 시스템 초기화
    }

    void EditorLayer::OnAttach()
    {
        m_ProjectSettings.Load();
        m_ProjectSettings.Normalize();

        FramebufferSpecification fbSpec;
        fbSpec.Width = 1280;
        fbSpec.Height = 720;
        m_Framebuffer = Framebuffer::Create(fbSpec);

        FramebufferSpecification gameFbSpec;
        gameFbSpec.Width = m_ProjectSettings.Data().GameWidth;
        gameFbSpec.Height = m_ProjectSettings.Data().GameHeight;
        m_GameFramebuffer = Framebuffer::Create(gameFbSpec);
        m_GameViewportSize = { (float)gameFbSpec.Width, (float)gameFbSpec.Height };

        m_ActiveScene = new Scene();

        // ==========================================
        // 씬 기본 오브젝트 세팅 
        // ==========================================
        auto cameraEntity = m_ActiveScene->CreateEntity("Main Camera");
        auto& cameraComp = cameraEntity.AddComponent<CameraComponent>();
        cameraComp.Primary = true;
        auto& camTransform = cameraEntity.GetComponent<TransformComponent>();
        camTransform.Translation = { 0.0f, 3.0f, -6.0f };
        camTransform.Rotation = { DirectX::XMConvertToRadians(20.0f), 0.0f, 0.0f };
        DirectX::XMVECTOR quat = DirectX::XMQuaternionRotationRollPitchYaw(camTransform.Rotation.x, camTransform.Rotation.y, camTransform.Rotation.z);
        DirectX::XMStoreFloat4(&camTransform.QuaternionRotation, quat);

        auto mainLight = m_ActiveScene->CreateEntity("Main Light (Warm)");
        auto& tcMain = mainLight.GetComponent<TransformComponent>();
        tcMain.Rotation = { DirectX::XMConvertToRadians(45.0f), DirectX::XMConvertToRadians(-45.0f), 0.0f };
        DirectX::XMVECTOR qMain = DirectX::XMQuaternionRotationRollPitchYaw(tcMain.Rotation.x, tcMain.Rotation.y, tcMain.Rotation.z);
        DirectX::XMStoreFloat4(&tcMain.QuaternionRotation, qMain);
        auto& lcMain = mainLight.AddComponent<LightComponent>();
        lcMain.LightColor = { 1.0f, 0.9f, 0.8f };
        lcMain.Intensity = 1.0f;

        auto fillLight = m_ActiveScene->CreateEntity("Fill Light (Cool)");
        auto& tcFill = fillLight.GetComponent<TransformComponent>();
        tcFill.Rotation = { DirectX::XMConvertToRadians(15.0f), DirectX::XMConvertToRadians(135.0f), 0.0f };
        DirectX::XMVECTOR qFill = DirectX::XMQuaternionRotationRollPitchYaw(tcFill.Rotation.x, tcFill.Rotation.y, tcFill.Rotation.z);
        DirectX::XMStoreFloat4(&tcFill.QuaternionRotation, qFill);
        auto& lcFill = fillLight.AddComponent<LightComponent>();
        lcFill.LightColor = { 0.4f, 0.5f, 1.0f };
        lcFill.Intensity = 0.5f;

        auto cube1 = m_ActiveScene->CreateEntity("Cube 1 (No Tex)");
        auto& tc1 = cube1.GetComponent<TransformComponent>();
        tc1.Translation = { -2.5f, 0.0f, 2.0f };
        auto& mesh1 = cube1.AddComponent<MeshComponent>(MeshComponent::MeshType::Cube);
        mesh1.MeshData = MeshFactory::CreateCube();
        mesh1.BaseColor = { 0.2f, 0.3f, 0.8f, 1.0f };

        Entity mayoModel = ModelImporter::ImportModel(m_ActiveScene, "assets/Chocolate rice/0.MAYO/FBX/FBX_MAYO.fbx");
        ConsoleLog::Info("Editor scene initialized.");

        // --- 에디터 기본 UI 세팅 ---
        BuildEditorUI();
        ConfigureUndoManager();

        std::filesystem::path assetsRoot = std::filesystem::current_path() / "assets";
        bool assetWatcherStarted = m_AssetFileWatcher.Start(assetsRoot);
        for (UI::AssetBrowserPanel* browser : m_AssetBrowserPanels)
        {
            if (browser)
                browser->SetExternalWatcherActive(assetWatcherStarted);
        }
        ConsoleLog::Info(assetWatcherStarted ? "Asset file watcher started." : "Asset file watcher unavailable. Polling fallback enabled.");

        // --- 인스펙터 패널에 기본 컴포넌트 등록 ---
        UI::InspectorUtils::InitStandardComponents();

        ValidateAssetReferences(true);

        std::string startScenePath = m_ProjectSettings.Data().StartScenePath;
        if (!m_ProjectSettings.Data().StartSceneGuid.empty())
        {
            std::filesystem::path resolved = AssetDatabase::GetPathFromGuid(m_ProjectSettings.Data().StartSceneGuid);
            if (!resolved.empty())
                startScenePath = resolved.string();
        }
        if (!startScenePath.empty())
            OpenScene(startScenePath);

        Application* app = Application::Get();
        if (app)
        {
            m_RunEditorQAOnStartup = app->HasCommandLineFlag("--run-editor-qa");
            m_CloseAfterEditorQA = app->HasCommandLineFlag("--exit");
        }
    }

    void EditorLayer::OnDetach()
    {
        m_AssetFileWatcher.Stop();

        if (IsInPlayMode())
        {
            if (m_ActiveScene)
            {
                m_ActiveScene->OnRuntimeStop();
                delete m_ActiveScene;
            }
            delete m_EditorScene;
        }
        else
        {
            delete m_ActiveScene;
        }
        m_ActiveScene = nullptr;
        m_EditorScene = nullptr;

        delete m_Framebuffer;
        delete m_GameFramebuffer;
        delete m_RootUI;
    }

    void EditorLayer::OnUpdate(float deltaTime)
    {
        auto editorFrameStartedAt = std::chrono::steady_clock::now();
        std::vector<EditorHitchStage> editorHitchStages;
        auto editorStageStartedAt = std::chrono::steady_clock::now();

        if (m_RunEditorQAOnStartup && !m_EditorQACompleted)
        {
            ++m_EditorQAFramesAfterAttach;
            if (m_EditorQAFramesAfterAttach >= 2)
            {
                // QA는 UI가 한 번 레이아웃된 뒤 실행한다.
                // 버튼 좌표, 드롭다운 hit-test처럼 화면 크기에 의존하는 검사는 초기 배치 전에는 의미가 없다.
                RunEditorQualityAssurance(m_CloseAfterEditorQA);
            }
        }

        // 외부 컴파일 작업의 완료 결과는 메인 스레드에서 Console로 전달한다.
        if (ScriptCompiler::Update())
        {
            // 스크립트 manifest가 바뀌면 인스펙터의 public field 목록도 다시 만들어야 한다.
            for (UI::InspectorPanel* inspector : m_InspectorPanels)
            {
                if (inspector)
                    inspector->RequestRebuild();
            }
        }
        AddEditorHitchStage(editorHitchStages, "ScriptCompiler", editorStageStartedAt);

        editorStageStartedAt = std::chrono::steady_clock::now();
        if (m_PendingAssetReferenceValidation)
        {
            auto now = std::chrono::steady_clock::now();
            if (now - m_AssetReferenceValidationRequestedAt >= std::chrono::milliseconds(350))
            {
                // 에셋 작업 직후에는 History와 브라우저 UI를 먼저 갱신한다.
                // 씬/프리팹 GUID 검사는 파일 전체를 훑을 수 있으므로 짧게 미뤄 연속 작업을 한 번으로 묶는다.
                m_PendingAssetReferenceValidation = false;
                ValidateAssetReferences(false);
            }
        }
        AddEditorHitchStage(editorHitchStages, "AssetReferenceValidation", editorStageStartedAt);

        auto& mainWindow = CCEngine::Application::Get()->GetWindow();

        // 1. 뷰포트 크기 계산 및 프레임버퍼 리사이즈
        auto getVisibleImageSize = [](const std::vector<UI::ImageWidget*>& widgets, DirectX::XMFLOAT2 fallback)
        {
            for (UI::ImageWidget* widget : widgets)
            {
                if (!widget || !widget->IsVisible())
                    continue;
                UI::Widget* parent = widget->GetParent();
                if (parent && !parent->IsVisible())
                    continue;

                auto size = widget->GetCalculatedSize();
                if (size.x > 0.0f && size.y > 0.0f)
                    return size;
            }
            return fallback;
        };

        editorStageStartedAt = std::chrono::steady_clock::now();
        if (!m_ViewportWidgets.empty())
        {
            auto vpSize = getVisibleImageSize(m_ViewportWidgets, m_ViewportSize);
            if (vpSize.x > 0 && vpSize.y > 0) m_ViewportSize = { vpSize.x, vpSize.y };
        }
        if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f &&
            (m_Framebuffer->GetSpecification().Width != (uint32_t)m_ViewportSize.x ||
                m_Framebuffer->GetSpecification().Height != (uint32_t)m_ViewportSize.y))
        {
            m_Framebuffer->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
            m_Camera.SetProjectionMatrix(m_Camera.GetFOV(), m_ViewportSize.x / m_ViewportSize.y, 0.1f, 100.0f);
        }

        if (!m_GameViewWidgets.empty())
        {
            auto gameSize = getVisibleImageSize(m_GameViewWidgets, m_GameViewportSize);
            // 에디터의 Game View는 창 크기를 따라간다.
            // 프로젝트 해상도는 빌드/플레이 기본값이고, 에디터 패널 크기를 고정하지 않는다.
            if (gameSize.x > 0.0f && gameSize.y > 0.0f)
                m_GameViewportSize = { gameSize.x, gameSize.y };
        }

        if (m_GameViewportSize.x > 0.0f && m_GameViewportSize.y > 0.0f &&
            (m_GameFramebuffer->GetSpecification().Width != (uint32_t)m_GameViewportSize.x ||
                m_GameFramebuffer->GetSpecification().Height != (uint32_t)m_GameViewportSize.y))
        {
            m_GameFramebuffer->Resize((uint32_t)m_GameViewportSize.x, (uint32_t)m_GameViewportSize.y);
        }
        AddEditorHitchStage(editorHitchStages, "ViewportResize", editorStageStartedAt);

        auto isWidgetOrChildOfForUpdate = [](UI::Widget* widget, UI::Widget* parent) -> bool
            {
                while (widget)
                {
                    if (widget == parent)
                        return true;
                    widget = widget->GetParent();
                }
                return false;
            };

        std::function<UI::Widget*(UI::Widget*, float, float)> getTopmostWidgetAtForUpdate =
            [&](UI::Widget* widget, float mouseX, float mouseY) -> UI::Widget*
            {
                if (!widget || !widget->IsVisible() || !widget->IsPointInside(mouseX, mouseY))
                    return nullptr;

                const auto& children = widget->GetChildren();
                for (auto it = children.rbegin(); it != children.rend(); ++it)
                {
                    if (UI::Widget* hit = getTopmostWidgetAtForUpdate(*it, mouseX, mouseY))
                        return hit;
                }
                return widget;
            };

        bool allowSceneCameraNavigation = false;
        if (m_RootUI && m_ViewportWidget)
        {
            auto [mouseX, mouseY] = mainWindow.GetMousePosition();
            UI::Widget* topmost = getTopmostWidgetAtForUpdate(m_RootUI, mouseX, mouseY);
            // 씬 카메라는 뷰포트 위에 다른 창이 없을 때만 움직인다.
            // 창 이동/우클릭 메뉴처럼 위에 떠 있는 UI는 항상 씬 뷰보다 먼저 입력을 가져야 한다.
            allowSceneCameraNavigation =
                m_ViewportWidget->IsPointInside(mouseX, mouseY) &&
                isWidgetOrChildOfForUpdate(topmost, m_ViewportWidget);
        }

        // 2. 카메라 및 로직 업데이트
        editorStageStartedAt = std::chrono::steady_clock::now();
        m_Camera.OnUpdate(deltaTime, m_ProjectSettings.Data(), allowSceneCameraNavigation);
        HandleShortcuts();
        AddEditorHitchStage(editorHitchStages, "CameraShortcuts", editorStageStartedAt);

        editorStageStartedAt = std::chrono::steady_clock::now();
        ProcessAssetFileWatcher();
        AddEditorHitchStage(editorHitchStages, "AssetFileWatcher", editorStageStartedAt);

        // 선택된 엔티티가 있다면 인스펙터 패널에 전달하여 UI 갱신
        editorStageStartedAt = std::chrono::steady_clock::now();
        if (m_HierarchyPanel && m_InspectorPanel)
        {
            Entity selected = m_HierarchyPanel->GetSelectedEntity();
            for (UI::InspectorPanel* inspector : m_InspectorPanels)
            {
                if (inspector)
                    inspector->SetSelectedEntity(selected);
            }
        }
        AddEditorHitchStage(editorHitchStages, "SelectionSync", editorStageStartedAt);

        editorStageStartedAt = std::chrono::steady_clock::now();
        if (m_RootUI)
        {
            BringEditorOverlaysToFront();

            float winWidth = (float)mainWindow.GetWidth();
            float winHeight = (float)mainWindow.GetHeight();

            if (winWidth >= 50.0f && winHeight >= 50.0f)
            {
                m_RootUI->UpdateLayout({ 0.0f, 0.0f }, { winWidth, winHeight });
            }
        }
        AddEditorHitchStage(editorHitchStages, "RootUILayout", editorStageStartedAt);

        editorStageStartedAt = std::chrono::steady_clock::now();
        if (m_HistoryPanelDirty)
            RebuildHistoryPanel();
        AddEditorHitchStage(editorHitchStages, "HistoryRebuild", editorStageStartedAt);
        // =========================================================================

        // 최신 프레임버퍼 텍스처를 뷰포트 위젯에 연결
        editorStageStartedAt = std::chrono::steady_clock::now();
        RendererHandle editorTexture = m_Framebuffer->GetColorAttachmentRendererID(0);
        for (UI::ImageWidget* viewportWidget : m_ViewportWidgets)
        {
            if (viewportWidget)
                viewportWidget->SetTexture(editorTexture);
        }
        RendererHandle gameTexture = m_GameFramebuffer->GetColorAttachmentRendererID(0);
        for (UI::ImageWidget* gameViewWidget : m_GameViewWidgets)
        {
            if (gameViewWidget)
                gameViewWidget->SetTexture(gameTexture);
        }
        AddEditorHitchStage(editorHitchStages, "ViewportTextureAssign", editorStageStartedAt);

        // 3. 에디터 프레임버퍼 렌더링
        editorStageStartedAt = std::chrono::steady_clock::now();
        Renderer::SetClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        Renderer::Clear();

        m_Framebuffer->Bind();
        Renderer::SetClearColor(m_ClearColor[0], m_ClearColor[1], m_ClearColor[2], m_ClearColor[3]);
        Renderer::Clear();
        m_Framebuffer->ClearAttachment(1, -1);

        m_ActiveScene->OnUpdate(deltaTime);
        m_ActiveScene->OnRender2D(m_Camera);
        m_ActiveScene->OnRender3D(m_Camera);

        // 자체 기즈모 시스템 구현
        auto selectedEntity = m_HierarchyPanel->GetSelectedEntity();
        auto selectedEntities = m_HierarchyPanel->GetSelectedEntities();
        RenderPhysicsDebugView(m_Camera, selectedEntities);
        m_GizmoSystem.OnRenderSkeleton(selectedEntity);
        m_GizmoSystem.OnRender(selectedEntities, selectedEntity, m_Camera.GetViewMatrix(), m_Camera.GetProjectionMatrix());

        m_Framebuffer->Unbind();
        AddEditorHitchStage(editorHitchStages, "EditorSceneRender", editorStageStartedAt);

        // 4. 게임 프레임버퍼 렌더링
        editorStageStartedAt = std::chrono::steady_clock::now();
        m_GameFramebuffer->Bind();
        Renderer::SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        Renderer::Clear();

        auto view = m_ActiveScene->GetRegistry().view<CameraComponent>();
        for (auto entity : view)
        {
            auto& cameraComp = view.get<CameraComponent>(entity);
            CCEngine::Entity cameraEntity(entity, m_ActiveScene);
            if (cameraComp.Primary && m_ActiveScene->IsEntityActiveInHierarchy(cameraEntity))
            {
                auto& transformComp = cameraEntity.GetComponent<TransformComponent>();

                float aspect = 16.0f / 9.0f;
                if (m_GameViewportSize.y > 0.001f) {
                    aspect = m_GameViewportSize.x / m_GameViewportSize.y;
                }

                PerspectiveCamera gameCamera(cameraComp.FOV, aspect, cameraComp.NearClip, cameraComp.FarClip);
                gameCamera.SetPosition(transformComp.Translation);
                gameCamera.SetRotation(transformComp.QuaternionRotation);

                m_ActiveScene->OnRender2D(gameCamera);
                m_ActiveScene->OnRender3D(gameCamera);
                break;
            }
        }
        m_GameFramebuffer->Unbind();
        AddEditorHitchStage(editorHitchStages, "GameSceneRender", editorStageStartedAt);

        editorStageStartedAt = std::chrono::steady_clock::now();
        if (!m_IsMultiTransformUndoOpen)
            m_UndoManager.TrackTransformUndo();
        AddEditorHitchStage(editorHitchStages, "UndoTracking", editorStageStartedAt);
        ReportEditorHitch(editorFrameStartedAt, editorHitchStages);
    }

    void EditorLayer::ProcessAssetFileWatcher()
    {
        std::vector<std::filesystem::path> changedPaths;
        const auto now = std::chrono::steady_clock::now();

        if (m_AssetFileWatcher.ConsumeDebouncedChanges(changedPaths))
        {
            bool collectedProjectAssetChange = false;
            for (const auto& path : changedPaths)
            {
                if (IsIgnoredAssetWatcherPath(path))
                    continue;

                auto found = std::find(
                    m_PendingAssetFileWatcherPaths.begin(),
                    m_PendingAssetFileWatcherPaths.end(),
                    path);

                if (found == m_PendingAssetFileWatcherPaths.end())
                    m_PendingAssetFileWatcherPaths.push_back(path);

                collectedProjectAssetChange = true;
            }

            if (collectedProjectAssetChange)
            {
                // 외부 저장 하나는 create/write/rename 이벤트 여러 개로 들어온다.
                // 마지막 이벤트 뒤에 한 번만 전체 DB를 맞춰야 UI가 순간적으로 멈추지 않는다.
                m_PendingAssetFileRefresh = true;
                m_AssetFileRefreshRequestedAt = now;
            }
        }

        if (!m_PendingAssetFileRefresh)
            return;

        constexpr auto refreshDelay = std::chrono::milliseconds(650);
        if (now - m_AssetFileRefreshRequestedAt < refreshDelay)
            return;

        std::filesystem::path assetsRoot = std::filesystem::current_path() / "assets";

        // 파일 감시 스레드는 신호만 모으고, 실제 DB 갱신은 메인 스레드에서 한다.
        // 렌더/UI가 쓰는 캐시를 다른 스레드에서 건드리면 재현 어려운 충돌이 생긴다.
        AssetDatabase::MarkDirty(assetsRoot);
        AssetDatabase::Scan(assetsRoot);

        m_PendingAssetFileRefresh = false;
        m_PendingAssetFileWatcherPaths.clear();

        for (UI::AssetBrowserPanel* browser : m_AssetBrowserPanels)
        {
            if (browser)
                browser->OnExternalAssetFilesChanged();
        }

        QueueAssetReferenceValidation();
    }

    void EditorLayer::BringEditorOverlaysToFront()
    {
        if (!m_RootUI)
            return;

        // 메뉴와 팝업은 일반 창보다 높은 레이어로 취급한다.
        // 움직인 패널이 BringToFront 되어도 드롭다운이 그 아래에 깔리면 안 된다.
        if (m_TitleBarPanel) m_TitleBarPanel->BringToFront();
        if (m_MenuBarPanel) m_MenuBarPanel->BringToFront();
        if (m_ProjectSettingsPanel) m_ProjectSettingsPanel->BringToFront();
        if (m_ObjectContextMenuPanel) m_ObjectContextMenuPanel->BringToFront();
        if (m_MeshObjectSubmenuPanel) m_MeshObjectSubmenuPanel->BringToFront();
        if (m_FileDropdownPanel) m_FileDropdownPanel->BringToFront();
        if (m_EditDropdownPanel) m_EditDropdownPanel->BringToFront();
        if (m_WindowDropdownPanel) m_WindowDropdownPanel->BringToFront();
        if (m_ColliderDebugDropdownPanel) m_ColliderDebugDropdownPanel->BringToFront();
    }

    void EditorLayer::OnEvent(Event& e)
    {
        auto getMousePoint = [](Event& event, float& mouseX, float& mouseY) -> bool
            {
                if (event.GetEventType() == EventType::MouseButtonPressed)
                {
                    auto& mouseEvent = static_cast<MouseButtonPressedEvent&>(event);
                    mouseX = mouseEvent.GetX();
                    mouseY = mouseEvent.GetY();
                    return true;
                }
                if (event.GetEventType() == EventType::MouseMoved)
                {
                    auto& mouseEvent = static_cast<MouseMovedEvent&>(event);
                    mouseX = mouseEvent.GetX();
                    mouseY = mouseEvent.GetY();
                    return true;
                }
                if (event.GetEventType() == EventType::MouseButtonReleased)
                {
                    auto& mouseEvent = static_cast<MouseButtonReleasedEvent&>(event);
                    mouseX = mouseEvent.GetX();
                    mouseY = mouseEvent.GetY();
                    return true;
                }
                return false;
            };

        std::function<UI::Widget*(UI::Widget*, float, float)> getTopmostWidgetAt =
            [&](UI::Widget* widget, float mouseX, float mouseY) -> UI::Widget*
            {
                if (!widget || !widget->IsVisible() || !widget->IsPointInside(mouseX, mouseY))
                    return nullptr;

                const auto& children = widget->GetChildren();
                for (auto it = children.rbegin(); it != children.rend(); ++it)
                {
                    if (UI::Widget* hit = getTopmostWidgetAt(*it, mouseX, mouseY))
                        return hit;
                }
                return widget;
            };

        auto isWidgetOrChildOf = [](UI::Widget* widget, UI::Widget* parent) -> bool
            {
                // 겹친 창에서는 마우스 아래 최상위 위젯만 입력을 가져야 한다.
                // 부모 체인을 타고 올라가며 현재 패널 안쪽 위젯인지 확인한다.
                while (widget)
                {
                    if (widget == parent)
                        return true;
                    widget = widget->GetParent();
                }
                return false;
            };

        float popupMouseX = 0.0f;
        float popupMouseY = 0.0f;
        bool isPopupMouseEvent = getMousePoint(e, popupMouseX, popupMouseY);
        bool hadBlockingPopup =
            (m_FileDropdownPanel && m_FileDropdownPanel->IsVisible()) ||
            (m_EditDropdownPanel && m_EditDropdownPanel->IsVisible()) ||
            (m_WindowDropdownPanel && m_WindowDropdownPanel->IsVisible()) ||
            (m_ColliderDebugDropdownPanel && m_ColliderDebugDropdownPanel->IsVisible()) ||
            (m_ObjectContextMenuPanel && m_ObjectContextMenuPanel->IsVisible()) ||
            (m_MeshObjectSubmenuPanel && m_MeshObjectSubmenuPanel->IsVisible()) ||
            (m_ProjectSettingsPanel && m_ProjectSettingsPanel->IsVisible());

        // 1. 드롭다운 바깥 클릭 시 닫히는 Focus Out 로직 
        if (e.GetEventType() == EventType::MouseButtonPressed)
        {
            MouseButtonPressedEvent& mouseEvent = static_cast<MouseButtonPressedEvent&>(e);
            RememberActiveAssetBrowserFromMouse(mouseEvent.GetX(), mouseEvent.GetY());

            bool insideObjectMenu = m_ObjectContextMenuPanel && m_ObjectContextMenuPanel->IsVisible() &&
                m_ObjectContextMenuPanel->IsPointInside(mouseEvent.GetX(), mouseEvent.GetY());
            bool insideMeshSubmenu = m_MeshObjectSubmenuPanel && m_MeshObjectSubmenuPanel->IsVisible() &&
                m_MeshObjectSubmenuPanel->IsPointInside(mouseEvent.GetX(), mouseEvent.GetY());
            if (m_ObjectContextMenuPanel && m_ObjectContextMenuPanel->IsVisible() &&
                !insideObjectMenu && !insideMeshSubmenu)
            {
                HideObjectContextMenu();
            }

            if (m_FileDropdownPanel && m_FileDropdownPanel->IsVisible())
            {
                if (!m_FileDropdownPanel->IsPointInside(mouseEvent.GetX(), mouseEvent.GetY()) &&
                    !m_BtnFileMenu->IsPointInside(mouseEvent.GetX(), mouseEvent.GetY()))
                {
                    m_FileDropdownPanel->SetVisible(false);
                }
            }

            if (m_EditDropdownPanel && m_EditDropdownPanel->IsVisible())
            {
                if (!m_EditDropdownPanel->IsPointInside(mouseEvent.GetX(), mouseEvent.GetY()) &&
                    !m_BtnEditMenu->IsPointInside(mouseEvent.GetX(), mouseEvent.GetY()))
                {
                    m_EditDropdownPanel->SetVisible(false);
                }
            }

            if (m_WindowDropdownPanel && m_WindowDropdownPanel->IsVisible())
            {
                if (!m_WindowDropdownPanel->IsPointInside(mouseEvent.GetX(), mouseEvent.GetY()) &&
                    !m_BtnWindowMenu->IsPointInside(mouseEvent.GetX(), mouseEvent.GetY()))
                {
                    m_WindowDropdownPanel->SetVisible(false);
                }
            }

            if (m_ColliderDebugDropdownPanel && m_ColliderDebugDropdownPanel->IsVisible())
            {
                if (!m_ColliderDebugDropdownPanel->IsPointInside(mouseEvent.GetX(), mouseEvent.GetY()) &&
                    !m_BtnColliderOutline->IsPointInside(mouseEvent.GetX(), mouseEvent.GetY()))
                {
                    HideColliderDebugDropdown();
                }
            }

            // 팝업/메뉴가 떠 있던 프레임의 마우스 입력은 아래 하이어라키/씬뷰로 보내지 않는다.
            if (hadBlockingPopup && isPopupMouseEvent)
            {
                if (m_RootUI)
                    m_RootUI->OnEvent(e);

                e.Handled = true;
                return;
            }

            if (mouseEvent.GetButton() == 1)
            {
                float mouseX = mouseEvent.GetX();
                float mouseY = mouseEvent.GetY();
                UI::Widget* topmostWidget = m_RootUI ? getTopmostWidgetAt(m_RootUI, mouseX, mouseY) : nullptr;

                if (m_HierarchyPanel && m_HierarchyPanel->IsPointInside(mouseX, mouseY) &&
                    isWidgetOrChildOf(topmostWidget, m_HierarchyPanel))
                {
                    Entity hoveredEntity = m_HierarchyPanel->GetEntityAt(mouseX, mouseY);
                    if (hoveredEntity && !m_HierarchyPanel->IsSelected(hoveredEntity))
                        m_HierarchyPanel->SetSelectedEntity(hoveredEntity);

                    ShowObjectContextMenu(mouseX, mouseY, hoveredEntity || m_HierarchyPanel->GetSelectedEntity());
                    mouseEvent.Handled = true;
                    return;
                }

                if (m_ViewportWidget && m_ViewportWidget->IsPointInside(mouseX, mouseY) &&
                    isWidgetOrChildOf(topmostWidget, m_ViewportWidget))
                {
                    auto now = std::chrono::steady_clock::now();
                    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_LastViewportRightClickTime).count();
                    float dx = mouseX - m_LastViewportRightClickX;
                    float dy = mouseY - m_LastViewportRightClickY;
                    bool isDoubleRightClick =
                        elapsedMs >= 0 &&
                        elapsedMs <= 350 &&
                        (dx * dx + dy * dy) <= 64.0f;

                    // 씬 뷰의 우클릭은 카메라 조작에 쓰인다.
                    // 컨텍스트 메뉴는 더블 우클릭일 때만 열어 두 기능이 서로 끼어들지 않게 한다.
                    m_LastViewportRightClickTime = now;
                    m_LastViewportRightClickX = mouseX;
                    m_LastViewportRightClickY = mouseY;

                    if (isDoubleRightClick)
                    {
                        ShowObjectContextMenu(mouseX, mouseY, m_HierarchyPanel && m_HierarchyPanel->GetSelectedEntity());
                        mouseEvent.Handled = true;
                        return;
                    }
                }
            }

            if (mouseEvent.GetButton() == 0 && m_HierarchyPanel && m_HierarchyPanel->IsPointInside(mouseEvent.GetX(), mouseEvent.GetY()))
            {
                Entity hoveredEntity = m_HierarchyPanel->GetEntityAt(mouseEvent.GetX(), mouseEvent.GetY());
                if (hoveredEntity)
                {
                    m_PrefabDragEntity = hoveredEntity;
                    m_PrefabDragStartX = mouseEvent.GetX();
                    m_PrefabDragStartY = mouseEvent.GetY();
                    m_IsDraggingPrefabToAssetBrowser = false;
                }
            }
        }

        if (hadBlockingPopup && isPopupMouseEvent)
        {
            if (m_RootUI)
                m_RootUI->OnEvent(e);

            e.Handled = true;
            return;
        }

        if (e.GetEventType() == EventType::MouseMoved && m_PrefabDragEntity)
        {
            MouseMovedEvent& mouseEvent = static_cast<MouseMovedEvent&>(e);
            float dx = mouseEvent.GetX() - m_PrefabDragStartX;
            float dy = mouseEvent.GetY() - m_PrefabDragStartY;

            if ((dx * dx + dy * dy) > 64.0f)
                m_IsDraggingPrefabToAssetBrowser = true;

            if (m_IsDraggingPrefabToAssetBrowser)
            {
                e.Handled = true;
                return;
            }
        }

        if (e.GetEventType() == EventType::MouseButtonReleased)
        {
            MouseButtonReleasedEvent& mouseEvent = static_cast<MouseButtonReleasedEvent&>(e);
            if (mouseEvent.GetButton() == 0 && m_PrefabDragEntity)
            {
                if (m_IsDraggingPrefabToAssetBrowser && m_AssetBrowserPanel &&
                    m_AssetBrowserPanel->IsDropTargetPoint(mouseEvent.GetX(), mouseEvent.GetY()))
                {
                    // 하이어라키 오브젝트를 에셋 브라우저에 놓으면 현재 폴더에 프리팹을 만든다.
                    SavePrefabToDirectory(m_PrefabDragEntity, m_AssetBrowserPanel->GetCurrentAssetDirectory());
                    e.Handled = true;
                }

                m_PrefabDragEntity = {};
                m_IsDraggingPrefabToAssetBrowser = false;
                if (e.Handled)
                    return;
            }
        }

        // 마우스 피킹 콜백
        m_ViewportWidget->SetOnMouseDown([this](float mouseX, float mouseY) {
            auto vpPos = m_ViewportWidget->GetCalculatedPosition();
            float localX = mouseX - vpPos.x;
            float localY = mouseY - vpPos.y;
            int pixelData = m_Framebuffer->ReadPixel((uint32_t)localX, (uint32_t)localY);

            if (pixelData >= 0 && m_ActiveScene->GetRegistry().valid((entt::entity)pixelData))
            {
                CCEngine::Entity clickedEntity{ (entt::entity)pixelData, m_ActiveScene };
                bool isCtrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                if (isCtrlPressed)
                {
                    std::vector<Entity> nextSelection = m_HierarchyPanel->GetSelectedEntities();
                    auto existing = std::find_if(nextSelection.begin(), nextSelection.end(),
                        [clickedEntity](Entity selected) { return selected == clickedEntity; });

                    if (existing != nextSelection.end())
                        nextSelection.erase(existing);
                    else
                        nextSelection.push_back(clickedEntity);

                    Entity activeEntity = nextSelection.empty() ? Entity{} : nextSelection.back();
                    m_HierarchyPanel->SetSelectedEntities(nextSelection, activeEntity);
                }
                else
                {
                    m_HierarchyPanel->SetSelectedEntity(clickedEntity);
                }
                std::cout << "[Picking] Picked Entity ID: " << pixelData << std::endl;
            }
            else
            {
                std::cout << "[Picking] Ignored Empty Space!" << std::endl;
            }
            });

        bool shouldRouteUIFirst = true;
        if (m_RootUI && m_ViewportWidget)
        {
            float mouseX = 0.0f;
            float mouseY = 0.0f;
            bool isMouseEvent = true;

            if (e.GetEventType() == EventType::MouseButtonPressed) { auto& me = static_cast<MouseButtonPressedEvent&>(e); mouseX = me.GetX(); mouseY = me.GetY(); }
            else if (e.GetEventType() == EventType::MouseMoved) { auto& me = static_cast<MouseMovedEvent&>(e); mouseX = me.GetX(); mouseY = me.GetY(); }
            else if (e.GetEventType() == EventType::MouseButtonReleased) { auto& me = static_cast<MouseButtonReleasedEvent&>(e); mouseX = me.GetX(); mouseY = me.GetY(); }
            else { isMouseEvent = false; }

            if (isMouseEvent)
            {
                UI::Widget* topmost = getTopmostWidgetAt(m_RootUI, mouseX, mouseY);
                shouldRouteUIFirst = !isWidgetOrChildOf(topmost, m_ViewportWidget);
            }
        }

        auto routeViewportGizmo = [&]()
        {
            if (e.Handled || !m_ViewportWidget)
                return;

            float mouseX = 0.0f; float mouseY = 0.0f;
            if (e.GetEventType() == EventType::MouseButtonPressed) { auto& me = static_cast<MouseButtonPressedEvent&>(e); mouseX = me.GetX(); mouseY = me.GetY(); }
            else if (e.GetEventType() == EventType::MouseMoved) { auto& me = static_cast<MouseMovedEvent&>(e); mouseX = me.GetX(); mouseY = me.GetY(); }
            else if (e.GetEventType() == EventType::MouseButtonReleased) { auto& me = static_cast<MouseButtonReleasedEvent&>(e); mouseX = me.GetX(); mouseY = me.GetY(); }

            auto vpPos = m_ViewportWidget->GetCalculatedPosition();
            auto vpSize = m_ViewportWidget->GetCalculatedSize();

            bool isInsideViewport = (mouseX >= vpPos.x && mouseX <= vpPos.x + vpSize.x && mouseY >= vpPos.y && mouseY <= vpPos.y + vpSize.y);
            UI::Widget* topmost = m_RootUI ? getTopmostWidgetAt(m_RootUI, mouseX, mouseY) : nullptr;
            bool viewportIsTopmost = isWidgetOrChildOf(topmost, m_ViewportWidget);

            if ((isInsideViewport && viewportIsTopmost) || m_GizmoSystem.IsDragging())
            {
                auto selectedEntity = m_HierarchyPanel->GetSelectedEntity();
                auto selectedEntities = m_HierarchyPanel->GetSelectedEntities();
                bool wasDragging = m_GizmoSystem.IsDragging();
                m_GizmoSystem.OnEvent(e, selectedEntities, selectedEntity, m_Camera.GetViewMatrix(), m_Camera.GetProjectionMatrix(), vpSize.x, vpSize.y, vpPos.x, vpPos.y);
                bool isDragging = m_GizmoSystem.IsDragging();

                if (!wasDragging && isDragging && selectedEntities.size() > 1 && !m_IsMultiTransformUndoOpen)
                {
                    // 여러 오브젝트가 함께 움직일 때는 개별 Transform 기록 대신 씬 스냅샷으로 묶는다.
                    // 그래야 Ctrl+Z 한 번에 선택 묶음 전체가 같은 시점으로 돌아간다.
                    m_UndoManager.BeginSceneStructureChange("Transform Multiple Objects");
                    m_IsMultiTransformUndoOpen = true;
                }
                else if (wasDragging && !isDragging && m_IsMultiTransformUndoOpen)
                {
                    m_UndoManager.CommitSceneStructureChange();
                    m_IsMultiTransformUndoOpen = false;
                }
            }
        };

        if (shouldRouteUIFirst)
        {
            if (!e.Handled && m_RootUI) m_RootUI->OnEvent(e);
            routeViewportGizmo();
        }
        else
        {
            routeViewportGizmo();
            if (!e.Handled && m_RootUI) m_RootUI->OnEvent(e);
        }

        // 4. UI가 이벤트를 먹지 않았다면, 3D 카메라나 씬에 넘겨줌
        if (!e.Handled)
        {
            // m_Camera.OnEvent(e); 
            // m_ActiveScene->OnEvent(e);
        }
    }

    void EditorLayer::OnImGuiRender()
    {}

    void EditorLayer::ShowObjectContextMenu(float x, float y, bool allowDelete)
    {
        if (!m_ObjectContextMenuPanel)
            return;

        float width = 160.0f;
        float height = allowDelete ? 156.0f : 104.0f;
        auto& mainWindow = Application::Get()->GetWindow();
        x = (std::min)(x, (float)mainWindow.GetWidth() - width);
        y = (std::min)(y, (float)mainWindow.GetHeight() - height);
        x = (std::max)(x, 0.0f);
        y = (std::max)(y, 0.0f);

        m_ObjectContextMenuPanel->SetAnchorMin(0.0f, 0.0f);
        m_ObjectContextMenuPanel->SetAnchorMax(0.0f, 0.0f);
        m_ObjectContextMenuPanel->SetOffsetMin(x, y);
        m_ObjectContextMenuPanel->SetOffsetMax(x + width, y + height);
        if (m_BtnDeleteObject)
            m_BtnDeleteObject->SetVisible(allowDelete);
        if (m_BtnCreatePrefab)
            m_BtnCreatePrefab->SetVisible(allowDelete);
        if (m_MeshObjectSubmenuPanel)
            m_MeshObjectSubmenuPanel->SetVisible(false);

        m_ObjectContextMenuPanel->SetVisible(true);
        m_ObjectContextMenuPanel->BringToFront();
    }

    void EditorLayer::ShowMeshObjectSubmenu()
    {
        if (!m_ObjectContextMenuPanel || !m_MeshObjectSubmenuPanel)
            return;

        const auto menuPos = m_ObjectContextMenuPanel->GetCalculatedPosition();
        const auto menuSize = m_ObjectContextMenuPanel->GetCalculatedSize();
        constexpr float submenuWidth = 160.0f;
        constexpr float submenuHeight = 182.0f;
        auto& mainWindow = Application::Get()->GetWindow();

        float x = menuPos.x + menuSize.x;
        float y = menuPos.y + 26.0f;
        if (x + submenuWidth > mainWindow.GetWidth())
            x = menuPos.x - submenuWidth;
        y = (std::min)(y, (float)mainWindow.GetHeight() - submenuHeight);
        y = (std::max)(y, 0.0f);

        m_MeshObjectSubmenuPanel->SetOffsetMin(x, y);
        m_MeshObjectSubmenuPanel->SetOffsetMax(x + submenuWidth, y + submenuHeight);
        m_MeshObjectSubmenuPanel->SetVisible(true);
    }

    void EditorLayer::HideObjectContextMenu()
    {
        if (m_ObjectContextMenuPanel)
            m_ObjectContextMenuPanel->SetVisible(false);
        if (m_MeshObjectSubmenuPanel)
            m_MeshObjectSubmenuPanel->SetVisible(false);
    }

    Entity EditorLayer::CreateEmptyObject()
    {
        if (!m_ActiveScene)
            return {};

        m_UndoManager.BeginSceneStructureChange("Create GameObject");
        Entity entity = m_ActiveScene->CreateEntity("GameObject");
        RefreshEditorSelection(entity);
        m_UndoManager.CommitSceneStructureChange();
        return entity;
    }

    Entity EditorLayer::CreatePrimitiveObject(const std::string& name, int meshTypeValue)
    {
        if (!m_ActiveScene)
            return {};

        MeshComponent::MeshType meshType = static_cast<MeshComponent::MeshType>(meshTypeValue);
        m_UndoManager.BeginSceneStructureChange("Create " + name);
        Entity entity = m_ActiveScene->CreateEntity(name);
        auto& mesh = entity.AddComponent<MeshComponent>(meshType);
        mesh.MeshData = CreateDefaultMeshForType(meshType);
        mesh.BaseColor = { 0.2f, 0.3f, 0.9f, 1.0f };

        RefreshEditorSelection(entity);
        m_UndoManager.CommitSceneStructureChange();
        return entity;
    }

    Entity EditorLayer::CreateLightObject()
    {
        if (!m_ActiveScene)
            return {};

        m_UndoManager.BeginSceneStructureChange("Create Light");
        Entity entity = m_ActiveScene->CreateEntity("Light");
        auto& transform = entity.GetComponent<TransformComponent>();
        auto& light = entity.AddComponent<LightComponent>();

        // 초기 씬의 Fill Light (Cool)과 같은 값을 새 Light의 고정 기본값으로 사용한다.
        transform.Rotation = {
            DirectX::XMConvertToRadians(15.0f),
            DirectX::XMConvertToRadians(135.0f),
            0.0f
        };
        DirectX::XMStoreFloat4(
            &transform.QuaternionRotation,
            DirectX::XMQuaternionRotationRollPitchYaw(
                transform.Rotation.x,
                transform.Rotation.y,
                transform.Rotation.z));
        light.LightColor = { 0.4f, 0.5f, 1.0f };
        light.Intensity = 0.5f;

        RefreshEditorSelection(entity);
        m_UndoManager.CommitSceneStructureChange();
        return entity;
    }

    Entity EditorLayer::CreateCameraObject()
    {
        if (!m_ActiveScene)
            return {};

        m_UndoManager.BeginSceneStructureChange("Create Camera");
        Entity entity = m_ActiveScene->CreateEntity("Camera");
        auto& transform = entity.GetComponent<TransformComponent>();
        auto& camera = entity.AddComponent<CameraComponent>();

        // 초기 Main Camera와 같은 고정 기본 위치/회전을 사용한다.
        transform.Translation = { 0.0f, 3.0f, -6.0f };
        transform.Rotation = { DirectX::XMConvertToRadians(20.0f), 0.0f, 0.0f };
        DirectX::XMStoreFloat4(
            &transform.QuaternionRotation,
            DirectX::XMQuaternionRotationRollPitchYaw(
                transform.Rotation.x,
                transform.Rotation.y,
                transform.Rotation.z));

        // 이미 게임 뷰 카메라가 있으면 보조 카메라, 없으면 즉시 게임 뷰 카메라가 된다.
        bool hasPrimaryCamera = false;
        auto cameraView = m_ActiveScene->GetRegistry().view<TransformComponent, CameraComponent>();
        for (auto source : cameraView)
        {
            if (source == (entt::entity)entity)
                continue;
            if (cameraView.get<CameraComponent>(source).Primary)
            {
                hasPrimaryCamera = true;
                break;
            }
        }
        camera.Primary = !hasPrimaryCamera;

        RefreshEditorSelection(entity);
        m_UndoManager.CommitSceneStructureChange();
        return entity;
    }

    void EditorLayer::DeleteSelectedObject()
    {
        if (!m_ActiveScene || !m_HierarchyPanel)
            return;

        std::vector<Entity> selectedEntities = m_HierarchyPanel->GetSelectedEntities();
        if (selectedEntities.empty())
            return;

        std::unordered_set<entt::entity> selectedSet;
        for (Entity selected : selectedEntities)
        {
            if (selected && m_ActiveScene->GetRegistry().valid((entt::entity)selected))
                selectedSet.insert((entt::entity)selected);
        }
        if (selectedSet.empty())
            return;

        std::vector<Entity> deleteRoots;
        for (Entity selected : selectedEntities)
        {
            if (!selected || !m_ActiveScene->GetRegistry().valid((entt::entity)selected))
                continue;

            bool hasSelectedAncestor = false;
            Entity current = selected;
            while (current.HasComponent<RelationshipComponent>())
            {
                entt::entity parent = current.GetComponent<RelationshipComponent>().Parent;
                if (parent == entt::null)
                    break;

                if (selectedSet.find(parent) != selectedSet.end())
                {
                    hasSelectedAncestor = true;
                    break;
                }
                current = Entity{ parent, m_ActiveScene };
            }

            if (!hasSelectedAncestor)
                deleteRoots.push_back(selected);
        }

        std::string label = deleteRoots.size() > 1 ? "Delete Multiple Objects" : "Delete Object";
        if (deleteRoots.size() == 1 && deleteRoots[0].HasComponent<TagComponent>())
            label = "Delete " + deleteRoots[0].GetComponent<TagComponent>().Tag;
        m_UndoManager.BeginSceneStructureChange(label);

        bool deletedPrimaryCamera = false;
        for (Entity selected : deleteRoots)
        {
            if (!selected || !m_ActiveScene->GetRegistry().valid((entt::entity)selected))
                continue;

            deletedPrimaryCamera = deletedPrimaryCamera ||
                (selected.HasComponent<CameraComponent>() && selected.GetComponent<CameraComponent>().Primary);
            m_ActiveScene->DestroyEntity(selected);
        }

        // 게임 뷰 카메라가 삭제되면 남은 첫 카메라가 자동 승계한다.
        if (deletedPrimaryCamera)
        {
            auto cameraView = m_ActiveScene->GetRegistry().view<CameraComponent>();
            for (auto entity : cameraView)
            {
                cameraView.get<CameraComponent>(entity).Primary = true;
                break;
            }
        }
        RefreshEditorSelection({});
        m_UndoManager.CommitSceneStructureChange();
    }

    void EditorLayer::DuplicateSelectedObject()
    {
        if (!m_ActiveScene || !m_HierarchyPanel)
            return;

        std::vector<Entity> selectedEntities = m_HierarchyPanel->GetSelectedEntities();
        if (selectedEntities.empty())
            return;

        std::string label = selectedEntities.size() > 1 ? "Duplicate Multiple Objects" : "Duplicate Object";
        if (selectedEntities.size() == 1 && selectedEntities[0].HasComponent<TagComponent>())
            label = "Duplicate " + selectedEntities[0].GetComponent<TagComponent>().Tag;

        m_UndoManager.BeginSceneStructureChange(label);
        std::vector<Entity> duplicatedEntities;
        for (Entity selected : selectedEntities)
        {
            if (!selected || !m_ActiveScene->GetRegistry().valid((entt::entity)selected))
                continue;

            Entity duplicated = m_ActiveScene->DuplicateEntity(selected);
            if (duplicated)
                duplicatedEntities.push_back(duplicated);
        }

        if (!duplicatedEntities.empty())
        {
            Entity activeDuplicate = duplicatedEntities.back();
            for (UI::HierarchyPanel* hierarchy : m_HierarchyPanels)
            {
                if (!hierarchy)
                    continue;

                hierarchy->SetSelectedEntities(duplicatedEntities, activeDuplicate);
                hierarchy->Refresh();
            }

            for (UI::InspectorPanel* inspector : m_InspectorPanels)
            {
                if (inspector)
                    inspector->SetSelectedEntity(activeDuplicate);
            }
        }
        m_UndoManager.CommitSceneStructureChange();
    }

    void EditorLayer::RefreshEditorSelection(Entity selected)
    {
        for (UI::HierarchyPanel* hierarchy : m_HierarchyPanels)
        {
            if (!hierarchy)
                continue;
            hierarchy->SetSelectedEntity(selected);
            hierarchy->Refresh();
        }

        for (UI::InspectorPanel* inspector : m_InspectorPanels)
        {
            if (inspector)
                inspector->SetSelectedEntity(selected);
        }
    }

    void EditorLayer::RebindScenePanels(Entity selected)
    {
        for (UI::HierarchyPanel* hierarchy : m_HierarchyPanels)
        {
            if (!hierarchy)
                continue;

            hierarchy->SetContext(m_ActiveScene);
            hierarchy->SetSelectedEntity(selected);
            hierarchy->Refresh();
        }

        for (UI::InspectorPanel* inspector : m_InspectorPanels)
        {
            if (inspector)
                inspector->SetSelectedEntity(selected);
        }
    }

    void EditorLayer::SetActiveScene(Scene* scene, Entity selected)
    {
        if (!scene || scene == m_ActiveScene)
        {
            RebindScenePanels(selected);
            return;
        }

        m_ActiveScene = scene;
        RebindScenePanels(selected);
    }

    bool EditorLayer::IsInPlayMode() const
    {
        return m_EditorScene != nullptr;
    }

    bool EditorLayer::EnterPlayMode()
    {
        if (!m_ActiveScene || IsInPlayMode())
            return false;

        if (ScriptCompiler::IsCompiling())
        {
            ConsoleLog::Warning("Wait for C# script compilation to finish before entering Play Mode.");
            return false;
        }

        m_UndoManager.ClearTransformHistory();
        m_UndoManager.ClearSceneStructureHistory();

        m_EditorScene = m_ActiveScene;
        Scene* runtimeScene = Scene::Copy(m_EditorScene);
        if (!runtimeScene)
        {
            m_EditorScene = nullptr;
            ConsoleLog::Error("Failed to create Play Mode scene copy.");
            return false;
        }

        // Play Mode는 에디터 씬을 직접 실행하지 않고 복사본을 실행한다.
        // 런타임에서 스크립트나 물리가 Transform을 바꿔도 Stop 순간 복사본을 버리므로 원본 씬은 그대로 남는다.
        m_ActiveScene = runtimeScene;
        m_ActiveScene->OnRuntimeStart();
        RebindScenePanels({});
        UpdatePlayModeButtons();
        ConsoleLog::Info("Entered Play Mode. Editor scene is isolated.");
        return true;
    }

    bool EditorLayer::ExitPlayMode()
    {
        if (!IsInPlayMode())
            return false;

        if (m_ActiveScene)
        {
            m_ActiveScene->OnRuntimeStop();
            delete m_ActiveScene;
        }

        // Stop은 런타임 복사본을 폐기하고 저장되어 있던 에디터 씬 포인터로 되돌아간다.
        // 이 경계가 있어야 Play 중 실험한 값이 씬 파일이나 Undo 히스토리에 섞이지 않는다.
        m_ActiveScene = m_EditorScene;
        m_EditorScene = nullptr;

        if (m_ActiveScene)
            m_ActiveScene->SetSceneState(SceneState::Edit);

        m_UndoManager.ClearTransformHistory();
        m_UndoManager.ClearSceneStructureHistory();
        RebindScenePanels({});
        UpdatePlayModeButtons();
        ConsoleLog::Info("Exited Play Mode. Runtime scene changes were discarded.");
        return true;
    }

    void EditorLayer::TogglePausePlayMode()
    {
        if (!m_ActiveScene || !IsInPlayMode())
            return;

        SceneState state = m_ActiveScene->GetState();
        if (state == SceneState::Play)
            m_ActiveScene->SetSceneState(SceneState::Pause);
        else if (state == SceneState::Pause)
            m_ActiveScene->SetSceneState(SceneState::Play);

        UpdatePlayModeButtons();
    }

    void EditorLayer::UpdatePlayModeButtons()
    {
        if (m_BtnPlay)
        {
            bool active = m_ActiveScene && m_ActiveScene->GetState() == SceneState::Play;
            m_BtnPlay->SetActive(active);
        }

        if (m_BtnPause)
        {
            bool active = m_ActiveScene && m_ActiveScene->GetState() == SceneState::Pause;
            m_BtnPause->SetActive(active);
        }
    }

    void EditorLayer::FrameSelectedEntity()
    {
        if (!m_HierarchyPanel)
            return;

        std::vector<Entity> selectedEntities = m_HierarchyPanel->GetSelectedEntities();
        if (selectedEntities.empty())
            return;

        DirectX::XMVECTOR minPoint = DirectX::XMVectorZero();
        DirectX::XMVECTOR maxPoint = DirectX::XMVectorZero();
        bool hasPoint = false;
        float radiusFloor = 1.0f;
        for (Entity selected : selectedEntities)
        {
            if (!selected || !selected.HasComponent<TransformComponent>())
                continue;

            AccumulateFrameBounds(selected, minPoint, maxPoint, hasPoint);

            if (selected.HasComponent<MeshComponent>())
            {
                const auto& transform = selected.GetComponent<TransformComponent>();
                radiusFloor = (std::max)(radiusFloor, (std::max)({ transform.Scale.x, transform.Scale.y, transform.Scale.z, 1.0f }));
            }
            if (selected.HasComponent<ModelComponent>())
                radiusFloor = (std::max)(radiusFloor, 3.0f);
        }

        if (!hasPoint)
            return;

        DirectX::XMVECTOR center = DirectX::XMVectorScale(DirectX::XMVectorAdd(minPoint, maxPoint), 0.5f);
        DirectX::XMVECTOR extent = DirectX::XMVectorSubtract(maxPoint, minPoint);
        float radius = DirectX::XMVectorGetX(DirectX::XMVector3Length(extent)) * 0.5f;

        radius = (std::max)(radius, radiusFloor);

        DirectX::XMFLOAT3 target;
        DirectX::XMStoreFloat3(&target, center);

        // 상용 에디터의 Frame Selected와 같은 역할이다.
        // 선택 대상의 계층 위치를 훑어 중심을 잡고, 카메라는 현재 방향을 유지한 채 뒤로 물러난다.
        m_Camera.FrameSelection(target, radius);
    }

    void EditorLayer::UpdateSceneToolButtons()
    {
        GizmoMode mode = m_GizmoSystem.GetMode();

        if (m_BtnToolSelect) m_BtnToolSelect->SetActive(mode == GizmoMode::None);
        if (m_BtnToolMove) m_BtnToolMove->SetActive(mode == GizmoMode::Translate);
        if (m_BtnToolRotate) m_BtnToolRotate->SetActive(mode == GizmoMode::Rotate);
        if (m_BtnToolScale) m_BtnToolScale->SetActive(mode == GizmoMode::Scale);

        if (m_BtnToolSpace)
        {
            bool isLocal = m_GizmoSystem.GetSpace() == GizmoSpace::Local;
            m_BtnToolSpace->SetText(isLocal ? "Local" : "World");
            m_BtnToolSpace->SetActive(isLocal);
        }

        if (m_BtnToolPivot)
        {
            bool isCenter = m_GizmoSystem.GetPivotMode() == GizmoPivotMode::Center;
            m_BtnToolPivot->SetText(isCenter ? "Center" : "Pivot");
            m_BtnToolPivot->SetActive(isCenter);
        }

        if (m_BtnToolSnap)
            m_BtnToolSnap->SetActive(m_GizmoSystem.IsSnappingEnabled());

        UpdatePhysicsDebugButton();
        UpdateColliderOutlineButton();
    }

    void EditorLayer::CyclePhysicsDebugViewMode()
    {
        m_PhysicsDebugViewMode = (m_PhysicsDebugViewMode + 1) % 7;
        UpdatePhysicsDebugButton();
    }

    void EditorLayer::UpdateColliderOutlineButton()
    {
        if (!m_BtnColliderOutline)
            return;

        if (m_ShowColliderOutlines)
            m_BtnColliderOutline->SetText("Collider: Outline");
        else if (m_ShowMeshColliderWire)
            m_BtnColliderOutline->SetText("Collider: Wire");
        else
            m_BtnColliderOutline->SetText("Collider: Off");

        m_BtnColliderOutline->SetActive(m_ShowColliderOutlines || m_ShowMeshColliderWire);

        if (m_BtnColliderOutlineMode)
        {
            m_BtnColliderOutlineMode->SetText(m_ShowColliderOutlines ? "Outline: On" : "Outline: Off");
            m_BtnColliderOutlineMode->SetActive(m_ShowColliderOutlines);
        }

        if (m_BtnMeshColliderWireMode)
        {
            m_BtnMeshColliderWireMode->SetText(m_ShowMeshColliderWire ? "Mesh Wire: On" : "Mesh Wire: Off");
            m_BtnMeshColliderWireMode->SetActive(m_ShowMeshColliderWire);
        }
    }

    void EditorLayer::HideColliderDebugDropdown()
    {
        if (m_ColliderDebugDropdownPanel)
            m_ColliderDebugDropdownPanel->SetVisible(false);
    }

    void EditorLayer::UpdatePhysicsDebugButton()
    {
        if (!m_BtnPhysicsDebug)
            return;

        switch (m_PhysicsDebugViewMode)
        {
            case 1:
                m_BtnPhysicsDebug->SetText("Phys 2D: Sel");
                m_BtnPhysicsDebug->SetActive(true);
                break;
            case 2:
                m_BtnPhysicsDebug->SetText("Phys 2D: All");
                m_BtnPhysicsDebug->SetActive(true);
                break;
            case 3:
                m_BtnPhysicsDebug->SetText("Phys 3D: Sel");
                m_BtnPhysicsDebug->SetActive(true);
                break;
            case 4:
                m_BtnPhysicsDebug->SetText("Phys 3D: All");
                m_BtnPhysicsDebug->SetActive(true);
                break;
            case 5:
                m_BtnPhysicsDebug->SetText("Phys Both: Sel");
                m_BtnPhysicsDebug->SetActive(true);
                break;
            case 6:
                m_BtnPhysicsDebug->SetText("Phys Both: All");
                m_BtnPhysicsDebug->SetActive(true);
                break;
            default:
                m_BtnPhysicsDebug->SetText("Physics: Off");
                m_BtnPhysicsDebug->SetActive(false);
                break;
        }
    }

    void EditorLayer::RenderPhysicsDebugView(const PerspectiveCamera& camera, const std::vector<Entity>& selectedEntities)
    {
        if (!m_ActiveScene || (!m_ShowColliderOutlines && !m_ShowMeshColliderWire))
            return;

        const bool selectedColliderOverlay = m_PhysicsDebugViewMode == 0;
        const bool draw2D = m_ShowColliderOutlines && (selectedColliderOverlay || m_PhysicsDebugViewMode == 1 || m_PhysicsDebugViewMode == 2 || m_PhysicsDebugViewMode == 5 || m_PhysicsDebugViewMode == 6);
        const bool draw3D = m_ShowMeshColliderWire || (m_ShowColliderOutlines && (selectedColliderOverlay || m_PhysicsDebugViewMode == 3 || m_PhysicsDebugViewMode == 4 || m_PhysicsDebugViewMode == 5 || m_PhysicsDebugViewMode == 6));
        const bool selectedOnly = selectedColliderOverlay || m_PhysicsDebugViewMode == 1 || m_PhysicsDebugViewMode == 3 || m_PhysicsDebugViewMode == 5;

        std::vector<Entity> targets;
        if (selectedOnly)
        {
            // Physics Debug가 꺼져 있어도 선택한 오브젝트의 콜라이더는 보여준다.
            // 전체 표시 토글과 선택 확인용 오버레이를 분리해야 배치 작업 중 콜라이더 크기를 바로 확인할 수 있다.
            targets = selectedEntities;
        }
        else
        {
            std::unordered_set<entt::entity> uniqueTargets;
            auto addTarget = [&](entt::entity entityID)
                {
                    if (uniqueTargets.insert(entityID).second)
                        targets.emplace_back(entityID, m_ActiveScene);
                };

            if (draw2D)
            {
                auto view = m_ActiveScene->GetRegistry().view<TransformComponent, BoxCollider2DComponent>();
                targets.reserve(targets.size() + (size_t)view.size_hint());
                for (auto entityID : view)
                    addTarget(entityID);
            }

            if (draw3D)
            {
                m_ActiveScene->GetRegistry().view<TransformComponent, BoxCollider3DComponent>().each([&](auto entityID, auto&, auto&) { addTarget(entityID); });
                m_ActiveScene->GetRegistry().view<TransformComponent, SphereCollider3DComponent>().each([&](auto entityID, auto&, auto&) { addTarget(entityID); });
                m_ActiveScene->GetRegistry().view<TransformComponent, CylinderCollider3DComponent>().each([&](auto entityID, auto&, auto&) { addTarget(entityID); });
                m_ActiveScene->GetRegistry().view<TransformComponent, MeshCollider3DComponent>().each([&](auto entityID, auto&, auto&) { addTarget(entityID); });
            }
        }

        if (targets.empty())
            return;

        Renderer2D::BeginScene(camera);

        for (Entity entity : targets)
        {
            if (!entity || entity.GetScene() != m_ActiveScene)
                continue;

            entt::entity handle = (entt::entity)entity;
            if (!m_ActiveScene->GetRegistry().valid(handle) ||
                !entity.HasComponent<TransformComponent>() ||
                !m_ActiveScene->IsEntityActiveInHierarchy(entity))
            {
                continue;
            }

            DirectX::XMMATRIX entityWorld = GetEditorWorldTransform(entity);
            if (draw2D && entity.HasComponent<BoxCollider2DComponent>())
            {
                const auto& collider = entity.GetComponent<BoxCollider2DComponent>();
                DirectX::XMFLOAT4 colliderColor = collider.IsTrigger ?
                    DirectX::XMFLOAT4{ 1.0f, 0.63f, 0.18f, 0.92f } :
                    DirectX::XMFLOAT4{ 0.25f, 0.95f, 0.48f, 0.92f };

                // 물리 디버그 뷰는 RuntimeBodyId를 읽지 않는다.
                // 에디터에서는 Play 전에도 배치 상태를 봐야 하므로 Transform/Collider 원본 데이터가 기준이다.
                DrawColliderBoxOutline(entityWorld, collider, colliderColor);

                if (entity.HasComponent<Rigidbody2DComponent>())
                {
                    const auto& rb = entity.GetComponent<Rigidbody2DComponent>();
                    DirectX::XMFLOAT3 center = TransformPoint(
                        { collider.Offset.x, collider.Offset.y, 0.02f },
                        entityWorld);

                    DirectX::XMMATRIX markerTransform =
                        DirectX::XMMatrixScaling(0.12f, 0.12f, 1.0f) *
                        DirectX::XMMatrixTranslation(center.x, center.y, center.z);

                    Renderer2D::DrawQuad(markerTransform, GetRigidbodyDebugColor(rb.Type), -1);
                }
            }

            if (draw3D)
            {
                const DirectX::XMFLOAT3 cameraPosition = camera.GetPosition();
                if (m_ShowColliderOutlines && entity.HasComponent<BoxCollider3DComponent>())
                {
                    const auto& collider = entity.GetComponent<BoxCollider3DComponent>();
                    DirectX::XMFLOAT4 color = collider.IsTrigger ? DirectX::XMFLOAT4{ 1.0f, 0.62f, 0.24f, 0.92f } : DirectX::XMFLOAT4{ 0.35f, 0.85f, 1.0f, 0.92f };
                    DrawBoxCollider3DOutline(entityWorld, collider, cameraPosition, color);
                }

                if (m_ShowColliderOutlines && entity.HasComponent<SphereCollider3DComponent>())
                {
                    const auto& collider = entity.GetComponent<SphereCollider3DComponent>();
                    DirectX::XMFLOAT4 color = collider.IsTrigger ? DirectX::XMFLOAT4{ 1.0f, 0.62f, 0.24f, 0.92f } : DirectX::XMFLOAT4{ 0.35f, 0.85f, 1.0f, 0.92f };
                    DrawSphereCollider3DOutline(entityWorld, collider, cameraPosition, color);
                }

                if (m_ShowColliderOutlines && entity.HasComponent<CylinderCollider3DComponent>())
                {
                    const auto& collider = entity.GetComponent<CylinderCollider3DComponent>();
                    DirectX::XMFLOAT4 color = collider.IsTrigger ? DirectX::XMFLOAT4{ 1.0f, 0.62f, 0.24f, 0.92f } : DirectX::XMFLOAT4{ 0.35f, 0.85f, 1.0f, 0.92f };
                    DrawCylinderCollider3DOutline(entityWorld, collider, cameraPosition, color);
                }

                if (entity.HasComponent<MeshCollider3DComponent>())
                {
                    const auto& collider = entity.GetComponent<MeshCollider3DComponent>();
                    DirectX::XMFLOAT4 color = collider.IsTrigger ? DirectX::XMFLOAT4{ 1.0f, 0.62f, 0.24f, 0.92f } : DirectX::XMFLOAT4{ 0.7f, 0.55f, 1.0f, 0.92f };
                    DirectX::XMMATRIX colliderWorld =
                        DirectX::XMMatrixScaling(collider.Size.x, collider.Size.y, collider.Size.z) *
                        DirectX::XMMatrixTranslation(collider.Offset.x, collider.Offset.y, collider.Offset.z) *
                        entityWorld;
                    if (m_ShowColliderOutlines)
                    {
                        DrawWireBox3D(colliderWorld, cameraPosition, color);
                    }
                    else if (m_ShowMeshColliderWire && entity.HasComponent<MeshComponent>())
                    {
                        const auto& meshComponent = entity.GetComponent<MeshComponent>();
                        std::shared_ptr<Mesh> mesh = meshComponent.MeshData ? meshComponent.MeshData : CreateDefaultMeshForType(meshComponent.Type);
                        DirectX::XMMATRIX meshWorld =
                            DirectX::XMMatrixTranslation(collider.Offset.x, collider.Offset.y, collider.Offset.z) *
                            entityWorld;
                        DrawMeshColliderWire3D(mesh, meshWorld, cameraPosition, color);
                    }
                }
            }
        }

        Renderer2D::EndScene();
    }

    void EditorLayer::ConfigureUndoManager()
    {
        EditorUndoManager::Callbacks callbacks;
        callbacks.GetActiveScene = [this]() { return m_ActiveScene; };
        callbacks.ReplaceActiveScene = [this](Scene* scene)
        {
            delete m_ActiveScene;
            m_ActiveScene = nullptr;
            SetActiveScene(scene);
        };
        callbacks.GetSelectedEntity = [this]()
        {
            return m_HierarchyPanel ? m_HierarchyPanel->GetSelectedEntity() : Entity{};
        };
        callbacks.SetSelectedEntity = [this](Entity entity)
        {
            RefreshEditorSelection(entity);
        };
        callbacks.IsLeftMouseDown = []()
        {
            return CCEngine::Application::Get()->GetWindow().IsMouseButtonPressed(0);
        };
        callbacks.IsGizmoDragging = [this]()
        {
            return m_GizmoSystem.IsDragging();
        };
        callbacks.OnHistoryChanged = [this]()
        {
            MarkHistoryPanelDirty();
        };

        // UndoManager는 스택과 명령만 관리하고, 씬/선택/UI 접근은 이 콜백을 통해 처리한다.
        m_UndoManager.SetCallbacks(std::move(callbacks));
    }

    // =========================================================================
    // 파일 세이브/로드 및 단축키 로직
    // =========================================================================
    void EditorLayer::SaveScene()
    {
        if (IsInPlayMode())
        {
            ConsoleLog::Warning("Stop Play Mode before saving the scene. Runtime scene changes are temporary.");
            return;
        }

        if (m_CurrentScenePath.empty()) { SaveSceneAs(); return; }
        CCEngine::SceneSerializer serializer(m_ActiveScene);
        serializer.Serialize(m_CurrentScenePath);
        ConsoleLog::Info("Scene saved: " + m_CurrentScenePath);
        printf("Scene Saved to: %s\n", m_CurrentScenePath.c_str());
    }

    void EditorLayer::SaveSceneAs()
    {
        if (IsInPlayMode())
        {
            ConsoleLog::Warning("Stop Play Mode before saving the scene. Runtime scene changes are temporary.");
            return;
        }

        std::filesystem::path initialDirPath = std::filesystem::current_path() / "assets" / "scenes";
        std::string initialDirStr = initialDirPath.string();
        std::string filepath = CCEngine::PlatformUtils::SaveFile("CCEngine Scene (*.ccscene)\0*.ccscene\0", initialDirStr.c_str());
        if (!filepath.empty()) {
            m_CurrentScenePath = filepath;
            CCEngine::SceneSerializer serializer(m_ActiveScene);
            serializer.Serialize(m_CurrentScenePath);
            ConsoleLog::Info("Scene saved as: " + m_CurrentScenePath);
            printf("Scene Saved As: %s\n", m_CurrentScenePath.c_str());
        }
    }

    void EditorLayer::OpenScene()
    {
        if (IsInPlayMode())
        {
            ConsoleLog::Warning("Stop Play Mode before opening another scene.");
            return;
        }

        std::filesystem::path initialDirPath = std::filesystem::current_path() / "assets" / "scenes";
        std::string initialDirStr = initialDirPath.string();
        std::string filepath = CCEngine::PlatformUtils::OpenFile("CCEngine Scene (*.ccscene)\0*.ccscene\0", initialDirStr.c_str());
        if (!filepath.empty()) OpenScene(filepath);
    }

    void EditorLayer::OpenScene(const std::string& filepath)
    {
        if (filepath.empty())
            return;
        if (IsInPlayMode())
        {
            ConsoleLog::Warning("Stop Play Mode before opening another scene.");
            return;
        }

        CCEngine::SceneSerializer serializer(m_ActiveScene);
        if (serializer.Deserialize(filepath)) {
            m_CurrentScenePath = filepath;
            m_UndoManager.ClearTransformHistory();
            m_UndoManager.ClearSceneStructureHistory();
            RefreshEditorSelection(CCEngine::Entity{});
            ConsoleLog::Info("Scene loaded: " + m_CurrentScenePath);
            printf("Scene Loaded from: %s\n", m_CurrentScenePath.c_str());
        }
        else
        {
            ConsoleLog::Error("Failed to load scene: " + filepath);
            printf("Failed to load scene: %s\n", filepath.c_str());
        }
    }

    void EditorLayer::LoadSceneAdditive(const std::string& filepath)
    {
        if (filepath.empty())
            return;
        if (IsInPlayMode())
        {
            ConsoleLog::Warning("Stop Play Mode before loading a scene additively.");
            return;
        }

        CCEngine::SceneSerializer serializer(m_ActiveScene);
        Entity sceneRoot = serializer.DeserializeAppend(filepath);
        if (sceneRoot)
        {
            RefreshEditorSelection(sceneRoot);
            ConsoleLog::Info("Scene added: " + filepath);
            printf("Scene Added from: %s\n", filepath.c_str());
        }
        else
        {
            ConsoleLog::Error("Failed to add scene: " + filepath);
            printf("Failed to add scene: %s\n", filepath.c_str());
        }
    }

    void EditorLayer::SetCurrentSceneAsProjectStartScene()
    {
        if (m_CurrentScenePath.empty())
        {
            ConsoleLog::Warning("Save the scene before setting it as start scene.");
            return;
        }

        std::string pathToStore = m_CurrentScenePath;
        try
        {
            pathToStore = std::filesystem::relative(std::filesystem::path(m_CurrentScenePath), std::filesystem::current_path()).string();
        }
        catch (...)
        {
            pathToStore = m_CurrentScenePath;
        }

        m_ProjectSettings.Data().StartScenePath = pathToStore;
        // 시작 씬도 경로만 저장하면 파일명을 바꾸는 순간 끊어진다.
        // GUID를 같이 저장해 두고, 경로는 사람이 읽기 쉬운 보조값으로 남긴다.
        m_ProjectSettings.Data().StartSceneGuid = AssetDatabase::GetGuidFromPath(m_CurrentScenePath);
        SaveProjectSettings();
    }

    void EditorLayer::OpenProjectStartScene()
    {
        std::string startScene = m_ProjectSettings.Data().StartScenePath;
        if (!m_ProjectSettings.Data().StartSceneGuid.empty())
        {
            std::filesystem::path resolved = AssetDatabase::GetPathFromGuid(m_ProjectSettings.Data().StartSceneGuid);
            if (!resolved.empty())
                startScene = resolved.string();
        }
        if (startScene.empty())
        {
            ConsoleLog::Warning("Project start scene is not set.");
            return;
        }

        OpenScene(startScene);
    }

    void EditorLayer::SaveProjectSettings()
    {
        m_ProjectSettings.Normalize();
        m_ProjectSettings.Save();
        if (m_ProjectSettingsPanel)
            m_ProjectSettingsPanel->OnOpened();
    }

    void EditorLayer::ApplyProjectGameResolution()
    {
        m_ProjectSettings.Normalize();
        uint32_t width = m_ProjectSettings.Data().GameWidth;
        uint32_t height = m_ProjectSettings.Data().GameHeight;

        // Game View 패널은 창 크기를 따라가고, 이 값은 패널이 없을 때와 플레이어 빌드의 기본값으로 쓴다.
        m_GameViewportSize = { (float)width, (float)height };
        SaveProjectSettings();
        ConsoleLog::Info("Default game resolution saved: " + std::to_string(width) + " x " + std::to_string(height));
    }

    void EditorLayer::ValidateAssetReferences(bool fullScan)
    {
        // 씬과 프리팹은 파일 경로가 아니라 GUID가 기준이다.
        // GUID로 현재 에셋을 찾을 수 있으면 저장 파일의 낡은 경로만 고치고, 둘 다 못 찾는 경우만 Missing으로 남긴다.
        std::filesystem::path assetsRoot = std::filesystem::current_path() / "assets";
        if (fullScan)
        {
            AssetDatabase::ValidateProjectReferences(assetsRoot, true);
            return;
        }

        // 자동 검증은 에셋 이동/이름변경/외부 변경 뒤에 자주 예약된다.
        // 매번 전체 프로젝트를 훑으면 입력 중에도 멈칫하므로, 이미 스캔된 씬/프리팹 목록만 빠르게 검사한다.
        AssetDatabase::ValidateKnownProjectReferences(assetsRoot, true, false);
    }

    void EditorLayer::QueueAssetReferenceValidation()
    {
        m_PendingAssetReferenceValidation = true;
        m_AssetReferenceValidationRequestedAt = std::chrono::steady_clock::now();
    }

    bool EditorLayer::RunEditorQualityAssurance(bool closeWhenFinished)
    {
        m_EditorQACompleted = true;

        if (m_RootUI)
        {
            auto& window = Application::Get()->GetWindow();
            m_RootUI->UpdateLayout({ 0.0f, 0.0f }, { (float)window.GetWidth(), (float)window.GetHeight() });
        }

        EditorQATestRunner runner;

        runner.AddTest("Editor.Scene.Exists", [this]()
            {
                EditorQATestResult result;
                result.Name = "Editor.Scene.Exists";
                result.Passed = m_ActiveScene != nullptr;
                result.Message = result.Passed ? "Active scene is ready." : "Active scene is null.";
                return result;
            });

        runner.AddTest("Editor.Scene.PrimaryCamera", [this]()
            {
                EditorQATestResult result;
                result.Name = "Editor.Scene.PrimaryCamera";

                if (!m_ActiveScene)
                {
                    result.Passed = false;
                    result.Message = "Active scene is null.";
                    return result;
                }

                bool hasPrimaryCamera = false;
                auto view = m_ActiveScene->GetRegistry().view<CameraComponent>();
                for (auto entity : view)
                {
                    const auto& camera = view.get<CameraComponent>(entity);
                    if (camera.Primary)
                    {
                        hasPrimaryCamera = true;
                        break;
                    }
                }

                result.Passed = hasPrimaryCamera;
                result.Message = hasPrimaryCamera ? "Primary camera found." : "No primary camera found.";
                return result;
            });

        runner.AddTest("PlayMode.SceneCopyIsolation", []()
            {
                EditorQATestResult result;
                result.Name = "PlayMode.SceneCopyIsolation";

                Scene editorScene;
                Entity source = editorScene.CreateEntity("QA Play Object");
                auto& sourceTransform = source.GetComponent<TransformComponent>();
                sourceTransform.Translation = { 1.0f, 2.0f, 3.0f };

                Scene* runtimeScene = Scene::Copy(&editorScene);
                if (!runtimeScene)
                {
                    result.Passed = false;
                    result.Message = "Scene copy returned null.";
                    return result;
                }

                Entity runtimeEntity;
                auto view = runtimeScene->GetRegistry().view<TagComponent, TransformComponent>();
                for (auto entity : view)
                {
                    const auto& tag = view.get<TagComponent>(entity);
                    if (tag.Tag == "QA Play Object")
                    {
                        runtimeEntity = Entity(entity, runtimeScene);
                        break;
                    }
                }

                if (!runtimeEntity)
                {
                    delete runtimeScene;
                    result.Passed = false;
                    result.Message = "Copied scene did not contain the test entity.";
                    return result;
                }

                // Play Mode 검증은 런타임 복사본만 바꿔 본다.
                // 원본 Transform이 그대로면 Stop 때 복사본을 버려도 에디터 씬이 오염되지 않는다는 뜻이다.
                runtimeEntity.GetComponent<TransformComponent>().Translation.x = 99.0f;
                const float originalX = source.GetComponent<TransformComponent>().Translation.x;

                delete runtimeScene;

                result.Passed = std::abs(originalX - 1.0f) < 0.0001f;
                result.Message = result.Passed ? "Runtime scene edits stayed isolated from the editor scene." : "Runtime scene edit changed the editor scene.";
                return result;
            });

        runner.AddTest("RuntimeLifecycle.ScriptOrderAndState", []()
            {
                EditorQATestResult result;
                result.Name = "RuntimeLifecycle.ScriptOrderAndState";

                Scene scene;
                Entity entity = scene.CreateEntity("QA Runtime Script");
                auto& transform = entity.GetComponent<TransformComponent>();
                transform.Translation = { 0.0f, 0.0f, 0.0f };

                auto& script = entity.AddComponent<ScriptComponent>();
                script.ClassName = "Game.MoveUp";
                script.Enabled = true;
                script.FieldOverrides["Speed"] = "1.0";

                scene.OnRuntimeStart();

                if (!script.RuntimeInstanceCreated || !script.RuntimeAwakeCalled || !script.RuntimeEnabledCalled || !script.RuntimeStartCalled)
                {
                    scene.OnRuntimeStop();
                    result.Passed = false;
                    result.Message = "Script did not pass Awake -> OnEnable -> Start setup. Make sure C# scripts are built.";
                    return result;
                }

                scene.OnUpdate(1.0f / 60.0f);
                const float afterUpdateY = transform.Translation.y;

                scene.SetSceneState(SceneState::Pause);
                scene.OnUpdate(1.0f);
                const float afterPauseY = transform.Translation.y;

                scene.SetSceneState(SceneState::Play);
                script.Enabled = false;
                scene.OnUpdate(1.0f);
                const float afterDisabledY = transform.Translation.y;
                const bool disabledStateSent = !script.RuntimeEnabledCalled;

                script.Enabled = true;
                scene.OnUpdate(1.0f / 60.0f);
                const float afterReenabledY = transform.Translation.y;
                const bool reenabledStateSent = script.RuntimeEnabledCalled;

                scene.OnRuntimeStop();
                const bool runtimeFlagsCleared =
                    !script.RuntimeInstanceCreated &&
                    !script.RuntimeAwakeCalled &&
                    !script.RuntimeEnabledCalled &&
                    !script.RuntimeStartCalled;

                const bool updateMoved = afterUpdateY > 0.0f;
                const bool pauseHeld = std::abs(afterPauseY - afterUpdateY) < 0.0001f;
                const bool disabledHeld = std::abs(afterDisabledY - afterPauseY) < 0.0001f;
                const bool reenabledMoved = afterReenabledY > afterDisabledY;

                result.Passed = updateMoved && pauseHeld && disabledHeld && disabledStateSent && reenabledMoved && reenabledStateSent && runtimeFlagsCleared;
                result.Message = result.Passed
                    ? "Script lifecycle setup, pause, disable/enable, update, and stop cleanup passed."
                    : "Script lifecycle state or movement did not match the expected Play/Pause/Enabled behavior.";
                return result;
            });

        runner.AddTest("RuntimeLifecycle.GameObjectActiveHierarchy", []()
            {
                EditorQATestResult result;
                result.Name = "RuntimeLifecycle.GameObjectActiveHierarchy";

                Scene scene;
                Entity parent = scene.CreateEntity("QA Active Parent");
                Entity child = scene.CreateEntity("QA Active Child");

                auto& parentRel = parent.AddComponent<RelationshipComponent>();
                auto& childRel = child.AddComponent<RelationshipComponent>();
                parentRel.Children.push_back((entt::entity)child);
                childRel.Parent = (entt::entity)parent;

                auto& transform = child.GetComponent<TransformComponent>();
                transform.Translation = { 0.0f, 0.0f, 0.0f };

                auto& script = child.AddComponent<ScriptComponent>();
                script.ClassName = "Game.MoveUp";
                script.Enabled = true;
                script.FieldOverrides["Speed"] = "10.0";

                scene.SetEntityActiveSelf(parent, false);
                const bool childInactiveByParent = !scene.IsEntityActiveInHierarchy(child);

                scene.OnRuntimeStart();
                scene.OnUpdate(1.0f / 60.0f);
                const bool scriptNotCreatedWhileInactive = !script.RuntimeInstanceCreated;
                const bool noMoveWhileInactive = std::abs(transform.Translation.y) < 0.0001f;

                scene.SetEntityActiveSelf(parent, true);
                scene.OnUpdate(1.0f / 60.0f);
                const bool scriptCreatedAfterEnable = script.RuntimeInstanceCreated && script.RuntimeAwakeCalled && script.RuntimeEnabledCalled && script.RuntimeStartCalled;
                const bool movedAfterEnable = transform.Translation.y > 0.0f;

                const float yAfterEnable = transform.Translation.y;
                scene.SetEntityActiveSelf(child, false);
                scene.OnUpdate(1.0f / 60.0f);
                const bool disabledStoppedUpdate = std::abs(transform.Translation.y - yAfterEnable) < 0.0001f && !script.RuntimeEnabledCalled;

                scene.OnRuntimeStop();

                result.Passed = childInactiveByParent && scriptNotCreatedWhileInactive && noMoveWhileInactive && scriptCreatedAfterEnable && movedAfterEnable && disabledStoppedUpdate;
                result.Message = result.Passed
                    ? "ActiveSelf and parent ActiveInHierarchy gate script creation, enable, start, and update correctly."
                    : "Active hierarchy did not gate script lifecycle or movement as expected.";
                return result;
            });

        runner.AddTest("RuntimeLifecycle.ScriptAddRemoveDestroyQueue", []()
            {
                EditorQATestResult result;
                result.Name = "RuntimeLifecycle.ScriptAddRemoveDestroyQueue";

                Scene scene;
                Entity scriptEntity = scene.CreateEntity("QA Runtime Add Remove");
                auto& scriptTransform = scriptEntity.GetComponent<TransformComponent>();
                scriptTransform.Translation = { 0.0f, 0.0f, 0.0f };

                scene.OnRuntimeStart();

                auto& script = scene.AddScriptComponent(scriptEntity, "Game.MoveUp", true);
                script.FieldOverrides["Speed"] = "10.0";

                const bool awakeAndEnableNow =
                    script.RuntimeInstanceCreated &&
                    script.RuntimeAwakeCalled &&
                    script.RuntimeEnabledCalled &&
                    !script.RuntimeStartCalled;

                scene.OnUpdate(1.0f / 60.0f);
                const bool startAndUpdateRan = script.RuntimeStartCalled && scriptTransform.Translation.y > 0.0f;

                const float yAfterRemove = scriptTransform.Translation.y;
                scene.RemoveScriptComponent(scriptEntity);
                const bool removedComponent = !scriptEntity.HasComponent<ScriptComponent>();

                scene.OnUpdate(1.0f / 60.0f);
                const bool removedScriptStopped = std::abs(scriptTransform.Translation.y - yAfterRemove) < 0.0001f;

                Entity destroyEntity = scene.CreateEntity("QA Runtime Destroy Queue");
                scene.AddScriptComponent(destroyEntity, "Game.MoveUp", true);
                scene.OnUpdate(1.0f / 60.0f);

                const entt::entity destroyHandle = (entt::entity)destroyEntity;
                scene.DestroyEntity(destroyEntity);
                const bool queuedUntilFrameEnd = scene.GetRegistry().valid(destroyHandle);

                // Destroy는 프레임 중간에 바로 registry를 지우지 않는다.
                // 다음 Update 끝에서 큐를 비워야 view 반복 중 삭제로 인한 크래시를 피할 수 있다.
                scene.OnUpdate(1.0f / 60.0f);
                const bool destroyedAfterFlush = !scene.GetRegistry().valid(destroyHandle);

                scene.OnRuntimeStop();

                result.Passed = awakeAndEnableNow && startAndUpdateRan && removedComponent && removedScriptStopped && queuedUntilFrameEnd && destroyedAfterFlush;
                result.Message = result.Passed
                    ? "Runtime script add/remove and queued destroy behaved safely in Play Mode."
                    : "Runtime script add/remove or destroy queue did not match the expected lifecycle behavior.";
                return result;
            });

        runner.AddTest("RuntimeLifecycle.PhysicsCollisionTriggerQueue", []()
            {
                EditorQATestResult result;
                result.Name = "RuntimeLifecycle.PhysicsCollisionTriggerQueue";

                auto addPhysicsBody = [](Entity entity, Rigidbody2DComponent::BodyType type, bool isTrigger)
                    {
                        auto& rb = entity.AddComponent<Rigidbody2DComponent>();
                        rb.Type = type;

                        auto& collider = entity.AddComponent<BoxCollider2DComponent>();
                        collider.Size = { 1.0f, 1.0f };
                        collider.IsTrigger = isTrigger;
                    };

                Scene scene;

                Entity collisionProbe = scene.CreateEntity("QA Collision Probe");
                collisionProbe.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 0.0f };
                addPhysicsBody(collisionProbe, Rigidbody2DComponent::BodyType::Dynamic, false);
                auto& collisionScript = collisionProbe.AddComponent<ScriptComponent>();
                collisionScript.ClassName = "Game.PhysicsEventProbe";

                Entity collisionWall = scene.CreateEntity("QA Collision Wall");
                collisionWall.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 0.0f };
                addPhysicsBody(collisionWall, Rigidbody2DComponent::BodyType::Static, false);

                Entity triggerProbe = scene.CreateEntity("QA Trigger Probe");
                triggerProbe.GetComponent<TransformComponent>().Translation = { 3.0f, 0.0f, 0.0f };
                addPhysicsBody(triggerProbe, Rigidbody2DComponent::BodyType::Dynamic, false);
                auto& triggerScript = triggerProbe.AddComponent<ScriptComponent>();
                triggerScript.ClassName = "Game.PhysicsEventProbe";

                Entity triggerZone = scene.CreateEntity("QA Trigger Zone");
                triggerZone.GetComponent<TransformComponent>().Translation = { 3.0f, 0.0f, 0.0f };
                addPhysicsBody(triggerZone, Rigidbody2DComponent::BodyType::Static, true);

                scene.OnRuntimeStart();

                scene.OnUpdate(1.0f / 60.0f);
                const auto collisionAfterEnter = collisionProbe.GetComponent<TransformComponent>().Translation;
                const auto triggerAfterEnter = triggerProbe.GetComponent<TransformComponent>().Translation;
                const bool collisionEnter = collisionAfterEnter.x > 9.0f;
                const bool triggerEnter = triggerAfterEnter.x > 39.0f;

                scene.OnUpdate(1.0f / 60.0f);
                const auto collisionAfterStay = collisionProbe.GetComponent<TransformComponent>().Translation;
                const auto triggerAfterStay = triggerProbe.GetComponent<TransformComponent>().Translation;
                const bool collisionStay = collisionAfterStay.y > 19.0f;
                const bool triggerStay = triggerAfterStay.y > 49.0f;

                scene.OnRuntimeStop();

                result.Passed = collisionEnter && collisionStay && triggerEnter && triggerStay;
                result.Message = result.Passed
                    ? "Collision and trigger events were queued after physics and dispatched to C# scripts."
                    : "Collision or trigger queued event dispatch did not reach the C# probe script.";
                return result;
            });

        runner.AddTest("Editor.UI.Root", [this]()
            {
                EditorQATestResult result;
                result.Name = "Editor.UI.Root";
                result.Passed = m_RootUI != nullptr && m_ViewportWindow != nullptr && m_GameWindow != nullptr;
                result.Message = result.Passed ? "Root UI and default view windows are ready." : "Default editor UI is incomplete.";
                return result;
            });

        runner.AddTest("Editor.Selection.MultiSelectionModel", [this]()
            {
                EditorQATestResult result;
                result.Name = "Editor.Selection.MultiSelectionModel";
                if (!m_ActiveScene || !m_HierarchyPanel)
                {
                    result.Passed = false;
                    result.Message = "Active scene or hierarchy panel is missing.";
                    return result;
                }

                std::vector<Entity> candidates;
                auto view = m_ActiveScene->GetRegistry().view<TransformComponent>();
                for (auto handle : view)
                {
                    candidates.emplace_back(handle, m_ActiveScene);
                    if (candidates.size() >= 2)
                        break;
                }

                if (candidates.size() < 2)
                {
                    result.Passed = false;
                    result.Message = "Need at least two transform entities for multi-selection QA.";
                    return result;
                }

                std::vector<Entity> previousSelection = m_HierarchyPanel->GetSelectedEntities();
                Entity previousActive = m_HierarchyPanel->GetSelectedEntity();

                // 다중 선택은 '선택 묶음'과 '마지막 활성 선택'을 따로 검증한다.
                // 인스펙터는 활성 선택을 보고, 하이어라키/기즈모는 선택 묶음을 본다.
                m_HierarchyPanel->SetSelectedEntities(candidates, candidates[1]);
                bool selectedBoth = m_HierarchyPanel->IsSelected(candidates[0]) && m_HierarchyPanel->IsSelected(candidates[1]);
                bool activeIsSecond = m_HierarchyPanel->GetSelectedEntity() == candidates[1];
                bool countIsTwo = m_HierarchyPanel->GetSelectedEntities().size() == 2;

                m_HierarchyPanel->ClearSelection();
                bool cleared = m_HierarchyPanel->GetSelectedEntities().empty() && !m_HierarchyPanel->GetSelectedEntity();

                m_HierarchyPanel->SetSelectedEntities(previousSelection, previousActive);

                result.Passed = selectedBoth && activeIsSecond && countIsTwo && cleared;
                result.Message = result.Passed ? "Multi-selection stores active and selected entities correctly." : "Multi-selection state did not match expected active/list behavior.";
                return result;
            });

        runner.AddTest("AssetBrowser.Panel.Exists", [this]()
            {
                EditorQATestResult result;
                result.Name = "AssetBrowser.Panel.Exists";
                result.Passed = !m_AssetBrowserPanels.empty() && m_AssetBrowserPanel != nullptr;
                result.Message = result.Passed ? "Asset browser panel is registered." : "Asset browser panel is missing.";
                return result;
            });

        runner.AddTest("AssetBrowser.InternalQA", [this]()
            {
                EditorQATestResult result;
                result.Name = "AssetBrowser.InternalQA";
                if (!m_AssetBrowserPanel)
                {
                    result.Passed = false;
                    result.Message = "Asset browser panel is missing.";
                    return result;
                }

                result.Passed = m_AssetBrowserPanel->RunQualityRegressionChecks();
                result.Message = result.Passed ? "Asset browser invariant checks passed." : "See console for asset browser QA failures.";
                return result;
            });

        runner.AddTest("AssetDatabase.ReferenceValidation", [this]()
            {
                EditorQATestResult result;
                result.Name = "AssetDatabase.ReferenceValidation";
                ValidateAssetReferences(false);
                result.Passed = true;
                result.Message = "Known asset references validation completed.";
                return result;
            });

        const std::filesystem::path qaRoot = std::filesystem::current_path() / "assets" / "__qa_temp__";
        auto makeResult = [](const std::string& name, bool passed, const std::string& message)
            {
                EditorQATestResult result;
                result.Name = name;
                result.Passed = passed;
                result.Message = message;
                return result;
            };

        auto resetQARoot = [qaRoot](std::string& errorMessage)
            {
                std::error_code ec;
                std::filesystem::remove_all(qaRoot, ec);
                if (ec)
                {
                    errorMessage = "Failed to clear QA temp folder: " + ec.message();
                    return false;
                }

                std::filesystem::create_directories(qaRoot, ec);
                if (ec)
                {
                    errorMessage = "Failed to create QA temp folder: " + ec.message();
                    return false;
                }

                return true;
            };

        auto cleanupQARoot = [qaRoot]()
            {
                std::error_code ec;
                std::filesystem::remove_all(qaRoot, ec);
                AssetDatabase::MarkDirty(qaRoot.parent_path());
            };

        auto writeSmallFile = [](const std::filesystem::path& path, const std::string& text, std::string& errorMessage)
            {
                if (!path.parent_path().empty())
                {
                    std::error_code ec;
                    std::filesystem::create_directories(path.parent_path(), ec);
                    if (ec)
                    {
                        errorMessage = "Failed to create parent folder: " + ec.message();
                        return false;
                    }
                }

                std::ofstream file(path, std::ios::binary);
                if (!file)
                {
                    errorMessage = "Failed to write file: " + path.string();
                    return false;
                }

                file << text;
                return true;
            };

        runner.AddTest("AssetUndo.CreateFolderUndoRedo", [qaRoot, makeResult, resetQARoot, cleanupQARoot]()
            {
                std::string errorMessage;
                if (!resetQARoot(errorMessage))
                    return makeResult("AssetUndo.CreateFolderUndoRedo", false, errorMessage);

                const std::filesystem::path folderPath = qaRoot / "CreatedFolder";
                std::error_code ec;
                std::filesystem::create_directories(folderPath, ec);
                if (ec)
                {
                    cleanupQARoot();
                    return makeResult("AssetUndo.CreateFolderUndoRedo", false, "Failed to create folder before undo registration.");
                }

                AssetUndoManager undoManager;
                AssetUndoManager::Command command;
                command.Operation = AssetUndoManager::Kind::CreateFolder;
                command.Label = "QA Create Folder";

                AssetUndoManager::Item item;
                item.ToPath = folderPath;
                item.IsDirectory = true;
                command.Items.push_back(item);
                undoManager.Push(command);

                // QA는 실제 에디터 히스토리를 건드리지 않는 별도 매니저를 쓴다.
                // 테스트 실패가 나도 사용자가 작업 중인 Undo 스택에는 영향이 없어야 한다.
                const bool undoOk = undoManager.Undo();
                const bool removedAfterUndo = !std::filesystem::exists(folderPath, ec) && !ec;
                const bool redoOk = undoManager.Redo();
                const bool restoredAfterRedo = std::filesystem::exists(folderPath, ec) && !ec;

                cleanupQARoot();
                const bool passed = undoOk && removedAfterUndo && redoOk && restoredAfterRedo;
                return makeResult("AssetUndo.CreateFolderUndoRedo", passed, passed ? "Create folder undo/redo changed disk state correctly." : "Create folder undo/redo did not restore the expected disk state.");
            });

        runner.AddTest("AssetUndo.RenameUndoRedo", [qaRoot, makeResult, resetQARoot, cleanupQARoot, writeSmallFile]()
            {
                std::string errorMessage;
                if (!resetQARoot(errorMessage))
                    return makeResult("AssetUndo.RenameUndoRedo", false, errorMessage);

                const std::filesystem::path sourcePath = qaRoot / "RenameSource.txt";
                const std::filesystem::path targetPath = qaRoot / "RenameTarget.txt";
                const std::filesystem::path sourceMeta = AssetDatabase::GetMetaPath(sourcePath);
                const std::filesystem::path targetMeta = AssetDatabase::GetMetaPath(targetPath);

                if (!writeSmallFile(sourcePath, "rename source", errorMessage) || !writeSmallFile(sourceMeta, "guid: qa-rename", errorMessage))
                {
                    cleanupQARoot();
                    return makeResult("AssetUndo.RenameUndoRedo", false, errorMessage);
                }

                std::error_code ec;
                std::filesystem::rename(sourcePath, targetPath, ec);
                if (!ec)
                    std::filesystem::rename(sourceMeta, targetMeta, ec);
                if (ec)
                {
                    cleanupQARoot();
                    return makeResult("AssetUndo.RenameUndoRedo", false, "Failed to prepare renamed file state.");
                }

                AssetUndoManager undoManager;
                AssetUndoManager::Command command;
                command.Operation = AssetUndoManager::Kind::Rename;
                command.Label = "QA Rename Asset";

                AssetUndoManager::Item item;
                item.FromPath = sourcePath;
                item.ToPath = targetPath;
                item.IsDirectory = false;
                command.Items.push_back(item);
                undoManager.Push(command);

                // 에셋 이름 변경은 파일과 meta를 한 묶음으로 되돌려야 GUID 참조가 끊기지 않는다.
                const bool undoOk = undoManager.Undo();
                const bool sourceRestored = std::filesystem::exists(sourcePath, ec) && !ec && std::filesystem::exists(sourceMeta, ec) && !ec;
                const bool targetRemoved = !std::filesystem::exists(targetPath, ec) && !ec && !std::filesystem::exists(targetMeta, ec) && !ec;
                const bool redoOk = undoManager.Redo();
                const bool targetRestored = std::filesystem::exists(targetPath, ec) && !ec && std::filesystem::exists(targetMeta, ec) && !ec;
                const bool sourceRemoved = !std::filesystem::exists(sourcePath, ec) && !ec && !std::filesystem::exists(sourceMeta, ec) && !ec;

                cleanupQARoot();
                const bool passed = undoOk && sourceRestored && targetRemoved && redoOk && targetRestored && sourceRemoved;
                return makeResult("AssetUndo.RenameUndoRedo", passed, passed ? "Rename undo/redo moved asset and meta together." : "Rename undo/redo left asset or meta in the wrong place.");
            });

        runner.AddTest("AssetUndo.MoveUndoRedo", [qaRoot, makeResult, resetQARoot, cleanupQARoot, writeSmallFile]()
            {
                std::string errorMessage;
                if (!resetQARoot(errorMessage))
                    return makeResult("AssetUndo.MoveUndoRedo", false, errorMessage);

                const std::filesystem::path sourcePath = qaRoot / "MoveSource" / "MovedAsset.txt";
                const std::filesystem::path targetPath = qaRoot / "MoveTarget" / "MovedAsset.txt";
                const std::filesystem::path sourceMeta = AssetDatabase::GetMetaPath(sourcePath);
                const std::filesystem::path targetMeta = AssetDatabase::GetMetaPath(targetPath);

                if (!writeSmallFile(sourcePath, "move source", errorMessage) || !writeSmallFile(sourceMeta, "guid: qa-move", errorMessage))
                {
                    cleanupQARoot();
                    return makeResult("AssetUndo.MoveUndoRedo", false, errorMessage);
                }

                std::error_code ec;
                std::filesystem::create_directories(targetPath.parent_path(), ec);
                if (!ec)
                    std::filesystem::rename(sourcePath, targetPath, ec);
                if (!ec)
                    std::filesystem::rename(sourceMeta, targetMeta, ec);
                if (ec)
                {
                    cleanupQARoot();
                    return makeResult("AssetUndo.MoveUndoRedo", false, "Failed to prepare moved file state.");
                }

                AssetUndoManager undoManager;
                AssetUndoManager::Command command;
                command.Operation = AssetUndoManager::Kind::Move;
                command.Label = "QA Move Asset";

                AssetUndoManager::Item item;
                item.FromPath = sourcePath;
                item.ToPath = targetPath;
                item.IsDirectory = false;
                command.Items.push_back(item);
                undoManager.Push(command);

                // 이동 Undo도 이름 변경과 같은 원리다.
                // 실제 파일과 meta가 항상 같은 폴더로 따라다녀야 씬과 프리팹의 GUID 참조가 유지된다.
                const bool undoOk = undoManager.Undo();
                const bool sourceRestored = std::filesystem::exists(sourcePath, ec) && !ec && std::filesystem::exists(sourceMeta, ec) && !ec;
                const bool targetRemoved = !std::filesystem::exists(targetPath, ec) && !ec && !std::filesystem::exists(targetMeta, ec) && !ec;
                const bool redoOk = undoManager.Redo();
                const bool targetRestored = std::filesystem::exists(targetPath, ec) && !ec && std::filesystem::exists(targetMeta, ec) && !ec;
                const bool sourceRemoved = !std::filesystem::exists(sourcePath, ec) && !ec && !std::filesystem::exists(sourceMeta, ec) && !ec;

                cleanupQARoot();
                const bool passed = undoOk && sourceRestored && targetRemoved && redoOk && targetRestored && sourceRemoved;
                return makeResult("AssetUndo.MoveUndoRedo", passed, passed ? "Move undo/redo moved asset and meta together." : "Move undo/redo left asset or meta in the wrong place.");
            });

        runner.AddTest("EditorUI.InputSimulator", []()
            {
                return RunEditorUIInputRegressionChecks();
            });

        // 테스트 러너는 결과를 구조화해서 모으고, 콘솔 출력은 마지막에 한 번만 한다.
        // 이렇게 해야 나중에 CLI 실행, UI 버튼 실행, 로그 파일 저장이 같은 결과 객체를 공유할 수 있다.
        EditorQATestSummary summary = runner.Run();
        EditorQATestRunner::LogSummary(summary);

        Application* app = Application::Get();
        if (app)
        {
            app->SetExitCode(summary.Passed() ? 0 : 1);
            if (closeWhenFinished)
                app->GetWindow().SetShouldClose(true);
        }

        return summary.Passed();
    }

    void EditorLayer::SaveSelectedPrefab()
    {
        if (!m_HierarchyPanel)
            return;

        Entity selected = m_HierarchyPanel->GetSelectedEntity();
        if (!selected)
        {
            ConsoleLog::Warning("No entity selected. Select an entity before saving a prefab.");
            printf("No entity selected. Select an entity before saving a prefab.\n");
            return;
        }

        std::filesystem::path initialDirPath = std::filesystem::current_path() / "assets" / "prefabs";
        std::filesystem::create_directories(initialDirPath);
        std::string initialDirStr = initialDirPath.string();
        std::string filepath = CCEngine::PlatformUtils::SaveFile("CCEngine Prefab (*.ccprefab)\0*.ccprefab\0", initialDirStr.c_str());

        if (!filepath.empty())
        {
            if (PrefabSerializer::Serialize(m_ActiveScene, selected, filepath))
            {
                AssetDatabase::MarkDirty(std::filesystem::current_path() / "assets");
                for (UI::AssetBrowserPanel* browser : m_AssetBrowserPanels)
                {
                    if (browser)
                        browser->Refresh(true);
                }
                ConsoleLog::Info("Prefab saved: " + filepath);
                printf("Prefab Saved to: %s\n", filepath.c_str());
            }
            else
            {
                ConsoleLog::Error("Failed to save prefab: " + filepath);
                printf("Failed to save prefab: %s\n", filepath.c_str());
            }
        }
    }

    void EditorLayer::SaveSelectedPrefabToCurrentAssetFolder()
    {
        if (!m_HierarchyPanel)
            return;

        Entity selected = m_HierarchyPanel->GetSelectedEntity();
        if (!selected)
        {
            ConsoleLog::Warning("No entity selected. Select an entity before creating a prefab.");
            printf("No entity selected. Select an entity before creating a prefab.\n");
            return;
        }

        std::filesystem::path targetDirectory = std::filesystem::current_path() / "assets" / "prefabs";
        if (m_AssetBrowserPanel)
            targetDirectory = m_AssetBrowserPanel->GetCurrentAssetDirectory();

        SavePrefabToDirectory(selected, targetDirectory);
    }

    void EditorLayer::SavePrefabToDirectory(Entity entity, const std::filesystem::path& directory)
    {
        if (!m_ActiveScene || !entity)
            return;

        std::filesystem::create_directories(directory);

        std::string baseName = "Prefab";
        if (entity.HasComponent<TagComponent>())
            baseName = entity.GetComponent<TagComponent>().Tag;

        // 에셋 브라우저로 만드는 프리팹은 현재 폴더에 바로 생성한다. 같은 이름은 번호를 붙여 보존한다.
        std::filesystem::path filepath = MakeUniquePrefabPath(directory, baseName);
        if (PrefabSerializer::Serialize(m_ActiveScene, entity, filepath.string()))
        {
            AssetDatabase::MarkDirty(std::filesystem::current_path() / "assets");
            if (m_AssetBrowserPanel)
                m_AssetBrowserPanel->Refresh(true);
            ConsoleLog::Info("Prefab created: " + filepath.string());
            printf("Prefab Created: %s\n", filepath.string().c_str());
        }
        else
        {
            ConsoleLog::Error("Failed to create prefab: " + filepath.string());
            printf("Failed to create prefab: %s\n", filepath.string().c_str());
        }
    }

    void EditorLayer::InstantiatePrefab()
    {
        std::filesystem::path initialDirPath = std::filesystem::current_path() / "assets" / "prefabs";
        std::filesystem::create_directories(initialDirPath);
        std::string initialDirStr = initialDirPath.string();
        std::string filepath = CCEngine::PlatformUtils::OpenFile("CCEngine Prefab (*.ccprefab)\0*.ccprefab\0", initialDirStr.c_str());

        if (!filepath.empty()) InstantiatePrefab(filepath);
    }

    void EditorLayer::InstantiatePrefab(const std::string& filepath)
    {
        if (!filepath.empty())
        {
            Entity instance = PrefabSerializer::Deserialize(m_ActiveScene, filepath);
            if (instance)
            {
                if (instance.HasComponent<TransformComponent>())
                    instance.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 0.0f };

                RefreshEditorSelection(instance);
                ConsoleLog::Info("Prefab instantiated: " + filepath);
                printf("Prefab Instantiated from: %s\n", filepath.c_str());
            }
            else
            {
                ConsoleLog::Error("Failed to instantiate prefab: " + filepath);
                printf("Failed to instantiate prefab: %s\n", filepath.c_str());
            }
        }
    }

    void EditorLayer::ImportModelAsset(const std::string& filepath)
    {
        if (filepath.empty())
            return;

        Entity modelEntity = ModelImporter::ImportModel(m_ActiveScene, filepath);
        if (modelEntity)
        {
            RefreshEditorSelection(modelEntity);
            ConsoleLog::Info("Model imported: " + filepath);
            printf("Model Imported from: %s\n", filepath.c_str());
        }
    }

    bool EditorLayer::ApplyTextureAssetToEntity(Entity entity, const std::string& filepath)
    {
        if (!entity || !entity.HasComponent<MeshComponent>())
            return false;

        if (AssetDatabase::GetAssetKind(filepath) != AssetKind::Texture)
            return false;

        std::shared_ptr<Texture2D> texture(Texture2D::Create(filepath));
        if (!texture)
            return false;

        m_UndoManager.BeginSceneStructureChange("Apply Texture");

        auto& mesh = entity.GetComponent<MeshComponent>();
        mesh.AlbedoMap = texture;
        // 저장 파일에는 GPU 텍스처 포인터를 남길 수 없다.
        // 경로와 GUID를 같이 남겨야 이름 변경/이동 후에도 같은 에셋을 다시 찾을 수 있다.
        mesh.AlbedoPath = filepath;
        mesh.AlbedoAssetGuid = AssetDatabase::GetGuidFromPath(filepath);

        m_UndoManager.CommitSceneStructureChange();

        for (UI::InspectorPanel* inspector : m_InspectorPanels)
        {
            if (inspector)
                inspector->RequestRebuild();
        }
        MarkHistoryPanelDirty();
        ConsoleLog::Info("Texture applied: " + filepath);
        return true;
    }

    bool EditorLayer::ApplyMaterialAssetToEntity(Entity entity, const std::string& filepath)
    {
        if (!entity || !entity.HasComponent<MeshComponent>())
            return false;

        if (AssetDatabase::GetAssetKind(filepath) != AssetKind::Material)
            return false;

        auto material = std::make_shared<MaterialAsset>();
        if (!material->LoadFromFile(filepath))
            return false;

        m_UndoManager.BeginSceneStructureChange("Apply Material");

        auto& mesh = entity.GetComponent<MeshComponent>();
        mesh.Material = material;
        // Material 에셋은 값 복사가 아니라 참조로 연결한다.
        // 색/텍스처를 재질 파일에서 바꾸면 같은 재질을 쓰는 오브젝트가 같은 결과를 낼 수 있다.
        mesh.MaterialPath = filepath;
        mesh.MaterialAssetGuid = AssetDatabase::GetGuidFromPath(filepath);

        m_UndoManager.CommitSceneStructureChange();

        for (UI::InspectorPanel* inspector : m_InspectorPanels)
        {
            if (inspector)
                inspector->RequestRebuild();
        }
        MarkHistoryPanelDirty();
        ConsoleLog::Info("Material applied: " + filepath);
        return true;
    }

    void EditorLayer::SelectAssetForInspection(const std::filesystem::path& assetPath, const std::string& assetType)
    {
        if (assetType != "material")
            return;

        for (UI::InspectorPanel* inspector : m_InspectorPanels)
        {
            if (inspector && inspector->IsVisible())
                inspector->SetSelectedAsset(assetPath, assetType);
        }
    }

    void EditorLayer::ClearMissingInspectorAssetSelections()
    {
        for (UI::InspectorPanel* inspector : m_InspectorPanels)
        {
            if (inspector)
                inspector->ClearSelectedAssetIfMissing();
        }
    }

    void EditorLayer::ApplyMaterialAssetPreview(const std::filesystem::path& materialPath, const MaterialAsset& material)
    {
        if (!m_ActiveScene || materialPath.empty())
            return;

        const std::string materialGuid = AssetDatabase::GetGuidFromPath(materialPath);
        const std::string materialPathText = materialPath.string();

        auto view = m_ActiveScene->GetRegistry().view<MeshComponent>();
        for (auto entityID : view)
        {
            auto& mesh = view.get<MeshComponent>(entityID);
            const bool sameGuid = !materialGuid.empty() && mesh.MaterialAssetGuid == materialGuid;
            const bool samePath = mesh.MaterialPath == materialPathText;
            if (!sameGuid && !samePath)
                continue;

            // 인스펙터 드래그 중에는 파일을 다시 읽지 않고 현재 메모리 값을 바로 씬에 반영한다.
            // 저장과 검증은 디바운스 후 한 번만 처리해서 머티리얼 편집이 끊기지 않게 한다.
            mesh.Material = std::make_shared<MaterialAsset>(material);
            mesh.MaterialPath = materialPathText;
            mesh.MaterialAssetGuid = materialGuid;
        }

        for (UI::AssetBrowserPanel* browser : m_AssetBrowserPanels)
        {
            if (browser)
                browser->ApplyMaterialPreviewOverride(materialPath, material);
        }
    }

    void EditorLayer::RefreshMaterialAssetReferences(const std::filesystem::path& materialPath)
    {
        if (!m_ActiveScene || materialPath.empty())
            return;

        MaterialAsset latestMaterial;
        if (!latestMaterial.LoadFromFile(materialPath))
            return;

        const std::string materialGuid = AssetDatabase::GetGuidFromPath(materialPath);
        const std::string materialPathText = materialPath.string();

        auto view = m_ActiveScene->GetRegistry().view<MeshComponent>();
        for (auto entityID : view)
        {
            auto& mesh = view.get<MeshComponent>(entityID);
            const bool sameGuid = !materialGuid.empty() && mesh.MaterialAssetGuid == materialGuid;
            const bool samePath = mesh.MaterialPath == materialPathText;
            if (!sameGuid && !samePath)
                continue;

            // 인스펙터에서 Material 파일을 바꾸면 현재 씬의 참조도 즉시 다시 읽는다.
            // 나중에 Material 캐시가 들어가면 이 부분은 캐시 dirty 처리로 바뀐다.
            mesh.Material = std::make_shared<MaterialAsset>(latestMaterial);
            mesh.MaterialPath = materialPathText;
            mesh.MaterialAssetGuid = materialGuid;
        }

    }

    Entity EditorLayer::PickSceneEntityAt(float mouseX, float mouseY) const
    {
        if (!m_Framebuffer || !m_ActiveScene)
            return {};

        for (UI::ImageWidget* viewportWidget : m_ViewportWidgets)
        {
            if (!viewportWidget || !viewportWidget->IsVisible())
                continue;

            auto vpPos = viewportWidget->GetCalculatedPosition();
            auto vpSize = viewportWidget->GetCalculatedSize();
            if (vpSize.x <= 0.0f || vpSize.y <= 0.0f)
                continue;

            const bool insideViewport =
                mouseX >= vpPos.x && mouseX <= vpPos.x + vpSize.x &&
                mouseY >= vpPos.y && mouseY <= vpPos.y + vpSize.y;
            if (!insideViewport)
                continue;

            float localX = mouseX - vpPos.x;
            float localY = mouseY - vpPos.y;
            const auto& spec = m_Framebuffer->GetSpecification();
            uint32_t pixelX = (uint32_t)((localX / vpSize.x) * (float)spec.Width);
            uint32_t pixelY = (uint32_t)((localY / vpSize.y) * (float)spec.Height);
            pixelX = (std::min)(pixelX, spec.Width - 1);
            pixelY = (std::min)(pixelY, spec.Height - 1);

            // 화면에 보이는 위치는 UI 좌표이고, 실제 선택은 피킹 버퍼의 엔티티 ID로 판단한다.
            // 그래서 빈 공간이나 MeshComponent가 없는 대상은 이후 단계에서 자연스럽게 걸러진다.
            int pixelData = m_Framebuffer->ReadPixel(pixelX, pixelY);
            if (pixelData >= 0 && m_ActiveScene->GetRegistry().valid((entt::entity)pixelData))
                return Entity{ (entt::entity)pixelData, m_ActiveScene };
        }

        return {};
    }

    void EditorLayer::HandleAssetDropped(const std::string& filepath, const std::string& assetType, float mouseX, float mouseY)
    {
        if (!m_HierarchyPanel)
            return;

        auto [fallbackMouseX, fallbackMouseY] = CCEngine::Application::Get()->GetWindow().GetMousePosition();
        if (mouseX <= 0.0f && mouseY <= 0.0f)
        {
            mouseX = fallbackMouseX;
            mouseY = fallbackMouseY;
        }

        if (assetType == "texture")
        {
            for (UI::InspectorPanel* inspector : m_InspectorPanels)
            {
                if (!inspector || !inspector->IsVisible())
                    continue;

                if (inspector->IsAlbedoTextureSlotPoint(mouseX, mouseY))
                {
                    if (!ApplyTextureAssetToEntity(inspector->GetSelectedEntity(), filepath))
                        ConsoleLog::Warning("Texture drop ignored: selected object has no Mesh Renderer.");
                    return;
                }
            }

            Entity target = PickSceneEntityAt(mouseX, mouseY);
            if (target)
            {
                if (!ApplyTextureAssetToEntity(target, filepath))
                    ConsoleLog::Warning("Texture drop ignored: target has no Mesh Renderer.");
                return;
            }

            return;
        }

        if (assetType == "material")
        {
            for (UI::InspectorPanel* inspector : m_InspectorPanels)
            {
                if (!inspector || !inspector->IsVisible())
                    continue;

                if (inspector->IsMaterialSlotPoint(mouseX, mouseY))
                {
                    if (!ApplyMaterialAssetToEntity(inspector->GetSelectedEntity(), filepath))
                        ConsoleLog::Warning("Material drop ignored: selected object has no Mesh Renderer.");
                    return;
                }
            }

            Entity target = PickSceneEntityAt(mouseX, mouseY);
            if (target)
            {
                if (!ApplyMaterialAssetToEntity(target, filepath))
                    ConsoleLog::Warning("Material drop ignored: target has no Mesh Renderer.");
                return;
            }

            return;
        }

        if (!m_HierarchyPanel->IsPointInside(mouseX, mouseY))
            return;

        if (assetType == "scene")
        {
            LoadSceneAdditive(filepath);
        }
        else if (assetType == "prefab")
        {
            InstantiatePrefab(filepath);
        }
        else if (assetType == "model")
        {
            ImportModelAsset(filepath);
        }
    }

    UI::AssetBrowserPanel* EditorLayer::FindAssetBrowserAt(float mouseX, float mouseY) const
    {
        for (auto it = m_AssetBrowserPanels.rbegin(); it != m_AssetBrowserPanels.rend(); ++it)
        {
            UI::AssetBrowserPanel* browser = *it;
            if (!browser || !browser->IsVisible())
                continue;

            if (!browser->IsPointInside(mouseX, mouseY))
                continue;

            if (browser->IsMouseBlockedByWidgetAbove(mouseX, mouseY))
                continue;

            return browser;
        }

        return nullptr;
    }

    void EditorLayer::RememberActiveAssetBrowserFromMouse(float mouseX, float mouseY)
    {
        if (UI::AssetBrowserPanel* browser = FindAssetBrowserAt(mouseX, mouseY))
        {
            // Edit 메뉴를 누르는 순간 마우스는 메뉴 위에 있으므로, 마지막으로 직접 조작한 에셋 창을 따로 기억한다.
            // 이 포인터는 파일 작업 Undo/Redo의 컨텍스트 역할만 하고, 실제 파일 이동은 AssetBrowserPanel 내부에서 처리한다.
            m_ActiveAssetBrowserPanel = browser;
        }
    }

    bool EditorLayer::TryUndoAssetOperation()
    {
        if (m_ActiveAssetBrowserPanel && m_ActiveAssetBrowserPanel->IsVisible() &&
            m_ActiveAssetBrowserPanel->CanUndoAssetOperation())
        {
            return m_ActiveAssetBrowserPanel->RequestAssetUndo();
        }

        auto& window = CCEngine::Application::Get()->GetWindow();
        auto [mouseX, mouseY] = window.GetMousePosition();
        if (UI::AssetBrowserPanel* browser = FindAssetBrowserAt(mouseX, mouseY))
        {
            m_ActiveAssetBrowserPanel = browser;
            if (browser->CanUndoAssetOperation())
                return browser->RequestAssetUndo();
        }

        return false;
    }

    bool EditorLayer::TryRedoAssetOperation()
    {
        if (m_ActiveAssetBrowserPanel && m_ActiveAssetBrowserPanel->IsVisible() &&
            m_ActiveAssetBrowserPanel->CanRedoAssetOperation())
        {
            return m_ActiveAssetBrowserPanel->RequestAssetRedo();
        }

        auto& window = CCEngine::Application::Get()->GetWindow();
        auto [mouseX, mouseY] = window.GetMousePosition();
        if (UI::AssetBrowserPanel* browser = FindAssetBrowserAt(mouseX, mouseY))
        {
            m_ActiveAssetBrowserPanel = browser;
            if (browser->CanRedoAssetOperation())
                return browser->RequestAssetRedo();
        }

        return false;
    }

    void EditorLayer::HandleShortcuts()
    {
        bool isRightMouseDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        bool isCtrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool isShiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        bool isSPressedNow = (GetAsyncKeyState('S') & 0x8000) != 0;
        static bool s_IsOPressedLastFrame = false;
        static bool s_IsZPressedLastFrame = false;
        static bool s_IsYPressedLastFrame = false;
        static bool s_IsDPressedLastFrame = false;
        static bool s_IsFPressedLastFrame = false;
        static bool s_IsXPressedLastFrame = false;
        static bool s_IsVPressedLastFrame = false;
        bool isOPressedNow = (GetAsyncKeyState('O') & 0x8000) != 0;
        bool isZPressedNow = (GetAsyncKeyState('Z') & 0x8000) != 0;
        bool isYPressedNow = (GetAsyncKeyState('Y') & 0x8000) != 0;
        bool isDPressedNow = (GetAsyncKeyState('D') & 0x8000) != 0;
        bool isFPressedNow = (GetAsyncKeyState('F') & 0x8000) != 0;
        bool isXPressedNow = (GetAsyncKeyState('X') & 0x8000) != 0;
        bool isVPressedNow = (GetAsyncKeyState('V') & 0x8000) != 0;

        if (!isRightMouseDown)
        {
            bool toolChanged = false;
            if (GetAsyncKeyState('Q') & 0x8000) { m_GizmoSystem.SetMode(GizmoMode::None); toolChanged = true; }
            if (GetAsyncKeyState('W') & 0x8000) { m_GizmoSystem.SetMode(GizmoMode::Translate); toolChanged = true; }
            if (GetAsyncKeyState('E') & 0x8000) { m_GizmoSystem.SetMode(GizmoMode::Rotate); toolChanged = true; }
            if (GetAsyncKeyState('R') & 0x8000) { m_GizmoSystem.SetMode(GizmoMode::Scale); toolChanged = true; }

            if (!isCtrlPressed && isSPressedNow && !m_IsSPressedLastFrame)
            {
                m_GizmoSystem.ToggleSnapping();
                toolChanged = true;
            }

            if (!isCtrlPressed && isXPressedNow && !s_IsXPressedLastFrame)
            {
                m_GizmoSystem.ToggleSpace();
                toolChanged = true;
            }

            if (!isCtrlPressed && isVPressedNow && !s_IsVPressedLastFrame)
            {
                m_GizmoSystem.TogglePivotMode();
                toolChanged = true;
            }

            if (!isCtrlPressed && isFPressedNow && !s_IsFPressedLastFrame)
                FrameSelectedEntity();

            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
            {
                for (UI::HierarchyPanel* hierarchy : m_HierarchyPanels)
                {
                    if (hierarchy)
                        hierarchy->ClearSelection();
                }
            }

            if (toolChanged)
                UpdateSceneToolButtons();
        }

        if (isSPressedNow && !m_IsSPressedLastFrame)
        {
            if (isCtrlPressed && isShiftPressed) SaveSceneAs();
            else if (isCtrlPressed && !isShiftPressed) SaveScene();
        }
        if (isCtrlPressed && isZPressedNow && !s_IsZPressedLastFrame)
        {
            if (isShiftPressed)
            {
                if (!TryRedoAssetOperation())
                    m_UndoManager.Redo();
            }
            else
            {
                if (!TryUndoAssetOperation())
                    m_UndoManager.Undo();
            }
        }
        if (isCtrlPressed && isYPressedNow && !s_IsYPressedLastFrame)
        {
            if (!TryRedoAssetOperation())
                m_UndoManager.Redo();
        }
        if (isCtrlPressed && isDPressedNow && !s_IsDPressedLastFrame)
        {
            DuplicateSelectedObject();
        }
        if (isCtrlPressed && isOPressedNow && !s_IsOPressedLastFrame) OpenScene();
        s_IsOPressedLastFrame = isOPressedNow;
        s_IsZPressedLastFrame = isZPressedNow;
        s_IsYPressedLastFrame = isYPressedNow;
        s_IsDPressedLastFrame = isDPressedNow;
        s_IsFPressedLastFrame = isFPressedNow;
        s_IsXPressedLastFrame = isXPressedNow;
        s_IsVPressedLastFrame = isVPressedNow;
        m_IsSPressedLastFrame = isSPressedNow;
    }


    void EditorLayer::RebuildHistoryPanel()
    {
        // UndoStack + RedoStack을 합쳐 포토샵/유니티식 History 목록으로 다시 그린다.
        // UndoStack 크기가 현재 적용된 작업 위치이며, 해당 항목을 활성 표시한다.
        m_HistoryPanelDirty = false;
        if (m_HistoryContentPanels.empty())
            return;

        const auto& transformUndoStack = m_UndoManager.GetTransformUndoStack();
        const auto& transformRedoStack = m_UndoManager.GetTransformRedoStack();
        const auto& sceneUndoStack = m_UndoManager.GetSceneUndoStack();
        const auto& sceneRedoStack = m_UndoManager.GetSceneRedoStack();

        UI::AssetBrowserPanel* assetHistoryBrowser = nullptr;
        std::vector<std::string> assetHistoryLabels;
        for (UI::AssetBrowserPanel* browser : m_AssetBrowserPanels)
        {
            if (!browser)
                continue;

            std::vector<std::string> labels = browser->GetAssetHistoryLabels();
            if (!labels.empty())
            {
                assetHistoryBrowser = browser;
                assetHistoryLabels = std::move(labels);
                break;
            }
        }

        std::string historySignature;
        if (assetHistoryBrowser && !assetHistoryLabels.empty())
        {
            historySignature = "asset:" + std::to_string(assetHistoryBrowser->GetAppliedAssetHistoryCount()) + ":";
            for (const std::string& label : assetHistoryLabels)
                historySignature += label + "|";
        }
        else if (transformUndoStack.empty() && transformRedoStack.empty() &&
            (!sceneUndoStack.empty() || !sceneRedoStack.empty()))
        {
            historySignature = "scene:" + std::to_string(sceneUndoStack.size()) + ":" + std::to_string(sceneRedoStack.size()) + ":";
            for (const auto& command : sceneUndoStack)
                historySignature += command.Label + "|";
            for (const auto& command : sceneRedoStack)
                historySignature += command.Label + "|";
        }
        else
        {
            historySignature = "transform:" + std::to_string(transformUndoStack.size()) + ":" + std::to_string(transformRedoStack.size()) + ":";
            for (const auto& command : transformUndoStack)
                historySignature += command.EntityPath.empty() ? "Entity|" : command.EntityPath.back() + "|";
            for (const auto& command : transformRedoStack)
                historySignature += command.EntityPath.empty() ? "Entity|" : command.EntityPath.back() + "|";
        }
        historySignature += "panels:" + std::to_string(m_HistoryContentPanels.size());

        if (historySignature == m_LastHistoryPanelSignature)
            return;
        m_LastHistoryPanelSignature = historySignature;

        for (UI::Panel* historyContent : m_HistoryContentPanels)
        {
            if (!historyContent)
                continue;

            historyContent->ClearChildren();

        if (assetHistoryBrowser && !assetHistoryLabels.empty())
        {
            size_t appliedCount = assetHistoryBrowser->GetAppliedAssetHistoryCount();
            float itemHeight = 24.0f;

            auto createAssetHistoryButton = [&](size_t stateIndex, const std::string& text)
            {
                // 에셋 히스토리는 씬 오브젝트가 아니라 디스크 파일 상태를 움직인다.
                // 실제 스택은 공용 AssetUndoManager에 있고, 브라우저는 새로고침 컨텍스트만 제공한다.
                UI::Button* button = new UI::Button("HistoryItem", text);
                button->SetAnchorMin(0.0f, 0.0f);
                button->SetAnchorMax(1.0f, 0.0f);
                button->SetOffsetMin(0.0f, (float)stateIndex * itemHeight);
                button->SetOffsetMax(0.0f, ((float)stateIndex + 1.0f) * itemHeight);
                button->SetNormalColor({ 0.14f, 0.14f, 0.15f, 1.0f });
                button->SetHoverColor({ 0.22f, 0.22f, 0.24f, 1.0f });
                button->SetClickColor({ 0.10f, 0.10f, 0.11f, 1.0f });
                button->SetActive(stateIndex == appliedCount);
                button->SetOnClick([assetHistoryBrowser, stateIndex]() { assetHistoryBrowser->SeekAssetHistory(stateIndex); });
                historyContent->AddChild(button);
            };

            createAssetHistoryButton(0, "Asset Start");
            for (size_t i = 0; i < assetHistoryLabels.size(); ++i)
                createAssetHistoryButton(i + 1, std::to_string(i + 1) + ". " + assetHistoryLabels[i]);

            continue;
        }

        if (transformUndoStack.empty() && transformRedoStack.empty() &&
            (!sceneUndoStack.empty() || !sceneRedoStack.empty()))
        {
            std::vector<EditorUndoManager::SceneStructureCommand> history = sceneUndoStack;
            for (auto it = sceneRedoStack.rbegin(); it != sceneRedoStack.rend(); ++it)
                history.push_back(*it);

            size_t appliedCount = sceneUndoStack.size();
            float itemHeight = 24.0f;

            auto createSceneHistoryButton = [&](size_t stateIndex, const std::string& text)
            {
                UI::Button* button = new UI::Button("HistoryItem", text);
                button->SetAnchorMin(0.0f, 0.0f);
                button->SetAnchorMax(1.0f, 0.0f);
                button->SetOffsetMin(0.0f, (float)stateIndex * itemHeight);
                button->SetOffsetMax(0.0f, ((float)stateIndex + 1.0f) * itemHeight);
                button->SetNormalColor({ 0.14f, 0.14f, 0.15f, 1.0f });
                button->SetHoverColor({ 0.22f, 0.22f, 0.24f, 1.0f });
                button->SetClickColor({ 0.10f, 0.10f, 0.11f, 1.0f });
                button->SetActive(stateIndex == appliedCount);
                button->SetOnClick([this, stateIndex]() { m_UndoManager.SeekSceneHistory(stateIndex); });
                historyContent->AddChild(button);
            };

            createSceneHistoryButton(0, "Scene Start");
            for (size_t i = 0; i < history.size(); ++i)
            {
                std::string label = std::to_string(i + 1) + ". " + history[i].Label;
                createSceneHistoryButton(i + 1, label);
            }
            continue;
        }

        std::vector<EditorUndoManager::TransformUndoCommand> history = transformUndoStack;
        for (auto it = transformRedoStack.rbegin(); it != transformRedoStack.rend(); ++it)
            history.push_back(*it);

        size_t appliedCount = transformUndoStack.size();
        float itemHeight = 24.0f;

        auto createHistoryButton = [&](size_t stateIndex, const std::string& text)
        {
            // stateIndex는 "이 버튼을 눌렀을 때 적용되어 있어야 하는 작업 개수"다.
            // 0은 Scene Start, 1은 첫 작업 적용 후, 2는 두 번째 작업 적용 후를 뜻한다.
            UI::Button* button = new UI::Button("HistoryItem", text);
            button->SetAnchorMin(0.0f, 0.0f);
            button->SetAnchorMax(1.0f, 0.0f);
            button->SetOffsetMin(0.0f, (float)stateIndex * itemHeight);
            button->SetOffsetMax(0.0f, ((float)stateIndex + 1.0f) * itemHeight);
            button->SetNormalColor({ 0.14f, 0.14f, 0.15f, 1.0f });
            button->SetHoverColor({ 0.22f, 0.22f, 0.24f, 1.0f });
            button->SetClickColor({ 0.10f, 0.10f, 0.11f, 1.0f });
            button->SetActive(stateIndex == appliedCount);
            button->SetOnClick([this, stateIndex]() { m_UndoManager.SeekTransformHistory(stateIndex); });
            historyContent->AddChild(button);
        };

        createHistoryButton(0, "Scene Start");
        for (size_t i = 0; i < history.size(); ++i)
        {
            const EditorUndoManager::TransformUndoCommand& command = history[i];
            std::string actionName = "Transform";
            if (command.Before.Translation.x != command.After.Translation.x ||
                command.Before.Translation.y != command.After.Translation.y ||
                command.Before.Translation.z != command.After.Translation.z)
                actionName = "Move";
            else if (command.Before.Rotation.x != command.After.Rotation.x ||
                command.Before.Rotation.y != command.After.Rotation.y ||
                command.Before.Rotation.z != command.After.Rotation.z)
                actionName = "Rotate";
            else if (command.Before.Scale.x != command.After.Scale.x ||
                command.Before.Scale.y != command.After.Scale.y ||
                command.Before.Scale.z != command.After.Scale.z)
                actionName = "Scale";

            std::string entityName = "Entity";
            if (!command.EntityPath.empty())
                entityName = command.EntityPath.back();

            std::string label = std::to_string(i + 1) + ". " + actionName + " " + entityName;
            createHistoryButton(i + 1, label);
        }
        }
    }

    void EditorLayer::MarkHistoryPanelDirty()
    {
        m_HistoryPanelDirty = true;
    }

    void EditorLayer::OpenEditorWindow(int windowKind)
    {
        if (!m_RootUI)
            return;

        auto showHiddenWindow = [](auto& windows) -> bool
        {
            for (auto* window : windows)
            {
                if (window && !window->IsVisible())
                {
                    window->SetVisible(true);
                    window->BringToFront();
                    return true;
                }
            }
            return false;
        };

        auto configureFloatingWindow = [this](UI::WindowPanel* window, float x, float y, float width, float height)
        {
            window->SetAnchorMin(0.0f, 0.0f);
            window->SetAnchorMax(0.0f, 0.0f);
            window->SetOffsetMin(x, y);
            window->SetOffsetMax(x + width, y + height);
            m_RootUI->AddChild(window);
            window->BringToFront();
        };

        auto configureInspector = [this](UI::InspectorPanel* inspector)
        {
            inspector->SetSceneStructureChangeCallbacks(
                [this](const std::string& label) { m_UndoManager.BeginSceneStructureChange(label); },
                [this]() { m_UndoManager.CommitSceneStructureChange(); });
            inspector->SetAssetChangedCallback(
                [this](const std::filesystem::path& path, const std::string& type)
                {
                    if (type == "material")
                        RefreshMaterialAssetReferences(path);
                    QueueAssetReferenceValidation();
                });
            inspector->SetMaterialPreviewChangedCallback(
                [this](const std::filesystem::path& path, const MaterialAsset& material)
                {
                    ApplyMaterialAssetPreview(path, material);
                });
            inspector->SetMaterialPreviewCapturedCallback(
                [this](const std::filesystem::path& path, uint32_t width, uint32_t height, const std::vector<uint32_t>& pixels)
                {
                    for (UI::AssetBrowserPanel* browser : m_AssetBrowserPanels)
                    {
                        if (browser)
                            browser->ApplyMaterialPreviewCapture(path, width, height, pixels);
                    }
                });
            inspector->SetMaterialPreviewTextureReadyCallback(
                [this](const std::filesystem::path& path, RendererHandle texture)
                {
                    for (UI::AssetBrowserPanel* browser : m_AssetBrowserPanels)
                    {
                        if (browser)
                            browser->ApplyMaterialPreviewTexture(path, texture);
                    }
                });
            if (m_HierarchyPanel)
                inspector->SetSelectedEntity(m_HierarchyPanel->GetSelectedEntity());
            m_InspectorPanels.push_back(inspector);
        };

        auto configureAssetBrowser = [this](UI::AssetBrowserPanel* browser)
        {
            browser->SetAssetUndoManager(&m_AssetUndoManager);
            browser->SetExternalWatcherActive(m_AssetFileWatcher.IsRunning());
            browser->SetOnPrefabSelected([this](const std::string& path) { InstantiatePrefab(path); });
            browser->SetOnModelSelected([this](const std::string& path) { ImportModelAsset(path); });
            browser->SetOnSceneSelected([this](const std::string& path) { OpenScene(path); });
            browser->SetOnAssetSelected([this](const std::string& path, const std::string& type) { SelectAssetForInspection(path, type); });
            browser->SetOnAssetDropped([this](const std::string& path, const std::string& type, float x, float y) { HandleAssetDropped(path, type, x, y); });
            browser->SetOnAssetDatabaseChanged([this]()
                {
                    ClearMissingInspectorAssetSelections();
                    QueueAssetReferenceValidation();
                });
            browser->SetOnAssetHistoryChanged([this]() { MarkHistoryPanelDirty(); });
            m_AssetBrowserPanels.push_back(browser);
        };

        switch (windowKind)
        {
            case 0:
            {
                if (showHiddenWindow(m_HierarchyPanels)) return;
                auto* hierarchy = new UI::HierarchyPanel("HierarchyExtra");
                hierarchy->SetContext(m_ActiveScene);
                hierarchy->Refresh();
                m_HierarchyPanels.push_back(hierarchy);
                configureFloatingWindow(hierarchy, 80.0f, 100.0f, 300.0f, 520.0f);
                break;
            }
            case 1:
            {
                if (showHiddenWindow(m_InspectorPanels)) return;
                auto* inspector = new UI::InspectorPanel("InspectorExtra", "Inspector");
                configureInspector(inspector);
                configureFloatingWindow(inspector, 920.0f, 100.0f, 320.0f, 520.0f);
                break;
            }
            case 2:
            {
                if (showHiddenWindow(m_ViewportWindows)) return;
                auto* sceneWindow = new UI::WindowPanel("SceneViewExtra", "Scene View");
                auto* sceneImage = new UI::ImageWidget("SceneViewImageExtra", m_Framebuffer->GetColorAttachmentRendererID(0));
                sceneImage->SetAnchorMin(0.0f, 0.0f);
                sceneImage->SetAnchorMax(1.0f, 1.0f);
                sceneImage->SetOffsetMin(0.0f, 24.0f);
                sceneImage->SetOffsetMax(0.0f, 0.0f);
                sceneWindow->AddChild(sceneImage);
                m_ViewportWindows.push_back(sceneWindow);
                m_ViewportWidgets.push_back(sceneImage);
                configureFloatingWindow(sceneWindow, 300.0f, 120.0f, 620.0f, 380.0f);
                break;
            }
            case 3:
            {
                if (showHiddenWindow(m_GameWindows)) return;
                auto* gameWindow = new UI::WindowPanel("GameViewExtra", "Game View");
                auto* gameImage = new UI::ImageWidget("GameViewImageExtra", m_GameFramebuffer->GetColorAttachmentRendererID(0));
                gameImage->SetAnchorMin(0.0f, 0.0f);
                gameImage->SetAnchorMax(1.0f, 1.0f);
                gameImage->SetOffsetMin(0.0f, 24.0f);
                gameImage->SetOffsetMax(0.0f, 0.0f);
                gameWindow->AddChild(gameImage);
                m_GameWindows.push_back(gameWindow);
                m_GameViewWidgets.push_back(gameImage);
                configureFloatingWindow(gameWindow, 320.0f, 520.0f, 620.0f, 260.0f);
                break;
            }
            case 4:
            {
                if (showHiddenWindow(m_AssetBrowserPanels)) return;
                auto* browser = new UI::AssetBrowserPanel("AssetBrowserExtra");
                configureAssetBrowser(browser);
                configureFloatingWindow(browser, 320.0f, 560.0f, 720.0f, 260.0f);
                break;
            }
            case 5:
            {
                if (showHiddenWindow(m_HistoryPanels)) return;
                auto* history = new UI::WindowPanel("HistoryPanelExtra", "History");
                auto* content = new UI::Panel("HistoryContentExtra", { 0.10f, 0.10f, 0.11f, 1.0f });
                content->SetAnchorMin(0.0f, 0.0f);
                content->SetAnchorMax(1.0f, 1.0f);
                content->SetOffsetMin(0.0f, 24.0f);
                content->SetOffsetMax(0.0f, 0.0f);
                content->SetClipToBounds(true);
                history->AddChild(content);
                m_HistoryPanels.push_back(history);
                m_HistoryContentPanels.push_back(content);
                configureFloatingWindow(history, 960.0f, 420.0f, 300.0f, 260.0f);
                MarkHistoryPanelDirty();
                break;
            }
            case 6:
            {
                if (showHiddenWindow(m_ConsolePanels)) return;
                auto* console = new UI::ConsolePanel("ConsolePanelExtra");
                m_ConsolePanels.push_back(console);
                configureFloatingWindow(console, 960.0f, 600.0f, 360.0f, 260.0f);
                break;
            }
            case 7:
            {
                OpenProjectSettingsWindow();
                break;
            }
            case 8:
            {
                OpenAssetReferenceValidatorWindow();
                break;
            }
            default:
                break;
        }
    }

    void EditorLayer::OpenProjectSettingsWindow()
    {
        if (!m_ProjectSettingsPanel)
            return;

        for (Window* window : Application::Get()->GetSecondaryWindows())
        {
            if (window && !window->ShouldClose() && window->GetRootUI() == m_ProjectSettingsPanel)
            {
                auto [mouseX, mouseY] = Application::Get()->GetWindow().GetScreenMousePosition();
                window->SetPosition(mouseX - 410, mouseY - 24);
                m_ProjectSettingsPanel->SetVisible(true);
                m_ProjectSettingsPanel->OnOpened();
                Application::Get()->SetModalInputWindow(window);
                return;
            }
        }

        if (m_ProjectSettingsPanel->GetParent())
            m_ProjectSettingsPanel->GetParent()->RemoveChild(m_ProjectSettingsPanel);

        Window* settingsWindow = Application::Get()->CreateSecondaryWindow("Project Settings", 820, 560);
        m_ProjectSettingsPanel->SetDockingEnabled(false);
        m_ProjectSettingsPanel->SetOwnerWindow(settingsWindow);
        m_ProjectSettingsPanel->SetVisible(true);
        m_ProjectSettingsPanel->SetAnchorMin(0.0f, 0.0f);
        m_ProjectSettingsPanel->SetAnchorMax(1.0f, 1.0f);
        m_ProjectSettingsPanel->SetOffsetMin(0.0f, 0.0f);
        m_ProjectSettingsPanel->SetOffsetMax(0.0f, 0.0f);
        settingsWindow->SetRootUI(m_ProjectSettingsPanel);
        m_ProjectSettingsPanel->OnOpened();
        Application::Get()->SetModalInputWindow(settingsWindow);
    }

    void EditorLayer::OpenAssetReferenceValidatorWindow()
    {
        if (!m_AssetReferenceValidatorPanel)
            return;

        for (Window* window : Application::Get()->GetSecondaryWindows())
        {
            if (window && !window->ShouldClose() && window->GetRootUI() == m_AssetReferenceValidatorPanel)
            {
                auto [mouseX, mouseY] = Application::Get()->GetWindow().GetScreenMousePosition();
                window->SetPosition(mouseX - 450, mouseY - 24);
                m_AssetReferenceValidatorPanel->SetVisible(true);
                m_AssetReferenceValidatorPanel->Validate(true);
                Application::Get()->SetModalInputWindow(window);
                return;
            }
        }

        if (m_AssetReferenceValidatorPanel->GetParent())
            m_AssetReferenceValidatorPanel->GetParent()->RemoveChild(m_AssetReferenceValidatorPanel);

        Window* validatorWindow = Application::Get()->CreateSecondaryWindow("Asset Reference Validator", 900, 560);
        // 이 창은 레이아웃 검사용 도구 창이다.
        // 메인 편집 패널처럼 리독 대상에 넣으면 검사 중인 창 자체가 도킹 상태에 끌려가므로,
        // 프로젝트 세팅과 같은 별도 OS 창으로만 열고 도킹 경로는 막아 둔다.
        m_AssetReferenceValidatorPanel->SetDockingEnabled(false);
        m_AssetReferenceValidatorPanel->SetOwnerWindow(validatorWindow);
        m_AssetReferenceValidatorPanel->SetVisible(true);
        m_AssetReferenceValidatorPanel->SetAnchorMin(0.0f, 0.0f);
        m_AssetReferenceValidatorPanel->SetAnchorMax(1.0f, 1.0f);
        m_AssetReferenceValidatorPanel->SetOffsetMin(0.0f, 0.0f);
        m_AssetReferenceValidatorPanel->SetOffsetMax(0.0f, 0.0f);
        validatorWindow->SetRootUI(m_AssetReferenceValidatorPanel);
        m_AssetReferenceValidatorPanel->Validate(true);
        Application::Get()->SetModalInputWindow(validatorWindow);
    }

    void EditorLayer::OpenKeyBindingPickerWindow(UI::KeyBindingInput* targetInput)
    {
        if (!targetInput)
            return;

        Window* pickerWindow = Application::Get()->CreateSecondaryWindow("Key Binding", 560, 360);
        auto* pickerPanel = new UI::KeyBindingPickerPanel("KeyBindingPickerPanelUI");
        pickerPanel->SetOwnerWindow(pickerWindow);
        pickerPanel->SetDockingEnabled(false);
        pickerPanel->SetAnchorMin(0.0f, 0.0f);
        pickerPanel->SetAnchorMax(1.0f, 1.0f);
        pickerPanel->SetOffsetMin(0.0f, 0.0f);
        pickerPanel->SetOffsetMax(0.0f, 0.0f);
        pickerPanel->SetInitialBinding(targetInput->GetBinding());
        pickerPanel->SetOnBindingSelected([targetInput](const std::string& binding)
            {
                // 선택 창은 키 문자열만 만들고, 실제 설정 반영은 입력칸의 변경 콜백을 탄다.
                targetInput->SetBinding(binding);
            });

        pickerWindow->SetRootUI(pickerPanel);
        Application::Get()->SetModalInputWindow(pickerWindow);
    }

    void EditorLayer::RefreshHierarchy()
    {}

    void EditorLayer::BuildEditorUI()
    {
        m_RootUI = new UI::Panel("Root", { 0.05f, 0.05f, 0.05f, 1.0f });
        m_RootUI->SetAnchorMin(0.0f, 0.0f);
        m_RootUI->SetAnchorMax(1.0f, 1.0f);
        m_RootUI->SetOffsetMin(0.0f, 0.0f);
        m_RootUI->SetOffsetMax(0.0f, 0.0f);

        m_TitleBarPanel = new UI::Panel("TitleBarUI", { 0.15f, 0.15f, 0.17f, 1.0f });
        m_TitleBarPanel->SetAnchorMin(0.0f, 0.0f); m_TitleBarPanel->SetAnchorMax(1.0f, 0.0f);
        m_TitleBarPanel->SetOffsetMin(0.0f, 0.0f); m_TitleBarPanel->SetOffsetMax(0.0f, 24.0f);
        m_RootUI->AddChild(m_TitleBarPanel);

        m_BtnCloseMain = new UI::Button("BtnCloseMain", "X");
        m_BtnCloseMain->SetAnchorMin(1.0f, 0.0f); m_BtnCloseMain->SetAnchorMax(1.0f, 0.0f);
        m_BtnCloseMain->SetOffsetMin(-30.0f, 0.0f); m_BtnCloseMain->SetOffsetMax(0.0f, 24.0f);
        m_BtnCloseMain->SetOnClick([]() { CCEngine::Application::Get()->GetWindow().SetShouldClose(true); });
        m_TitleBarPanel->AddChild(m_BtnCloseMain);

        m_MenuBarPanel = new UI::Panel("MenuBarUI", { 0.12f, 0.12f, 0.12f, 1.0f });
        m_MenuBarPanel->SetAnchorMin(0.0f, 0.0f); m_MenuBarPanel->SetAnchorMax(1.0f, 0.0f);
        m_MenuBarPanel->SetOffsetMin(0.0f, 24.0f); m_MenuBarPanel->SetOffsetMax(0.0f, 48.0f);
        m_RootUI->AddChild(m_MenuBarPanel);

        m_BtnFileMenu = new UI::Button("BtnFileMenu", "File");
        m_BtnFileMenu->SetAnchorMin(0.0f, 0.0f); m_BtnFileMenu->SetAnchorMax(0.0f, 1.0f);
        m_BtnFileMenu->SetOffsetMin(0.0f, 0.0f); m_BtnFileMenu->SetOffsetMax(60.0f, 0.0f);
        m_MenuBarPanel->AddChild(m_BtnFileMenu);

        m_BtnEditMenu = new UI::Button("BtnEditMenu", "Edit");
        m_BtnEditMenu->SetAnchorMin(0.0f, 0.0f); m_BtnEditMenu->SetAnchorMax(0.0f, 1.0f);
        m_BtnEditMenu->SetOffsetMin(60.0f, 0.0f); m_BtnEditMenu->SetOffsetMax(120.0f, 0.0f);
        m_MenuBarPanel->AddChild(m_BtnEditMenu);

        m_BtnWindowMenu = new UI::Button("BtnWindowMenu", "Window");
        m_BtnWindowMenu->SetAnchorMin(0.0f, 0.0f); m_BtnWindowMenu->SetAnchorMax(0.0f, 1.0f);
        m_BtnWindowMenu->SetOffsetMin(120.0f, 0.0f); m_BtnWindowMenu->SetOffsetMax(205.0f, 0.0f);
        m_MenuBarPanel->AddChild(m_BtnWindowMenu);

        m_HierarchyPanel = new UI::HierarchyPanel("Hierarchy");
        m_HierarchyPanel->SetAnchorMin(0.0f, 0.0f);
        m_HierarchyPanel->SetAnchorMax(0.2f, 1.0f);
        m_HierarchyPanel->SetOffsetMin(0.0f, 48.0f);
        m_HierarchyPanel->SetOffsetMax(0.0f, 0.0f);
        m_RootUI->AddChild(m_HierarchyPanel);
        m_HierarchyPanel->SetContext(m_ActiveScene);
        m_HierarchyPanel->Refresh();
        m_HierarchyPanels.push_back(m_HierarchyPanel);

        m_InspectorPanel = new UI::InspectorPanel("InspectorUI", "Inspector");
        m_InspectorPanel->SetAnchorMin(0.8f, 0.0f);
        m_InspectorPanel->SetAnchorMax(1.0f, 0.50f);
        m_InspectorPanel->SetOffsetMin(0.0f, 48.0f);
        m_InspectorPanel->SetOffsetMax(0.0f, 0.0f);
        m_InspectorPanel->SetSceneStructureChangeCallbacks(
            [this](const std::string& label) { m_UndoManager.BeginSceneStructureChange(label); },
            [this]() { m_UndoManager.CommitSceneStructureChange(); });
        m_InspectorPanel->SetAssetChangedCallback(
            [this](const std::filesystem::path& path, const std::string& type)
            {
                if (type == "material")
                    RefreshMaterialAssetReferences(path);
                QueueAssetReferenceValidation();
            });
        m_InspectorPanel->SetMaterialPreviewChangedCallback(
            [this](const std::filesystem::path& path, const MaterialAsset& material)
            {
                ApplyMaterialAssetPreview(path, material);
            });
        m_InspectorPanel->SetMaterialPreviewCapturedCallback(
            [this](const std::filesystem::path& path, uint32_t width, uint32_t height, const std::vector<uint32_t>& pixels)
            {
                for (UI::AssetBrowserPanel* browser : m_AssetBrowserPanels)
                {
                    if (browser)
                        browser->ApplyMaterialPreviewCapture(path, width, height, pixels);
                }
            });
        m_InspectorPanel->SetMaterialPreviewTextureReadyCallback(
            [this](const std::filesystem::path& path, RendererHandle texture)
            {
                for (UI::AssetBrowserPanel* browser : m_AssetBrowserPanels)
                {
                    if (browser)
                        browser->ApplyMaterialPreviewTexture(path, texture);
                }
            });
        m_RootUI->AddChild(m_InspectorPanel);
        m_InspectorPanels.push_back(m_InspectorPanel);

        m_HistoryPanel = new UI::WindowPanel("HistoryPanelUI", "History");
        m_HistoryPanel->SetAnchorMin(0.8f, 0.50f);
        m_HistoryPanel->SetAnchorMax(1.0f, 0.68f);
        m_HistoryPanel->SetOffsetMin(0.0f, 0.0f);
        m_HistoryPanel->SetOffsetMax(0.0f, 0.0f);
        m_RootUI->AddChild(m_HistoryPanel);

        m_HistoryContentPanel = new UI::Panel("HistoryContentUI", { 0.10f, 0.10f, 0.11f, 1.0f });
        m_HistoryContentPanel->SetAnchorMin(0.0f, 0.0f);
        m_HistoryContentPanel->SetAnchorMax(1.0f, 1.0f);
        m_HistoryContentPanel->SetOffsetMin(0.0f, 24.0f);
        m_HistoryContentPanel->SetOffsetMax(0.0f, 0.0f);
        m_HistoryContentPanel->SetClipToBounds(true);
        m_HistoryPanel->AddChild(m_HistoryContentPanel);
        m_HistoryPanels.push_back(m_HistoryPanel);
        m_HistoryContentPanels.push_back(m_HistoryContentPanel);
        MarkHistoryPanelDirty();

        m_ConsolePanel = new UI::ConsolePanel("ConsolePanelUI");
        m_ConsolePanel->SetAnchorMin(0.8f, 0.68f);
        m_ConsolePanel->SetAnchorMax(1.0f, 1.0f);
        m_ConsolePanel->SetOffsetMin(0.0f, 0.0f);
        m_ConsolePanel->SetOffsetMax(0.0f, 0.0f);
        m_RootUI->AddChild(m_ConsolePanel);
        m_ConsolePanels.push_back(m_ConsolePanel);

        m_ToolbarPanel = new UI::Panel("ToolbarUI", { 0.15f, 0.15f, 0.15f, 1.0f });
        m_ToolbarPanel->SetAnchorMin(0.0f, 0.0f); m_ToolbarPanel->SetAnchorMax(1.0f, 0.0f);
        m_ToolbarPanel->SetOffsetMin(250.0f, 48.0f); m_ToolbarPanel->SetOffsetMax(-300.0f, 88.0f);
        m_RootUI->AddChild(m_ToolbarPanel);

        m_BtnToolSelect = new UI::Button("BtnToolSelect", "Q");
        m_BtnToolSelect->SetAnchorMin(0.0f, 0.5f); m_BtnToolSelect->SetAnchorMax(0.0f, 0.5f);
        m_BtnToolSelect->SetOffsetMin(10.0f, -12.0f); m_BtnToolSelect->SetOffsetMax(40.0f, 12.0f);
        m_ToolbarPanel->AddChild(m_BtnToolSelect);

        m_BtnToolMove = new UI::Button("BtnToolMove", "W");
        m_BtnToolMove->SetAnchorMin(0.0f, 0.5f); m_BtnToolMove->SetAnchorMax(0.0f, 0.5f);
        m_BtnToolMove->SetOffsetMin(44.0f, -12.0f); m_BtnToolMove->SetOffsetMax(74.0f, 12.0f);
        m_ToolbarPanel->AddChild(m_BtnToolMove);

        m_BtnToolRotate = new UI::Button("BtnToolRotate", "E");
        m_BtnToolRotate->SetAnchorMin(0.0f, 0.5f); m_BtnToolRotate->SetAnchorMax(0.0f, 0.5f);
        m_BtnToolRotate->SetOffsetMin(78.0f, -12.0f); m_BtnToolRotate->SetOffsetMax(108.0f, 12.0f);
        m_ToolbarPanel->AddChild(m_BtnToolRotate);

        m_BtnToolScale = new UI::Button("BtnToolScale", "R");
        m_BtnToolScale->SetAnchorMin(0.0f, 0.5f); m_BtnToolScale->SetAnchorMax(0.0f, 0.5f);
        m_BtnToolScale->SetOffsetMin(112.0f, -12.0f); m_BtnToolScale->SetOffsetMax(142.0f, 12.0f);
        m_ToolbarPanel->AddChild(m_BtnToolScale);

        m_BtnToolSpace = new UI::Button("BtnToolSpace", "Local");
        m_BtnToolSpace->SetAnchorMin(0.0f, 0.5f); m_BtnToolSpace->SetAnchorMax(0.0f, 0.5f);
        m_BtnToolSpace->SetOffsetMin(154.0f, -12.0f); m_BtnToolSpace->SetOffsetMax(224.0f, 12.0f);
        m_ToolbarPanel->AddChild(m_BtnToolSpace);

        m_BtnToolPivot = new UI::Button("BtnToolPivot", "Pivot");
        m_BtnToolPivot->SetAnchorMin(0.0f, 0.5f); m_BtnToolPivot->SetAnchorMax(0.0f, 0.5f);
        m_BtnToolPivot->SetOffsetMin(228.0f, -12.0f); m_BtnToolPivot->SetOffsetMax(298.0f, 12.0f);
        m_ToolbarPanel->AddChild(m_BtnToolPivot);

        m_BtnToolSnap = new UI::Button("BtnToolSnap", "Snap");
        m_BtnToolSnap->SetAnchorMin(0.0f, 0.5f); m_BtnToolSnap->SetAnchorMax(0.0f, 0.5f);
        m_BtnToolSnap->SetOffsetMin(302.0f, -12.0f); m_BtnToolSnap->SetOffsetMax(358.0f, 12.0f);
        m_ToolbarPanel->AddChild(m_BtnToolSnap);

        m_BtnToolFrame = new UI::Button("BtnToolFrame", "Frame");
        m_BtnToolFrame->SetAnchorMin(0.0f, 0.5f); m_BtnToolFrame->SetAnchorMax(0.0f, 0.5f);
        m_BtnToolFrame->SetOffsetMin(362.0f, -12.0f); m_BtnToolFrame->SetOffsetMax(424.0f, 12.0f);
        m_ToolbarPanel->AddChild(m_BtnToolFrame);

        m_BtnPhysicsDebug = new UI::Button("BtnPhysicsDebug", "Physics: Off");
        m_BtnPhysicsDebug->SetAnchorMin(0.0f, 0.5f); m_BtnPhysicsDebug->SetAnchorMax(0.0f, 0.5f);
        m_BtnPhysicsDebug->SetOffsetMin(432.0f, -12.0f); m_BtnPhysicsDebug->SetOffsetMax(580.0f, 12.0f);
        m_ToolbarPanel->AddChild(m_BtnPhysicsDebug);

        m_BtnColliderOutline = new UI::Button("BtnColliderOutline", "Collider: Off");
        m_BtnColliderOutline->SetAnchorMin(0.0f, 0.5f); m_BtnColliderOutline->SetAnchorMax(0.0f, 0.5f);
        m_BtnColliderOutline->SetOffsetMin(588.0f, -12.0f); m_BtnColliderOutline->SetOffsetMax(724.0f, 12.0f);
        m_ToolbarPanel->AddChild(m_BtnColliderOutline);

        m_ColliderDebugDropdownPanel = new UI::Panel("ColliderDebugDropdown", { 0.18f, 0.18f, 0.18f, 1.0f });
        m_ColliderDebugDropdownPanel->SetVisible(false);
        m_ColliderDebugDropdownPanel->SetBlockMouseEvents(true);
        m_ColliderDebugDropdownPanel->SetAnchorMin(0.0f, 0.0f);
        m_ColliderDebugDropdownPanel->SetAnchorMax(0.0f, 0.0f);
        m_ColliderDebugDropdownPanel->SetOffsetMin(838.0f, 88.0f);
        m_ColliderDebugDropdownPanel->SetOffsetMax(1010.0f, 140.0f);
        m_RootUI->AddChild(m_ColliderDebugDropdownPanel);

        m_BtnColliderOutlineMode = new UI::Button("BtnColliderOutlineMode", "Outline: Off");
        m_BtnColliderOutlineMode->SetAnchorMin(0.0f, 0.0f); m_BtnColliderOutlineMode->SetAnchorMax(1.0f, 0.0f);
        m_BtnColliderOutlineMode->SetOffsetMin(0.0f, 0.0f); m_BtnColliderOutlineMode->SetOffsetMax(0.0f, 26.0f);
        m_ColliderDebugDropdownPanel->AddChild(m_BtnColliderOutlineMode);

        m_BtnMeshColliderWireMode = new UI::Button("BtnMeshColliderWireMode", "Mesh Wire: Off");
        m_BtnMeshColliderWireMode->SetAnchorMin(0.0f, 0.0f); m_BtnMeshColliderWireMode->SetAnchorMax(1.0f, 0.0f);
        m_BtnMeshColliderWireMode->SetOffsetMin(0.0f, 26.0f); m_BtnMeshColliderWireMode->SetOffsetMax(0.0f, 52.0f);
        m_ColliderDebugDropdownPanel->AddChild(m_BtnMeshColliderWireMode);

        m_BtnPlay = new UI::Button("BtnPlay", "Play");
        m_BtnPlay->SetAnchorMin(1.0f, 0.5f); m_BtnPlay->SetAnchorMax(1.0f, 0.5f);
        m_BtnPlay->SetOffsetMin(-220.0f, -12.0f); m_BtnPlay->SetOffsetMax(-160.0f, 12.0f);
        m_ToolbarPanel->AddChild(m_BtnPlay);

        m_BtnPause = new UI::Button("BtnPause", "Pause");
        m_BtnPause->SetAnchorMin(1.0f, 0.5f); m_BtnPause->SetAnchorMax(1.0f, 0.5f);
        m_BtnPause->SetOffsetMin(-150.0f, -12.0f); m_BtnPause->SetOffsetMax(-80.0f, 12.0f);
        m_ToolbarPanel->AddChild(m_BtnPause);

        m_BtnStop = new UI::Button("BtnStop", "Stop");
        m_BtnStop->SetAnchorMin(1.0f, 0.5f); m_BtnStop->SetAnchorMax(1.0f, 0.5f);
        m_BtnStop->SetOffsetMin(-70.0f, -12.0f); m_BtnStop->SetOffsetMax(-10.0f, 12.0f);
        m_ToolbarPanel->AddChild(m_BtnStop);

        m_ViewportWindow = new UI::WindowPanel("ViewportWindowUI", "Scene View");
        m_ViewportWindow->SetAnchorMin(0.2f, 0.0f);
        m_ViewportWindow->SetAnchorMax(0.8f, 0.55f);
        m_ViewportWindow->SetOffsetMin(0.0f, 48.0f);
        m_ViewportWindow->SetOffsetMax(0.0f, 0.0f);
        m_RootUI->AddChild(m_ViewportWindow);

        RendererHandle editorTex = m_Framebuffer->GetColorAttachmentRendererID(0);
        m_ViewportWidget = new UI::ImageWidget("ViewportWidget", editorTex);
        m_ViewportWidget->SetAnchorMin(0.0f, 0.0f); m_ViewportWidget->SetAnchorMax(1.0f, 1.0f);
        m_ViewportWidget->SetOffsetMin(0.0f, 24.0f); m_ViewportWidget->SetOffsetMax(0.0f, 0.0f);
        m_ViewportWindow->AddChild(m_ViewportWidget);
        m_ViewportWindows.push_back(m_ViewportWindow);
        m_ViewportWidgets.push_back(m_ViewportWidget);

        m_GameWindow = new UI::WindowPanel("GameWindowUI", "Game View");
        m_GameWindow->SetAnchorMin(0.2f, 0.55f); m_GameWindow->SetAnchorMax(0.8f, 0.75f);
        m_GameWindow->SetOffsetMin(0.0f, 0.0f); m_GameWindow->SetOffsetMax(0.0f, 0.0f);
        m_RootUI->AddChild(m_GameWindow);

        RendererHandle gameTex = m_GameFramebuffer->GetColorAttachmentRendererID(0);
        m_GameViewWidget = new UI::ImageWidget("GameViewWidget", gameTex);
        m_GameViewWidget->SetAnchorMin(0.0f, 0.0f); m_GameViewWidget->SetAnchorMax(1.0f, 1.0f);
        m_GameViewWidget->SetOffsetMin(0.0f, 24.0f); m_GameViewWidget->SetOffsetMax(0.0f, 0.0f);
        m_GameWindow->AddChild(m_GameViewWidget);
        m_GameWindows.push_back(m_GameWindow);
        m_GameViewWidgets.push_back(m_GameViewWidget);

        m_AssetBrowserPanel = new UI::AssetBrowserPanel("AssetBrowserUI");
        m_AssetBrowserPanel->SetAnchorMin(0.2f, 0.75f);
        m_AssetBrowserPanel->SetAnchorMax(0.8f, 1.0f);
        m_AssetBrowserPanel->SetOffsetMin(0.0f, 0.0f);
        m_AssetBrowserPanel->SetOffsetMax(0.0f, 0.0f);
        m_AssetBrowserPanel->SetAssetUndoManager(&m_AssetUndoManager);
        m_AssetBrowserPanel->SetExternalWatcherActive(m_AssetFileWatcher.IsRunning());
        m_AssetBrowserPanel->SetOnPrefabSelected([this](const std::string& path) { InstantiatePrefab(path); });
        m_AssetBrowserPanel->SetOnModelSelected([this](const std::string& path) { ImportModelAsset(path); });
        m_AssetBrowserPanel->SetOnSceneSelected([this](const std::string& path) { OpenScene(path); });
        m_AssetBrowserPanel->SetOnAssetSelected([this](const std::string& path, const std::string& type) { SelectAssetForInspection(path, type); });
        m_AssetBrowserPanel->SetOnAssetDropped([this](const std::string& path, const std::string& type, float x, float y) { HandleAssetDropped(path, type, x, y); });
        m_AssetBrowserPanel->SetOnAssetDatabaseChanged([this]()
            {
                ClearMissingInspectorAssetSelections();
                QueueAssetReferenceValidation();
            });
        m_AssetBrowserPanel->SetOnAssetHistoryChanged([this]() { MarkHistoryPanelDirty(); });
        m_RootUI->AddChild(m_AssetBrowserPanel);
        m_AssetBrowserPanels.push_back(m_AssetBrowserPanel);

        m_FileDropdownPanel = new UI::Panel("FileDropdownUI", { 0.18f, 0.18f, 0.18f, 1.0f });
        m_FileDropdownPanel->SetVisible(false);
        m_FileDropdownPanel->SetBlockMouseEvents(true);
        m_FileDropdownPanel->SetAnchorMin(0.0f, 0.0f); m_FileDropdownPanel->SetAnchorMax(0.0f, 0.0f);
        m_FileDropdownPanel->SetOffsetMin(0.0f, 48.0f); m_FileDropdownPanel->SetOffsetMax(150.0f, 48.0f + 150.0f);
        m_RootUI->AddChild(m_FileDropdownPanel);

        m_BtnOpen = new UI::Button("BtnOpen", "Open Scene");
        m_BtnOpen->SetAnchorMin(0.0f, 0.0f); m_BtnOpen->SetAnchorMax(1.0f, 0.0f);
        m_BtnOpen->SetOffsetMin(0.0f, 0.0f); m_BtnOpen->SetOffsetMax(0.0f, 25.0f);
        m_FileDropdownPanel->AddChild(m_BtnOpen);

        m_BtnSave = new UI::Button("BtnSave", "Save");
        m_BtnSave->SetAnchorMin(0.0f, 0.0f); m_BtnSave->SetAnchorMax(1.0f, 0.0f);
        m_BtnSave->SetOffsetMin(0.0f, 25.0f); m_BtnSave->SetOffsetMax(0.0f, 50.0f);
        m_FileDropdownPanel->AddChild(m_BtnSave);

        m_BtnSaveAs = new UI::Button("BtnSaveAs", "Save As...");
        m_BtnSaveAs->SetAnchorMin(0.0f, 0.0f); m_BtnSaveAs->SetAnchorMax(1.0f, 0.0f);
        m_BtnSaveAs->SetOffsetMin(0.0f, 50.0f); m_BtnSaveAs->SetOffsetMax(0.0f, 75.0f);
        m_FileDropdownPanel->AddChild(m_BtnSaveAs);

        m_BtnSavePrefab = new UI::Button("BtnSavePrefab", "Save Prefab");
        m_BtnSavePrefab->SetAnchorMin(0.0f, 0.0f); m_BtnSavePrefab->SetAnchorMax(1.0f, 0.0f);
        m_BtnSavePrefab->SetOffsetMin(0.0f, 75.0f); m_BtnSavePrefab->SetOffsetMax(0.0f, 100.0f);
        m_FileDropdownPanel->AddChild(m_BtnSavePrefab);

        m_BtnInstantiatePrefab = new UI::Button("BtnInstantiatePrefab", "Load Prefab");
        m_BtnInstantiatePrefab->SetAnchorMin(0.0f, 0.0f); m_BtnInstantiatePrefab->SetAnchorMax(1.0f, 0.0f);
        m_BtnInstantiatePrefab->SetOffsetMin(0.0f, 100.0f); m_BtnInstantiatePrefab->SetOffsetMax(0.0f, 125.0f);
        m_FileDropdownPanel->AddChild(m_BtnInstantiatePrefab);

        m_BtnExit = new UI::Button("BtnExit", "Exit");
        m_BtnExit->SetAnchorMin(0.0f, 0.0f); m_BtnExit->SetAnchorMax(1.0f, 0.0f);
        m_BtnExit->SetOffsetMin(0.0f, 125.0f); m_BtnExit->SetOffsetMax(0.0f, 150.0f);
        m_FileDropdownPanel->AddChild(m_BtnExit);

        m_EditDropdownPanel = new UI::Panel("EditDropdownUI", { 0.18f, 0.18f, 0.18f, 1.0f });
        m_EditDropdownPanel->SetVisible(false);
        m_EditDropdownPanel->SetBlockMouseEvents(true);
        m_EditDropdownPanel->SetAnchorMin(0.0f, 0.0f); m_EditDropdownPanel->SetAnchorMax(0.0f, 0.0f);
        m_EditDropdownPanel->SetOffsetMin(60.0f, 48.0f); m_EditDropdownPanel->SetOffsetMax(230.0f, 48.0f + 100.0f);
        m_RootUI->AddChild(m_EditDropdownPanel);

        m_BtnEditUndo = new UI::Button("BtnEditUndo", "Undo    Ctrl+Z");
        m_BtnEditUndo->SetAnchorMin(0.0f, 0.0f); m_BtnEditUndo->SetAnchorMax(1.0f, 0.0f);
        m_BtnEditUndo->SetOffsetMin(0.0f, 0.0f); m_BtnEditUndo->SetOffsetMax(0.0f, 25.0f);
        m_EditDropdownPanel->AddChild(m_BtnEditUndo);

        m_BtnEditRedo = new UI::Button("BtnEditRedo", "Redo    Ctrl+Y");
        m_BtnEditRedo->SetAnchorMin(0.0f, 0.0f); m_BtnEditRedo->SetAnchorMax(1.0f, 0.0f);
        m_BtnEditRedo->SetOffsetMin(0.0f, 25.0f); m_BtnEditRedo->SetOffsetMax(0.0f, 50.0f);
        m_EditDropdownPanel->AddChild(m_BtnEditRedo);

        m_BtnEditDuplicate = new UI::Button("BtnEditDuplicate", "Duplicate Ctrl+D");
        m_BtnEditDuplicate->SetAnchorMin(0.0f, 0.0f); m_BtnEditDuplicate->SetAnchorMax(1.0f, 0.0f);
        m_BtnEditDuplicate->SetOffsetMin(0.0f, 50.0f); m_BtnEditDuplicate->SetOffsetMax(0.0f, 75.0f);
        m_EditDropdownPanel->AddChild(m_BtnEditDuplicate);

        m_BtnProjectSettings = new UI::Button("BtnProjectSettings", "Project Settings...");
        m_BtnProjectSettings->SetAnchorMin(0.0f, 0.0f); m_BtnProjectSettings->SetAnchorMax(1.0f, 0.0f);
        m_BtnProjectSettings->SetOffsetMin(0.0f, 75.0f); m_BtnProjectSettings->SetOffsetMax(0.0f, 100.0f);
        m_EditDropdownPanel->AddChild(m_BtnProjectSettings);

        m_WindowDropdownPanel = new UI::Panel("WindowDropdownUI", { 0.18f, 0.18f, 0.18f, 1.0f });
        m_WindowDropdownPanel->SetVisible(false);
        m_WindowDropdownPanel->SetBlockMouseEvents(true);
        m_WindowDropdownPanel->SetAnchorMin(0.0f, 0.0f);
        m_WindowDropdownPanel->SetAnchorMax(0.0f, 0.0f);
        m_WindowDropdownPanel->SetOffsetMin(120.0f, 48.0f);
        m_WindowDropdownPanel->SetOffsetMax(320.0f, 48.0f + 225.0f);
        m_RootUI->AddChild(m_WindowDropdownPanel);

        const char* windowNames[] =
        {
            "Hierarchy",
            "Inspector",
            "Scene View",
            "Game View",
            "Asset Browser",
            "History",
            "Console",
            "Project Settings...",
            "Asset Reference Validator"
        };

        for (int i = 0; i < 9; ++i)
        {
            auto* button = new UI::Button(std::string("BtnWindowMenu") + std::to_string(i), windowNames[i]);
            button->SetAnchorMin(0.0f, 0.0f);
            button->SetAnchorMax(1.0f, 0.0f);
            button->SetOffsetMin(0.0f, (float)i * 25.0f);
            button->SetOffsetMax(0.0f, ((float)i + 1.0f) * 25.0f);
            m_WindowDropdownPanel->AddChild(button);
            m_WindowMenuButtons.push_back(button);
        }

        m_ProjectSettingsPanel = new UI::ProjectSettingsPanel("ProjectSettingsPanelUI");
        m_ProjectSettingsPanel->SetSettings(&m_ProjectSettings);
        m_ProjectSettingsPanel->SetDockingEnabled(false);
        m_ProjectSettingsPanel->SetVisible(false);
        m_ProjectSettingsPanel->SetBlockMouseEvents(true);
        m_ProjectSettingsPanel->SetAnchorMin(0.0f, 0.0f);
        m_ProjectSettingsPanel->SetAnchorMax(1.0f, 1.0f);
        m_ProjectSettingsPanel->SetOffsetMin(0.0f, 0.0f);
        m_ProjectSettingsPanel->SetOffsetMax(0.0f, 0.0f);
        m_ProjectSettingsPanel->SetCallbacks(
            [this]() { SetCurrentSceneAsProjectStartScene(); },
            [this]() { OpenProjectStartScene(); },
            [this]() { SaveProjectSettings(); },
            [this]() { ApplyProjectGameResolution(); });
        m_ProjectSettingsPanel->SetKeyBindingPickerCallback(
            [this](UI::KeyBindingInput* targetInput) { OpenKeyBindingPickerWindow(targetInput); });

        m_AssetReferenceValidatorPanel = new UI::AssetReferenceValidatorPanel("AssetReferenceValidatorPanelUI");
        m_AssetReferenceValidatorPanel->SetVisible(false);
        m_AssetReferenceValidatorPanel->SetBlockMouseEvents(true);
        m_AssetReferenceValidatorPanel->SetDockingEnabled(false);
        m_AssetReferenceValidatorPanel->SetAnchorMin(0.0f, 0.0f);
        m_AssetReferenceValidatorPanel->SetAnchorMax(1.0f, 1.0f);
        m_AssetReferenceValidatorPanel->SetOffsetMin(0.0f, 0.0f);
        m_AssetReferenceValidatorPanel->SetOffsetMax(0.0f, 0.0f);
        // 이 패널은 메인 루트에 붙이지 않는다.
        // Window 메뉴에서 열 때 보조 창의 Root UI로 연결해야 OS 창 이동/리사이즈가 그대로 적용된다.

        m_ObjectContextMenuPanel = new UI::Panel("ObjectContextMenu", { 0.14f, 0.14f, 0.15f, 1.0f });
        m_ObjectContextMenuPanel->SetVisible(false);
        m_ObjectContextMenuPanel->SetBlockMouseEvents(true);
        m_ObjectContextMenuPanel->SetAnchorMin(0.0f, 0.0f);
        m_ObjectContextMenuPanel->SetAnchorMax(0.0f, 0.0f);
        m_ObjectContextMenuPanel->SetOffsetMin(0.0f, 0.0f);
        m_ObjectContextMenuPanel->SetOffsetMax(160.0f, 130.0f);
        m_RootUI->AddChild(m_ObjectContextMenuPanel);

        m_BtnCreateEmpty = new UI::Button("BtnCreateEmpty", "Create Empty");
        m_BtnCreateEmpty->SetAnchorMin(0.0f, 0.0f); m_BtnCreateEmpty->SetAnchorMax(1.0f, 0.0f);
        m_BtnCreateEmpty->SetOffsetMin(0.0f, 0.0f); m_BtnCreateEmpty->SetOffsetMax(0.0f, 26.0f);
        m_ObjectContextMenuPanel->AddChild(m_BtnCreateEmpty);

        m_BtnCreateMeshObject = new UI::Button("BtnCreateMeshObject", "Create Mesh Object >");
        m_BtnCreateMeshObject->SetAnchorMin(0.0f, 0.0f); m_BtnCreateMeshObject->SetAnchorMax(1.0f, 0.0f);
        m_BtnCreateMeshObject->SetOffsetMin(0.0f, 26.0f); m_BtnCreateMeshObject->SetOffsetMax(0.0f, 52.0f);
        m_ObjectContextMenuPanel->AddChild(m_BtnCreateMeshObject);

        m_BtnCreateLight = new UI::Button("BtnCreateLight", "Create Light");
        m_BtnCreateLight->SetAnchorMin(0.0f, 0.0f); m_BtnCreateLight->SetAnchorMax(1.0f, 0.0f);
        m_BtnCreateLight->SetOffsetMin(0.0f, 52.0f); m_BtnCreateLight->SetOffsetMax(0.0f, 78.0f);
        m_ObjectContextMenuPanel->AddChild(m_BtnCreateLight);

        m_BtnCreateCamera = new UI::Button("BtnCreateCamera", "Create Camera");
        m_BtnCreateCamera->SetAnchorMin(0.0f, 0.0f); m_BtnCreateCamera->SetAnchorMax(1.0f, 0.0f);
        m_BtnCreateCamera->SetOffsetMin(0.0f, 78.0f); m_BtnCreateCamera->SetOffsetMax(0.0f, 104.0f);
        m_ObjectContextMenuPanel->AddChild(m_BtnCreateCamera);

        m_BtnCreatePrefab = new UI::Button("BtnCreatePrefab", "Create Prefab");
        m_BtnCreatePrefab->SetAnchorMin(0.0f, 0.0f); m_BtnCreatePrefab->SetAnchorMax(1.0f, 0.0f);
        m_BtnCreatePrefab->SetOffsetMin(0.0f, 104.0f); m_BtnCreatePrefab->SetOffsetMax(0.0f, 130.0f);
        m_ObjectContextMenuPanel->AddChild(m_BtnCreatePrefab);

        m_BtnDeleteObject = new UI::Button("BtnDeleteObject", "Delete Selected");
        m_BtnDeleteObject->SetAnchorMin(0.0f, 0.0f); m_BtnDeleteObject->SetAnchorMax(1.0f, 0.0f);
        m_BtnDeleteObject->SetOffsetMin(0.0f, 130.0f); m_BtnDeleteObject->SetOffsetMax(0.0f, 156.0f);
        m_ObjectContextMenuPanel->AddChild(m_BtnDeleteObject);

        // 메시 프리미티브는 별도 하위 메뉴로 묶는다.
        m_MeshObjectSubmenuPanel = new UI::Panel("MeshObjectSubmenu", { 0.14f, 0.14f, 0.15f, 1.0f });
        m_MeshObjectSubmenuPanel->SetVisible(false);
        m_MeshObjectSubmenuPanel->SetBlockMouseEvents(true);
        m_MeshObjectSubmenuPanel->SetAnchorMin(0.0f, 0.0f);
        m_MeshObjectSubmenuPanel->SetAnchorMax(0.0f, 0.0f);
        m_MeshObjectSubmenuPanel->SetOffsetMin(0.0f, 0.0f);
        m_MeshObjectSubmenuPanel->SetOffsetMax(160.0f, 182.0f);
        m_RootUI->AddChild(m_MeshObjectSubmenuPanel);

        m_BtnCreateCube = new UI::Button("BtnCreateCube", "Create Cube");
        m_BtnCreateCube->SetAnchorMin(0.0f, 0.0f); m_BtnCreateCube->SetAnchorMax(1.0f, 0.0f);
        m_BtnCreateCube->SetOffsetMin(0.0f, 0.0f); m_BtnCreateCube->SetOffsetMax(0.0f, 26.0f);
        m_MeshObjectSubmenuPanel->AddChild(m_BtnCreateCube);

        m_BtnCreateSphere = new UI::Button("BtnCreateSphere", "Create Sphere");
        m_BtnCreateSphere->SetAnchorMin(0.0f, 0.0f); m_BtnCreateSphere->SetAnchorMax(1.0f, 0.0f);
        m_BtnCreateSphere->SetOffsetMin(0.0f, 26.0f); m_BtnCreateSphere->SetOffsetMax(0.0f, 52.0f);
        m_MeshObjectSubmenuPanel->AddChild(m_BtnCreateSphere);

        m_BtnCreateCapsule = new UI::Button("BtnCreateCapsule", "Create Capsule");
        m_BtnCreateCapsule->SetAnchorMin(0.0f, 0.0f); m_BtnCreateCapsule->SetAnchorMax(1.0f, 0.0f);
        m_BtnCreateCapsule->SetOffsetMin(0.0f, 52.0f); m_BtnCreateCapsule->SetOffsetMax(0.0f, 78.0f);
        m_MeshObjectSubmenuPanel->AddChild(m_BtnCreateCapsule);

        m_BtnCreateCylinder = new UI::Button("BtnCreateCylinder", "Create Cylinder");
        m_BtnCreateCylinder->SetAnchorMin(0.0f, 0.0f); m_BtnCreateCylinder->SetAnchorMax(1.0f, 0.0f);
        m_BtnCreateCylinder->SetOffsetMin(0.0f, 78.0f); m_BtnCreateCylinder->SetOffsetMax(0.0f, 104.0f);
        m_MeshObjectSubmenuPanel->AddChild(m_BtnCreateCylinder);

        m_BtnCreatePlane = new UI::Button("BtnCreatePlane", "Create Plane");
        m_BtnCreatePlane->SetAnchorMin(0.0f, 0.0f); m_BtnCreatePlane->SetAnchorMax(1.0f, 0.0f);
        m_BtnCreatePlane->SetOffsetMin(0.0f, 104.0f); m_BtnCreatePlane->SetOffsetMax(0.0f, 130.0f);
        m_MeshObjectSubmenuPanel->AddChild(m_BtnCreatePlane);

        m_BtnCreateQuad = new UI::Button("BtnCreateQuad", "Create Quad");
        m_BtnCreateQuad->SetAnchorMin(0.0f, 0.0f); m_BtnCreateQuad->SetAnchorMax(1.0f, 0.0f);
        m_BtnCreateQuad->SetOffsetMin(0.0f, 130.0f); m_BtnCreateQuad->SetOffsetMax(0.0f, 156.0f);
        m_MeshObjectSubmenuPanel->AddChild(m_BtnCreateQuad);

        m_BtnCreateTorus = new UI::Button("BtnCreateTorus", "Create Torus");
        m_BtnCreateTorus->SetAnchorMin(0.0f, 0.0f); m_BtnCreateTorus->SetAnchorMax(1.0f, 0.0f);
        m_BtnCreateTorus->SetOffsetMin(0.0f, 156.0f); m_BtnCreateTorus->SetOffsetMax(0.0f, 182.0f);
        m_MeshObjectSubmenuPanel->AddChild(m_BtnCreateTorus);

        // --- 버튼 & 툴바 콜백 등록 ---
        m_BtnFileMenu->SetOnClick([this]()
            {
                m_FileDropdownPanel->SetVisible(!m_FileDropdownPanel->IsVisible());
                m_EditDropdownPanel->SetVisible(false);
                m_WindowDropdownPanel->SetVisible(false);
                HideColliderDebugDropdown();
                BringEditorOverlaysToFront();
            });
        m_BtnEditMenu->SetOnClick([this]()
            {
                m_EditDropdownPanel->SetVisible(!m_EditDropdownPanel->IsVisible());
                m_FileDropdownPanel->SetVisible(false);
                m_WindowDropdownPanel->SetVisible(false);
                HideColliderDebugDropdown();
                BringEditorOverlaysToFront();
            });
        m_BtnWindowMenu->SetOnClick([this]()
            {
                m_WindowDropdownPanel->SetVisible(!m_WindowDropdownPanel->IsVisible());
                m_FileDropdownPanel->SetVisible(false);
                m_EditDropdownPanel->SetVisible(false);
                HideColliderDebugDropdown();
                BringEditorOverlaysToFront();
            });

        for (size_t i = 0; i < m_WindowMenuButtons.size(); ++i)
        {
            m_WindowMenuButtons[i]->SetOnClick([this, i]()
                {
                    m_WindowDropdownPanel->SetVisible(false);
                    OpenEditorWindow((int)i);
                });
        }
        m_BtnOpen->SetOnClick([this]() { m_FileDropdownPanel->SetVisible(false); OpenScene(); });
        m_BtnSave->SetOnClick([this]() { m_FileDropdownPanel->SetVisible(false); SaveScene(); });
        m_BtnSaveAs->SetOnClick([this]() { m_FileDropdownPanel->SetVisible(false); SaveSceneAs(); });
        m_BtnSavePrefab->SetOnClick([this]() { m_FileDropdownPanel->SetVisible(false); SaveSelectedPrefab(); });
        m_BtnInstantiatePrefab->SetOnClick([this]() { m_FileDropdownPanel->SetVisible(false); InstantiatePrefab(); });
        m_BtnExit->SetOnClick([this]() { CCEngine::Application::Get()->GetWindow().SetShouldClose(true); });
        m_BtnEditUndo->SetOnClick([this]()
            {
                m_EditDropdownPanel->SetVisible(false);
                if (!TryUndoAssetOperation())
                    m_UndoManager.Undo();
            });
        m_BtnEditRedo->SetOnClick([this]()
            {
                m_EditDropdownPanel->SetVisible(false);
                if (!TryRedoAssetOperation())
                    m_UndoManager.Redo();
            });
        m_BtnEditDuplicate->SetOnClick([this]() { m_EditDropdownPanel->SetVisible(false); DuplicateSelectedObject(); });
        m_BtnProjectSettings->SetOnClick([this]()
            {
                m_EditDropdownPanel->SetVisible(false);
                OpenProjectSettingsWindow();
                BringEditorOverlaysToFront();
            });
        m_BtnCreateEmpty->SetOnClick([this]() { CreateEmptyObject(); HideObjectContextMenu(); });
        m_BtnCreateMeshObject->SetOnClick([this]() { ShowMeshObjectSubmenu(); });
        m_BtnCreateLight->SetOnClick([this]() { CreateLightObject(); HideObjectContextMenu(); });
        m_BtnCreateCamera->SetOnClick([this]() { CreateCameraObject(); HideObjectContextMenu(); });
        m_BtnCreatePrefab->SetOnClick([this]() { SaveSelectedPrefab(); HideObjectContextMenu(); });
        m_BtnCreateCube->SetOnClick([this]() { CreatePrimitiveObject("Cube", (int)MeshComponent::MeshType::Cube); HideObjectContextMenu(); });
        m_BtnCreateSphere->SetOnClick([this]() { CreatePrimitiveObject("Sphere", (int)MeshComponent::MeshType::Sphere); HideObjectContextMenu(); });
        m_BtnCreateCapsule->SetOnClick([this]() { CreatePrimitiveObject("Capsule", (int)MeshComponent::MeshType::Capsule); HideObjectContextMenu(); });
        m_BtnCreateCylinder->SetOnClick([this]() { CreatePrimitiveObject("Cylinder", (int)MeshComponent::MeshType::Cylinder); HideObjectContextMenu(); });
        m_BtnCreatePlane->SetOnClick([this]() { CreatePrimitiveObject("Plane", (int)MeshComponent::MeshType::Plane); HideObjectContextMenu(); });
        m_BtnCreateQuad->SetOnClick([this]() { CreatePrimitiveObject("Quad", (int)MeshComponent::MeshType::Quad); HideObjectContextMenu(); });
        m_BtnCreateTorus->SetOnClick([this]() { CreatePrimitiveObject("Torus", (int)MeshComponent::MeshType::Torus); HideObjectContextMenu(); });
        m_BtnDeleteObject->SetOnClick([this]() { DeleteSelectedObject(); HideObjectContextMenu(); });

        m_BtnToolSelect->SetOnClick([this]() { m_GizmoSystem.SetMode(GizmoMode::None); UpdateSceneToolButtons(); });
        m_BtnToolMove->SetOnClick([this]() { m_GizmoSystem.SetMode(GizmoMode::Translate); UpdateSceneToolButtons(); });
        m_BtnToolRotate->SetOnClick([this]() { m_GizmoSystem.SetMode(GizmoMode::Rotate); UpdateSceneToolButtons(); });
        m_BtnToolScale->SetOnClick([this]() { m_GizmoSystem.SetMode(GizmoMode::Scale); UpdateSceneToolButtons(); });
        m_BtnToolSpace->SetOnClick([this]() { m_GizmoSystem.ToggleSpace(); UpdateSceneToolButtons(); });
        m_BtnToolPivot->SetOnClick([this]() { m_GizmoSystem.TogglePivotMode(); UpdateSceneToolButtons(); });
        m_BtnToolSnap->SetOnClick([this]() { m_GizmoSystem.ToggleSnapping(); UpdateSceneToolButtons(); });
        m_BtnToolFrame->SetOnClick([this]() { FrameSelectedEntity(); });
        m_BtnPhysicsDebug->SetOnClick([this]() { CyclePhysicsDebugViewMode(); });
        m_BtnColliderOutline->SetOnClick([this]()
            {
                if (!m_ColliderDebugDropdownPanel)
                    return;

                // 메인 버튼은 상태를 바로 바꾸지 않고 메뉴만 연다.
                // 실제 표시 모드는 아래 항목에서 하나만 선택하게 해 두어 상태 충돌을 막는다.
                m_ColliderDebugDropdownPanel->SetVisible(!m_ColliderDebugDropdownPanel->IsVisible());
                if (m_FileDropdownPanel) m_FileDropdownPanel->SetVisible(false);
                if (m_EditDropdownPanel) m_EditDropdownPanel->SetVisible(false);
                if (m_WindowDropdownPanel) m_WindowDropdownPanel->SetVisible(false);
                m_ColliderDebugDropdownPanel->BringToFront();
            });
        m_BtnColliderOutlineMode->SetOnClick([this]()
            {
                m_ShowColliderOutlines = !m_ShowColliderOutlines;
                if (m_ShowColliderOutlines)
                    m_ShowMeshColliderWire = false;
                HideColliderDebugDropdown();
                UpdateColliderOutlineButton();
            });
        m_BtnMeshColliderWireMode->SetOnClick([this]()
            {
                m_ShowMeshColliderWire = !m_ShowMeshColliderWire;
                if (m_ShowMeshColliderWire)
                    m_ShowColliderOutlines = false;
                HideColliderDebugDropdown();
                UpdateColliderOutlineButton();
            });

        m_BtnPlay->SetOnClick([this]() {
            if (!m_ActiveScene)
                return;

            if (!IsInPlayMode())
            {
                EnterPlayMode();
                return;
            }

            if (m_ActiveScene->GetState() == CCEngine::SceneState::Pause)
            {
                m_ActiveScene->SetSceneState(CCEngine::SceneState::Play);
                UpdatePlayModeButtons();
                return;
            }

            ExitPlayMode();
            });

        m_BtnPause->SetOnClick([this]() {
            TogglePausePlayMode();
            });

        m_BtnStop->SetOnClick([this]() {
            ExitPlayMode();
            });

        UpdatePlayModeButtons();
        UpdateSceneToolButtons();

        CCEngine::Application::Get()->GetWindow().SetRootUI(m_RootUI);
    }
}
