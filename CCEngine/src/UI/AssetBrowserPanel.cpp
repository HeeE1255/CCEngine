#include "UI/AssetBrowserPanel.h"
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>
#include "Core/AssetDatabase.h"
#include "Core/ConsoleLog.h"
#include "Editor/AssetUndoManager.h"
#include "Renderer/UIRenderer.h"
#include "Renderer/Texture.h"
#include "Application.h"
#include "Core/Window.h"
#include "Events/ApplicationEvent.h"
#include "Events/KeyEvent.h"
#include "stb_image.h"
#include "json.hpp"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>

namespace CCEngine
{
    namespace UI
    {
        namespace
        {
            std::atomic<int> s_ActivePreviewLoads{ 0 };
            std::atomic<int> s_ActiveFbxMeshPreviewLoads{ 0 };

            bool TryAcquirePreviewSlot(std::atomic<int>& counter, int maxCount)
            {
                int current = counter.load(std::memory_order_relaxed);
                while (current < maxCount)
                {
                    if (counter.compare_exchange_weak(current, current + 1, std::memory_order_acq_rel))
                        return true;
                }

                return false;
            }

            struct PreviewLoadSlot
            {
                std::atomic<int>* Counter = nullptr;

                explicit PreviewLoadSlot(std::atomic<int>& counter)
                    : Counter(&counter)
                {
                }

                PreviewLoadSlot(const PreviewLoadSlot&) = delete;
                PreviewLoadSlot& operator=(const PreviewLoadSlot&) = delete;

                ~PreviewLoadSlot()
                {
                    if (Counter)
                        Counter->fetch_sub(1, std::memory_order_acq_rel);
                }
            };

            struct DecodedPreviewPixels
            {
                int Width = 0;
                int Height = 0;
                std::vector<uint32_t> Pixels;
                bool Success = false;
            };

            uint32_t PackPreviewPixel(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
            {
                return ((uint32_t)r) | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
            }

            void PutPreviewPixel(DecodedPreviewPixels& image, int x, int y, uint32_t color)
            {
                if (x < 0 || y < 0 || x >= image.Width || y >= image.Height)
                    return;

                image.Pixels[(size_t)y * (size_t)image.Width + (size_t)x] = color;
            }

            void FillPreviewRect(DecodedPreviewPixels& image, int x, int y, int width, int height, uint32_t color)
            {
                for (int py = y; py < y + height; ++py)
                {
                    for (int px = x; px < x + width; ++px)
                        PutPreviewPixel(image, px, py, color);
                }
            }

            void DrawPreviewLine(DecodedPreviewPixels& image, int x0, int y0, int x1, int y1, uint32_t color)
            {
                int dx = std::abs(x1 - x0);
                int sx = x0 < x1 ? 1 : -1;
                int dy = -std::abs(y1 - y0);
                int sy = y0 < y1 ? 1 : -1;
                int error = dx + dy;

                while (true)
                {
                    PutPreviewPixel(image, x0, y0, color);
                    if (x0 == x1 && y0 == y1)
                        break;

                    int e2 = error * 2;
                    if (e2 >= dy)
                    {
                        error += dy;
                        x0 += sx;
                    }
                    if (e2 <= dx)
                    {
                        error += dx;
                        y0 += sy;
                    }
                }
            }

            uint64_t HashPreviewText(const std::string& text)
            {
                uint64_t hash = 1469598103934665603ull;
                for (unsigned char c : text)
                {
                    hash ^= (uint64_t)c;
                    hash *= 1099511628211ull;
                }
                return hash;
            }

            constexpr const char* ThumbnailAlgorithmVersion = "fit2d-v3";

            std::filesystem::path GetPreviewCachePath(const std::filesystem::path& assetPath, const std::string& typeKey)
            {
                std::error_code ec;
                uint64_t stamp = 0;

                auto writeTime = std::filesystem::last_write_time(assetPath, ec);
                if (!ec)
                    stamp ^= (uint64_t)writeTime.time_since_epoch().count();
                ec.clear();

                if (std::filesystem::is_regular_file(assetPath, ec))
                    stamp ^= (uint64_t)std::filesystem::file_size(assetPath, ec);
                ec.clear();

                uint64_t hash = HashPreviewText(assetPath.string() + "|" + typeKey + "|" + ThumbnailAlgorithmVersion + "|" + std::to_string(stamp));
                std::filesystem::path cacheRoot = std::filesystem::current_path() / ".ccengine" / "AssetThumbnails";
                return cacheRoot / (typeKey + "_" + std::to_string(hash) + ".ccthumb");
            }

            bool LoadPreviewCacheFile(const std::filesystem::path& cachePath, DecodedPreviewPixels& outPixels)
            {
                std::ifstream input(cachePath, std::ios::binary);
                if (!input)
                    return false;

                char magic[8] = {};
                uint32_t width = 0;
                uint32_t height = 0;
                input.read(magic, sizeof(magic));
                input.read(reinterpret_cast<char*>(&width), sizeof(width));
                input.read(reinterpret_cast<char*>(&height), sizeof(height));
                const char expectedMagic[8] = { 'C', 'C', 'T', 'H', 'M', 'B', '1', '\0' };
                if (std::memcmp(magic, expectedMagic, sizeof(expectedMagic)) != 0 || width == 0 || height == 0 || width > 512 || height > 512)
                    return false;

                outPixels.Width = (int)width;
                outPixels.Height = (int)height;
                outPixels.Pixels.resize((size_t)width * (size_t)height);
                input.read(reinterpret_cast<char*>(outPixels.Pixels.data()), (std::streamsize)(outPixels.Pixels.size() * sizeof(uint32_t)));
                outPixels.Success = input.good();
                return outPixels.Success;
            }

            void SavePreviewCacheFile(const std::filesystem::path& cachePath, const DecodedPreviewPixels& pixels)
            {
                if (!pixels.Success || pixels.Pixels.empty())
                    return;

                std::error_code ec;
                std::filesystem::create_directories(cachePath.parent_path(), ec);
                if (ec)
                    return;

                std::ofstream output(cachePath, std::ios::binary | std::ios::trunc);
                if (!output)
                    return;

                const char magic[8] = { 'C', 'C', 'T', 'H', 'M', 'B', '1', '\0' };
                uint32_t width = (uint32_t)pixels.Width;
                uint32_t height = (uint32_t)pixels.Height;
                output.write(magic, sizeof(magic));
                output.write(reinterpret_cast<const char*>(&width), sizeof(width));
                output.write(reinterpret_cast<const char*>(&height), sizeof(height));
                output.write(reinterpret_cast<const char*>(pixels.Pixels.data()), (std::streamsize)(pixels.Pixels.size() * sizeof(uint32_t)));
            }

            DecodedPreviewPixels DecodeTexturePreviewPixels(const std::string& path, int maxSize)
            {
                DecodedPreviewPixels result;

                int width = 0;
                int height = 0;
                int channels = 0;
                stbi_uc* source = stbi_load(path.c_str(), &width, &height, &channels, 4);
                if (!source || width <= 0 || height <= 0)
                {
                    if (source)
                        stbi_image_free(source);
                    return result;
                }

                float scale = (std::min)(1.0f, (float)maxSize / (float)(std::max)(width, height));
                result.Width = (std::max)(1, (int)std::round((float)width * scale));
                result.Height = (std::max)(1, (int)std::round((float)height * scale));
                result.Pixels.resize((size_t)result.Width * (size_t)result.Height);

                for (int y = 0; y < result.Height; ++y)
                {
                    int sourceY = (std::min)(height - 1, (int)((float)y / scale));
                    for (int x = 0; x < result.Width; ++x)
                    {
                        int sourceX = (std::min)(width - 1, (int)((float)x / scale));
                        const stbi_uc* pixel = source + ((sourceY * width + sourceX) * 4);

                        uint32_t packed =
                            ((uint32_t)pixel[0]) |
                            ((uint32_t)pixel[1] << 8) |
                            ((uint32_t)pixel[2] << 16) |
                            ((uint32_t)pixel[3] << 24);
                        result.Pixels[(size_t)y * (size_t)result.Width + (size_t)x] = packed;
                    }
                }

                stbi_image_free(source);
                result.Success = true;
                return result;
            }

            DecodedPreviewPixels GenerateModelOrPrefabPreviewPixels(const std::filesystem::path& path, bool prefab, int size)
            {
                DecodedPreviewPixels result;
                result.Width = size;
                result.Height = size;
                result.Pixels.resize((size_t)size * (size_t)size);

                uint64_t hash = HashPreviewText(path.filename().string());
                uint8_t tintR = prefab ? 85 : 70;
                uint8_t tintG = prefab ? 125 : 150;
                uint8_t tintB = prefab ? 230 : 105;
                tintR = (uint8_t)std::min(245, (int)tintR + (int)(hash % 32));
                tintG = (uint8_t)std::min(245, (int)tintG + (int)((hash >> 8) % 28));
                tintB = (uint8_t)std::min(245, (int)tintB + (int)((hash >> 16) % 24));

                for (int y = 0; y < size; ++y)
                {
                    for (int x = 0; x < size; ++x)
                    {
                        float t = (float)y / (float)(std::max)(1, size - 1);
                        uint8_t bg = (uint8_t)(28 + (int)(t * 18.0f));
                        result.Pixels[(size_t)y * (size_t)size + (size_t)x] = PackPreviewPixel(bg, bg, (uint8_t)(bg + 5));
                    }
                }

                int cx = size / 2;
                int cy = size / 2 + size / 12;
                int half = size / 5;
                int lift = size / 7;

                uint32_t front = PackPreviewPixel(tintR, tintG, tintB);
                uint32_t top = PackPreviewPixel((uint8_t)std::min(255, tintR + 32), (uint8_t)std::min(255, tintG + 32), (uint8_t)std::min(255, tintB + 32));
                uint32_t side = PackPreviewPixel((uint8_t)(tintR * 0.65f), (uint8_t)(tintG * 0.65f), (uint8_t)(tintB * 0.65f));
                uint32_t outline = PackPreviewPixel(22, 24, 28);

                FillPreviewRect(result, cx - half, cy - half, half * 2, half * 2, front);
                for (int i = 0; i < lift; ++i)
                {
                    DrawPreviewLine(result, cx - half + i, cy - half - i, cx + half + i, cy - half - i, top);
                    DrawPreviewLine(result, cx + half + i, cy - half - i, cx + half + i, cy + half - i, side);
                }
                DrawPreviewLine(result, cx - half, cy - half, cx + half, cy - half, outline);
                DrawPreviewLine(result, cx + half, cy - half, cx + half, cy + half, outline);
                DrawPreviewLine(result, cx + half, cy + half, cx - half, cy + half, outline);
                DrawPreviewLine(result, cx - half, cy + half, cx - half, cy - half, outline);

                if (prefab)
                {
                    // 프리팹은 단일 파일이지만 실제로는 여러 컴포넌트와 자식 엔티티를 묶은 설계 단위다.
                    // 작은 노드 표시를 추가해 일반 모델 파일과 시각적으로 구분한다.
                    uint32_t node = PackPreviewPixel(210, 225, 255);
                    int nodeSize = std::max(3, size / 14);
                    FillPreviewRect(result, size / 5, size / 5, nodeSize, nodeSize, node);
                    FillPreviewRect(result, size - size / 5 - nodeSize, size / 4, nodeSize, nodeSize, node);
                    FillPreviewRect(result, size / 2 - nodeSize / 2, size - size / 5 - nodeSize, nodeSize, nodeSize, node);
                    DrawPreviewLine(result, size / 5 + nodeSize, size / 5 + nodeSize, cx - half, cy - half, node);
                    DrawPreviewLine(result, size - size / 5 - nodeSize, size / 4 + nodeSize, cx + half, cy - half, node);
                    DrawPreviewLine(result, size / 2, size - size / 5 - nodeSize, cx, cy + half, node);
                }
                else
                {
                    uint32_t axisX = PackPreviewPixel(220, 84, 72);
                    uint32_t axisY = PackPreviewPixel(84, 220, 110);
                    uint32_t axisZ = PackPreviewPixel(84, 120, 230);
                    DrawPreviewLine(result, cx, cy + half + 8, cx + half, cy + half + 14, axisX);
                    DrawPreviewLine(result, cx, cy + half + 8, cx, cy + half - 18, axisY);
                    DrawPreviewLine(result, cx, cy + half + 8, cx - half, cy + half + 2, axisZ);
                }

                result.Success = true;
                return result;
            }

            struct PreviewPoint
            {
                float X = 0.0f;
                float Y = 0.0f;
            };

            float GetPreviewPercentile(std::vector<float>& values, float percentile)
            {
                if (values.empty())
                    return 0.0f;

                std::sort(values.begin(), values.end());
                size_t index = (size_t)std::clamp(percentile * (float)(values.size() - 1), 0.0f, (float)(values.size() - 1));
                return values[index];
            }

            DecodedPreviewPixels GenerateModelWirePreviewPixels(const std::filesystem::path& path, int size, int onlyMeshIndex = -1)
            {
                Assimp::Importer importer;
                const aiScene* scene = importer.ReadFile(
                    path.string(),
                    aiProcess_Triangulate |
                    aiProcess_JoinIdenticalVertices |
                    aiProcess_PreTransformVertices |
                    aiProcess_GenNormals);

                if (!scene || !scene->HasMeshes())
                    return GenerateModelOrPrefabPreviewPixels(path, false, size);

                DecodedPreviewPixels result;
                result.Width = size;
                result.Height = size;
                result.Pixels.resize((size_t)size * (size_t)size);

                for (int y = 0; y < size; ++y)
                {
                    for (int x = 0; x < size; ++x)
                    {
                        float t = (float)y / (float)(std::max)(1, size - 1);
                        uint8_t bg = (uint8_t)(24 + (int)(t * 22.0f));
                        result.Pixels[(size_t)y * (size_t)size + (size_t)x] = PackPreviewPixel(bg, bg, (uint8_t)(bg + 7));
                    }
                }

                unsigned int totalVertices = 0;
                for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
                {
                    if (onlyMeshIndex >= 0 && (int)meshIndex != onlyMeshIndex)
                        continue;

                    aiMesh* mesh = scene->mMeshes[meshIndex];
                    if (mesh)
                        totalVertices += mesh->mNumVertices;
                }

                unsigned int sampleStep = (std::max)(1u, totalVertices / 20000u);
                unsigned int sampleCounter = 0;
                std::vector<float> xs;
                std::vector<float> ys;
                std::vector<float> zs;
                xs.reserve((std::min)(totalVertices, 20000u));
                ys.reserve(xs.capacity());
                zs.reserve(xs.capacity());

                for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
                {
                    if (onlyMeshIndex >= 0 && (int)meshIndex != onlyMeshIndex)
                        continue;

                    aiMesh* mesh = scene->mMeshes[meshIndex];
                    if (!mesh)
                        continue;

                    for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
                    {
                        if ((sampleCounter++ % sampleStep) != 0)
                            continue;

                        const aiVector3D& p = mesh->mVertices[i];
                        xs.push_back(p.x);
                        ys.push_back(p.y);
                        zs.push_back(p.z);
                    }
                }

                if (xs.empty())
                    return GenerateModelOrPrefabPreviewPixels(path, false, size);

                // 모델 파일에는 본, 더미, 장식처럼 본체에서 멀리 튄 점이 섞일 수 있다.
                // 전체 최소/최대를 그대로 쓰면 본체가 작은 점처럼 줄어드므로 대표 범위만 잡아 프레임을 맞춘다.
                aiVector3D minPos(
                    GetPreviewPercentile(xs, 0.02f),
                    GetPreviewPercentile(ys, 0.02f),
                    GetPreviewPercentile(zs, 0.02f));
                aiVector3D maxPos(
                    GetPreviewPercentile(xs, 0.98f),
                    GetPreviewPercentile(ys, 0.98f),
                    GetPreviewPercentile(zs, 0.98f));

                aiVector3D center = (minPos + maxPos) * 0.5f;
                float extent = (std::max)({ maxPos.x - minPos.x, maxPos.y - minPos.y, maxPos.z - minPos.z, 0.001f });

                auto projectRaw = [&](const aiVector3D& source) -> PreviewPoint
                    {
                        aiVector3D p = (source - center) * (1.0f / extent);
                        float isoX = (p.x - p.z) * 0.92f;
                        float isoY = (p.x + p.z) * 0.42f - p.y * 0.92f;
                        return { isoX, isoY };
                    };

                std::vector<float> projectedXs;
                std::vector<float> projectedYs;
                projectedXs.reserve(xs.capacity());
                projectedYs.reserve(xs.capacity());
                sampleCounter = 0;
                for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
                {
                    if (onlyMeshIndex >= 0 && (int)meshIndex != onlyMeshIndex)
                        continue;

                    aiMesh* mesh = scene->mMeshes[meshIndex];
                    if (!mesh)
                        continue;

                    for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
                    {
                        if ((sampleCounter++ % sampleStep) != 0)
                            continue;

                        PreviewPoint p = projectRaw(mesh->mVertices[i]);
                        projectedXs.push_back(p.X);
                        projectedYs.push_back(p.Y);
                    }
                }

                PreviewPoint minProjected = {
                    GetPreviewPercentile(projectedXs, 0.01f),
                    GetPreviewPercentile(projectedYs, 0.01f)
                };
                PreviewPoint maxProjected = {
                    GetPreviewPercentile(projectedXs, 0.99f),
                    GetPreviewPercentile(projectedYs, 0.99f)
                };

                float projectedWidth = (std::max)(0.001f, maxProjected.X - minProjected.X);
                float projectedHeight = (std::max)(0.001f, maxProjected.Y - minProjected.Y);
                float padding = (float)size * 0.14f;
                float fitScale = (std::min)((size - padding * 2.0f) / projectedWidth, (size - padding * 2.0f) / projectedHeight);

                auto project = [&](const aiVector3D& source) -> PreviewPoint
                    {
                        PreviewPoint p = projectRaw(source);
                        float x = padding + (p.X - minProjected.X) * fitScale;
                        float y = padding + (p.Y - minProjected.Y) * fitScale;
                        return { x, y };
                    };

                uint32_t edgeColor = PackPreviewPixel(118, 184, 235);
                uint32_t brightEdge = PackPreviewPixel(190, 225, 255);
                unsigned int drawnFaces = 0;

                // 실제 모델을 에디터 프리뷰로 다시 그리면 매 프레임 비용이 커진다.
                // 대표 버텍스 범위만 써서 프레임을 잡고, 결과는 캐시에 저장해 다음부터 파일만 읽는다.
                for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes && drawnFaces < 2500; ++meshIndex)
                {
                    if (onlyMeshIndex >= 0 && (int)meshIndex != onlyMeshIndex)
                        continue;

                    aiMesh* mesh = scene->mMeshes[meshIndex];
                    if (!mesh)
                        continue;

                    for (unsigned int faceIndex = 0; faceIndex < mesh->mNumFaces && drawnFaces < 2500; ++faceIndex)
                    {
                        const aiFace& face = mesh->mFaces[faceIndex];
                        if (face.mNumIndices < 3)
                            continue;

                        PreviewPoint a = project(mesh->mVertices[face.mIndices[0]]);
                        PreviewPoint b = project(mesh->mVertices[face.mIndices[1]]);
                        PreviewPoint c = project(mesh->mVertices[face.mIndices[2]]);
                        uint32_t color = (drawnFaces % 5 == 0) ? brightEdge : edgeColor;
                        DrawPreviewLine(result, (int)a.X, (int)a.Y, (int)b.X, (int)b.Y, color);
                        DrawPreviewLine(result, (int)b.X, (int)b.Y, (int)c.X, (int)c.Y, color);
                        DrawPreviewLine(result, (int)c.X, (int)c.Y, (int)a.X, (int)a.Y, color);
                        ++drawnFaces;
                    }
                }

                result.Success = true;
                return result;
            }

            DecodedPreviewPixels GeneratePrefabPreviewPixels(const std::filesystem::path& path, int size)
            {
                std::ifstream input(path);
                if (input)
                {
                    try
                    {
                        nlohmann::json data;
                        input >> data;
                        if (data.contains("Entities") && data["Entities"].is_array())
                        {
                            for (const auto& entityData : data["Entities"])
                            {
                                if (!entityData.contains("ModelComponent"))
                                    continue;

                                const auto& modelData = entityData["ModelComponent"];
                                std::string modelPath;
                                std::string guid = modelData.value("Guid", "");
                                if (!guid.empty())
                                    modelPath = AssetDatabase::GetPathFromGuid(guid).string();
                                if (modelPath.empty())
                                    modelPath = modelData.value("Path", "");

                                if (!modelPath.empty() && std::filesystem::exists(modelPath))
                                {
                                    DecodedPreviewPixels modelPreview = GenerateModelWirePreviewPixels(modelPath, size);
                                    if (modelPreview.Success)
                                    {
                                        uint32_t badge = PackPreviewPixel(82, 128, 235);
                                        FillPreviewRect(modelPreview, size - size / 4, size - size / 4, size / 5, size / 5, badge);
                                        DrawPreviewLine(modelPreview, size - size / 4, size - size / 4, size - size / 20, size - size / 20, PackPreviewPixel(220, 232, 255));
                                        return modelPreview;
                                    }
                                }
                            }
                        }
                    }
                    catch (...)
                    {
                    }
                }

                return GenerateModelOrPrefabPreviewPixels(path, true, size);
            }

            DecodedPreviewPixels LoadOrGenerateCachedAssetPreview(const std::filesystem::path& path, bool prefab, int size)
            {
                std::string typeKey = prefab ? "prefab" : "model";
                std::filesystem::path cachePath = GetPreviewCachePath(path, typeKey);

                DecodedPreviewPixels cached;
                if (LoadPreviewCacheFile(cachePath, cached))
                    return cached;

                // 모델/프리팹 프리뷰는 파일 분석과 그리기 비용이 누적될 수 있다.
                // 한 번 만든 픽셀은 디스크 캐시에 저장해 다음 실행에서도 바로 재사용한다.
                DecodedPreviewPixels generated = prefab
                    ? GeneratePrefabPreviewPixels(path, size)
                    : GenerateModelWirePreviewPixels(path, size);
                SavePreviewCacheFile(cachePath, generated);
                return generated;
            }

            DecodedPreviewPixels LoadOrGenerateCachedFbxMeshPreview(const std::filesystem::path& path, int meshIndex, int size)
            {
                std::string typeKey = "fbxmesh_" + std::to_string(meshIndex);
                std::filesystem::path cachePath = GetPreviewCachePath(path, typeKey);

                DecodedPreviewPixels cached;
                if (LoadPreviewCacheFile(cachePath, cached))
                    return cached;

                // FBX 파일 자체는 컨테이너 아이콘으로 두고, 내부 mesh sub-asset만 프리뷰를 만든다.
                // mesh index를 캐시 키에 넣어 같은 FBX 안의 여러 메쉬가 서로 다른 썸네일을 갖게 한다.
                DecodedPreviewPixels generated = GenerateModelWirePreviewPixels(path, size, meshIndex);
                SavePreviewCacheFile(cachePath, generated);
                return generated;
            }

            std::string TrimText(const std::string& text)
            {
                size_t begin = 0;
                while (begin < text.size() && std::isspace((unsigned char)text[begin]))
                    ++begin;

                size_t end = text.size();
                while (end > begin && std::isspace((unsigned char)text[end - 1]))
                    --end;

                return text.substr(begin, end - begin);
            }

            std::string ToLowerText(const std::string& text)
            {
                std::string result = text;
                std::transform(result.begin(), result.end(), result.begin(),
                    [](unsigned char c) { return (char)std::tolower(c); });
                return result;
            }

            class ScopedUIClip
            {
            public:
                ScopedUIClip(float x, float y, float width, float height)
                    : m_Enabled(width > 0.0f && height > 0.0f)
                {
                    if (m_Enabled)
                        UIRenderer::SetClipRect(x, y, width, height);
                }

                ~ScopedUIClip()
                {
                    if (m_Enabled)
                        UIRenderer::ClearClipRect();
                }

            private:
                bool m_Enabled = false;
            };

            bool IsMetaFile(const std::filesystem::path& path)
            {
                std::string extension = path.extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                    [](unsigned char c) { return (char)std::tolower(c); });
                return extension == ".meta";
            }

            bool CopyExternalPathWithoutMeta(const std::filesystem::path& source, const std::filesystem::path& destination)
            {
                std::error_code ec;
                if (IsMetaFile(source))
                    return false;

                if (std::filesystem::is_directory(source, ec) && !ec)
                {
                    std::filesystem::create_directories(destination, ec);
                    if (ec)
                        return false;

                    for (const auto& child : std::filesystem::directory_iterator(
                        source,
                        std::filesystem::directory_options::skip_permission_denied,
                        ec))
                    {
                        if (ec)
                            return false;

                        const std::filesystem::path childDestination = destination / child.path().filename();
                        if (!CopyExternalPathWithoutMeta(child.path(), childDestination))
                            return false;
                    }

                    return true;
                }

                if (std::filesystem::is_regular_file(source, ec) && !ec)
                {
                    std::filesystem::create_directories(destination.parent_path(), ec);
                    if (ec)
                        return false;

                    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, ec);
                    return !ec;
                }

                return false;
            }

            void HashCombine(uint64_t& seed, uint64_t value)
            {
                seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
            }

            bool IsKeyHeld(int virtualKey)
            {
                return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
            }
        }

        AssetBrowserPanel::AssetBrowserPanel(const std::string& name)
            : WindowPanel(name, "Asset Browser")
        {
            SetClipToBounds(true);
            m_ContentTop = 70.0f;
            SetRootDirectory(std::filesystem::current_path() / "assets");
        }

        void AssetBrowserPanel::SetRootDirectory(const std::filesystem::path& rootDirectory)
        {
            m_RootDirectory = rootDirectory;
            m_CurrentDirectory = rootDirectory;
            m_TreeChildCache.clear();
            m_DirectoryWatchSignatures.clear();
            Refresh();
        }

        void AssetBrowserPanel::Refresh(bool forceAssetScan)
        {
            // 브라우저를 열거나 폴더를 이동할 때마다 전체 에셋을 훑으면 큰 프로젝트에서 멈칫한다.
            // 일반 새로고침은 캐시를 쓰고, 실제 파일 작업 뒤에만 강제 스캔한다.
            if (forceAssetScan)
            {
                m_TreeChildCache.clear();
                m_TexturePreviewCache.clear();
                m_TexturePreviewOrder.clear();
                m_FbxMeshCache.clear();
                AssetDatabase::Scan(m_RootDirectory);
            }
            else
            {
                AssetDatabase::ScanIfNeeded(m_RootDirectory);
            }

            m_Entries.clear();
            m_ViewEntries.clear();
            ClearSelection();
            m_HoveredIndex = -1;
            m_LastClickedIndex = -1;
            m_ContextMenuVisible = false;
            m_IsMouseDownOnEmptyContent = false;
            m_IsDraggingSelectionBox = false;
            m_SelectionBeforeBox.clear();

            if (!std::filesystem::exists(m_CurrentDirectory))
            {
                m_CurrentDirectory = m_RootDirectory;
                return;
            }

            BuildTreeEntries();

            if (m_CurrentDirectory != m_RootDirectory)
            {
                AssetEntry parentEntry;
                parentEntry.Path = m_CurrentDirectory.parent_path();
                parentEntry.DisplayName = "..";
                parentEntry.Type = AssetType::Folder;
                m_Entries.push_back(parentEntry);
            }

            for (const auto& entry : std::filesystem::directory_iterator(m_CurrentDirectory))
            {
                AssetType type = AssetType::Unknown;
                if (entry.is_directory())
                {
                    type = AssetType::Folder;
                }
                else if (entry.is_regular_file())
                {
                    type = GetAssetType(entry.path());
                }

                if (type == AssetType::Unknown)
                    continue;

                AssetEntry assetEntry;
                assetEntry.Path = entry.path();
                assetEntry.Type = type;
                assetEntry.DisplayName = entry.path().filename().string();

                m_Entries.push_back(assetEntry);
            }

            std::sort(m_Entries.begin(), m_Entries.end(), [](const AssetEntry& a, const AssetEntry& b)
                {
                    if (a.DisplayName == "..") return true;
                    if (b.DisplayName == "..") return false;
                    if (a.Type != b.Type)
                        return static_cast<int>(a.Type) < static_cast<int>(b.Type);
                    return a.DisplayName < b.DisplayName;
                });

            ApplyFilter();
            UpdateDirectoryWatchState();
        }

        bool AssetBrowserPanel::ImportExternalPaths(const std::vector<std::filesystem::path>& sourcePaths, float mouseX, float mouseY)
        {
            if (sourcePaths.empty() || !IsPointInside(mouseX, mouseY))
                return false;

            if (!IsPathInsideRoot(m_CurrentDirectory, true))
                return false;

            bool importedAny = false;
            std::vector<std::filesystem::path> importedPaths;
            AssetUndoManager::Command undoCommand;
            undoCommand.Operation = AssetUndoManager::Kind::Import;
            undoCommand.Label = sourcePaths.size() == 1 ? "Import Asset" : "Import Assets";

            for (const auto& sourcePath : sourcePaths)
            {
                std::error_code ec;
                if (IsMetaFile(sourcePath) || !std::filesystem::exists(sourcePath, ec) || ec)
                    continue;

                const bool isDirectory = std::filesystem::is_directory(sourcePath, ec) && !ec;
                const bool isFile = std::filesystem::is_regular_file(sourcePath, ec) && !ec;
                if (!isDirectory && !isFile)
                    continue;

                std::filesystem::path destination = isDirectory
                    ? MakeUniquePath(m_CurrentDirectory, sourcePath.filename().string(), "")
                    : MakeUniquePath(m_CurrentDirectory, sourcePath.stem().string(), sourcePath.extension().string());

                auto sourceCanonical = std::filesystem::weakly_canonical(sourcePath, ec);
                if (ec)
                    continue;

                auto destinationCanonical = std::filesystem::weakly_canonical(destination.parent_path(), ec) / destination.filename();
                if (ec)
                    continue;

                if (sourceCanonical == destinationCanonical)
                    continue;

                // Explorer에서 들어오는 파일은 외부 원본을 프로젝트 안으로 복사하는 import다.
                // 외부 .meta는 복사하지 않고, AssetDatabase가 현재 프로젝트용 GUID를 새로 만들게 둔다.
                if (!CopyExternalPathWithoutMeta(sourceCanonical, destination))
                    continue;

                importedAny = true;
                importedPaths.push_back(destination);

                AssetUndoManager::Item undoItem;
                if (m_AssetUndoManager && m_AssetUndoManager->PrepareImportBackup(destination, undoItem))
                    undoCommand.Items.push_back(undoItem);
            }

            if (!importedAny)
                return false;

            AssetDatabase::MarkDirty(m_RootDirectory);
            if (!undoCommand.Items.empty())
                PushAssetUndoCommand(undoCommand);

            m_TreeChildCache.clear();
            Refresh(true);

            ClearSelection();
            for (const auto& importedPath : importedPaths)
            {
                for (int i = 0; i < (int)m_ViewEntries.size(); ++i)
                {
                    std::error_code ec;
                    if (std::filesystem::equivalent(m_ViewEntries[i].Path, importedPath, ec) && !ec)
                        m_SelectedIndices.insert(i);
                }
            }
            if (!m_SelectedIndices.empty())
            {
                m_SelectedIndex = *m_SelectedIndices.begin();
                m_AnchorSelectedIndex = m_SelectedIndex;
            }

            std::cout << "[AssetBrowser] Imported " << importedPaths.size() << " external asset(s) into "
                << m_CurrentDirectory.string() << std::endl;
            NotifyAssetDatabaseChanged();
            return true;
        }

        void AssetBrowserPanel::BuildTreeEntries()
        {
            m_TreeEntries.clear();

            if (!std::filesystem::exists(m_RootDirectory))
                return;

            std::string rootKey = GetTreeKey(m_RootDirectory);
            if (m_ExpandedTreeFolders.empty())
                m_ExpandedTreeFolders.insert(rootKey);

            TreeEntry root;
            root.Path = m_RootDirectory;
            root.DisplayName = "Assets";
            root.Depth = 0;
            root.HasChildren = TreeEntryHasChildren(m_RootDirectory);
            m_TreeEntries.push_back(root);

            if (m_ExpandedTreeFolders.find(rootKey) != m_ExpandedTreeFolders.end())
                BuildTreeEntriesRecursive(m_RootDirectory, 1);
        }

        void AssetBrowserPanel::BuildTreeEntriesRecursive(const std::filesystem::path& directory, int depth)
        {
            std::vector<TreeEntry> folders;
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(
                directory,
                std::filesystem::directory_options::skip_permission_denied,
                ec))
            {
                if (ec)
                {
                    ec.clear();
                    continue;
                }

                if (!entry.is_directory(ec) || ec)
                {
                    ec.clear();
                    continue;
                }

                TreeEntry treeEntry;
                treeEntry.Path = entry.path();
                treeEntry.DisplayName = entry.path().filename().string();
                treeEntry.Depth = depth;
                treeEntry.HasChildren = TreeEntryHasChildren(entry.path());
                folders.push_back(treeEntry);
            }

            std::sort(folders.begin(), folders.end(), [](const TreeEntry& a, const TreeEntry& b)
                {
                    return a.DisplayName < b.DisplayName;
                });

            for (const TreeEntry& folder : folders)
            {
                m_TreeEntries.push_back(folder);

                // 접힌 폴더의 자식은 만들지 않는다. 표시 데이터와 입력 데이터가 같아야 클릭 위치가 어긋나지 않는다.
                if (folder.HasChildren && m_ExpandedTreeFolders.find(GetTreeKey(folder.Path)) != m_ExpandedTreeFolders.end())
                    BuildTreeEntriesRecursive(folder.Path, depth + 1);
            }
        }

        std::string AssetBrowserPanel::GetTreeKey(const std::filesystem::path& path) const
        {
            std::error_code ec;
            auto canonical = std::filesystem::weakly_canonical(path, ec);
            if (!ec)
                return canonical.generic_string();

            ec.clear();
            return std::filesystem::absolute(path, ec).generic_string();
        }

        bool AssetBrowserPanel::TreeEntryHasChildren(const std::filesystem::path& path) const
        {
            std::string key = GetTreeKey(path);
            auto cached = m_TreeChildCache.find(key);
            if (cached != m_TreeChildCache.end())
                return cached->second;

            bool hasChildren = false;
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(
                path,
                std::filesystem::directory_options::skip_permission_denied,
                ec))
            {
                if (ec)
                {
                    ec.clear();
                    continue;
                }

                if (entry.is_directory(ec) && !ec)
                {
                    hasChildren = true;
                    break;
                }
                ec.clear();
            }

            // 폴더 트리는 같은 폴더를 여러 번 그리거나 다시 만들 수 있다.
            // 자식 폴더 존재 여부를 저장해 두면 큰 프로젝트에서 불필요한 디스크 접근을 줄인다.
            m_TreeChildCache[key] = hasChildren;
            return hasChildren;
        }

        void AssetBrowserPanel::ToggleTreeFolder(const std::filesystem::path& path)
        {
            if (!TreeEntryHasChildren(path))
                return;

            std::string key = GetTreeKey(path);
            auto it = m_ExpandedTreeFolders.find(key);
            if (it != m_ExpandedTreeFolders.end())
                m_ExpandedTreeFolders.erase(it);
            else
                m_ExpandedTreeFolders.insert(key);

            // 펼침 상태가 바뀌면 행 개수도 바뀌므로 스크롤 범위를 즉시 다시 계산한다.
            BuildTreeEntries();
            m_TreeScrollState.ContentHeight = (float)m_TreeEntries.size() * 22.0f + 12.0f;
            m_TreeScrollState.ScrollY = std::clamp(m_TreeScrollState.ScrollY, 0.0f, m_TreeScrollState.GetMaxScroll());
        }

        AssetBrowserPanel::AssetType AssetBrowserPanel::GetAssetType(const std::filesystem::path& path) const
        {
            std::string extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return (char)std::tolower(c); });

            if (extension == ".ccscene") return AssetType::Scene;
            if (extension == ".ccprefab") return AssetType::Prefab;
            if (extension == ".fbx" || extension == ".obj" || extension == ".gltf" || extension == ".glb") return AssetType::Model;
            if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga") return AssetType::Texture;
            if (extension == ".cs") return AssetType::Script;
            return AssetType::Unknown;
        }

        std::string AssetBrowserPanel::GetTypeLabel(AssetType type) const
        {
            switch (type)
            {
                case AssetType::Folder: return "DIR";
                case AssetType::Scene: return "SCN";
                case AssetType::Prefab: return "PFB";
                case AssetType::Model: return "MDL";
                case AssetType::FbxMesh: return "MSH";
                case AssetType::Texture: return "TEX";
                case AssetType::Script: return "C#";
                default: return "???";
            }
        }

        std::string AssetBrowserPanel::GetTypeKey(AssetType type) const
        {
            switch (type)
            {
                case AssetType::Scene: return "scene";
                case AssetType::Prefab: return "prefab";
                case AssetType::Model: return "model";
                case AssetType::FbxMesh: return "mesh";
                case AssetType::Texture: return "texture";
                case AssetType::Script: return "script";
                case AssetType::Folder: return "folder";
                default: return "unknown";
            }
        }

        void AssetBrowserPanel::ActivateEntry(const AssetEntry& entry)
        {
            std::string path = entry.Path.string();

            switch (entry.Type)
            {
                case AssetType::Folder:
                    NavigateTo(entry.Path);
                    break;
                case AssetType::Scene:
                    if (m_OnSceneSelected) m_OnSceneSelected(path);
                    break;
                case AssetType::Prefab:
                    if (m_OnPrefabSelected) m_OnPrefabSelected(path);
                    break;
                case AssetType::Model:
                    if (m_OnModelSelected) m_OnModelSelected(path);
                    break;
                case AssetType::FbxMesh:
                    // FBX 내부 메쉬는 독립 파일이 아니라 컨테이너 안의 sub-asset이다.
                    // 지금 단계에서는 선택/프리뷰만 제공하고, 실제 인스턴스화는 FBX 파일 단위로 처리한다.
                    break;
                case AssetType::Script:
                {
                    // 스크립트는 운영체제에 연결된 Visual Studio나 코드 편집기로 연다.
                    auto result = reinterpret_cast<intptr_t>(
                        ShellExecuteW(nullptr, L"open", entry.Path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
                    if (result <= 32)
                        std::cout << "[AssetBrowser] Failed to open script: " << path << std::endl;
                    break;
                }
                default:
                    std::cout << "[AssetBrowser] No action for asset: " << path << std::endl;
                    break;
            }
        }

        bool AssetBrowserPanel::DeleteSelectedAsset()
        {
            return RequestDeleteSelectedAsset();
        }

        bool AssetBrowserPanel::RequestDeleteSelectedAsset()
        {
            if (GetSelectedEntries().empty())
                return false;

            BeginDeleteSelected();
            return true;
        }

        bool AssetBrowserPanel::RecycleSelectedAsset()
        {
            if (m_ModalTargetPaths.empty())
                return false;

            bool anyDeleted = false;
            AssetUndoManager::Command undoCommand;
            undoCommand.Operation = AssetUndoManager::Kind::RecycleDelete;
            undoCommand.Label = m_ModalTargetPaths.size() == 1 ? "Delete Asset" : "Delete Assets";

            for (const auto& path : m_ModalTargetPaths)
            {
                if (!IsPathInsideRoot(path, false))
                    continue;

                std::error_code ec;
                AssetUndoManager::Item undoItem;
                undoItem.FromPath = path;
                undoItem.IsDirectory = std::filesystem::is_directory(path, ec) && !ec;
                bool hasUndoBackup = m_AssetUndoManager && m_AssetUndoManager->PrepareDeleteBackup(path, undoItem);

                if (AssetDatabase::RecycleAsset(path))
                {
                    anyDeleted = true;
                    if (hasUndoBackup)
                        undoCommand.Items.push_back(undoItem);
                    else
                        ConsoleLog::Warning("Asset delete cannot be restored by Undo: " + path.string());
                    std::cout << "[AssetBrowser] Asset moved to recycle bin: " << path.string() << std::endl;
                }
            }

            if (!anyDeleted)
                return false;

            if (!undoCommand.Items.empty())
                PushAssetUndoCommand(undoCommand);

            CancelModal();
            ClearSelection();
            Refresh(true);
            NotifyAssetDatabaseChanged();
            return true;
        }

        bool AssetBrowserPanel::CreateFolderInCurrentDirectory()
        {
            if (!IsPathInsideRoot(m_CurrentDirectory, true))
                return false;

            std::filesystem::path folderPath = MakeUniquePath(m_CurrentDirectory, "New Folder", "");
            std::error_code ec;
            std::filesystem::create_directory(folderPath, ec);
            if (ec)
                return false;

            AssetUndoManager::Command undoCommand;
            undoCommand.Operation = AssetUndoManager::Kind::CreateFolder;
            undoCommand.Label = "Create Folder";
            undoCommand.Items.push_back({ {}, folderPath, {}, {}, true });
            PushAssetUndoCommand(undoCommand);

            // 폴더 생성 뒤에는 트리 캐시를 비워야 왼쪽 폴더 목록에도 바로 나타난다.
            AssetDatabase::MarkDirty(m_RootDirectory);
            m_TreeChildCache.clear();
            Refresh(true);
            return true;
        }

        bool AssetBrowserPanel::RenameSelectedAsset(const std::string& newName)
        {
            if (m_ModalTargetPath.empty())
                return false;

            std::string cleanName = TrimText(newName);
            if (cleanName.empty())
                return false;

            std::filesystem::path namePath(cleanName);
            if (namePath.has_parent_path() || cleanName.find('/') != std::string::npos || cleanName.find('\\') != std::string::npos)
                return false;

            if (!IsPathInsideRoot(m_ModalTargetPath, false))
                return false;

            std::error_code ec;
            bool isDirectory = std::filesystem::is_directory(m_ModalTargetPath, ec) && !ec;
            bool renamed = false;
            std::filesystem::path sourcePath = m_ModalTargetPath;
            std::filesystem::path targetPath;

            if (isDirectory)
            {
                targetPath = m_ModalTargetPath.parent_path() / cleanName;
                if (std::filesystem::exists(targetPath, ec))
                    return false;

                // 폴더 이름 변경은 하위 파일 구조를 그대로 옮기는 작업이다.
                // 파일 에셋은 AssetDatabase가 meta를 같이 옮기지만, 폴더는 파일 시스템 rename 후 스캔을 다시 한다.
                std::filesystem::rename(m_ModalTargetPath, targetPath, ec);
                renamed = !ec;
                if (renamed)
                    AssetDatabase::MarkDirty(m_RootDirectory);
            }
            else
            {
                targetPath = m_ModalTargetPath.parent_path() / cleanName;
                if (targetPath.extension().empty())
                    targetPath.replace_extension(m_ModalTargetPath.extension());
                renamed = AssetDatabase::RenameAsset(m_ModalTargetPath, cleanName);
            }

            if (!renamed)
                return false;

            AssetUndoManager::Command undoCommand;
            undoCommand.Operation = AssetUndoManager::Kind::Rename;
            undoCommand.Label = "Rename Asset";
            undoCommand.Items.push_back({ sourcePath, targetPath, {}, {}, isDirectory });
            PushAssetUndoCommand(undoCommand);

            CancelModal();
            m_TreeChildCache.clear();
            Refresh(true);
            NotifyAssetDatabaseChanged();
            return true;
        }

        void AssetBrowserPanel::BeginCreateFolder()
        {
            m_ContextMenuVisible = false;
            m_ModalMode = ModalMode::CreateFolder;
            m_ModalText = MakeUniquePath(m_CurrentDirectory, "New Folder", "").filename().string();
            m_ModalTargetPath.clear();
            m_ModalTargetPaths.clear();
        }

        void AssetBrowserPanel::BeginRenameSelected()
        {
            auto selected = GetSelectedEntries();
            if (selected.size() != 1)
                return;

            const auto& entry = selected.front();
            if (entry.DisplayName == "..")
                return;

            m_ContextMenuVisible = false;
            m_ModalMode = ModalMode::Rename;
            m_ModalTargetPath = entry.Path;
            m_ModalTargetPaths.clear();
            m_ModalText = entry.Path.filename().string();
        }

        void AssetBrowserPanel::BeginDeleteSelected()
        {
            auto selected = GetSelectedEntries();
            if (selected.empty())
                return;

            m_ContextMenuVisible = false;
            m_ModalMode = ModalMode::ConfirmDelete;
            m_ModalTargetPath.clear();
            m_ModalTargetPaths.clear();
            for (const AssetEntry& entry : selected)
                m_ModalTargetPaths.push_back(entry.Path);

            m_ModalText = selected.size() == 1
                ? selected.front().DisplayName
                : std::to_string(selected.size()) + " assets selected";
        }

        void AssetBrowserPanel::CancelModal()
        {
            m_ModalMode = ModalMode::None;
            m_ModalText.clear();
            m_ModalTargetPath.clear();
            m_ModalTargetPaths.clear();
        }

        bool AssetBrowserPanel::ConfirmNameModal()
        {
            if (m_ModalMode == ModalMode::CreateFolder)
            {
                std::string cleanName = TrimText(m_ModalText);
                if (cleanName.empty())
                    return false;

                std::filesystem::path namePath(cleanName);
                if (namePath.has_parent_path() || cleanName.find('/') != std::string::npos || cleanName.find('\\') != std::string::npos)
                    return false;

                std::filesystem::path folderPath = m_CurrentDirectory / cleanName;
                if (std::filesystem::exists(folderPath))
                    folderPath = MakeUniquePath(m_CurrentDirectory, cleanName, "");

                std::error_code ec;
                std::filesystem::create_directory(folderPath, ec);
                if (ec)
                    return false;

                AssetUndoManager::Command undoCommand;
                undoCommand.Operation = AssetUndoManager::Kind::CreateFolder;
                undoCommand.Label = "Create Folder";
                undoCommand.Items.push_back({ {}, folderPath, {}, {}, true });
                PushAssetUndoCommand(undoCommand);

                AssetDatabase::MarkDirty(m_RootDirectory);
                CancelModal();
                m_TreeChildCache.clear();
                Refresh(true);
                return true;
            }

            if (m_ModalMode == ModalMode::Rename)
                return RenameSelectedAsset(m_ModalText);

            return false;
        }

        bool AssetBrowserPanel::ConfirmDeleteModal()
        {
            if (m_ModalMode != ModalMode::ConfirmDelete)
                return false;

            return RecycleSelectedAsset();
        }

        void AssetBrowserPanel::NavigateTo(const std::filesystem::path& directory)
        {
            std::error_code ec;
            auto target = std::filesystem::weakly_canonical(directory, ec);
            if (ec || !std::filesystem::exists(target) || !std::filesystem::is_directory(target))
                return;

            auto root = std::filesystem::weakly_canonical(m_RootDirectory, ec);
            if (ec)
                return;

            std::wstring targetString = target.wstring();
            std::wstring rootString = root.wstring();
            if (targetString.rfind(rootString, 0) != 0)
                return;

            m_CurrentDirectory = target;
            m_ScrollState.ScrollY = 0.0f;
            Refresh();
        }

        bool AssetBrowserPanel::IsContentPoint(float mouseX, float mouseY) const
        {
            float contentX = m_CalculatedPos.x + m_TreeWidth + 6.0f;
            return mouseX >= contentX &&
                mouseX <= m_CalculatedPos.x + m_CalculatedSize.x - 20.0f &&
                mouseY >= m_CalculatedPos.y + m_ContentTop &&
                mouseY <= m_CalculatedPos.y + m_CalculatedSize.y;
        }

        bool AssetBrowserPanel::IsTreePoint(float mouseX, float mouseY) const
        {
            return mouseX >= m_CalculatedPos.x &&
                mouseX <= m_CalculatedPos.x + m_TreeWidth - 6.0f &&
                mouseY >= m_CalculatedPos.y + m_ContentTop &&
                mouseY <= m_CalculatedPos.y + m_CalculatedSize.y;
        }

        bool AssetBrowserPanel::IsSplitterPoint(float mouseX, float mouseY) const
        {
            float splitterX = m_CalculatedPos.x + m_TreeWidth;
            return mouseX >= splitterX - 5.0f &&
                mouseX <= splitterX + 7.0f &&
                mouseY >= m_CalculatedPos.y + m_ContentTop &&
                mouseY <= m_CalculatedPos.y + m_CalculatedSize.y;
        }

        bool AssetBrowserPanel::IsScrollbarPoint(float mouseX, float mouseY) const
        {
            if (m_ScrollState.GetMaxScroll() <= 0.0f)
                return false;

            float thumbX = m_CalculatedPos.x + m_CalculatedSize.x - 16.0f;
            float trackTop = m_CalculatedPos.y + m_ContentTop;
            float trackBottom = trackTop + m_ScrollState.ViewportHeight;

            return mouseX >= thumbX &&
                mouseX <= thumbX + 8.0f &&
                mouseY >= trackTop &&
                mouseY <= trackBottom;
        }

        bool AssetBrowserPanel::IsTreeScrollbarPoint(float mouseX, float mouseY) const
        {
            if (m_TreeScrollState.GetMaxScroll() <= 0.0f)
                return false;

            float thumbX = m_CalculatedPos.x + m_TreeWidth - 12.0f;
            float trackTop = m_CalculatedPos.y + m_ContentTop;
            float trackBottom = trackTop + m_TreeScrollState.ViewportHeight;

            return mouseX >= thumbX &&
                mouseX <= thumbX + 8.0f &&
                mouseY >= trackTop &&
                mouseY <= trackBottom;
        }

        int AssetBrowserPanel::GetEntryIndexAt(float mouseX, float mouseY) const
        {
            if (!IsContentPoint(mouseX, mouseY))
                return -1;

            float contentX = m_CalculatedPos.x + m_TreeWidth + 14.0f;
            float contentY = m_CalculatedPos.y + m_ContentTop + 10.0f;

            if (m_IconSizeStep == 0)
            {
                float localY = mouseY - contentY + m_ScrollState.ScrollY;
                int index = (int)(localY / (m_RowHeight + m_RowGap));
                if (index < 0 || index >= (int)m_ViewEntries.size())
                    return -1;
                return index;
            }

            const float iconSizes[5] = { 18.0f, 32.0f, 48.0f, 72.0f, 96.0f };
            float iconSize = iconSizes[(std::clamp)(m_IconSizeStep, 0, 4)];
            float cellW = iconSize + 58.0f;
            float cellH = iconSize + 42.0f;
            float viewW = (std::max)(1.0f, m_CalculatedSize.x - m_TreeWidth - 34.0f);
            int columns = (std::max)(1, (int)(viewW / cellW));

            float localX = mouseX - contentX;
            float localY = mouseY - contentY + m_ScrollState.ScrollY;
            if (localX < 0.0f || localY < 0.0f)
                return -1;

            int col = (int)(localX / cellW);
            int row = (int)(localY / cellH);
            if (col < 0 || col >= columns)
                return -1;

            int index = row * columns + col;
            if (index < 0 || index >= (int)m_ViewEntries.size())
                return -1;

            return index;
        }

        bool AssetBrowserPanel::GetEntryBounds(int index, float& x, float& y, float& w, float& h) const
        {
            if (index < 0 || index >= (int)m_ViewEntries.size())
                return false;

            float contentX = m_CalculatedPos.x + m_TreeWidth + 14.0f;
            float contentY = m_CalculatedPos.y + m_ContentTop + 10.0f;

            if (m_IconSizeStep == 0)
            {
                x = contentX;
                y = contentY + (float)index * (m_RowHeight + m_RowGap) - m_ScrollState.ScrollY;
                w = (std::max)(1.0f, m_CalculatedSize.x - m_TreeWidth - 40.0f);
                h = m_RowHeight;
                return true;
            }

            const float iconSizes[5] = { 18.0f, 32.0f, 48.0f, 72.0f, 96.0f };
            float iconSize = iconSizes[(std::clamp)(m_IconSizeStep, 0, 4)];
            float cellW = iconSize + 58.0f;
            float cellH = iconSize + 42.0f;
            float viewW = (std::max)(1.0f, m_CalculatedSize.x - m_TreeWidth - 34.0f);
            int columns = (std::max)(1, (int)(viewW / cellW));

            int col = index % columns;
            int row = index / columns;
            x = contentX + (float)col * cellW;
            y = contentY + (float)row * cellH - m_ScrollState.ScrollY;
            w = cellW - 8.0f;
            h = cellH - 4.0f;
            return true;
        }

        bool AssetBrowserPanel::IsFbxExpandButtonPoint(int index, float mouseX, float mouseY) const
        {
            if (index < 0 || index >= (int)m_ViewEntries.size())
                return false;

            if (!IsFbxContainer(m_ViewEntries[index]))
                return false;

            float x = 0.0f;
            float y = 0.0f;
            float w = 0.0f;
            float h = 0.0f;
            if (!GetEntryBounds(index, x, y, w, h))
                return false;

            if (m_IconSizeStep == 0)
            {
                float buttonX = x + 6.0f;
                float buttonY = y + 5.0f;
                return mouseX >= buttonX && mouseX <= buttonX + 16.0f &&
                    mouseY >= buttonY && mouseY <= buttonY + 16.0f;
            }

            const float iconSizes[5] = { 18.0f, 32.0f, 48.0f, 72.0f, 96.0f };
            float iconSize = iconSizes[(std::clamp)(m_IconSizeStep, 0, 4)];
            float iconX = x + (w + 8.0f - iconSize) * 0.5f - 4.0f;
            float iconY = y + 6.0f;
            float buttonSize = 18.0f;
            float buttonX = iconX + iconSize - buttonSize * 0.45f;
            float buttonY = iconY + iconSize * 0.38f;

            return mouseX >= buttonX && mouseX <= buttonX + buttonSize &&
                mouseY >= buttonY && mouseY <= buttonY + buttonSize;
        }

        int AssetBrowserPanel::GetTreeIndexAt(float mouseX, float mouseY) const
        {
            if (!IsTreePoint(mouseX, mouseY))
                return -1;

            float localY = mouseY - (m_CalculatedPos.y + m_ContentTop + 6.0f) + m_TreeScrollState.ScrollY;
            int index = (int)(localY / 22.0f);
            if (index < 0 || index >= (int)m_TreeEntries.size())
                return -1;

            return index;
        }

        bool AssetBrowserPanel::IsContextMenuPoint(float mouseX, float mouseY) const
        {
            return m_ContextMenuVisible &&
                mouseX >= m_ContextMenuX &&
                mouseX <= m_ContextMenuX + m_ContextMenuWidth &&
                mouseY >= m_ContextMenuY &&
                mouseY <= m_ContextMenuY + m_ContextMenuHeight;
        }

        int AssetBrowserPanel::GetContextMenuItemAt(float mouseX, float mouseY) const
        {
            if (!IsContextMenuPoint(mouseX, mouseY))
                return -1;

            int index = (int)((mouseY - m_ContextMenuY) / 28.0f);
            if (index < 0 || index >= (int)m_ContextMenuItems.size())
                return -1;
            return index;
        }

        bool AssetBrowserPanel::IsNameModalPoint(float mouseX, float mouseY) const
        {
            if (m_ModalMode != ModalMode::CreateFolder && m_ModalMode != ModalMode::Rename)
                return false;

            float modalW = 360.0f;
            float modalH = 150.0f;
            float modalX = m_CalculatedPos.x + (m_CalculatedSize.x - modalW) * 0.5f;
            float modalY = m_CalculatedPos.y + (m_CalculatedSize.y - modalH) * 0.5f;
            return mouseX >= modalX && mouseX <= modalX + modalW &&
                mouseY >= modalY && mouseY <= modalY + modalH;
        }

        bool AssetBrowserPanel::IsDeleteModalPoint(float mouseX, float mouseY) const
        {
            if (m_ModalMode != ModalMode::ConfirmDelete)
                return false;

            float modalW = 380.0f;
            float modalH = 150.0f;
            float modalX = m_CalculatedPos.x + (m_CalculatedSize.x - modalW) * 0.5f;
            float modalY = m_CalculatedPos.y + (m_CalculatedSize.y - modalH) * 0.5f;
            return mouseX >= modalX && mouseX <= modalX + modalW &&
                mouseY >= modalY && mouseY <= modalY + modalH;
        }

        bool AssetBrowserPanel::IsPathInsideRoot(const std::filesystem::path& path, bool allowRoot) const
        {
            std::error_code ec;
            auto root = std::filesystem::weakly_canonical(m_RootDirectory, ec);
            if (ec)
                return false;

            auto target = std::filesystem::exists(path, ec)
                ? std::filesystem::weakly_canonical(path, ec)
                : std::filesystem::weakly_canonical(path.parent_path(), ec) / path.filename();
            if (ec)
                return false;

            if (!allowRoot && target == root)
                return false;

            // 문자열 prefix만 보면 assets_backup 같은 경로가 통과할 수 있다.
            // root를 기준으로 relative를 구해 '..'로 시작하는 경우를 프로젝트 밖으로 판단한다.
            auto relative = std::filesystem::relative(target, root, ec);
            if (ec || relative.empty())
                return allowRoot && target == root;

            auto first = *relative.begin();
            return first.string() != "..";
        }

        bool AssetBrowserPanel::IsSearchBoxPoint(float mouseX, float mouseY) const
        {
            float plusX = m_CalculatedPos.x + m_CalculatedSize.x - 46.0f;
            float searchX = m_CalculatedPos.x + (std::max)(150.0f, m_TreeWidth + 18.0f);
            float searchY = m_CalculatedPos.y + 36.0f;
            float searchW = (std::max)(80.0f, plusX - searchX - 10.0f);

            return mouseX >= searchX && mouseX <= searchX + searchW &&
                mouseY >= searchY && mouseY <= searchY + 22.0f;
        }

        void AssetBrowserPanel::SetSearchQuery(const std::string& query)
        {
            if (m_SearchQuery == query)
                return;

            m_SearchQuery = query;
            ApplyFilter();
            ClearSelection();
            m_HoveredIndex = -1;
            m_LastClickedIndex = -1;
            m_ScrollState.ScrollY = 0.0f;
        }

        void AssetBrowserPanel::ApplyFilter()
        {
            m_ViewEntries.clear();

            std::string query = ToLowerText(TrimText(m_SearchQuery));
            if (query.empty())
            {
                for (const AssetEntry& entry : m_Entries)
                {
                    m_ViewEntries.push_back(entry);
                    AppendFbxSubAssetEntries(entry, query);
                }
                return;
            }

            for (const AssetEntry& entry : m_Entries)
            {
                // 검색 중에도 상위 폴더 이동 항목은 남긴다.
                // 필터 결과에서 빠져나갈 길이 없어지면 브라우저 조작이 답답해진다.
                if (entry.DisplayName == "..")
                {
                    m_ViewEntries.push_back(entry);
                    continue;
                }

                std::string name = ToLowerText(entry.DisplayName);
                std::string extension = ToLowerText(entry.Path.extension().string());
                std::string type = ToLowerText(GetTypeLabel(entry.Type));

                bool matchesEntry =
                    name.find(query) != std::string::npos ||
                    extension.find(query) != std::string::npos ||
                    type.find(query) != std::string::npos;

                if (matchesEntry)
                {
                    m_ViewEntries.push_back(entry);
                }

                AppendFbxSubAssetEntries(entry, query);
            }
        }

        void AssetBrowserPanel::AppendFbxSubAssetEntries(const AssetEntry& fbxEntry, const std::string& query)
        {
            if (!IsFbxContainer(fbxEntry))
                return;

            std::string key = GetTreeKey(fbxEntry.Path);
            if (m_ExpandedFbxAssets.find(key) == m_ExpandedFbxAssets.end())
                return;

            const auto& meshes = GetFbxMeshInfos(fbxEntry.Path);
            for (const FbxMeshInfo& meshInfo : meshes)
            {
                if (meshInfo.MeshIndex < 0)
                    continue;

                if (!query.empty())
                {
                    std::string meshName = ToLowerText(meshInfo.Name);
                    if (meshName.find(query) == std::string::npos)
                        continue;
                }

                AssetEntry meshEntry;
                meshEntry.Path = fbxEntry.Path;
                meshEntry.SourceAssetPath = fbxEntry.Path;
                meshEntry.DisplayName = meshInfo.Name;
                meshEntry.Type = AssetType::FbxMesh;
                meshEntry.SubAssetIndex = meshInfo.MeshIndex;
                meshEntry.IsSubAsset = true;
                m_ViewEntries.push_back(meshEntry);
            }
        }

        const std::vector<AssetBrowserPanel::FbxMeshInfo>& AssetBrowserPanel::GetFbxMeshInfos(const std::filesystem::path& path)
        {
            std::string key = GetTreeKey(path);
            auto found = m_FbxMeshCache.find(key);
            if (found != m_FbxMeshCache.end())
                return found->second;

            std::vector<FbxMeshInfo> meshes;
            Assimp::Importer importer;
            const aiScene* scene = importer.ReadFile(
                path.string(),
                aiProcess_Triangulate |
                aiProcess_JoinIdenticalVertices |
                aiProcess_GenNormals);

            if (scene && scene->HasMeshes())
            {
                meshes.reserve(scene->mNumMeshes);
                for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
                {
                    aiMesh* mesh = scene->mMeshes[i];
                    if (!mesh)
                        continue;

                    FbxMeshInfo info;
                    info.MeshIndex = (int)i;
                    info.Name = mesh->mName.length > 0
                        ? mesh->mName.C_Str()
                        : "Mesh " + std::to_string(i);
                    meshes.push_back(info);
                }
            }

            // FBX 내부 메쉬는 디스크의 독립 파일이 아니다.
            // 한 번 파싱한 이름 목록만 캐시에 두고, 실제 삭제/이동/이름변경 대상에서는 제외한다.
            auto [inserted, _] = m_FbxMeshCache.emplace(key, std::move(meshes));
            return inserted->second;
        }

        bool AssetBrowserPanel::IsFbxContainer(const AssetEntry& entry) const
        {
            if (entry.IsSubAsset || entry.Type != AssetType::Model)
                return false;

            std::string extension = ToLowerText(entry.Path.extension().string());
            return extension == ".fbx";
        }

        bool AssetBrowserPanel::IsVirtualSubAsset(const AssetEntry& entry) const
        {
            return entry.IsSubAsset || entry.Type == AssetType::FbxMesh;
        }

        void AssetBrowserPanel::ToggleFbxExpanded(const std::filesystem::path& path)
        {
            std::string key = GetTreeKey(path);
            if (m_ExpandedFbxAssets.find(key) != m_ExpandedFbxAssets.end())
                m_ExpandedFbxAssets.erase(key);
            else
                m_ExpandedFbxAssets.insert(key);

            ClearSelection();
            ApplyFilter();
        }

        bool AssetBrowserPanel::IsEntrySelected(int index) const
        {
            return m_SelectedIndices.find(index) != m_SelectedIndices.end();
        }

        void AssetBrowserPanel::ClearSelection()
        {
            m_SelectedIndices.clear();
            m_SelectedIndex = -1;
            m_AnchorSelectedIndex = -1;
        }

        void AssetBrowserPanel::SelectSingle(int index)
        {
            m_SelectedIndices.clear();
            if (index >= 0 && index < (int)m_ViewEntries.size())
            {
                m_SelectedIndices.insert(index);
                m_SelectedIndex = index;
                m_AnchorSelectedIndex = index;
                return;
            }

            m_SelectedIndex = -1;
            m_AnchorSelectedIndex = -1;
        }

        void AssetBrowserPanel::ToggleSelection(int index)
        {
            if (index < 0 || index >= (int)m_ViewEntries.size())
                return;

            // Ctrl 선택은 기존 선택을 유지하면서 해당 항목만 켜고 끈다.
            // 마지막 포커스 항목은 우클릭/더블클릭 기준으로 계속 사용한다.
            if (IsEntrySelected(index))
                m_SelectedIndices.erase(index);
            else
                m_SelectedIndices.insert(index);

            m_SelectedIndex = index;
            if (m_AnchorSelectedIndex < 0)
                m_AnchorSelectedIndex = index;
        }

        void AssetBrowserPanel::SelectRange(int index)
        {
            if (index < 0 || index >= (int)m_ViewEntries.size())
                return;

            if (m_AnchorSelectedIndex < 0 || m_AnchorSelectedIndex >= (int)m_ViewEntries.size())
                m_AnchorSelectedIndex = index;

            m_SelectedIndices.clear();
            int begin = (std::min)(m_AnchorSelectedIndex, index);
            int end = (std::max)(m_AnchorSelectedIndex, index);
            for (int i = begin; i <= end; ++i)
            {
                if (m_ViewEntries[i].DisplayName != "..")
                    m_SelectedIndices.insert(i);
            }

            m_SelectedIndex = index;
        }

        void AssetBrowserPanel::UpdateSelectionBox(float mouseX, float mouseY)
        {
            m_SelectionBoxCurrentX = mouseX;
            m_SelectionBoxCurrentY = mouseY;

            float left = (std::min)(m_SelectionBoxStartX, m_SelectionBoxCurrentX);
            float right = (std::max)(m_SelectionBoxStartX, m_SelectionBoxCurrentX);
            float top = (std::min)(m_SelectionBoxStartY, m_SelectionBoxCurrentY);
            float bottom = (std::max)(m_SelectionBoxStartY, m_SelectionBoxCurrentY);

            if (m_SelectionBoxAdditive)
                m_SelectedIndices = m_SelectionBeforeBox;
            else
                m_SelectedIndices.clear();

            for (int i = 0; i < (int)m_ViewEntries.size(); ++i)
            {
                if (m_ViewEntries[i].DisplayName == "..")
                    continue;

                float x = 0.0f;
                float y = 0.0f;
                float w = 0.0f;
                float h = 0.0f;
                if (!GetEntryBounds(i, x, y, w, h))
                    continue;

                bool intersects = x <= right && x + w >= left && y <= bottom && y + h >= top;
                if (intersects)
                {
                    m_SelectedIndices.insert(i);
                    m_SelectedIndex = i;
                    if (m_AnchorSelectedIndex < 0)
                        m_AnchorSelectedIndex = i;
                }
            }

            if (m_SelectedIndices.empty() && !m_SelectionBoxAdditive)
                m_SelectedIndex = -1;
        }

        std::vector<AssetBrowserPanel::AssetEntry> AssetBrowserPanel::GetSelectedEntries() const
        {
            std::vector<AssetEntry> entries;
            std::vector<int> indices(m_SelectedIndices.begin(), m_SelectedIndices.end());
            std::sort(indices.begin(), indices.end());

            for (int index : indices)
            {
                if (index < 0 || index >= (int)m_ViewEntries.size())
                    continue;

                const AssetEntry& entry = m_ViewEntries[index];
                if (entry.DisplayName == ".." || entry.Type == AssetType::Unknown || IsVirtualSubAsset(entry))
                    continue;

                entries.push_back(entry);
            }

            return entries;
        }

        std::filesystem::path AssetBrowserPanel::MakeUniquePath(const std::filesystem::path& directory, const std::string& baseName, const std::string& extension) const
        {
            std::filesystem::path candidate = directory / (baseName + extension);
            if (!std::filesystem::exists(candidate))
                return candidate;

            for (int i = 1; i < 10000; ++i)
            {
                candidate = directory / (baseName + " " + std::to_string(i) + extension);
                if (!std::filesystem::exists(candidate))
                    return candidate;
            }

            return directory / (baseName + " 9999" + extension);
        }

        uint64_t AssetBrowserPanel::ComputeDirectorySignature(const std::filesystem::path& directory) const
        {
            std::error_code ec;
            if (!std::filesystem::exists(directory, ec) || !std::filesystem::is_directory(directory, ec))
                return 0;

            uint64_t signature = 1469598103934665603ull;
            for (const auto& entry : std::filesystem::directory_iterator(
                directory,
                std::filesystem::directory_options::skip_permission_denied,
                ec))
            {
                if (ec)
                {
                    ec.clear();
                    continue;
                }

                bool isDirectory = entry.is_directory(ec) && !ec;
                bool isRegular = entry.is_regular_file(ec) && !ec;
                ec.clear();
                if (!isDirectory && !isRegular)
                    continue;

                std::hash<std::string> hasher;
                HashCombine(signature, hasher(entry.path().filename().string()));
                HashCombine(signature, isDirectory ? 1ull : 2ull);

                auto time = entry.last_write_time(ec);
                if (!ec)
                    HashCombine(signature, (uint64_t)time.time_since_epoch().count());
                ec.clear();
            }

            return signature;
        }

        void AssetBrowserPanel::UpdateDirectoryWatchState()
        {
            m_DirectoryWatchSignatures.clear();
            m_DirectoryWatchOrder.clear();
            m_DirectoryWatchCursor = 0;

            // 외부 탐색기 변경은 엔진 내부 작업이 아니어서 캐시가 자동으로 더러워지지 않는다.
            // watcher를 못 쓰는 환경에서는 저장된 폴더 시그니처를 조금씩 비교해 변경을 찾는다.
            auto rememberDirectory = [this](const std::filesystem::path& directory)
                {
                    std::string key = GetTreeKey(directory);
                    if (m_DirectoryWatchSignatures.find(key) != m_DirectoryWatchSignatures.end())
                        return;

                    m_DirectoryWatchSignatures[key] = ComputeDirectorySignature(directory);
                    m_DirectoryWatchOrder.push_back(key);
                };

            rememberDirectory(m_CurrentDirectory);
            for (const TreeEntry& entry : m_TreeEntries)
                rememberDirectory(entry.Path);
        }

        void AssetBrowserPanel::CheckExternalFileChanges()
        {
            if (m_ExternalWatcherActive)
                return;

            auto now = std::chrono::steady_clock::now();
            if (m_LastExternalFileCheck.time_since_epoch().count() != 0 &&
                now - m_LastExternalFileCheck < std::chrono::milliseconds(350))
            {
                return;
            }
            m_LastExternalFileCheck = now;

            std::error_code ec;
            if (!std::filesystem::exists(m_CurrentDirectory, ec))
            {
                std::filesystem::path fallback = m_CurrentDirectory.parent_path();
                while (!fallback.empty() && !std::filesystem::exists(fallback, ec))
                    fallback = fallback.parent_path();

                m_CurrentDirectory = IsPathInsideRoot(fallback, true) ? fallback : m_RootDirectory;
                AssetDatabase::MarkDirty(m_RootDirectory);
                m_TreeChildCache.clear();
                Refresh(true);
                return;
            }

            if (m_DirectoryWatchOrder.empty())
                return;

            // 한 번에 모든 폴더를 훑으면 대형 프로젝트에서 1초마다 프레임이 멈춘다.
            // 그래서 현재 프레임에는 몇 개만 검사하고, 다음 프레임에서 이어서 본다.
            constexpr size_t MaxDirectorySignatureChecksPerTick = 4;
            size_t checks = (std::min)(MaxDirectorySignatureChecksPerTick, m_DirectoryWatchOrder.size());
            for (size_t i = 0; i < checks; ++i)
            {
                if (m_DirectoryWatchCursor >= m_DirectoryWatchOrder.size())
                    m_DirectoryWatchCursor = 0;

                const std::string& pathText = m_DirectoryWatchOrder[m_DirectoryWatchCursor++];
                auto signatureIt = m_DirectoryWatchSignatures.find(pathText);
                if (signatureIt == m_DirectoryWatchSignatures.end())
                    continue;

                uint64_t oldSignature = signatureIt->second;
                uint64_t newSignature = ComputeDirectorySignature(std::filesystem::path(pathText));
                if (newSignature != oldSignature)
                {
                    AssetDatabase::MarkDirty(m_RootDirectory);
                    m_TreeChildCache.clear();
                    Refresh(true);
                    return;
                }
            }
        }

        void AssetBrowserPanel::OnExternalAssetFilesChanged()
        {
            // OS watcher가 이미 에셋 DB를 갱신했다.
            // 브라우저는 트리 캐시와 현재 화면 목록만 다시 만든다.
            m_TreeChildCache.clear();
            Refresh(false);
        }

        bool AssetBrowserPanel::MoveEntryToDirectory(const AssetEntry& entry, const std::filesystem::path& targetDirectory, AssetUndoManager::Command* undoCommand)
        {
            if (entry.DisplayName == ".." || entry.Path == targetDirectory)
                return false;

            std::error_code ec;
            auto target = std::filesystem::weakly_canonical(targetDirectory, ec);
            if (ec || !std::filesystem::exists(target) || !std::filesystem::is_directory(target))
                return false;

            auto root = std::filesystem::weakly_canonical(m_RootDirectory, ec);
            if (ec)
                return false;

            std::wstring targetString = target.wstring();
            std::wstring rootString = root.wstring();
            if (targetString.rfind(rootString, 0) != 0)
                return false;

            std::filesystem::path destination = target / entry.Path.filename();
            if (std::filesystem::equivalent(entry.Path, destination, ec) || std::filesystem::exists(destination, ec))
                return false;

            if (entry.Type == AssetType::Folder)
            {
                auto source = std::filesystem::weakly_canonical(entry.Path, ec);
                if (ec)
                    return false;
                std::wstring sourceString = source.wstring();
                if (targetString.rfind(sourceString, 0) == 0)
                    return false;

                // 폴더 이동은 내부 파일과 meta를 통째로 옮긴다.
                // 단일 파일은 AssetDatabase를 거쳐 GUID/sourcePath를 바로 고친다.
                std::filesystem::rename(source, destination, ec);
                if (ec)
                    return false;
                AssetDatabase::MarkDirty(m_RootDirectory);
            }
            else
            {
                if (!AssetDatabase::MoveAsset(entry.Path, destination))
                    return false;
            }

            if (undoCommand)
                undoCommand->Items.push_back({ entry.Path, destination, {}, {}, entry.Type == AssetType::Folder });
            return true;
        }

        bool AssetBrowserPanel::MoveSelectedEntriesToDirectory(const std::filesystem::path& targetDirectory)
        {
            std::vector<AssetEntry> selected = GetSelectedEntries();
            if (selected.empty())
                return false;

            bool movedAny = false;
            AssetUndoManager::Command undoCommand;
            undoCommand.Operation = AssetUndoManager::Kind::Move;
            undoCommand.Label = selected.size() == 1 ? "Move Asset" : "Move Assets";
            for (const AssetEntry& entry : selected)
            {
                if (MoveEntryToDirectory(entry, targetDirectory, &undoCommand))
                    movedAny = true;
            }

            if (movedAny)
            {
                // 다중 선택 이동은 사용자가 한 번 끌어서 만든 한 작업이다.
                // 파일 수만큼 히스토리가 쪼개지면 Ctrl+Z 한 번으로 원래 상태에 못 돌아간다.
                PushAssetUndoCommand(undoCommand);
                ClearSelection();
                Refresh(true);
                NotifyAssetDatabaseChanged();
            }
            return movedAny;
        }

        void AssetBrowserPanel::StepIconSize(int direction)
        {
            m_IconSizeStep = (std::clamp)(m_IconSizeStep + direction, 0, 4);
            m_ScrollState.ScrollY = 0.0f;
        }

        void AssetBrowserPanel::NotifyAssetDatabaseChanged()
        {
            // 에셋 브라우저는 파일을 직접 바꾸지만, 씬/프리팹 참조 검사는 에디터가 담당한다.
            // 콜백으로 경계를 나누면 브라우저가 에디터 내부 구현을 알 필요가 없다.
            if (m_OnAssetDatabaseChanged)
                m_OnAssetDatabaseChanged();
        }

        void AssetBrowserPanel::PushAssetUndoCommand(const AssetUndoManager::Command& command)
        {
            if (!m_AssetUndoManager)
                return;

            m_AssetUndoManager->Push(command);
            if (m_OnAssetHistoryChanged)
                m_OnAssetHistoryChanged();
        }

        std::vector<std::string> AssetBrowserPanel::GetAssetHistoryLabels() const
        {
            if (!m_AssetUndoManager)
                return {};

            return m_AssetUndoManager->GetHistoryLabels();
        }

        size_t AssetBrowserPanel::GetAppliedAssetHistoryCount() const
        {
            return m_AssetUndoManager ? m_AssetUndoManager->GetAppliedCount() : 0;
        }

        bool AssetBrowserPanel::CanUndoAssetOperation() const
        {
            return m_AssetUndoManager && m_AssetUndoManager->CanUndo();
        }

        bool AssetBrowserPanel::CanRedoAssetOperation() const
        {
            return m_AssetUndoManager && m_AssetUndoManager->CanRedo();
        }

        bool AssetBrowserPanel::RequestAssetUndo()
        {
            if (!m_AssetUndoManager)
                return false;

            std::filesystem::path preferredDirectory;
            if (!m_AssetUndoManager->Undo(&preferredDirectory))
                return false;

            // 파일 작업 자체는 공용 매니저가 처리하고, 패널은 현재 경로와 표시 캐시만 갱신한다.
            // 이렇게 나누면 여러 에셋 브라우저가 열려도 Undo 기록은 하나로 유지된다.
            RefreshAfterAssetUndo(preferredDirectory);
            if (m_OnAssetHistoryChanged)
                m_OnAssetHistoryChanged();
            return true;
        }

        bool AssetBrowserPanel::RequestAssetRedo()
        {
            if (!m_AssetUndoManager)
                return false;

            std::filesystem::path preferredDirectory;
            if (!m_AssetUndoManager->Redo(&preferredDirectory))
                return false;

            RefreshAfterAssetUndo(preferredDirectory);
            if (m_OnAssetHistoryChanged)
                m_OnAssetHistoryChanged();
            return true;
        }

        bool AssetBrowserPanel::SeekAssetHistory(size_t targetAppliedCount)
        {
            if (!m_AssetUndoManager)
                return false;

            std::filesystem::path preferredDirectory;
            if (!m_AssetUndoManager->Seek(targetAppliedCount, &preferredDirectory))
                return false;

            RefreshAfterAssetUndo(preferredDirectory);
            if (m_OnAssetHistoryChanged)
                m_OnAssetHistoryChanged();
            return true;
        }

        void AssetBrowserPanel::RefreshAfterAssetUndo(const std::filesystem::path& preferredDirectory)
        {
            if (IsPathInsideRoot(preferredDirectory, true))
                m_CurrentDirectory = preferredDirectory;

            AssetDatabase::MarkDirty(m_RootDirectory);
            m_TreeChildCache.clear();
            Refresh(true);
            NotifyAssetDatabaseChanged();
        }

        Texture2D* AssetBrowserPanel::GetAssetPreviewTexture(const AssetEntry& entry)
        {
            if (entry.Type == AssetType::FbxMesh && m_IconSizeStep == 0)
            {
                // 리스트 모드는 아이콘이 작아서 mesh 프리뷰 이득이 거의 없다.
                // 펼침 직후에는 이름 확인이 먼저라서, 작은 목록에서는 기본 아이콘만 써서 부하를 줄인다.
                return nullptr;
            }

            bool isTexture = entry.Type == AssetType::Texture;
            bool isFbxContainerModel = IsFbxContainer(entry);
            bool isGeneratedPreview =
                entry.Type == AssetType::Prefab ||
                entry.Type == AssetType::FbxMesh ||
                (entry.Type == AssetType::Model && !isFbxContainerModel);
            if (!isTexture && !isGeneratedPreview)
                return nullptr;

            std::string key = GetTreeKey(entry.IsSubAsset ? entry.SourceAssetPath : entry.Path);
            if (isGeneratedPreview)
                key += std::string("|") + ThumbnailAlgorithmVersion;
            if (entry.Type == AssetType::FbxMesh)
                key += "|mesh:" + std::to_string(entry.SubAssetIndex);

            auto cached = m_TexturePreviewCache.find(key);
            if (cached != m_TexturePreviewCache.end())
            {
                std::shared_ptr<TexturePreviewJob> job = cached->second;
                if (!job || job->Failed)
                    return nullptr;

                if (job->Texture)
                    return job->Texture.get();

                if (!job->IsReady)
                    return nullptr;

                constexpr int MaxUploadsPerFrame = 2;
                if (m_TexturePreviewUploadsThisFrame >= MaxUploadsPerFrame)
                    return nullptr;

                std::vector<uint32_t> pixels;
                int width = 0;
                int height = 0;
                {
                    std::lock_guard<std::mutex> lock(job->Mutex);
                    pixels = std::move(job->Pixels);
                    width = job->Width;
                    height = job->Height;
                }

                if (pixels.empty() || width <= 0 || height <= 0)
                {
                    job->Failed = true;
                    return nullptr;
                }

                // DirectX 리소스 생성은 렌더 스레드에서만 한다.
                // 백그라운드 스레드는 파일 디코딩과 축소만 맡고, 여기서는 프레임당 소량만 GPU에 올린다.
                job->Texture.reset(Texture2D::Create((uint32_t)width, (uint32_t)height, pixels.data()));
                ++m_TexturePreviewUploadsThisFrame;
                if (!job->Texture || !job->Texture->GetRendererID())
                {
                    job->Failed = true;
                    return nullptr;
                }

                return job->Texture.get();
            }

            constexpr int MaxPreviewLoadsStartedPerFrame = 2;
            if (m_TexturePreviewLoadsStartedThisFrame >= MaxPreviewLoadsStartedPerFrame)
                return nullptr;

            constexpr int MaxActivePreviewLoads = 2;
            constexpr int MaxActiveFbxMeshPreviewLoads = 1;
            bool fbxMeshPreview = entry.Type == AssetType::FbxMesh;
            if (!TryAcquirePreviewSlot(s_ActivePreviewLoads, MaxActivePreviewLoads))
                return nullptr;

            if (fbxMeshPreview && !TryAcquirePreviewSlot(s_ActiveFbxMeshPreviewLoads, MaxActiveFbxMeshPreviewLoads))
            {
                s_ActivePreviewLoads.fetch_sub(1, std::memory_order_acq_rel);
                return nullptr;
            }

            auto job = std::make_shared<TexturePreviewJob>();
            job->IsLoading = true;
            m_TexturePreviewCache[key] = job;
            m_TexturePreviewOrder.push_back(key);
            ++m_TexturePreviewLoadsStartedThisFrame;

            std::string path = (entry.IsSubAsset ? entry.SourceAssetPath : entry.Path).string();
            AssetType type = entry.Type;
            int subAssetIndex = entry.SubAssetIndex;
            std::thread([job, path, type, subAssetIndex]()
                {
                    // 썸네일 작업은 detach된 스레드에서 끝나므로, 카운터도 스레드 종료 시점에 반납한다.
                    // 이렇게 해야 큰 FBX를 펼쳤을 때 여러 mesh 프리뷰가 동시에 Assimp 로드를 시작하지 않는다.
                    PreviewLoadSlot activeSlot(s_ActivePreviewLoads);
                    std::unique_ptr<PreviewLoadSlot> fbxMeshSlot;
                    if (type == AssetType::FbxMesh)
                        fbxMeshSlot = std::make_unique<PreviewLoadSlot>(s_ActiveFbxMeshPreviewLoads);

                    DecodedPreviewPixels decoded;
                    if (type == AssetType::Texture)
                        decoded = DecodeTexturePreviewPixels(path, 128);
                    else if (type == AssetType::FbxMesh)
                        decoded = LoadOrGenerateCachedFbxMeshPreview(std::filesystem::path(path), subAssetIndex, 128);
                    else
                        decoded = LoadOrGenerateCachedAssetPreview(std::filesystem::path(path), type == AssetType::Prefab, 128);

                    if (!decoded.Success)
                    {
                        job->Failed = true;
                        job->IsLoading = false;
                        return;
                    }

                    {
                        std::lock_guard<std::mutex> lock(job->Mutex);
                        job->Width = decoded.Width;
                        job->Height = decoded.Height;
                        job->Pixels = std::move(decoded.Pixels);
                    }

                    job->IsReady = true;
                    job->IsLoading = false;
                }).detach();

            // 썸네일 디코딩은 백그라운드에서 하지만, 시작 자체도 몰리면 디스크 IO가 튄다.
            // 한 프레임에 새 작업을 조금만 열어 두면 폴더를 열 때 화면이 덜 멈춘다.
            // 임시 썸네일 캐시는 UI 응답성을 위한 메모리 캐시다.
            // 너무 많이 쌓이면 에디터 전체 메모리를 갉아먹으므로 오래된 항목부터 버린다.
            constexpr size_t MaxTexturePreviewCache = 128;
            while (m_TexturePreviewOrder.size() > MaxTexturePreviewCache)
            {
                const std::string& oldKey = m_TexturePreviewOrder.front();
                m_TexturePreviewCache.erase(oldKey);
                m_TexturePreviewOrder.erase(m_TexturePreviewOrder.begin());
            }

            return nullptr;
        }

        void AssetBrowserPanel::DrawAssetPreview(const AssetEntry& entry, float x, float y, float size)
        {
            UIRenderer::DrawRectFilled(x, y, size, size, { 0.075f, 0.075f, 0.082f, 1.0f });
            UIRenderer::DrawRect(x, y, size, size, { 0.22f, 0.22f, 0.24f, 1.0f });

            Texture2D* previewTexture = GetAssetPreviewTexture(entry);
            if (previewTexture)
            {
                float checker = (std::max)(4.0f, size / 4.0f);
                for (int row = 0; row < 4; ++row)
                {
                    for (int col = 0; col < 4; ++col)
                    {
                        DirectX::XMFLOAT4 color = ((row + col) % 2 == 0)
                            ? DirectX::XMFLOAT4{ 0.18f, 0.18f, 0.19f, 1.0f }
                            : DirectX::XMFLOAT4{ 0.12f, 0.12f, 0.13f, 1.0f };
                        UIRenderer::DrawRectFilled(x + (float)col * checker, y + (float)row * checker, checker, checker, color);
                    }
                }

                UIRenderer::DrawImage(x + 2.0f, y + 2.0f, size - 4.0f, size - 4.0f, previewTexture->GetRendererID());
                return;
            }

            DrawFallbackAssetIcon(entry, x, y, size);
        }

        void AssetBrowserPanel::DrawFallbackAssetIcon(const AssetEntry& entry, float x, float y, float size)
        {
            DirectX::XMFLOAT4 iconColor = { 0.38f, 0.62f, 0.82f, 1.0f };
            DirectX::XMFLOAT4 accent = { 0.85f, 0.88f, 0.92f, 1.0f };
            std::string label = "???";

            switch (entry.Type)
            {
                case AssetType::Folder:
                    iconColor = { 0.72f, 0.72f, 0.72f, 1.0f };
                    label = "";
                    break;
                case AssetType::Scene:
                    iconColor = { 0.70f, 0.52f, 0.30f, 1.0f };
                    label = "SCN";
                    break;
                case AssetType::Prefab:
                    iconColor = { 0.36f, 0.56f, 0.90f, 1.0f };
                    label = "PFB";
                    break;
                case AssetType::Model:
                    iconColor = { 0.35f, 0.56f, 0.38f, 1.0f };
                    label = "MDL";
                    break;
                case AssetType::FbxMesh:
                    iconColor = { 0.42f, 0.62f, 0.86f, 1.0f };
                    label = "MSH";
                    break;
                case AssetType::Texture:
                    iconColor = { 0.54f, 0.40f, 0.58f, 1.0f };
                    label = "TEX";
                    break;
                case AssetType::Script:
                    iconColor = { 0.28f, 0.48f, 0.72f, 1.0f };
                    label = "C#";
                    break;
                default:
                    break;
            }

            if (entry.Type == AssetType::Folder)
            {
                UIRenderer::DrawRectFilled(x + size * 0.10f, y + size * 0.30f, size * 0.80f, size * 0.52f, iconColor);
                UIRenderer::DrawRectFilled(x + size * 0.10f, y + size * 0.18f, size * 0.42f, size * 0.20f, iconColor);
                return;
            }

            if (entry.Type == AssetType::Model || entry.Type == AssetType::Prefab || entry.Type == AssetType::FbxMesh)
            {
                float body = size * 0.54f;
                float bx = x + size * 0.23f;
                float by = y + size * 0.28f;
                UIRenderer::DrawRectFilled(bx, by, body, body, iconColor);
                UIRenderer::DrawRectFilled(bx + body * 0.18f, by - body * 0.18f, body, body * 0.18f, { iconColor.x + 0.10f, iconColor.y + 0.10f, iconColor.z + 0.10f, 1.0f });
                UIRenderer::DrawRectFilled(bx + body, by - body * 0.18f, body * 0.18f, body, { iconColor.x * 0.75f, iconColor.y * 0.75f, iconColor.z * 0.75f, 1.0f });
            }
            else if (entry.Type == AssetType::Scene)
            {
                UIRenderer::DrawRectFilled(x + size * 0.20f, y + size * 0.20f, size * 0.60f, size * 0.60f, iconColor);
                UIRenderer::DrawRectFilled(x + size * 0.26f, y + size * 0.30f, size * 0.48f, size * 0.08f, accent);
                UIRenderer::DrawRectFilled(x + size * 0.26f, y + size * 0.48f, size * 0.48f, size * 0.08f, accent);
            }
            else
            {
                UIRenderer::DrawRectFilled(x + size * 0.18f, y + size * 0.18f, size * 0.64f, size * 0.64f, iconColor);
                UIRenderer::DrawRectFilled(x + size * 0.28f, y + size * 0.30f, size * 0.44f, size * 0.10f, accent);
                UIRenderer::DrawRectFilled(x + size * 0.28f, y + size * 0.48f, size * 0.36f, size * 0.10f, accent);
            }

            if (size >= 46.0f && !label.empty())
                UIRenderer::DrawString(label, x + size * 0.18f, y + size * 0.84f, { 0.88f, 0.88f, 0.90f, 1.0f });
        }

        void AssetBrowserPanel::UpdateLayout(const DirectX::XMFLOAT2& parentPos, const DirectX::XMFLOAT2& parentSize)
        {
            WindowPanel::UpdateLayout(parentPos, parentSize);

            m_TreeWidth = (std::clamp)(m_TreeWidth, m_MinTreeWidth, (std::max)(m_MinTreeWidth, m_CalculatedSize.x - 180.0f));

            if (m_IconSizeStep == 0)
            {
                m_ScrollState.ContentHeight = (float)m_ViewEntries.size() * (m_RowHeight + m_RowGap) + 16.0f;
            }
            else
            {
                // 아이콘 크기가 바뀌면 한 줄에 들어가는 개수가 달라진다.
                // 스크롤 높이도 현재 폭과 아이콘 단계로 다시 계산해야 빈 공간이나 잘림이 생기지 않는다.
                const float iconSizes[5] = { 18.0f, 32.0f, 48.0f, 72.0f, 96.0f };
                float iconSize = iconSizes[(std::clamp)(m_IconSizeStep, 0, 4)];
                float cellW = iconSize + 58.0f;
                float cellH = iconSize + 42.0f;
                float viewW = (std::max)(1.0f, m_CalculatedSize.x - m_TreeWidth - 34.0f);
                int columns = (std::max)(1, (int)(viewW / cellW));
                int rows = (int)std::ceil((float)m_ViewEntries.size() / (float)columns);
                m_ScrollState.ContentHeight = (float)rows * cellH + 16.0f;
            }

            m_ScrollState.ViewportHeight = (std::max)(0.0f, m_CalculatedSize.y - m_ContentTop);
            m_TreeScrollState.ContentHeight = (float)m_TreeEntries.size() * 22.0f + 12.0f;
            m_TreeScrollState.ViewportHeight = m_ScrollState.ViewportHeight;
        }

        void AssetBrowserPanel::OnRender()
        {
            if (!m_IsVisible)
                return;

            CheckExternalFileChanges();

            SetClipPadding(0.0f, m_ContentTop, 0.0f, 0.0f);
            WindowPanel::OnRender();

            Window* mouseWindow = Widget::GetCurrentRenderWindow()
                ? Widget::GetCurrentRenderWindow()
                : (GetOwnerWindow() ? GetOwnerWindow() : &CCEngine::Application::Get()->GetWindow());
            auto [mouseX, mouseY] = mouseWindow->GetMousePosition();
            m_HoveredIndex = -1;
            m_TexturePreviewUploadsThisFrame = 0;
            m_TexturePreviewLoadsStartedThisFrame = 0;

            std::error_code ec;
            auto relativeCurrent = std::filesystem::relative(m_CurrentDirectory, m_RootDirectory, ec);
            std::string pathLabel = ec || relativeCurrent.empty() ? "assets" : ("assets\\" + relativeCurrent.string());
            if (pathLabel.size() > 72)
                pathLabel = "..." + pathLabel.substr(pathLabel.size() - 69);

            float toolbarY = m_CalculatedPos.y + 34.0f;
            UIRenderer::DrawString(pathLabel, m_CalculatedPos.x + 10.0f, toolbarY + 18.0f, { 0.62f, 0.62f, 0.62f, 1.0f });

            float btnSize = 22.0f;
            float btnY = m_CalculatedPos.y + 36.0f;
            float plusX = m_CalculatedPos.x + m_CalculatedSize.x - 46.0f;
            float minusX = plusX - 28.0f;
            float searchX = m_CalculatedPos.x + (std::max)(150.0f, m_TreeWidth + 18.0f);
            float searchW = (std::max)(80.0f, minusX - searchX - 10.0f);
            DirectX::XMFLOAT4 searchBg = m_SearchFocused
                ? DirectX::XMFLOAT4{ 0.13f, 0.16f, 0.19f, 1.0f }
                : DirectX::XMFLOAT4{ 0.08f, 0.08f, 0.09f, 1.0f };
            UIRenderer::DrawRectFilled(searchX, btnY, searchW, btnSize, searchBg);
            UIRenderer::DrawRect({ searchX, btnY }, { searchW, btnSize }, m_SearchFocused
                ? DirectX::XMFLOAT4{ 0.30f, 0.42f, 0.54f, 1.0f }
                : DirectX::XMFLOAT4{ 0.20f, 0.20f, 0.21f, 1.0f });
            std::string searchText = m_SearchQuery.empty() ? "Search..." : m_SearchQuery;
            if (searchText.size() > (size_t)(searchW / 7.0f))
                searchText = searchText.substr(0, (size_t)(searchW / 7.0f));
            UIRenderer::DrawString(searchText, searchX + 8.0f, btnY + 17.0f,
                m_SearchQuery.empty() ? DirectX::XMFLOAT4{ 0.48f, 0.48f, 0.50f, 1.0f } : DirectX::XMFLOAT4{ 0.86f, 0.86f, 0.86f, 1.0f });
            UIRenderer::DrawRectFilled(minusX, btnY, btnSize, btnSize, { 0.18f, 0.18f, 0.19f, 1.0f });
            UIRenderer::DrawRectFilled(plusX, btnY, btnSize, btnSize, { 0.18f, 0.18f, 0.19f, 1.0f });
            UIRenderer::DrawString("-", minusX + 8.0f, btnY + 17.0f, { 0.9f, 0.9f, 0.9f, 1.0f });
            UIRenderer::DrawString("+", plusX + 7.0f, btnY + 17.0f, { 0.9f, 0.9f, 0.9f, 1.0f });

            // 한 창 안에서 왼쪽은 프로젝트 폴더 트리, 오른쪽은 현재 폴더 내용이다.
            // 가운데 선을 움직이면 두 영역의 비율만 바뀌고, 창 자체 크기는 그대로 둔다.
            float treeX = m_CalculatedPos.x;
            float treeY = m_CalculatedPos.y + m_ContentTop;
            float treeH = m_CalculatedSize.y - m_ContentTop;
            float splitterX = m_CalculatedPos.x + m_TreeWidth;
            float contentX = splitterX + 6.0f;
            float contentW = m_CalculatedSize.x - m_TreeWidth - 22.0f;

            UIRenderer::DrawRectFilled(treeX, treeY, m_TreeWidth, treeH, { 0.105f, 0.105f, 0.11f, 1.0f });
            UIRenderer::DrawRectFilled(splitterX - 1.0f, treeY, 2.0f, treeH, { 0.34f, 0.34f, 0.35f, 1.0f });
            UIRenderer::DrawRectFilled(contentX, treeY, contentW, treeH, { 0.095f, 0.095f, 0.10f, 1.0f });

            float treeItemY = treeY + 6.0f - m_TreeScrollState.ScrollY;
            {
                // 트리 목록은 직접 그리는 영역이라 창 클립만으로는 잘리지 않는다.
                // 스크롤되는 항목은 트리 본문 안에서만 보이도록 별도 클립을 건다.
                ScopedUIClip treeClip(treeX, treeY, (std::max)(0.0f, m_TreeWidth - 12.0f), treeH);
                for (size_t i = 0; i < m_TreeEntries.size(); ++i)
                {
                    float currentY = treeItemY + (float)i * 22.0f;
                    if (currentY + 22.0f < treeY || currentY > treeY + treeH)
                        continue;

                    bool current = std::filesystem::equivalent(m_TreeEntries[i].Path, m_CurrentDirectory, ec);
                    ec.clear();
                    bool hovered = Widget::IsCurrentRenderWindowMouseActive() &&
                        !Widget::IsMouseInteractionActive() &&
                        mouseX >= treeX && mouseX <= treeX + m_TreeWidth &&
                        mouseY >= currentY && mouseY <= currentY + 22.0f &&
                        !IsMouseBlockedByWidgetAbove(mouseX, mouseY);

                    if (current)
                        UIRenderer::DrawRectFilled(treeX + 2.0f, currentY, m_TreeWidth - 16.0f, 22.0f, { 0.18f, 0.30f, 0.42f, 1.0f });
                    else if (hovered)
                        UIRenderer::DrawRectFilled(treeX + 2.0f, currentY, m_TreeWidth - 16.0f, 22.0f, { 0.20f, 0.20f, 0.21f, 1.0f });

                    float indent = 10.0f + (float)m_TreeEntries[i].Depth * 14.0f;
                    if (m_TreeEntries[i].HasChildren)
                    {
                        bool expanded = m_ExpandedTreeFolders.find(GetTreeKey(m_TreeEntries[i].Path)) != m_ExpandedTreeFolders.end();
                        UIRenderer::DrawString(expanded ? "v" : ">", treeX + indent, currentY + 17.0f, { 0.68f, 0.68f, 0.68f, 1.0f });
                    }

                    UIRenderer::DrawRectFilled(treeX + indent + 14.0f, currentY + 6.0f, 11.0f, 9.0f, { 0.70f, 0.70f, 0.70f, 1.0f });

                    std::string treeLabel = m_TreeEntries[i].DisplayName;
                    float labelX = treeX + indent + 30.0f;
                    float availableW = (std::max)(12.0f, (treeX + m_TreeWidth - 16.0f) - labelX);
                    size_t maxChars = (size_t)(availableW / 7.0f);
                    if (maxChars > 3 && treeLabel.size() > maxChars)
                        treeLabel = treeLabel.substr(0, maxChars - 3) + "...";

                    UIRenderer::DrawString(treeLabel, labelX, currentY + 17.0f, { 0.82f, 0.82f, 0.82f, 1.0f });
                }
            }

            if (m_TreeScrollState.GetMaxScroll() > 0.0f)
            {
                float thumbH = m_TreeScrollState.GetThumbHeight();
                float thumbY = m_TreeScrollState.GetThumbY(treeY);
                float thumbX = treeX + m_TreeWidth - 12.0f;

                UIRenderer::DrawRect({ thumbX, treeY }, { 8.0f, m_TreeScrollState.ViewportHeight }, { 0.08f, 0.08f, 0.08f, 0.5f });
                UIRenderer::DrawRect({ thumbX, thumbY }, { 8.0f, thumbH }, { 0.42f, 0.42f, 0.42f, 1.0f });
            }

            float startX = contentX + 8.0f;
            float startY = treeY + 10.0f - m_ScrollState.ScrollY;

            {
                // 오른쪽 파일 목록도 직접 렌더링한다.
                // 큰 아이콘 모드에서 위아래로 넘친 부분은 본문 밖으로 그리지 않는다.
                ScopedUIClip contentClip(contentX, treeY, (std::max)(0.0f, contentW - 16.0f), treeH);
                if (m_IconSizeStep == 0)
                {
                    // 0단계는 예전 리스트 모드다. 많은 파일 이름을 빠르게 훑을 때 쓴다.
                    float rowWidth = contentW - 18.0f;
                for (size_t i = 0; i < m_ViewEntries.size(); ++i)
                {
                    float currentY = startY + (float)i * (m_RowHeight + m_RowGap);
                    if (currentY + m_RowHeight < treeY || currentY > treeY + treeH)
                        continue;

                    bool hovered = Widget::IsCurrentRenderWindowMouseActive() &&
                        !Widget::IsMouseInteractionActive() &&
                        mouseX >= startX && mouseX <= startX + rowWidth &&
                        mouseY >= currentY && mouseY <= currentY + m_RowHeight &&
                        !IsMouseBlockedByWidgetAbove(mouseX, mouseY);
                    if (hovered)
                        m_HoveredIndex = (int)i;

                    DirectX::XMFLOAT4 rowColor = { 0.13f, 0.13f, 0.14f, 1.0f };
                    if (IsEntrySelected((int)i)) rowColor = { 0.18f, 0.30f, 0.42f, 1.0f };
                    else if (hovered) rowColor = { 0.20f, 0.20f, 0.21f, 1.0f };

                    UIRenderer::DrawRectFilled(startX, currentY, rowWidth, m_RowHeight, rowColor);
                    DrawAssetPreview(m_ViewEntries[i], startX + 7.0f, currentY + 4.0f, 18.0f);
                    if (IsFbxContainer(m_ViewEntries[i]))
                    {
                        bool expanded = m_ExpandedFbxAssets.find(GetTreeKey(m_ViewEntries[i].Path)) != m_ExpandedFbxAssets.end();
                        UIRenderer::DrawRectFilled(startX + 8.0f, currentY + 6.0f, 14.0f, 14.0f, { 0.23f, 0.24f, 0.26f, 1.0f });
                        UIRenderer::DrawRect({ startX + 8.0f, currentY + 6.0f }, { 14.0f, 14.0f }, { 0.48f, 0.50f, 0.54f, 1.0f });
                        UIRenderer::DrawString(expanded ? "v" : ">", startX + 11.0f, currentY + 18.0f, { 0.88f, 0.88f, 0.90f, 1.0f });
                    }
                    UIRenderer::DrawString(GetTypeLabel(m_ViewEntries[i].Type), startX + 34.0f, currentY + 18.0f, { 0.82f, 0.78f, 0.58f, 1.0f });
                    UIRenderer::DrawString(m_ViewEntries[i].DisplayName, startX + 76.0f, currentY + 18.0f, { 0.86f, 0.86f, 0.86f, 1.0f });
                }
            }
            else
            {
                // 1~4단계는 아이콘 모드다. 단계가 올라갈수록 아이콘과 셀을 같이 키운다.
                const float iconSizes[5] = { 18.0f, 32.0f, 48.0f, 72.0f, 96.0f };
                float iconSize = iconSizes[(std::clamp)(m_IconSizeStep, 0, 4)];
                float cellW = iconSize + 58.0f;
                float cellH = iconSize + 42.0f;
                int columns = (std::max)(1, (int)((contentW - 16.0f) / cellW));

                for (size_t i = 0; i < m_ViewEntries.size(); ++i)
                {
                    int col = (int)i % columns;
                    int row = (int)i / columns;
                    float cellX = startX + (float)col * cellW;
                    float cellY = startY + (float)row * cellH;
                    if (cellY + cellH < treeY || cellY > treeY + treeH)
                        continue;

                    bool hovered = Widget::IsCurrentRenderWindowMouseActive() &&
                        !Widget::IsMouseInteractionActive() &&
                        mouseX >= cellX && mouseX <= cellX + cellW - 6.0f &&
                        mouseY >= cellY && mouseY <= cellY + cellH - 4.0f &&
                        !IsMouseBlockedByWidgetAbove(mouseX, mouseY);
                    if (hovered)
                        m_HoveredIndex = (int)i;

                    if (IsEntrySelected((int)i))
                        UIRenderer::DrawRectFilled(cellX, cellY, cellW - 8.0f, cellH - 4.0f, { 0.18f, 0.30f, 0.42f, 1.0f });
                    else if (hovered)
                        UIRenderer::DrawRectFilled(cellX, cellY, cellW - 8.0f, cellH - 4.0f, { 0.18f, 0.18f, 0.19f, 1.0f });

                    float iconX = cellX + (cellW - iconSize) * 0.5f - 4.0f;
                    float iconY = cellY + 6.0f;
                    DrawAssetPreview(m_ViewEntries[i], iconX, iconY, iconSize);
                    if (IsFbxContainer(m_ViewEntries[i]))
                    {
                        bool expanded = m_ExpandedFbxAssets.find(GetTreeKey(m_ViewEntries[i].Path)) != m_ExpandedFbxAssets.end();
                        float buttonSize = 18.0f;
                        float buttonX = iconX + iconSize - buttonSize * 0.45f;
                        float buttonY = iconY + iconSize * 0.38f;
                        UIRenderer::DrawRectFilled(buttonX, buttonY, buttonSize, buttonSize, { 0.23f, 0.24f, 0.26f, 1.0f });
                        UIRenderer::DrawRect({ buttonX, buttonY }, { buttonSize, buttonSize }, { 0.52f, 0.54f, 0.58f, 1.0f });
                        UIRenderer::DrawString(expanded ? "<" : ">", buttonX + 5.0f, buttonY + 14.0f, { 0.90f, 0.90f, 0.92f, 1.0f });
                    }

                    std::string label = m_ViewEntries[i].DisplayName;
                    size_t maxChars = m_IconSizeStep >= 3 ? 14 : 10;
                    if (label.size() > maxChars)
                        label = label.substr(0, maxChars - 3) + "...";
                    UIRenderer::DrawString(label, cellX + 4.0f, cellY + iconSize + 28.0f, { 0.82f, 0.82f, 0.82f, 1.0f });
                }
            }

                if (m_ViewEntries.empty())
                    UIRenderer::DrawString(m_SearchQuery.empty() ? "No supported assets found." : "No assets match search.", contentX + 12.0f, treeY + 28.0f, { 0.65f, 0.65f, 0.65f, 1.0f });

                if (m_IsDraggingSelectionBox)
                {
                    float left = (std::min)(m_SelectionBoxStartX, m_SelectionBoxCurrentX);
                    float right = (std::max)(m_SelectionBoxStartX, m_SelectionBoxCurrentX);
                    float top = (std::min)(m_SelectionBoxStartY, m_SelectionBoxCurrentY);
                    float bottom = (std::max)(m_SelectionBoxStartY, m_SelectionBoxCurrentY);

                    // 빈 공간 드래그 선택은 상용 에디터에서 자주 쓰는 선택 방식이다.
                    // 반투명 면과 외곽선을 같이 그려서 선택 범위를 바로 알 수 있게 한다.
                    UIRenderer::DrawRectFilled(left, top, right - left, bottom - top, { 0.20f, 0.42f, 0.70f, 0.22f });
                    UIRenderer::DrawRect({ left, top }, { right - left, bottom - top }, { 0.35f, 0.58f, 0.86f, 0.85f });
                }
            }

            if (m_IsDraggingAsset && m_DragEntryIndex >= 0 && m_DragEntryIndex < (int)m_ViewEntries.size())
            {
                size_t dragCount = GetSelectedEntries().size();
                std::string dragLabel = dragCount > 1
                    ? std::to_string(dragCount) + " assets"
                    : m_ViewEntries[m_DragEntryIndex].DisplayName;
                UIRenderer::DrawRectFilled(mouseX + 12.0f, mouseY + 12.0f, 130.0f, 24.0f, { 0.10f, 0.10f, 0.11f, 0.9f });
                UIRenderer::DrawString(dragLabel, mouseX + 18.0f, mouseY + 30.0f, { 0.9f, 0.9f, 0.9f, 1.0f });
            }

            if (m_ScrollState.GetMaxScroll() > 0.0f)
            {
                float thumbH = m_ScrollState.GetThumbHeight();
                float thumbY = m_ScrollState.GetThumbY(m_CalculatedPos.y + m_ContentTop);
                float thumbX = m_CalculatedPos.x + m_CalculatedSize.x - 16.0f;

                UIRenderer::DrawRect({ thumbX, m_CalculatedPos.y + m_ContentTop }, { 8.0f, m_ScrollState.ViewportHeight }, { 0.08f, 0.08f, 0.08f, 0.5f });
                UIRenderer::DrawRect({ thumbX, thumbY }, { 8.0f, thumbH }, { 0.42f, 0.42f, 0.42f, 1.0f });
            }

            if (m_ContextMenuVisible)
            {
                UIRenderer::DrawRectFilled(m_ContextMenuX, m_ContextMenuY, m_ContextMenuWidth, m_ContextMenuHeight, { 0.14f, 0.14f, 0.15f, 1.0f });
                UIRenderer::DrawRect({ m_ContextMenuX, m_ContextMenuY }, { m_ContextMenuWidth, m_ContextMenuHeight }, { 0.28f, 0.28f, 0.30f, 1.0f });
                for (size_t i = 0; i < m_ContextMenuItems.size(); ++i)
                {
                    float itemY = m_ContextMenuY + (float)i * 28.0f;
                    bool hovered = Widget::IsCurrentRenderWindowMouseActive() &&
                        mouseX >= m_ContextMenuX && mouseX <= m_ContextMenuX + m_ContextMenuWidth &&
                        mouseY >= itemY && mouseY <= itemY + 28.0f;
                    if (hovered)
                        UIRenderer::DrawRectFilled(m_ContextMenuX, itemY, m_ContextMenuWidth, 28.0f, { 0.24f, 0.24f, 0.25f, 1.0f });

                    DirectX::XMFLOAT4 color = m_ContextMenuItems[i] == "Delete" ? DirectX::XMFLOAT4{ 0.95f, 0.72f, 0.72f, 1.0f } : DirectX::XMFLOAT4{ 0.86f, 0.86f, 0.86f, 1.0f };
                    UIRenderer::DrawString(m_ContextMenuItems[i], m_ContextMenuX + 10.0f, itemY + 19.0f, color);
                }
            }

            if (m_ModalMode == ModalMode::CreateFolder || m_ModalMode == ModalMode::Rename)
            {
                float modalW = 360.0f;
                float modalH = 150.0f;
                float modalX = m_CalculatedPos.x + (m_CalculatedSize.x - modalW) * 0.5f;
                float modalY = m_CalculatedPos.y + (m_CalculatedSize.y - modalH) * 0.5f;
                const char* title = m_ModalMode == ModalMode::CreateFolder ? "Create Folder" : "Rename";

                UIRenderer::DrawRectFilled(modalX, modalY, modalW, modalH, { 0.13f, 0.13f, 0.15f, 1.0f });
                UIRenderer::DrawRect({ modalX, modalY }, { modalW, modalH }, { 0.30f, 0.30f, 0.32f, 1.0f });
                UIRenderer::DrawString(title, modalX + 14.0f, modalY + 24.0f, { 0.90f, 0.90f, 0.90f, 1.0f });

                UIRenderer::DrawRectFilled(modalX + 14.0f, modalY + 48.0f, modalW - 28.0f, 28.0f, { 0.08f, 0.08f, 0.09f, 1.0f });
                UIRenderer::DrawString(m_ModalText.empty() ? "Name" : m_ModalText, modalX + 22.0f, modalY + 68.0f, { 0.92f, 0.92f, 0.92f, 1.0f });

                UIRenderer::DrawRectFilled(modalX + modalW - 174.0f, modalY + modalH - 42.0f, 76.0f, 26.0f, { 0.22f, 0.22f, 0.23f, 1.0f });
                UIRenderer::DrawString("OK", modalX + modalW - 145.0f, modalY + modalH - 23.0f, { 0.88f, 0.88f, 0.88f, 1.0f });
                UIRenderer::DrawRectFilled(modalX + modalW - 90.0f, modalY + modalH - 42.0f, 76.0f, 26.0f, { 0.22f, 0.22f, 0.23f, 1.0f });
                UIRenderer::DrawString("Cancel", modalX + modalW - 74.0f, modalY + modalH - 23.0f, { 0.88f, 0.88f, 0.88f, 1.0f });
            }

            if (m_ModalMode == ModalMode::ConfirmDelete)
            {
                float modalW = 380.0f;
                float modalH = 150.0f;
                float modalX = m_CalculatedPos.x + (m_CalculatedSize.x - modalW) * 0.5f;
                float modalY = m_CalculatedPos.y + (m_CalculatedSize.y - modalH) * 0.5f;

                UIRenderer::DrawRectFilled(modalX, modalY, modalW, modalH, { 0.13f, 0.13f, 0.15f, 1.0f });
                UIRenderer::DrawRect({ modalX, modalY }, { modalW, modalH }, { 0.30f, 0.30f, 0.32f, 1.0f });
                UIRenderer::DrawString("Move to Recycle Bin?", modalX + 14.0f, modalY + 26.0f, { 0.95f, 0.82f, 0.72f, 1.0f });
                UIRenderer::DrawString(m_ModalText, modalX + 14.0f, modalY + 58.0f, { 0.86f, 0.86f, 0.86f, 1.0f });

                UIRenderer::DrawRectFilled(modalX + modalW - 174.0f, modalY + modalH - 42.0f, 76.0f, 26.0f, { 0.38f, 0.16f, 0.16f, 1.0f });
                UIRenderer::DrawString("Delete", modalX + modalW - 158.0f, modalY + modalH - 23.0f, { 0.95f, 0.88f, 0.88f, 1.0f });
                UIRenderer::DrawRectFilled(modalX + modalW - 90.0f, modalY + modalH - 42.0f, 76.0f, 26.0f, { 0.22f, 0.22f, 0.23f, 1.0f });
                UIRenderer::DrawString("Cancel", modalX + modalW - 74.0f, modalY + modalH - 23.0f, { 0.88f, 0.88f, 0.88f, 1.0f });
            }
        }

        bool AssetBrowserPanel::OnEvent(Event& e)
        {
            if (!m_IsVisible)
                return false;

            if (e.GetEventType() == EventType::FileDrop)
            {
                auto& dropEvent = static_cast<FileDropEvent&>(e);
                if (ImportExternalPaths(dropEvent.GetPaths(), dropEvent.GetX(), dropEvent.GetY()))
                {
                    e.Handled = true;
                    return true;
                }
            }

            if (m_ContextMenuVisible && Widget::IsMouseInteractionActive())
                m_ContextMenuVisible = false;

            if (e.GetEventType() == EventType::MouseButtonPressed)
            {
                auto& me = static_cast<MouseButtonPressedEvent&>(e);
                constexpr float frameEdge = 8.0f;
                constexpr float titleBarHeight = 24.0f;

                bool isLeft = me.GetX() >= m_CalculatedPos.x && me.GetX() <= m_CalculatedPos.x + frameEdge;
                bool isRight = me.GetX() >= m_CalculatedPos.x + m_CalculatedSize.x - frameEdge && me.GetX() <= m_CalculatedPos.x + m_CalculatedSize.x;
                bool isTop = me.GetY() >= m_CalculatedPos.y && me.GetY() <= m_CalculatedPos.y + frameEdge;
                bool isBottom = me.GetY() >= m_CalculatedPos.y + m_CalculatedSize.y - frameEdge && me.GetY() <= m_CalculatedPos.y + m_CalculatedSize.y;
                bool isTitleBar = me.GetY() >= m_CalculatedPos.y && me.GetY() <= m_CalculatedPos.y + titleBarHeight &&
                    me.GetX() >= m_CalculatedPos.x && me.GetX() <= m_CalculatedPos.x + m_CalculatedSize.x;

                if (me.GetButton() == 0 && (isLeft || isRight || isTop || isBottom || isTitleBar))
                {
                    m_ContextMenuVisible = false;

                    // 창 프레임은 내부 파일 목록보다 먼저 입력을 받는다.
                    // 그래야 하단/우측 테두리를 잡았을 때 목록 선택이나 스크롤 처리에 먹히지 않고 리사이즈가 시작된다.
                    if (WindowPanel::OnEvent(e))
                        return true;
                }
            }

            if (m_ModalMode != ModalMode::None)
            {
                if (e.GetEventType() == EventType::TextInput &&
                    (m_ModalMode == ModalMode::CreateFolder || m_ModalMode == ModalMode::Rename))
                {
                    auto& te = static_cast<TextInputEvent&>(e);
                    char c = te.GetCharacter();
                    if (c >= 32 && c != '/' && c != '\\' && c != ':' && c != '*' && c != '?' && c != '"' && c != '<' && c != '>' && c != '|')
                        m_ModalText.push_back(c);

                    e.Handled = true;
                    return true;
                }

                if (e.GetEventType() == EventType::KeyPressed)
                {
                    auto& ke = static_cast<KeyPressedEvent&>(e);
                    if (ke.GetKeyCode() == 8 && !m_ModalText.empty() &&
                        (m_ModalMode == ModalMode::CreateFolder || m_ModalMode == ModalMode::Rename))
                    {
                        m_ModalText.pop_back();
                    }
                    else if (ke.GetKeyCode() == 13)
                    {
                        if (m_ModalMode == ModalMode::ConfirmDelete)
                            ConfirmDeleteModal();
                        else
                            ConfirmNameModal();
                    }
                    else if (ke.GetKeyCode() == 27)
                    {
                        CancelModal();
                    }

                    e.Handled = true;
                    return true;
                }

                if (e.GetEventType() == EventType::MouseButtonPressed)
                {
                    auto& me = static_cast<MouseButtonPressedEvent&>(e);
                    if (me.GetButton() == 0)
                    {
                        if (m_ModalMode == ModalMode::CreateFolder || m_ModalMode == ModalMode::Rename)
                        {
                            float modalW = 360.0f;
                            float modalH = 150.0f;
                            float modalX = m_CalculatedPos.x + (m_CalculatedSize.x - modalW) * 0.5f;
                            float modalY = m_CalculatedPos.y + (m_CalculatedSize.y - modalH) * 0.5f;
                            bool ok = me.GetX() >= modalX + modalW - 174.0f && me.GetX() <= modalX + modalW - 98.0f &&
                                me.GetY() >= modalY + modalH - 42.0f && me.GetY() <= modalY + modalH - 16.0f;
                            bool cancel = me.GetX() >= modalX + modalW - 90.0f && me.GetX() <= modalX + modalW - 14.0f &&
                                me.GetY() >= modalY + modalH - 42.0f && me.GetY() <= modalY + modalH - 16.0f;

                            if (ok)
                                ConfirmNameModal();
                            else if (cancel)
                                CancelModal();
                        }
                        else if (m_ModalMode == ModalMode::ConfirmDelete)
                        {
                            float modalW = 380.0f;
                            float modalH = 150.0f;
                            float modalX = m_CalculatedPos.x + (m_CalculatedSize.x - modalW) * 0.5f;
                            float modalY = m_CalculatedPos.y + (m_CalculatedSize.y - modalH) * 0.5f;
                            bool del = me.GetX() >= modalX + modalW - 174.0f && me.GetX() <= modalX + modalW - 98.0f &&
                                me.GetY() >= modalY + modalH - 42.0f && me.GetY() <= modalY + modalH - 16.0f;
                            bool cancel = me.GetX() >= modalX + modalW - 90.0f && me.GetX() <= modalX + modalW - 14.0f &&
                                me.GetY() >= modalY + modalH - 42.0f && me.GetY() <= modalY + modalH - 16.0f;

                            if (del)
                                ConfirmDeleteModal();
                            else if (cancel)
                                CancelModal();
                        }
                    }

                    e.Handled = true;
                    return true;
                }

                e.Handled = true;
                return true;
            }

            if (e.GetEventType() == EventType::KeyPressed)
            {
                auto& ke = static_cast<KeyPressedEvent&>(e);
                auto& window = GetOwnerWindow() ? *GetOwnerWindow() : CCEngine::Application::Get()->GetWindow();
                auto [mouseX, mouseY] = window.GetMousePosition();
                bool mouseInsideBrowser = IsPointInside(mouseX, mouseY);
                bool ctrl = IsKeyHeld(VK_CONTROL);
                bool shift = IsKeyHeld(VK_SHIFT);

                // 에셋 Undo는 디스크 파일을 움직이는 작업이라 씬 Transform Undo와 분리한다.
                // 마우스가 에셋 브라우저 안에 있을 때만 Ctrl+Z/Y를 가져가면 다른 패널의 단축키를 침범하지 않는다.
                if (mouseInsideBrowser && ctrl && ke.GetKeyCode() == 'Z')
                {
                    bool handled = shift ? RequestAssetRedo() : RequestAssetUndo();
                    e.Handled = handled;
                    return handled;
                }

                if (mouseInsideBrowser && ctrl && ke.GetKeyCode() == 'Y')
                {
                    bool handled = RequestAssetRedo();
                    e.Handled = handled;
                    return handled;
                }
            }

            if (m_SearchFocused)
            {
                if (e.GetEventType() == EventType::TextInput)
                {
                    auto& te = static_cast<TextInputEvent&>(e);
                    char c = te.GetCharacter();
                    if (c >= 32 && c != '\t')
                        SetSearchQuery(m_SearchQuery + c);

                    e.Handled = true;
                    return true;
                }

                if (e.GetEventType() == EventType::KeyPressed)
                {
                    auto& ke = static_cast<KeyPressedEvent&>(e);
                    if (ke.GetKeyCode() == 8 && !m_SearchQuery.empty())
                        SetSearchQuery(m_SearchQuery.substr(0, m_SearchQuery.size() - 1));
                    else if (ke.GetKeyCode() == 27)
                    {
                        if (!m_SearchQuery.empty())
                            SetSearchQuery("");
                        else
                            m_SearchFocused = false;
                    }
                    else if (ke.GetKeyCode() == 13)
                    {
                        m_SearchFocused = false;
                    }

                    e.Handled = true;
                    return true;
                }
            }

            if (e.GetEventType() == EventType::MouseScrolled)
            {
                auto& se = static_cast<MouseScrolledEvent&>(e);
                auto& window = GetOwnerWindow() ? *GetOwnerWindow() : CCEngine::Application::Get()->GetWindow();
                auto [mouseX, mouseY] = window.GetMousePosition();
                if (IsTreePoint(mouseX, mouseY))
                {
                    m_TreeScrollState.ApplyScroll(se.GetYOffset() * -1.0f);
                    e.Handled = true;
                    return true;
                }
                if (IsContentPoint(mouseX, mouseY))
                {
                    m_ScrollState.ApplyScroll(se.GetYOffset() * -1.0f);
                    e.Handled = true;
                    return true;
                }
            }

            if (e.GetEventType() == EventType::KeyPressed)
            {
                auto& ke = static_cast<KeyPressedEvent&>(e);
                if (ke.GetKeyCode() == 46 && RequestDeleteSelectedAsset())
                {
                    e.Handled = true;
                    return true;
                }
            }

            if (e.GetEventType() == EventType::MouseButtonPressed)
            {
                auto& me = static_cast<MouseButtonPressedEvent&>(e);
                float btnSize = 22.0f;
                float btnY = m_CalculatedPos.y + 36.0f;
                float plusX = m_CalculatedPos.x + m_CalculatedSize.x - 46.0f;
                float minusX = plusX - 28.0f;

                if (me.GetButton() == 0)
                {
                    if (IsSearchBoxPoint(me.GetX(), me.GetY()))
                    {
                        m_SearchFocused = true;
                        m_ContextMenuVisible = false;
                        e.Handled = true;
                        return true;
                    }
                    else if (IsPointInside(me.GetX(), me.GetY()))
                    {
                        m_SearchFocused = false;
                    }
                }

                if (me.GetButton() == 0 && m_ContextMenuVisible)
                {
                    int menuItem = GetContextMenuItemAt(me.GetX(), me.GetY());
                    if (menuItem >= 0 && menuItem < (int)m_ContextMenuItems.size())
                    {
                        std::string command = m_ContextMenuItems[menuItem];
                        m_ContextMenuVisible = false;
                        if (command == "Create Folder")
                            BeginCreateFolder();
                        else if (command == "Rename")
                            BeginRenameSelected();
                        else if (command == "Delete")
                            RequestDeleteSelectedAsset();

                        e.Handled = true;
                        return true;
                    }

                    m_ContextMenuVisible = false;
                }

                if (me.GetButton() == 0 &&
                    me.GetY() >= btnY && me.GetY() <= btnY + btnSize &&
                    me.GetX() >= minusX && me.GetX() <= minusX + btnSize)
                {
                    StepIconSize(-1);
                    e.Handled = true;
                    return true;
                }

                if (me.GetButton() == 0 &&
                    me.GetY() >= btnY && me.GetY() <= btnY + btnSize &&
                    me.GetX() >= plusX && me.GetX() <= plusX + btnSize)
                {
                    StepIconSize(1);
                    e.Handled = true;
                    return true;
                }

                if (me.GetButton() == 0 && IsTreeScrollbarPoint(me.GetX(), me.GetY()))
                {
                    m_IsDraggingTreeScrollbar = true;
                    m_IsMouseDownOnEntry = false;
                    m_DragMouseStartY = me.GetY();
                    m_DragTreeScrollStartY = m_TreeScrollState.ScrollY;
                    e.Handled = true;
                    return true;
                }

                if (me.GetButton() == 0 && IsSplitterPoint(me.GetX(), me.GetY()))
                {
                    m_IsDraggingSplitter = true;
                    m_DragMouseStartX = me.GetX();
                    m_DragSplitterStartWidth = m_TreeWidth;
                    e.Handled = true;
                    return true;
                }

                if (me.GetButton() == 0 && IsScrollbarPoint(me.GetX(), me.GetY()))
                {
                    m_IsDraggingScrollbar = true;
                    m_IsMouseDownOnEntry = false;
                    m_DragMouseStartY = me.GetY();
                    m_DragScrollStartY = m_ScrollState.ScrollY;
                    e.Handled = true;
                    return true;
                }

                if (me.GetButton() == 0 && IsTreePoint(me.GetX(), me.GetY()))
                {
                    ClearSelection();
                    int index = GetTreeIndexAt(me.GetX(), me.GetY());
                    if (index >= 0 && index < (int)m_TreeEntries.size())
                    {
                        auto now = std::chrono::steady_clock::now();
                        bool isDoubleClick = m_LastTreeClickedIndex == index &&
                            (now - m_LastTreeClickTime) < std::chrono::milliseconds(450);

                        TreeEntry treeEntry = m_TreeEntries[index];
                        float rowIndent = 10.0f + (float)treeEntry.Depth * 14.0f;
                        float toggleX = m_CalculatedPos.x + rowIndent;
                        bool clickedToggle = treeEntry.HasChildren &&
                            me.GetX() >= toggleX &&
                            me.GetX() <= toggleX + 14.0f;

                        if (clickedToggle || isDoubleClick)
                            ToggleTreeFolder(treeEntry.Path);

                        NavigateTo(treeEntry.Path);
                        m_LastTreeClickedIndex = index;
                        m_LastTreeClickTime = now;
                    }

                    e.Handled = true;
                    return true;
                }

                if (me.GetButton() == 0 && IsContentPoint(me.GetX(), me.GetY()))
                {
                    int index = GetEntryIndexAt(me.GetX(), me.GetY());

                    if (index >= 0 && index < (int)m_ViewEntries.size())
                    {
                        if (IsFbxExpandButtonPoint(index, me.GetX(), me.GetY()))
                        {
                            ToggleFbxExpanded(m_ViewEntries[index].Path);
                            m_IsMouseDownOnEntry = false;
                            m_DragEntryIndex = -1;
                            e.Handled = true;
                            return true;
                        }

                        m_IsMouseDownOnEmptyContent = false;
                        m_IsDraggingSelectionBox = false;
                        auto now = std::chrono::steady_clock::now();
                        bool isDoubleClick = m_LastClickedIndex == index &&
                            (now - m_LastClickTime) < std::chrono::milliseconds(450);
                        bool ctrl = IsKeyHeld(VK_CONTROL);
                        bool shift = IsKeyHeld(VK_SHIFT);

                        if (shift)
                            SelectRange(index);
                        else if (ctrl)
                            ToggleSelection(index);
                        else if (!IsEntrySelected(index))
                            SelectSingle(index);
                        else
                            m_SelectedIndex = index;

                        if (isDoubleClick && !ctrl && !shift)
                        {
                            ActivateEntry(m_ViewEntries[index]);
                            m_IsMouseDownOnEntry = false;
                        }
                        else
                        {
                            m_IsMouseDownOnEntry = IsEntrySelected(index) && !IsVirtualSubAsset(m_ViewEntries[index]);
                            m_DragEntryIndex = m_IsMouseDownOnEntry ? index : -1;
                            m_DragStartX = me.GetX();
                            m_DragStartY = me.GetY();
                        }

                        m_LastClickedIndex = index;
                        m_LastClickTime = now;
                    }
                    else
                    {
                        bool additive = IsKeyHeld(VK_CONTROL) || IsKeyHeld(VK_SHIFT);
                        m_IsMouseDownOnEmptyContent = true;
                        m_IsDraggingSelectionBox = false;
                        m_SelectionBoxAdditive = additive;
                        m_SelectionBeforeBox = m_SelectedIndices;
                        m_SelectionBoxStartX = me.GetX();
                        m_SelectionBoxStartY = me.GetY();
                        m_SelectionBoxCurrentX = me.GetX();
                        m_SelectionBoxCurrentY = me.GetY();
                        if (!additive)
                            ClearSelection();
                    }

                    e.Handled = true;
                    return true;
                }

                if (me.GetButton() == 1 && IsContentPoint(me.GetX(), me.GetY()))
                {
                    int index = GetEntryIndexAt(me.GetX(), me.GetY());
                    m_ContextMenuItems.clear();
                    if (index >= 0 && index < (int)m_ViewEntries.size())
                    {
                        if (!IsEntrySelected(index))
                            SelectSingle(index);
                        else
                            m_SelectedIndex = index;

                        const auto& entry = m_ViewEntries[index];
                        if (entry.DisplayName != ".." && entry.Type != AssetType::Unknown && !IsVirtualSubAsset(entry))
                        {
                            if (GetSelectedEntries().size() == 1)
                                m_ContextMenuItems.push_back("Rename");
                            m_ContextMenuItems.push_back("Delete");
                        }
                    }
                    else
                    {
                        ClearSelection();
                        m_ContextMenuItems.push_back("Create Folder");
                    }

                    m_ContextMenuVisible = !m_ContextMenuItems.empty();
                    m_ContextMenuHeight = 28.0f * (float)m_ContextMenuItems.size();
                    if (m_ContextMenuVisible)
                    {
                        m_ContextMenuX = (std::min)(me.GetX(), m_CalculatedPos.x + m_CalculatedSize.x - m_ContextMenuWidth - 4.0f);
                        m_ContextMenuY = (std::min)(me.GetY(), m_CalculatedPos.y + m_CalculatedSize.y - m_ContextMenuHeight - 4.0f);
                    }

                    e.Handled = true;
                    return true;
                }

                if (me.GetButton() == 1 && IsPointInside(me.GetX(), me.GetY()))
                {
                    // 에셋 브라우저 위에서 우클릭한 입력은 브라우저가 끝까지 소유한다.
                    // 빈 콘텐츠가 아닌 제목줄/트리/분할선 영역에서 메뉴를 만들지 않더라도 아래 씬 뷰 메뉴로 새면 안 된다.
                    m_ContextMenuVisible = false;
                    e.Handled = true;
                    return true;
                }
            }

            if (e.GetEventType() == EventType::MouseMoved && m_IsDraggingScrollbar)
            {
                auto& me = static_cast<MouseMovedEvent&>(e);
                float trackSpace = m_ScrollState.ViewportHeight - m_ScrollState.GetThumbHeight();

                if (trackSpace > 0.0f)
                {
                    float deltaY = me.GetY() - m_DragMouseStartY;
                    float scrollDelta = (deltaY / trackSpace) * m_ScrollState.GetMaxScroll();
                    m_ScrollState.ScrollY = std::clamp(m_DragScrollStartY + scrollDelta, 0.0f, m_ScrollState.GetMaxScroll());
                }

                e.Handled = true;
                return true;
            }

            if (e.GetEventType() == EventType::MouseMoved && m_IsDraggingTreeScrollbar)
            {
                auto& me = static_cast<MouseMovedEvent&>(e);
                float trackSpace = m_TreeScrollState.ViewportHeight - m_TreeScrollState.GetThumbHeight();

                if (trackSpace > 0.0f)
                {
                    float deltaY = me.GetY() - m_DragMouseStartY;
                    float scrollDelta = (deltaY / trackSpace) * m_TreeScrollState.GetMaxScroll();
                    m_TreeScrollState.ScrollY = std::clamp(m_DragTreeScrollStartY + scrollDelta, 0.0f, m_TreeScrollState.GetMaxScroll());
                }

                e.Handled = true;
                return true;
            }

            if (e.GetEventType() == EventType::MouseMoved && m_IsDraggingSplitter)
            {
                auto& me = static_cast<MouseMovedEvent&>(e);
                m_TreeWidth = (std::clamp)(m_DragSplitterStartWidth + (me.GetX() - m_DragMouseStartX), m_MinTreeWidth, m_MaxTreeWidth);
                e.Handled = true;
                return true;
            }

            if (e.GetEventType() == EventType::MouseMoved && m_IsMouseDownOnEmptyContent)
            {
                auto& me = static_cast<MouseMovedEvent&>(e);
                float dx = me.GetX() - m_SelectionBoxStartX;
                float dy = me.GetY() - m_SelectionBoxStartY;

                if (!m_IsDraggingSelectionBox && (dx * dx + dy * dy) > 16.0f)
                    m_IsDraggingSelectionBox = true;

                if (m_IsDraggingSelectionBox)
                {
                    UpdateSelectionBox(me.GetX(), me.GetY());
                    e.Handled = true;
                    return true;
                }
            }

            if (e.GetEventType() == EventType::MouseMoved && m_IsMouseDownOnEntry && m_DragEntryIndex >= 0)
            {
                auto& me = static_cast<MouseMovedEvent&>(e);
                float dx = me.GetX() - m_DragStartX;
                float dy = me.GetY() - m_DragStartY;

                if ((dx * dx + dy * dy) > 36.0f)
                {
                    m_IsDraggingAsset = true;
                }

                if (m_IsDraggingAsset)
                {
                    e.Handled = true;
                    return true;
                }
            }

            if (e.GetEventType() == EventType::MouseButtonReleased)
            {
                auto& me = static_cast<MouseButtonReleasedEvent&>(e);
                if (me.GetButton() == 0 && m_IsDraggingScrollbar)
                {
                    m_IsDraggingScrollbar = false;
                    e.Handled = true;
                    return true;
                }

                if (me.GetButton() == 0 && m_IsDraggingTreeScrollbar)
                {
                    m_IsDraggingTreeScrollbar = false;
                    e.Handled = true;
                    return true;
                }

                if (me.GetButton() == 0 && m_IsDraggingSplitter)
                {
                    m_IsDraggingSplitter = false;
                    e.Handled = true;
                    return true;
                }

                if (me.GetButton() == 0 && m_IsMouseDownOnEmptyContent)
                {
                    if (m_IsDraggingSelectionBox)
                        UpdateSelectionBox(me.GetX(), me.GetY());
                    else if (!m_SelectionBoxAdditive)
                        ClearSelection();

                    m_IsMouseDownOnEmptyContent = false;
                    m_IsDraggingSelectionBox = false;
                    m_SelectionBeforeBox.clear();
                    e.Handled = true;
                    return true;
                }

                if (me.GetButton() == 0 && m_IsMouseDownOnEntry)
                {
                    if (m_IsDraggingAsset && m_DragEntryIndex >= 0 && m_DragEntryIndex < (int)m_ViewEntries.size())
                    {
                        const auto& entry = m_ViewEntries[m_DragEntryIndex];
                        std::vector<AssetEntry> selected = GetSelectedEntries();
                        if (selected.empty())
                            selected.push_back(entry);

                        bool movedInsideBrowser = false;

                        int treeIndex = GetTreeIndexAt(me.GetX(), me.GetY());
                        if (treeIndex >= 0 && treeIndex < (int)m_TreeEntries.size())
                            movedInsideBrowser = MoveSelectedEntriesToDirectory(m_TreeEntries[treeIndex].Path);

                        if (!movedInsideBrowser)
                        {
                            int targetIndex = GetEntryIndexAt(me.GetX(), me.GetY());
                            if (targetIndex >= 0 && targetIndex < (int)m_ViewEntries.size() && m_ViewEntries[targetIndex].Type == AssetType::Folder)
                                movedInsideBrowser = MoveSelectedEntriesToDirectory(m_ViewEntries[targetIndex].Path);
                        }

                        if (!movedInsideBrowser && m_OnAssetDropped)
                        {
                            for (const AssetEntry& selectedEntry : selected)
                            {
                                if (selectedEntry.Type != AssetType::Folder)
                                    m_OnAssetDropped(selectedEntry.Path.string(), GetTypeKey(selectedEntry.Type), me.GetX(), me.GetY());
                            }
                        }
                    }

                    m_IsMouseDownOnEntry = false;
                    m_IsDraggingAsset = false;
                    m_DragEntryIndex = -1;
                    e.Handled = true;
                    return true;
                }
            }

            return WindowPanel::OnEvent(e);
        }
    }
}
