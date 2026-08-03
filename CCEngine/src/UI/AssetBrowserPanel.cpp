#include "UI/AssetBrowserPanel.h"
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>
#include "Core/AssetDatabase.h"
#include "Core/ConsoleLog.h"
#include "Editor/AssetUndoManager.h"
#include "Renderer/MaterialAsset.h"
#include "Renderer/MeshFactory.h"
#include "Renderer/MaterialPreviewRenderer.h"
#include "Renderer/PerspectiveCamera.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Renderer.h"
#include "Renderer/Renderer3D.h"
#include "Renderer/UIRenderer.h"
#include "Renderer/Texture.h"
#include "Scene/Components.h"
#include "Scripting/ScriptCompiler.h"
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
#include <array>
#include <cctype>
#include <cstring>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <unordered_set>

namespace CCEngine
{
    namespace UI
    {
        namespace
        {
            std::atomic<int> s_ActivePreviewLoads{ 0 };
            std::atomic<int> s_ActiveFbxMeshPreviewLoads{ 0 };
            constexpr float kToolbarButtonHeight = 22.0f;
            constexpr float kToolbarButtonGap = 8.0f;
            constexpr float kTypeFilterButtonWidth = 126.0f;
            constexpr float kSortButtonWidth = 102.0f;
            constexpr float kDropdownItemHeight = 26.0f;
            constexpr int kTypeFilterItemCount = 7;
            constexpr int kSortItemCount = 3;

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

            bool IsMaterialAssetPath(const std::filesystem::path& path)
            {
                std::string extension = path.extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                return extension == ".ccmat";
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
            constexpr const char* MaterialThumbnailAlgorithmVersion = "material-sphere-v8";
            constexpr int MaterialPreviewTextureSize = 384;

            void DrawPreviewBorder(float x, float y, float width, float height, const DirectX::XMFLOAT4& color)
            {
                // UIRenderer::DrawRect는 현재 채워진 사각형으로 동작한다.
                // 썸네일 위에는 전체 면이 아니라 얇은 테두리만 올려야 캡처 이미지가 가려지지 않는다.
                constexpr float thickness = 1.0f;
                UIRenderer::DrawRectFilled(x, y, width, thickness, color);
                UIRenderer::DrawRectFilled(x, y + height - thickness, width, thickness, color);
                UIRenderer::DrawRectFilled(x, y, thickness, height, color);
                UIRenderer::DrawRectFilled(x + width - thickness, y, thickness, height, color);
            }

            std::filesystem::path GetThumbnailDebugDirectory()
            {
                std::filesystem::path current = std::filesystem::current_path();
                std::filesystem::path localPath = current / "local" / "thumbnail_debug";
                if (std::filesystem::exists(localPath / "enable.txt"))
                    return localPath;

                std::filesystem::path parentLocalPath = current.parent_path() / "local" / "thumbnail_debug";
                if (std::filesystem::exists(parentLocalPath / "enable.txt"))
                    return parentLocalPath;

                return localPath;
            }

            bool IsThumbnailDebugDumpEnabled()
            {
                static const bool s_Enabled = std::filesystem::exists(GetThumbnailDebugDirectory() / "enable.txt");
                return s_Enabled;
            }

            std::string SanitizeDebugFileName(std::string value)
            {
                for (char& c : value)
                {
                    if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                        c = '_';
                }
                return value;
            }

            void WriteThumbnailDebugBmp(const std::filesystem::path& path, uint32_t width, uint32_t height, const std::vector<uint32_t>& pixels)
            {
                if (width == 0 || height == 0 || pixels.size() != (size_t)width * (size_t)height)
                    return;

                std::ofstream stream(path, std::ios::binary);
                if (!stream)
                    return;

                const uint32_t bytesPerPixel = 4;
                const uint32_t imageBytes = width * height * bytesPerPixel;
                const uint32_t fileBytes = 14 + 40 + imageBytes;

                auto writeU16 = [&](uint16_t value)
                    {
                        stream.put((char)(value & 0xff));
                        stream.put((char)((value >> 8) & 0xff));
                    };

                auto writeU32 = [&](uint32_t value)
                    {
                        stream.put((char)(value & 0xff));
                        stream.put((char)((value >> 8) & 0xff));
                        stream.put((char)((value >> 16) & 0xff));
                        stream.put((char)((value >> 24) & 0xff));
                    };

                writeU16(0x4d42);
                writeU32(fileBytes);
                writeU16(0);
                writeU16(0);
                writeU32(14 + 40);

                writeU32(40);
                writeU32(width);
                writeU32(height);
                writeU16(1);
                writeU16(32);
                writeU32(0);
                writeU32(imageBytes);
                writeU32(2835);
                writeU32(2835);
                writeU32(0);
                writeU32(0);

                for (int y = (int)height - 1; y >= 0; --y)
                {
                    for (uint32_t x = 0; x < width; ++x)
                    {
                        uint32_t pixel = pixels[(size_t)y * (size_t)width + (size_t)x];
                        uint8_t r = (uint8_t)(pixel & 0xff);
                        uint8_t g = (uint8_t)((pixel >> 8) & 0xff);
                        uint8_t b = (uint8_t)((pixel >> 16) & 0xff);
                        uint8_t a = (uint8_t)((pixel >> 24) & 0xff);

                        stream.put((char)b);
                        stream.put((char)g);
                        stream.put((char)r);
                        stream.put((char)a);
                    }
                }
            }

            void DumpThumbnailDebugImage(const std::string& label, uint32_t width, uint32_t height, const std::vector<uint32_t>& pixels)
            {
                if (!IsThumbnailDebugDumpEnabled())
                    return;

                static std::mutex s_DumpMutex;
                static std::unordered_set<std::string> s_DumpedLabels;

                std::lock_guard<std::mutex> lock(s_DumpMutex);
                if (s_DumpedLabels.contains(label) || s_DumpedLabels.size() >= 4)
                    return;

                s_DumpedLabels.insert(label);
                std::filesystem::create_directories(GetThumbnailDebugDirectory());

                std::ostringstream name;
                name << std::setw(2) << std::setfill('0') << s_DumpedLabels.size() << "_" << SanitizeDebugFileName(label) << ".bmp";

                // 썸네일 문제는 렌더, 크롭, GPU 텍스처 업로드 중 어디서 깨지는지 나눠서 봐야 한다.
                // 그래서 최종 UI가 아니라 썸네일 생성 중간 픽셀을 로컬 BMP로 남긴다.
                WriteThumbnailDebugBmp(GetThumbnailDebugDirectory() / name.str(), width, height, pixels);
            }

            void ForcePreviewAlphaOpaque(std::vector<uint32_t>& pixels)
            {
                for (uint32_t& pixel : pixels)
                    pixel = (pixel & 0x00ffffffu) | 0xff000000u;
            }

            void AppendThumbnailDebugLog(const std::string& message)
            {
                if (!IsThumbnailDebugDumpEnabled())
                    return;

                std::filesystem::create_directories(GetThumbnailDebugDirectory());
                std::ofstream stream(GetThumbnailDebugDirectory() / "trace.txt", std::ios::app);
                if (stream)
                    stream << message << "\n";
            }

            void AppendThumbnailDebugLogOnce(const std::string& key, const std::string& message)
            {
                if (!IsThumbnailDebugDumpEnabled())
                    return;

                static std::mutex s_LogOnceMutex;
                static std::unordered_set<std::string> s_LoggedKeys;

                std::lock_guard<std::mutex> lock(s_LogOnceMutex);
                if (!s_LoggedKeys.insert(key).second)
                    return;

                AppendThumbnailDebugLog(message);
            }

            std::shared_ptr<Mesh> CreatePreviewMeshForType(MeshComponent::MeshType type)
            {
                static std::unordered_map<int, std::shared_ptr<Mesh>> s_MeshCache;
                int key = static_cast<int>(type);
                auto found = s_MeshCache.find(key);
                if (found != s_MeshCache.end())
                    return found->second;

                std::shared_ptr<Mesh> mesh;
                switch (type)
                {
                    case MeshComponent::MeshType::Cube: mesh = MeshFactory::CreateCube(); break;
                    case MeshComponent::MeshType::Sphere: mesh = MeshFactory::CreateSphere(); break;
                    case MeshComponent::MeshType::Plane: mesh = MeshFactory::CreatePlane(); break;
                    case MeshComponent::MeshType::Quad: mesh = MeshFactory::CreateQuad(); break;
                    case MeshComponent::MeshType::Capsule: mesh = MeshFactory::CreateCapsule(); break;
                    case MeshComponent::MeshType::Cylinder: mesh = MeshFactory::CreateCylinder(); break;
                    case MeshComponent::MeshType::Torus: mesh = MeshFactory::CreateTorus(); break;
                    default: break;
                }

                if (mesh)
                    s_MeshCache[key] = mesh;
                return mesh;
            }

            DirectX::XMFLOAT3 ReadRenderPreviewFloat3(const nlohmann::json& data, const DirectX::XMFLOAT3& fallback)
            {
                if (!data.is_array() || data.size() < 3)
                    return fallback;
                return { data[0].get<float>(), data[1].get<float>(), data[2].get<float>() };
            }

            DirectX::XMFLOAT4 ReadRenderPreviewFloat4(const nlohmann::json& data, const DirectX::XMFLOAT4& fallback)
            {
                if (!data.is_array() || data.size() < 4)
                    return fallback;
                return { data[0].get<float>(), data[1].get<float>(), data[2].get<float>(), data[3].get<float>() };
            }

            DirectX::XMMATRIX ReadPrefabPreviewTransform(const nlohmann::json& entityData)
            {
                if (!entityData.contains("TransformComponent"))
                    return DirectX::XMMatrixIdentity();

                const auto& transformData = entityData["TransformComponent"];
                DirectX::XMFLOAT3 translation = transformData.contains("Translation")
                    ? ReadRenderPreviewFloat3(transformData["Translation"], { 0.0f, 0.0f, 0.0f })
                    : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
                DirectX::XMFLOAT3 rotation = transformData.contains("Rotation")
                    ? ReadRenderPreviewFloat3(transformData["Rotation"], { 0.0f, 0.0f, 0.0f })
                    : DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
                DirectX::XMFLOAT3 scale = transformData.contains("Scale")
                    ? ReadRenderPreviewFloat3(transformData["Scale"], { 1.0f, 1.0f, 1.0f })
                    : DirectX::XMFLOAT3{ 1.0f, 1.0f, 1.0f };

                DirectX::XMVECTOR quat = DirectX::XMQuaternionRotationRollPitchYaw(rotation.x, rotation.y, rotation.z);
                if (transformData.contains("QuaternionRotation"))
                {
                    DirectX::XMFLOAT4 storedQuat = ReadRenderPreviewFloat4(transformData["QuaternionRotation"], { 0.0f, 0.0f, 0.0f, 1.0f });
                    quat = DirectX::XMLoadFloat4(&storedQuat);
                }

                return DirectX::XMMatrixScaling(scale.x, scale.y, scale.z)
                    * DirectX::XMMatrixRotationQuaternion(quat)
                    * DirectX::XMMatrixTranslation(translation.x, translation.y, translation.z);
            }

            void ExpandPrefabPreviewBounds(const std::shared_ptr<Mesh>& mesh, const DirectX::XMMATRIX& transform, DirectX::XMFLOAT3& minBounds, DirectX::XMFLOAT3& maxBounds, bool& hasBounds)
            {
                if (!mesh)
                    return;

                for (const Vertex3D& vertex : mesh->GetVertices())
                {
                    DirectX::XMVECTOR position = DirectX::XMLoadFloat3(&vertex.Position);
                    position = DirectX::XMVector3TransformCoord(position, transform);
                    DirectX::XMFLOAT3 p;
                    DirectX::XMStoreFloat3(&p, position);

                    if (!hasBounds)
                    {
                        minBounds = p;
                        maxBounds = p;
                        hasBounds = true;
                    }
                    else
                    {
                        minBounds.x = (std::min)(minBounds.x, p.x);
                        minBounds.y = (std::min)(minBounds.y, p.y);
                        minBounds.z = (std::min)(minBounds.z, p.z);
                        maxBounds.x = (std::max)(maxBounds.x, p.x);
                        maxBounds.y = (std::max)(maxBounds.y, p.y);
                        maxBounds.z = (std::max)(maxBounds.z, p.z);
                    }
                }
            }

            bool HasVisibleMaterialPreviewPixels(const std::vector<uint32_t>& pixels)
            {
                if (pixels.empty())
                    return false;

                auto delta = [](uint8_t a, uint8_t b) -> int
                    {
                        return a > b ? (int)(a - b) : (int)(b - a);
                    };

                const uint32_t backgroundPixel = pixels.front();
                const uint8_t backgroundR = (uint8_t)(backgroundPixel & 0xff);
                const uint8_t backgroundG = (uint8_t)((backgroundPixel >> 8) & 0xff);
                const uint8_t backgroundB = (uint8_t)((backgroundPixel >> 16) & 0xff);

                size_t visiblePixels = 0;
                const size_t requiredVisiblePixels = (std::max)((size_t)64, pixels.size() / 100);
                for (uint32_t pixel : pixels)
                {
                    uint8_t r = (uint8_t)(pixel & 0xff);
                    uint8_t g = (uint8_t)((pixel >> 8) & 0xff);
                    uint8_t b = (uint8_t)((pixel >> 16) & 0xff);

                    // 프레임 모서리의 배경색과 실제로 다른 픽셀이 충분해야 렌더 성공으로 본다.
                    // 고정 색상과 비교하면 드라이버의 색 변환 때문에 빈 회색 화면도 성공으로 오인할 수 있다.
                    if (delta(r, backgroundR) + delta(g, backgroundG) + delta(b, backgroundB) >= 24)
                    {
                        if (++visiblePixels >= requiredVisiblePixels)
                            return true;
                    }
                }

                return false;
            }

            DecodedPreviewPixels CropMaterialPreviewToContent(uint32_t width, uint32_t height, const std::vector<uint32_t>& pixels, int outputSize)
            {
                DecodedPreviewPixels result;
                if (width == 0 || height == 0 || outputSize <= 0 || pixels.size() != (size_t)width * (size_t)height)
                    return result;

                auto delta = [](uint8_t a, uint8_t b) -> int
                    {
                        return a > b ? (int)(a - b) : (int)(b - a);
                    };

                const uint32_t backgroundPixel = pixels.front();
                const uint8_t backgroundR = (uint8_t)(backgroundPixel & 0xff);
                const uint8_t backgroundG = (uint8_t)((backgroundPixel >> 8) & 0xff);
                const uint8_t backgroundB = (uint8_t)((backgroundPixel >> 16) & 0xff);

                int minX = (int)width;
                int minY = (int)height;
                int maxX = -1;
                int maxY = -1;

                for (uint32_t y = 0; y < height; ++y)
                {
                    for (uint32_t x = 0; x < width; ++x)
                    {
                        uint32_t pixel = pixels[(size_t)y * (size_t)width + (size_t)x];
                        uint8_t r = (uint8_t)(pixel & 0xff);
                        uint8_t g = (uint8_t)((pixel >> 8) & 0xff);
                        uint8_t b = (uint8_t)((pixel >> 16) & 0xff);

                        if (delta(r, backgroundR) + delta(g, backgroundG) + delta(b, backgroundB) < 24)
                            continue;

                        minX = (std::min)(minX, (int)x);
                        minY = (std::min)(minY, (int)y);
                        maxX = (std::max)(maxX, (int)x);
                        maxY = (std::max)(maxY, (int)y);
                    }
                }

                if (maxX < minX || maxY < minY)
                    return result;

                int contentWidth = maxX - minX + 1;
                int contentHeight = maxY - minY + 1;
                if (contentWidth < 4 || contentHeight < 4)
                    return result;

                // 프레임 전체를 그대로 줄이면 물체가 한쪽 구석에 작게 찍힌 캡처도 성공 처리된다.
                // 배경과 다른 픽셀의 박스를 찾고 정사각형으로 다시 샘플링해서 브라우저 썸네일 중심을 맞춘다.
                int padding = (std::max)(6, (std::max)(contentWidth, contentHeight) / 8);
                float centerX = (float)(minX + maxX) * 0.5f;
                float centerY = (float)(minY + maxY) * 0.5f;
                float sourceSize = (float)((std::max)(contentWidth, contentHeight) + padding * 2);
                float sourceLeft = centerX - sourceSize * 0.5f;
                float sourceTop = centerY - sourceSize * 0.5f;

                result.Width = outputSize;
                result.Height = outputSize;
                result.Pixels.resize((size_t)outputSize * (size_t)outputSize);

                for (int y = 0; y < outputSize; ++y)
                {
                    float v = ((float)y + 0.5f) / (float)outputSize;
                    int sourceY = (std::clamp)((int)std::round(sourceTop + v * sourceSize), 0, (int)height - 1);
                    for (int x = 0; x < outputSize; ++x)
                    {
                        float u = ((float)x + 0.5f) / (float)outputSize;
                        int sourceX = (std::clamp)((int)std::round(sourceLeft + u * sourceSize), 0, (int)width - 1);
                        result.Pixels[(size_t)y * (size_t)outputSize + (size_t)x] = pixels[(size_t)sourceY * (size_t)width + (size_t)sourceX];
                    }
                }

                result.Success = HasVisibleMaterialPreviewPixels(result.Pixels);
                return result;
            }

            DecodedPreviewPixels ResizePreviewFrame(uint32_t width, uint32_t height, const std::vector<uint32_t>& pixels, int outputSize)
            {
                DecodedPreviewPixels result;
                if (width == 0 || height == 0 || outputSize <= 0 || pixels.size() != (size_t)width * (size_t)height)
                    return result;

                result.Width = outputSize;
                result.Height = outputSize;
                result.Pixels.resize((size_t)outputSize * (size_t)outputSize);

                for (int y = 0; y < outputSize; ++y)
                {
                    int sourceY = (std::clamp)((int)(((float)y + 0.5f) / (float)outputSize * (float)height), 0, (int)height - 1);
                    for (int x = 0; x < outputSize; ++x)
                    {
                        int sourceX = (std::clamp)((int)(((float)x + 0.5f) / (float)outputSize * (float)width), 0, (int)width - 1);
                        result.Pixels[(size_t)y * (size_t)outputSize + (size_t)x] = pixels[(size_t)sourceY * (size_t)width + (size_t)sourceX];
                    }
                }

                result.Success = HasVisibleMaterialPreviewPixels(result.Pixels);
                return result;
            }

            bool PathsReferToSameExistingFile(const std::filesystem::path& a, const std::filesystem::path& b)
            {
                std::error_code ec;
                if (a.empty() || b.empty() || !std::filesystem::exists(a, ec))
                    return false;
                ec.clear();
                if (!std::filesystem::exists(b, ec))
                    return false;

                ec.clear();
                if (std::filesystem::equivalent(a, b, ec) && !ec)
                    return true;

                ec.clear();
                auto ca = std::filesystem::weakly_canonical(a, ec);
                if (ec)
                    return false;
                ec.clear();
                auto cb = std::filesystem::weakly_canonical(b, ec);
                if (ec)
                    return false;

                return ca == cb;
            }

            std::filesystem::path NormalizeAssetPathForKey(const std::filesystem::path& path)
            {
                if (path.empty())
                    return {};

                std::error_code ec;
                std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
                if (!ec)
                    return canonical;

                ec.clear();
                std::filesystem::path absolute = std::filesystem::absolute(path, ec);
                if (!ec)
                    return absolute;

                return path;
            }

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
                const std::streamsize byteCount = (std::streamsize)(outPixels.Pixels.size() * sizeof(uint32_t));
                input.read(reinterpret_cast<char*>(outPixels.Pixels.data()), byteCount);

                // 캐시 파일은 정확히 픽셀 끝에서 EOF가 걸릴 수 있다.
                // 스트림 상태보다 실제 읽은 바이트를 기준으로 판단해야 정상 캐시가 실패 처리되지 않는다.
                outPixels.Success = input.gcount() == byteCount;
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

            DecodedPreviewPixels GenerateMaterialPreviewPixels(const std::filesystem::path& path, int size)
            {
                DecodedPreviewPixels result;
                if (size <= 0)
                    return result;

                DirectX::XMFLOAT4 albedo = { 1.0f, 1.0f, 1.0f, 1.0f };
                float roughness = 0.5f;
                float metallic = 0.0f;
                std::filesystem::path albedoTexturePath;

                std::ifstream input(path);
                if (!input)
                    return result;

                try
                {
                    nlohmann::json root;
                    input >> root;
                    const nlohmann::json& data = root.contains("Material") && root["Material"].is_object() ? root["Material"] : root;
                    if (data.contains("AlbedoColor") && data["AlbedoColor"].is_array() && data["AlbedoColor"].size() >= 4)
                    {
                        albedo = {
                            data["AlbedoColor"][0].get<float>(),
                            data["AlbedoColor"][1].get<float>(),
                            data["AlbedoColor"][2].get<float>(),
                            data["AlbedoColor"][3].get<float>()
                        };
                    }
                    roughness = std::clamp(data.value("Roughness", roughness), 0.0f, 1.0f);
                    metallic = std::clamp(data.value("Metallic", metallic), 0.0f, 1.0f);
                    std::string albedoGuid = data.value("AlbedoTextureGuid", "");
                    albedoTexturePath = data.value("AlbedoTexturePath", "");
                    if (!albedoGuid.empty())
                    {
                        std::filesystem::path resolvedByGuid = AssetDatabase::GetPathFromGuid(albedoGuid);
                        if (!resolvedByGuid.empty())
                            albedoTexturePath = resolvedByGuid;
                    }
                }
                catch (...)
                {
                    return result;
                }

                int textureWidth = 0;
                int textureHeight = 0;
                int textureChannels = 0;
                stbi_uc* texturePixels = nullptr;
                if (!albedoTexturePath.empty())
                {
                    std::filesystem::path resolvedTexturePath = albedoTexturePath;
                    if (!std::filesystem::exists(resolvedTexturePath))
                        resolvedTexturePath = path.parent_path() / albedoTexturePath;

                    if (std::filesystem::exists(resolvedTexturePath))
                        texturePixels = stbi_load(resolvedTexturePath.string().c_str(), &textureWidth, &textureHeight, &textureChannels, 4);
                }

                result.Width = size;
                result.Height = size;
                result.Pixels.resize((size_t)size * (size_t)size);

                for (int y = 0; y < size; ++y)
                {
                    float t = (float)y / (float)(std::max)(1, size - 1);
                    uint8_t background = (uint8_t)(22 + (int)(t * 12.0f));
                    uint8_t blue = (uint8_t)(background + 4);
                    for (int x = 0; x < size; ++x)
                        result.Pixels[(size_t)y * (size_t)size + (size_t)x] = PackPreviewPixel(background, background, blue);
                }

                const float centerX = (float)size * 0.5f;
                const float centerY = (float)size * 0.47f;
                const float radius = (float)size * 0.36f;
                constexpr float Pi = 3.14159265358979323846f;

                for (int y = 0; y < size; ++y)
                {
                    for (int x = 0; x < size; ++x)
                    {
                        float normalX = ((float)x + 0.5f - centerX) / radius;
                        float normalY = -(((float)y + 0.5f - centerY) / radius);
                        float radiusSquared = normalX * normalX + normalY * normalY;
                        if (radiusSquared > 1.0f)
                            continue;

                        float normalZ = std::sqrt((std::max)(0.0f, 1.0f - radiusSquared));
                        float textureR = 1.0f;
                        float textureG = 1.0f;
                        float textureB = 1.0f;

                        if (texturePixels && textureWidth > 0 && textureHeight > 0)
                        {
                            float u = 0.5f + std::atan2(normalX, normalZ) / (2.0f * Pi);
                            float v = 0.5f - std::asin((std::clamp)(normalY, -1.0f, 1.0f)) / Pi;
                            int textureX = (std::clamp)((int)(u * (float)(textureWidth - 1)), 0, textureWidth - 1);
                            int textureY = (std::clamp)((int)(v * (float)(textureHeight - 1)), 0, textureHeight - 1);
                            const stbi_uc* pixel = texturePixels + ((textureY * textureWidth + textureX) * 4);
                            textureR = (float)pixel[0] / 255.0f;
                            textureG = (float)pixel[1] / 255.0f;
                            textureB = (float)pixel[2] / 255.0f;
                        }

                        float lightX = -0.38f;
                        float lightY = 0.62f;
                        float lightZ = 0.69f;
                        float diffuse = (std::max)(0.0f, normalX * lightX + normalY * lightY + normalZ * lightZ);
                        // 작은 브라우저 아이콘에서는 어두운 조명이 곧바로 회색 덩어리처럼 보인다.
                        // 실제 재질 색은 유지하되 프리뷰용 기본광을 조금 올려 텍스처 무늬가 남도록 한다.
                        float lighting = 0.34f + diffuse * 0.86f;

                        float halfX = -0.20f;
                        float halfY = 0.33f;
                        float halfZ = 0.92f;
                        float specularBase = (std::max)(0.0f, normalX * halfX + normalY * halfY + normalZ * halfZ);
                        float specularPower = 6.0f + (1.0f - roughness) * 58.0f;
                        float specular = std::pow(specularBase, specularPower) * (0.12f + metallic * 0.65f);

                        float albedoR = (std::clamp)(albedo.x, 0.0f, 1.0f);
                        float albedoG = (std::clamp)(albedo.y, 0.0f, 1.0f);
                        float albedoB = (std::clamp)(albedo.z, 0.0f, 1.0f);
                        if (texturePixels)
                        {
                            // 썸네일은 재질을 식별하는 용도다.
                            // 순색 틴트가 텍스처 무늬를 완전히 지우지 않도록 프리뷰에서만 최소 채널을 둔다.
                            constexpr float textureVisibility = 0.78f;
                            constexpr float tintStrength = 1.0f - textureVisibility;
                            albedoR = textureVisibility + albedoR * tintStrength;
                            albedoG = textureVisibility + albedoG * tintStrength;
                            albedoB = textureVisibility + albedoB * tintStrength;
                            textureR = std::pow((std::clamp)(textureR, 0.0f, 1.0f), 0.82f);
                            textureG = std::pow((std::clamp)(textureG, 0.0f, 1.0f), 0.82f);
                            textureB = std::pow((std::clamp)(textureB, 0.0f, 1.0f), 0.82f);
                        }

                        float red = textureR * albedoR * lighting + specular;
                        float green = textureG * albedoG * lighting + specular;
                        float blue = textureB * albedoB * lighting + specular;
                        result.Pixels[(size_t)y * (size_t)size + (size_t)x] = PackPreviewPixel(
                            (uint8_t)((std::clamp)(red, 0.0f, 1.0f) * 255.0f),
                            (uint8_t)((std::clamp)(green, 0.0f, 1.0f) * 255.0f),
                            (uint8_t)((std::clamp)(blue, 0.0f, 1.0f) * 255.0f));
                    }
                }

                if (texturePixels)
                    stbi_image_free(texturePixels);

                // 브라우저 썸네일은 렌더 스레드를 점유하지 않도록 픽셀만 만든다.
                // 완성된 결과는 다른 에셋과 같은 업로드 큐와 디스크 캐시를 사용한다.
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

            float GetPreviewPercentile(std::vector<float>& values, float percentile);

            struct PrefabPreviewVertex
            {
                DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
                DirectX::XMFLOAT3 Normal = { 0.0f, 1.0f, 0.0f };
            };

            struct PrefabPreviewMesh
            {
                MeshComponent::MeshType Type = MeshComponent::MeshType::Cube;
                DirectX::XMFLOAT3 Translation = { 0.0f, 0.0f, 0.0f };
                DirectX::XMFLOAT3 Rotation = { 0.0f, 0.0f, 0.0f };
                DirectX::XMFLOAT3 Scale = { 1.0f, 1.0f, 1.0f };
                DirectX::XMFLOAT4 Color = { 0.55f, 0.70f, 1.0f, 1.0f };
                std::vector<PrefabPreviewVertex> Vertices;
                std::vector<uint32_t> Indices;
            };

            struct PrefabProjectedVertex
            {
                float X = 0.0f;
                float Y = 0.0f;
                float Depth = 0.0f;
                DirectX::XMFLOAT3 Normal = { 0.0f, 1.0f, 0.0f };
            };

            struct PrefabPreviewTriangle
            {
                PrefabProjectedVertex A;
                PrefabProjectedVertex B;
                PrefabProjectedVertex C;
                DirectX::XMFLOAT4 Color = { 1.0f, 1.0f, 1.0f, 1.0f };
            };

            DirectX::XMFLOAT3 ReadPreviewFloat3(const nlohmann::json& data, const DirectX::XMFLOAT3& fallback)
            {
                if (!data.is_array() || data.size() < 3)
                    return fallback;

                return {
                    data[0].get<float>(),
                    data[1].get<float>(),
                    data[2].get<float>()
                };
            }

            DirectX::XMFLOAT4 ReadPreviewFloat4(const nlohmann::json& data, const DirectX::XMFLOAT4& fallback)
            {
                if (!data.is_array() || data.size() < 4)
                    return fallback;

                return {
                    data[0].get<float>(),
                    data[1].get<float>(),
                    data[2].get<float>(),
                    data[3].get<float>()
                };
            }

            MeshComponent::MeshType ReadPrefabPreviewMeshType(const nlohmann::json& meshData)
            {
                int type = meshData.value("Type", (int)MeshComponent::MeshType::Cube);
                if (type < (int)MeshComponent::MeshType::Custom || type >(int)MeshComponent::MeshType::Torus)
                    return MeshComponent::MeshType::Cube;

                return (MeshComponent::MeshType)type;
            }

            DirectX::XMFLOAT4 ReadPrefabPreviewMaterialColor(const nlohmann::json& meshData, const DirectX::XMFLOAT4& fallback)
            {
                std::string materialPath;
                std::string materialGuid = meshData.value("MaterialGuid", "");
                if (!materialGuid.empty())
                    materialPath = AssetDatabase::GetPathFromGuid(materialGuid).string();
                if (materialPath.empty())
                    materialPath = meshData.value("MaterialPath", "");
                if (materialPath.empty() || !std::filesystem::exists(materialPath))
                    return fallback;

                std::ifstream materialFile(materialPath);
                if (!materialFile.is_open())
                    return fallback;

                try
                {
                    nlohmann::json root;
                    materialFile >> root;
                    const nlohmann::json& materialData = root.contains("Material") ? root["Material"] : root;
                    if (materialData.contains("AlbedoColor"))
                        return ReadPreviewFloat4(materialData["AlbedoColor"], fallback);
                }
                catch (...)
                {
                }

                return fallback;
            }

            void AddPrefabPreviewVertex(std::vector<PrefabPreviewVertex>& vertices, float x, float y, float z, float nx, float ny, float nz)
            {
                vertices.push_back({ { x, y, z }, { nx, ny, nz } });
            }

            void BuildPrefabCubeMesh(PrefabPreviewMesh& mesh)
            {
                auto& v = mesh.Vertices;
                auto& i = mesh.Indices;
                const float h = 0.5f;
                AddPrefabPreviewVertex(v, -h, -h, -h, 0.0f, 0.0f, -1.0f);
                AddPrefabPreviewVertex(v,  h, -h, -h, 0.0f, 0.0f, -1.0f);
                AddPrefabPreviewVertex(v,  h,  h, -h, 0.0f, 0.0f, -1.0f);
                AddPrefabPreviewVertex(v, -h,  h, -h, 0.0f, 0.0f, -1.0f);
                AddPrefabPreviewVertex(v,  h, -h,  h, 0.0f, 0.0f, 1.0f);
                AddPrefabPreviewVertex(v, -h, -h,  h, 0.0f, 0.0f, 1.0f);
                AddPrefabPreviewVertex(v, -h,  h,  h, 0.0f, 0.0f, 1.0f);
                AddPrefabPreviewVertex(v,  h,  h,  h, 0.0f, 0.0f, 1.0f);
                AddPrefabPreviewVertex(v, -h,  h, -h, 0.0f, 1.0f, 0.0f);
                AddPrefabPreviewVertex(v,  h,  h, -h, 0.0f, 1.0f, 0.0f);
                AddPrefabPreviewVertex(v,  h,  h,  h, 0.0f, 1.0f, 0.0f);
                AddPrefabPreviewVertex(v, -h,  h,  h, 0.0f, 1.0f, 0.0f);
                AddPrefabPreviewVertex(v, -h, -h,  h, 0.0f, -1.0f, 0.0f);
                AddPrefabPreviewVertex(v,  h, -h,  h, 0.0f, -1.0f, 0.0f);
                AddPrefabPreviewVertex(v,  h, -h, -h, 0.0f, -1.0f, 0.0f);
                AddPrefabPreviewVertex(v, -h, -h, -h, 0.0f, -1.0f, 0.0f);
                AddPrefabPreviewVertex(v,  h, -h, -h, 1.0f, 0.0f, 0.0f);
                AddPrefabPreviewVertex(v,  h, -h,  h, 1.0f, 0.0f, 0.0f);
                AddPrefabPreviewVertex(v,  h,  h,  h, 1.0f, 0.0f, 0.0f);
                AddPrefabPreviewVertex(v,  h,  h, -h, 1.0f, 0.0f, 0.0f);
                AddPrefabPreviewVertex(v, -h, -h,  h, -1.0f, 0.0f, 0.0f);
                AddPrefabPreviewVertex(v, -h, -h, -h, -1.0f, 0.0f, 0.0f);
                AddPrefabPreviewVertex(v, -h,  h, -h, -1.0f, 0.0f, 0.0f);
                AddPrefabPreviewVertex(v, -h,  h,  h, -1.0f, 0.0f, 0.0f);
                i = {
                    0, 3, 2, 2, 1, 0,
                    4, 7, 6, 6, 5, 4,
                    8, 11, 10, 10, 9, 8,
                    12, 15, 14, 14, 13, 12,
                    16, 19, 18, 18, 17, 16,
                    20, 23, 22, 22, 21, 20
                };
            }

            void BuildPrefabPlaneMesh(PrefabPreviewMesh& mesh, bool vertical)
            {
                auto& v = mesh.Vertices;
                auto& i = mesh.Indices;
                if (vertical)
                {
                    AddPrefabPreviewVertex(v, -0.5f, -0.5f, 0.0f, 0.0f, 0.0f, -1.0f);
                    AddPrefabPreviewVertex(v,  0.5f, -0.5f, 0.0f, 0.0f, 0.0f, -1.0f);
                    AddPrefabPreviewVertex(v,  0.5f,  0.5f, 0.0f, 0.0f, 0.0f, -1.0f);
                    AddPrefabPreviewVertex(v, -0.5f,  0.5f, 0.0f, 0.0f, 0.0f, -1.0f);
                }
                else
                {
                    AddPrefabPreviewVertex(v, -0.5f, 0.0f, -0.5f, 0.0f, 1.0f, 0.0f);
                    AddPrefabPreviewVertex(v,  0.5f, 0.0f, -0.5f, 0.0f, 1.0f, 0.0f);
                    AddPrefabPreviewVertex(v,  0.5f, 0.0f,  0.5f, 0.0f, 1.0f, 0.0f);
                    AddPrefabPreviewVertex(v, -0.5f, 0.0f,  0.5f, 0.0f, 1.0f, 0.0f);
                }
                i = { 0, 3, 2, 2, 1, 0 };
            }

            void BuildPrefabSphereLikeMesh(PrefabPreviewMesh& mesh, float radius, int slices, int stacks)
            {
                auto& v = mesh.Vertices;
                auto& i = mesh.Indices;
                slices = (std::max)(slices, 6);
                stacks = (std::max)(stacks, 4);
                for (int stack = 0; stack <= stacks; ++stack)
                {
                    float phi = DirectX::XM_PI * (float)stack / (float)stacks;
                    for (int slice = 0; slice <= slices; ++slice)
                    {
                        float theta = DirectX::XM_2PI * (float)slice / (float)slices;
                        float x = radius * std::sin(phi) * std::cos(theta);
                        float y = radius * std::cos(phi);
                        float z = radius * std::sin(phi) * std::sin(theta);
                        AddPrefabPreviewVertex(v, x, y, z, x / radius, y / radius, z / radius);
                    }
                }
                for (int stack = 0; stack < stacks; ++stack)
                {
                    for (int slice = 0; slice < slices; ++slice)
                    {
                        uint32_t first = (uint32_t)(stack * (slices + 1) + slice);
                        uint32_t second = first + (uint32_t)slices + 1;
                        i.insert(i.end(), { first, first + 1, second, second, first + 1, second + 1 });
                    }
                }
            }

            void BuildPrefabCylinderMesh(PrefabPreviewMesh& mesh, float radius, float height, int segments)
            {
                auto& v = mesh.Vertices;
                auto& i = mesh.Indices;
                segments = (std::max)(segments, 6);
                float half = height * 0.5f;
                for (int segment = 0; segment <= segments; ++segment)
                {
                    float theta = DirectX::XM_2PI * (float)segment / (float)segments;
                    float x = radius * std::cos(theta);
                    float z = radius * std::sin(theta);
                    AddPrefabPreviewVertex(v, x, -half, z, std::cos(theta), 0.0f, std::sin(theta));
                    AddPrefabPreviewVertex(v, x,  half, z, std::cos(theta), 0.0f, std::sin(theta));
                }
                for (int segment = 0; segment < segments; ++segment)
                {
                    uint32_t first = (uint32_t)segment * 2;
                    i.insert(i.end(), { first, first + 1, first + 2, first + 2, first + 1, first + 3 });
                }

                uint32_t topCenter = (uint32_t)v.size();
                AddPrefabPreviewVertex(v, 0.0f, half, 0.0f, 0.0f, 1.0f, 0.0f);
                uint32_t bottomCenter = (uint32_t)v.size();
                AddPrefabPreviewVertex(v, 0.0f, -half, 0.0f, 0.0f, -1.0f, 0.0f);
                for (int segment = 0; segment <= segments; ++segment)
                {
                    float theta = DirectX::XM_2PI * (float)segment / (float)segments;
                    float x = radius * std::cos(theta);
                    float z = radius * std::sin(theta);
                    AddPrefabPreviewVertex(v, x, half, z, 0.0f, 1.0f, 0.0f);
                    AddPrefabPreviewVertex(v, x, -half, z, 0.0f, -1.0f, 0.0f);
                }
                uint32_t capStart = topCenter + 2;
                for (int segment = 0; segment < segments; ++segment)
                {
                    uint32_t topA = capStart + (uint32_t)segment * 2;
                    uint32_t topB = topA + 2;
                    uint32_t bottomA = topA + 1;
                    uint32_t bottomB = topB + 1;
                    i.insert(i.end(), { topCenter, topB, topA, bottomCenter, bottomA, bottomB });
                }
            }

            void BuildPrefabTorusMesh(PrefabPreviewMesh& mesh, float majorRadius, float minorRadius, int radialSegments, int tubularSegments)
            {
                auto& v = mesh.Vertices;
                auto& i = mesh.Indices;
                radialSegments = (std::max)(radialSegments, 8);
                tubularSegments = (std::max)(tubularSegments, 6);
                for (int ring = 0; ring <= radialSegments; ++ring)
                {
                    float u = DirectX::XM_2PI * (float)ring / (float)radialSegments;
                    for (int tube = 0; tube <= tubularSegments; ++tube)
                    {
                        float t = DirectX::XM_2PI * (float)tube / (float)tubularSegments;
                        float x = (majorRadius + minorRadius * std::cos(t)) * std::cos(u);
                        float y = (majorRadius + minorRadius * std::cos(t)) * std::sin(u);
                        float z = minorRadius * std::sin(t);
                        AddPrefabPreviewVertex(v, x, y, z, std::cos(t) * std::cos(u), std::cos(t) * std::sin(u), std::sin(t));
                    }
                }
                for (int ring = 0; ring < radialSegments; ++ring)
                {
                    for (int tube = 0; tube < tubularSegments; ++tube)
                    {
                        uint32_t a = (uint32_t)(ring * (tubularSegments + 1) + tube);
                        uint32_t b = a + (uint32_t)tubularSegments + 1;
                        uint32_t c = a + 1;
                        uint32_t d = b + 1;
                        i.insert(i.end(), { a, b, c, c, b, d });
                    }
                }
            }

            void BuildPrefabPrimitiveMesh(PrefabPreviewMesh& mesh)
            {
                switch (mesh.Type)
                {
                    case MeshComponent::MeshType::Sphere:
                        BuildPrefabSphereLikeMesh(mesh, 0.5f, 14, 8);
                        break;
                    case MeshComponent::MeshType::Plane:
                        BuildPrefabPlaneMesh(mesh, false);
                        mesh.Scale.x *= 2.0f;
                        mesh.Scale.z *= 2.0f;
                        break;
                    case MeshComponent::MeshType::Quad:
                        BuildPrefabPlaneMesh(mesh, true);
                        break;
                    case MeshComponent::MeshType::Capsule:
                        BuildPrefabSphereLikeMesh(mesh, 0.5f, 12, 8);
                        mesh.Scale.y *= 1.65f;
                        break;
                    case MeshComponent::MeshType::Cylinder:
                        BuildPrefabCylinderMesh(mesh, 0.5f, 1.0f, 14);
                        break;
                    case MeshComponent::MeshType::Torus:
                        BuildPrefabTorusMesh(mesh, 0.38f, 0.14f, 18, 8);
                        break;
                    case MeshComponent::MeshType::Cube:
                    default:
                        BuildPrefabCubeMesh(mesh);
                        break;
                }
            }

            bool CollectPrefabPrimitivePreviewMeshes(const nlohmann::json& data, std::vector<PrefabPreviewMesh>& meshes)
            {
                if (!data.contains("Entities") || !data["Entities"].is_array())
                    return false;

                for (const auto& entityData : data["Entities"])
                {
                    if (!entityData.contains("MeshComponent"))
                        continue;

                    const auto& meshData = entityData["MeshComponent"];
                    PrefabPreviewMesh mesh;
                    mesh.Type = ReadPrefabPreviewMeshType(meshData);
                    if (mesh.Type == MeshComponent::MeshType::Custom)
                        continue;

                    mesh.Color = ReadPreviewFloat4(meshData.value("BaseColor", nlohmann::json::array()), mesh.Color);
                    mesh.Color = ReadPrefabPreviewMaterialColor(meshData, mesh.Color);
                    if (entityData.contains("TransformComponent"))
                    {
                        const auto& transformData = entityData["TransformComponent"];
                        mesh.Translation = ReadPreviewFloat3(transformData.value("Translation", nlohmann::json::array()), mesh.Translation);
                        mesh.Rotation = ReadPreviewFloat3(transformData.value("Rotation", nlohmann::json::array()), mesh.Rotation);
                        mesh.Scale = ReadPreviewFloat3(transformData.value("Scale", nlohmann::json::array()), mesh.Scale);
                    }

                    BuildPrefabPrimitiveMesh(mesh);
                    if (!mesh.Vertices.empty() && mesh.Indices.size() >= 3)
                        meshes.push_back(std::move(mesh));
                }

                return !meshes.empty();
            }

            DirectX::XMFLOAT3 TransformPrefabPreviewNormal(const DirectX::XMFLOAT3& normal, const DirectX::XMMATRIX& transform)
            {
                DirectX::XMVECTOR n = DirectX::XMLoadFloat3(&normal);
                n = DirectX::XMVector3Normalize(DirectX::XMVector3TransformNormal(n, transform));
                DirectX::XMFLOAT3 result;
                DirectX::XMStoreFloat3(&result, n);
                return result;
            }

            DirectX::XMFLOAT3 TransformPrefabPreviewPosition(const DirectX::XMFLOAT3& position, const DirectX::XMMATRIX& transform)
            {
                DirectX::XMVECTOR p = DirectX::XMLoadFloat3(&position);
                p = DirectX::XMVector3TransformCoord(p, transform);
                DirectX::XMFLOAT3 result;
                DirectX::XMStoreFloat3(&result, p);
                return result;
            }

            PreviewPoint ProjectPrefabPreviewRaw(const DirectX::XMFLOAT3& position)
            {
                return {
                    (position.x - position.z) * 0.92f,
                    (position.x + position.z) * 0.42f - position.y * 0.92f
                };
            }

            uint32_t ShadePrefabPreviewColor(const DirectX::XMFLOAT4& baseColor, const DirectX::XMFLOAT3& normal)
            {
                DirectX::XMVECTOR light = DirectX::XMVector3Normalize(DirectX::XMVectorSet(-0.45f, 0.72f, -0.54f, 0.0f));
                DirectX::XMVECTOR n = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&normal));
                float diffuse = (std::max)(0.0f, DirectX::XMVectorGetX(DirectX::XMVector3Dot(n, light)));
                float lighting = 0.38f + diffuse * 0.72f;
                return PackPreviewPixel(
                    (uint8_t)((std::clamp)(baseColor.x * lighting, 0.0f, 1.0f) * 255.0f),
                    (uint8_t)((std::clamp)(baseColor.y * lighting, 0.0f, 1.0f) * 255.0f),
                    (uint8_t)((std::clamp)(baseColor.z * lighting, 0.0f, 1.0f) * 255.0f));
            }

            void FillPrefabPreviewTriangle(DecodedPreviewPixels& image, std::vector<float>& depthBuffer, const PrefabPreviewTriangle& triangle)
            {
                float minX = (std::min)({ triangle.A.X, triangle.B.X, triangle.C.X });
                float maxX = (std::max)({ triangle.A.X, triangle.B.X, triangle.C.X });
                float minY = (std::min)({ triangle.A.Y, triangle.B.Y, triangle.C.Y });
                float maxY = (std::max)({ triangle.A.Y, triangle.B.Y, triangle.C.Y });

                int x0 = (std::max)(0, (int)std::floor(minX));
                int x1 = (std::min)(image.Width - 1, (int)std::ceil(maxX));
                int y0 = (std::max)(0, (int)std::floor(minY));
                int y1 = (std::min)(image.Height - 1, (int)std::ceil(maxY));

                float area =
                    (triangle.B.X - triangle.A.X) * (triangle.C.Y - triangle.A.Y) -
                    (triangle.B.Y - triangle.A.Y) * (triangle.C.X - triangle.A.X);
                if (std::abs(area) < 0.0001f)
                    return;

                DirectX::XMFLOAT3 faceNormal = {
                    (triangle.A.Normal.x + triangle.B.Normal.x + triangle.C.Normal.x) / 3.0f,
                    (triangle.A.Normal.y + triangle.B.Normal.y + triangle.C.Normal.y) / 3.0f,
                    (triangle.A.Normal.z + triangle.B.Normal.z + triangle.C.Normal.z) / 3.0f
                };
                uint32_t color = ShadePrefabPreviewColor(triangle.Color, faceNormal);

                for (int y = y0; y <= y1; ++y)
                {
                    for (int x = x0; x <= x1; ++x)
                    {
                        float px = (float)x + 0.5f;
                        float py = (float)y + 0.5f;
                        float w0 = ((triangle.B.X - px) * (triangle.C.Y - py) - (triangle.B.Y - py) * (triangle.C.X - px)) / area;
                        float w1 = ((triangle.C.X - px) * (triangle.A.Y - py) - (triangle.C.Y - py) * (triangle.A.X - px)) / area;
                        float w2 = 1.0f - w0 - w1;
                        if (w0 < -0.001f || w1 < -0.001f || w2 < -0.001f)
                            continue;

                        float depth = triangle.A.Depth * w0 + triangle.B.Depth * w1 + triangle.C.Depth * w2;
                        size_t pixelIndex = (size_t)y * (size_t)image.Width + (size_t)x;
                        if (depth < depthBuffer[pixelIndex])
                            continue;

                        depthBuffer[pixelIndex] = depth;
                        image.Pixels[pixelIndex] = color;
                    }
                }
            }

            DecodedPreviewPixels GeneratePrefabPrimitivePreviewPixels(const std::filesystem::path& path, const std::vector<PrefabPreviewMesh>& meshes, int size)
            {
                DecodedPreviewPixels result;
                result.Width = size;
                result.Height = size;
                result.Pixels.resize((size_t)size * (size_t)size);
                std::vector<float> depthBuffer(result.Pixels.size(), -std::numeric_limits<float>::infinity());

                for (int y = 0; y < size; ++y)
                {
                    for (int x = 0; x < size; ++x)
                    {
                        float t = (float)y / (float)(std::max)(1, size - 1);
                        uint8_t bg = (uint8_t)(25 + (int)(t * 17.0f));
                        result.Pixels[(size_t)y * (size_t)size + (size_t)x] = PackPreviewPixel(bg, bg, (uint8_t)(bg + 5));
                    }
                }

                std::vector<DirectX::XMFLOAT3> worldPositions;
                std::vector<PrefabPreviewTriangle> triangles;
                for (const PrefabPreviewMesh& mesh : meshes)
                {
                    DirectX::XMMATRIX transform =
                        DirectX::XMMatrixScaling(mesh.Scale.x, mesh.Scale.y, mesh.Scale.z) *
                        DirectX::XMMatrixRotationRollPitchYaw(mesh.Rotation.x, mesh.Rotation.y, mesh.Rotation.z) *
                        DirectX::XMMatrixTranslation(mesh.Translation.x, mesh.Translation.y, mesh.Translation.z);

                    std::vector<DirectX::XMFLOAT3> transformedPositions;
                    std::vector<DirectX::XMFLOAT3> transformedNormals;
                    transformedPositions.reserve(mesh.Vertices.size());
                    transformedNormals.reserve(mesh.Vertices.size());
                    for (const PrefabPreviewVertex& vertex : mesh.Vertices)
                    {
                        transformedPositions.push_back(TransformPrefabPreviewPosition(vertex.Position, transform));
                        transformedNormals.push_back(TransformPrefabPreviewNormal(vertex.Normal, transform));
                        worldPositions.push_back(transformedPositions.back());
                    }

                    for (size_t index = 0; index + 2 < mesh.Indices.size(); index += 3)
                    {
                        uint32_t ia = mesh.Indices[index + 0];
                        uint32_t ib = mesh.Indices[index + 1];
                        uint32_t ic = mesh.Indices[index + 2];
                        if (ia >= transformedPositions.size() || ib >= transformedPositions.size() || ic >= transformedPositions.size())
                            continue;

                        PrefabPreviewTriangle triangle;
                        triangle.Color = mesh.Color;
                        triangle.A.Normal = transformedNormals[ia];
                        triangle.B.Normal = transformedNormals[ib];
                        triangle.C.Normal = transformedNormals[ic];
                        triangle.A.Depth = transformedPositions[ia].x + transformedPositions[ia].z - transformedPositions[ia].y * 0.25f;
                        triangle.B.Depth = transformedPositions[ib].x + transformedPositions[ib].z - transformedPositions[ib].y * 0.25f;
                        triangle.C.Depth = transformedPositions[ic].x + transformedPositions[ic].z - transformedPositions[ic].y * 0.25f;
                        PreviewPoint pa = ProjectPrefabPreviewRaw(transformedPositions[ia]);
                        PreviewPoint pb = ProjectPrefabPreviewRaw(transformedPositions[ib]);
                        PreviewPoint pc = ProjectPrefabPreviewRaw(transformedPositions[ic]);
                        triangle.A.X = pa.X; triangle.A.Y = pa.Y;
                        triangle.B.X = pb.X; triangle.B.Y = pb.Y;
                        triangle.C.X = pc.X; triangle.C.Y = pc.Y;
                        triangles.push_back(triangle);
                    }
                }

                if (worldPositions.empty() || triangles.empty())
                    return GenerateModelOrPrefabPreviewPixels(path, true, size);

                std::vector<float> projectedXs;
                std::vector<float> projectedYs;
                projectedXs.reserve(worldPositions.size());
                projectedYs.reserve(worldPositions.size());
                for (const DirectX::XMFLOAT3& position : worldPositions)
                {
                    PreviewPoint projected = ProjectPrefabPreviewRaw(position);
                    projectedXs.push_back(projected.X);
                    projectedYs.push_back(projected.Y);
                }

                PreviewPoint minProjected = { GetPreviewPercentile(projectedXs, 0.01f), GetPreviewPercentile(projectedYs, 0.01f) };
                PreviewPoint maxProjected = { GetPreviewPercentile(projectedXs, 0.99f), GetPreviewPercentile(projectedYs, 0.99f) };
                float projectedWidth = (std::max)(0.001f, maxProjected.X - minProjected.X);
                float projectedHeight = (std::max)(0.001f, maxProjected.Y - minProjected.Y);
                float padding = (float)size * 0.17f;
                float fitScale = (std::min)((size - padding * 2.0f) / projectedWidth, (size - padding * 2.0f) / projectedHeight);

                for (PrefabPreviewTriangle& triangle : triangles)
                {
                    triangle.A.X = padding + (triangle.A.X - minProjected.X) * fitScale;
                    triangle.A.Y = padding + (triangle.A.Y - minProjected.Y) * fitScale;
                    triangle.B.X = padding + (triangle.B.X - minProjected.X) * fitScale;
                    triangle.B.Y = padding + (triangle.B.Y - minProjected.Y) * fitScale;
                    triangle.C.X = padding + (triangle.C.X - minProjected.X) * fitScale;
                    triangle.C.Y = padding + (triangle.C.Y - minProjected.Y) * fitScale;
                    FillPrefabPreviewTriangle(result, depthBuffer, triangle);
                }

                uint32_t outline = PackPreviewPixel(24, 27, 34);
                for (const PrefabPreviewTriangle& triangle : triangles)
                {
                    DrawPreviewLine(result, (int)triangle.A.X, (int)triangle.A.Y, (int)triangle.B.X, (int)triangle.B.Y, outline);
                    DrawPreviewLine(result, (int)triangle.B.X, (int)triangle.B.Y, (int)triangle.C.X, (int)triangle.C.Y, outline);
                    DrawPreviewLine(result, (int)triangle.C.X, (int)triangle.C.Y, (int)triangle.A.X, (int)triangle.A.Y, outline);
                }

                // 프리팹은 여러 엔티티를 묶는 에셋이다.
                // 실제 메시를 그린 뒤 작은 연결 표시만 얹어 모델 파일과 구분한다.
                uint32_t badge = PackPreviewPixel(82, 128, 235);
                int nodeSize = std::max(3, size / 16);
                FillPreviewRect(result, size - size / 4, size - size / 4, nodeSize, nodeSize, badge);
                result.Success = true;
                return result;
            }

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

                            std::vector<PrefabPreviewMesh> primitiveMeshes;
                            if (CollectPrefabPrimitivePreviewMeshes(data, primitiveMeshes))
                            {
                                DecodedPreviewPixels primitivePreview = GeneratePrefabPrimitivePreviewPixels(path, primitiveMeshes, size);
                                if (primitivePreview.Success)
                                    return primitivePreview;
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
                std::string typeKey = prefab ? "prefab_primitive_render_v1" : "model";
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

            DecodedPreviewPixels LoadOrGenerateCachedMaterialPreview(const std::filesystem::path& path, int size)
            {
                const std::string typeKey = MaterialThumbnailAlgorithmVersion;
                const std::filesystem::path cachePath = GetPreviewCachePath(path, typeKey);

                DecodedPreviewPixels cached;
                if (LoadPreviewCacheFile(cachePath, cached))
                    return cached;

                DecodedPreviewPixels generated = GenerateMaterialPreviewPixels(path, size);
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
                InvalidateMaterialPreviewCache(false);
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
            if (extension == ".ccmat") return AssetType::Material;
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
                case AssetType::Material: return "MAT";
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
                case AssetType::Material: return "material";
                case AssetType::Model: return "model";
                case AssetType::FbxMesh: return "mesh";
                case AssetType::Texture: return "texture";
                case AssetType::Script: return "script";
                case AssetType::Folder: return "folder";
                default: return "unknown";
            }
        }

        std::string AssetBrowserPanel::GetTypeFilterLabel(TypeFilter filter) const
        {
            switch (filter)
            {
                case TypeFilter::Texture: return "Texture";
                case TypeFilter::Model: return "Model";
                case TypeFilter::Material: return "Material";
                case TypeFilter::Prefab: return "Prefab";
                case TypeFilter::Scene: return "Scene";
                case TypeFilter::Script: return "Script";
                default: return "All Types";
            }
        }

        std::string AssetBrowserPanel::GetSortModeLabel(SortMode mode) const
        {
            switch (mode)
            {
                case SortMode::Type: return "Type";
                case SortMode::ModifiedTime: return "Modified";
                default: return "Name";
            }
        }

        std::string AssetBrowserPanel::GetTypeFilterLabel() const
        {
            return GetTypeFilterLabel(m_TypeFilter);
        }

        std::string AssetBrowserPanel::GetSortModeLabel() const
        {
            return GetSortModeLabel(m_SortMode);
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

        bool AssetBrowserPanel::ReimportSelectedAssets()
        {
            std::vector<AssetEntry> selected = GetSelectedEntries();
            if (selected.empty())
                return false;

            bool reimportedAny = false;
            bool requestedScriptCompile = false;
            for (const AssetEntry& entry : selected)
            {
                if (entry.Type == AssetType::Folder || IsVirtualSubAsset(entry))
                    continue;

                AssetImportResult result = AssetDatabase::ReimportAsset(entry.Path, true);
                if (!result.Success)
                    continue;

                reimportedAny = true;
                if (result.Type == AssetKind::Script)
                    requestedScriptCompile = true;
            }

            if (!reimportedAny)
                return false;

            if (requestedScriptCompile)
            {
                // 스크립트 reimport는 meta 갱신에서 끝나지 않는다.
                // 실제 런타임에서 쓰는 assembly/manifest를 맞추기 위해 컴파일 큐까지 연결한다.
                ScriptCompiler::RequestCompile();
            }

            m_TexturePreviewCache.clear();
            m_TexturePreviewOrder.clear();
            InvalidateMaterialPreviewCache(true);
            m_TreeChildCache.clear();
            Refresh(true);
            NotifyAssetDatabaseChanged();
            return true;
        }

        bool AssetBrowserPanel::RefreshCurrentFolder(bool forceReimport)
        {
            std::filesystem::path refreshRoot = IsPathInsideRoot(m_CurrentDirectory, true) ? m_CurrentDirectory : m_RootDirectory;
            AssetRefreshReport report = AssetDatabase::RefreshAssets(refreshRoot, forceReimport);

            bool requestedScriptCompile = false;
            for (const AssetImportResult& result : report.Results)
            {
                if (result.Success && result.Type == AssetKind::Script)
                {
                    requestedScriptCompile = true;
                    break;
                }
            }

            if (requestedScriptCompile)
                ScriptCompiler::RequestCompile();

            m_TexturePreviewCache.clear();
            m_TexturePreviewOrder.clear();
            InvalidateMaterialPreviewCache(forceReimport);
            m_TreeChildCache.clear();
            Refresh(false);
            NotifyAssetDatabaseChanged();
            return report.Failed == 0;
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

        bool AssetBrowserPanel::CreateMaterialInCurrentDirectory()
        {
            if (!IsPathInsideRoot(m_CurrentDirectory, true))
                return false;

            std::filesystem::path materialPath = MakeUniquePath(m_CurrentDirectory, "New Material", ".ccmat");
            MaterialAsset material = MaterialAsset::CreateDefault(materialPath.stem().string());
            if (!material.SaveToFile(materialPath))
                return false;

            // Material도 일반 에셋이므로 생성 즉시 meta를 만든다.
            // 그래야 씬/프리팹이 처음부터 GUID로 참조할 수 있다.
            AssetDatabase::EnsureMetaFile(materialPath);
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

            const float iconSizes[5] = { 18.0f, 36.0f, 64.0f, 96.0f, 128.0f };
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

            const float iconSizes[5] = { 18.0f, 36.0f, 64.0f, 96.0f, 128.0f };
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

            const float iconSizes[5] = { 18.0f, 36.0f, 64.0f, 96.0f, 128.0f };
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

        AssetBrowserPanel::ToolbarMetrics AssetBrowserPanel::GetToolbarMetrics() const
        {
            ToolbarMetrics metrics;
            metrics.ButtonY = m_CalculatedPos.y + 36.0f;
            metrics.PlusX = m_CalculatedPos.x + m_CalculatedSize.x - 46.0f;
            metrics.MinusX = metrics.PlusX - 28.0f;
            metrics.SortX = metrics.MinusX - kToolbarButtonGap - kSortButtonWidth;
            metrics.TypeX = metrics.SortX - kToolbarButtonGap - kTypeFilterButtonWidth;
            metrics.SearchX = m_CalculatedPos.x + (std::max)(150.0f, m_TreeWidth + 18.0f);
            metrics.SearchW = (std::max)(0.0f, metrics.TypeX - metrics.SearchX - kToolbarButtonGap);
            return metrics;
        }

        bool AssetBrowserPanel::IsSearchBoxPoint(float mouseX, float mouseY) const
        {
            const ToolbarMetrics metrics = GetToolbarMetrics();
            if (metrics.SearchW <= 8.0f)
                return false;

            return mouseX >= metrics.SearchX && mouseX <= metrics.SearchX + metrics.SearchW &&
                mouseY >= metrics.ButtonY && mouseY <= metrics.ButtonY + kToolbarButtonHeight;
        }

        bool AssetBrowserPanel::IsTypeFilterButtonPoint(float mouseX, float mouseY) const
        {
            const ToolbarMetrics metrics = GetToolbarMetrics();
            return mouseX >= metrics.TypeX && mouseX <= metrics.TypeX + kTypeFilterButtonWidth &&
                mouseY >= metrics.ButtonY && mouseY <= metrics.ButtonY + kToolbarButtonHeight;
        }

        bool AssetBrowserPanel::IsSortButtonPoint(float mouseX, float mouseY) const
        {
            const ToolbarMetrics metrics = GetToolbarMetrics();
            return mouseX >= metrics.SortX && mouseX <= metrics.SortX + kSortButtonWidth &&
                mouseY >= metrics.ButtonY && mouseY <= metrics.ButtonY + kToolbarButtonHeight;
        }

        bool AssetBrowserPanel::IsTypeFilterDropdownPoint(float mouseX, float mouseY) const
        {
            const ToolbarMetrics metrics = GetToolbarMetrics();
            float y = metrics.ButtonY + kToolbarButtonHeight + 2.0f;
            return mouseX >= metrics.TypeX && mouseX <= metrics.TypeX + kTypeFilterButtonWidth &&
                mouseY >= y && mouseY <= y + kDropdownItemHeight * (float)kTypeFilterItemCount;
        }

        bool AssetBrowserPanel::IsSortDropdownPoint(float mouseX, float mouseY) const
        {
            const ToolbarMetrics metrics = GetToolbarMetrics();
            float y = metrics.ButtonY + kToolbarButtonHeight + 2.0f;
            return mouseX >= metrics.SortX && mouseX <= metrics.SortX + kSortButtonWidth &&
                mouseY >= y && mouseY <= y + kDropdownItemHeight * (float)kSortItemCount;
        }

        int AssetBrowserPanel::GetTypeFilterDropdownItemAt(float mouseX, float mouseY) const
        {
            if (!IsTypeFilterDropdownPoint(mouseX, mouseY))
                return -1;

            const ToolbarMetrics metrics = GetToolbarMetrics();
            float y = metrics.ButtonY + kToolbarButtonHeight + 2.0f;
            int index = (int)((mouseY - y) / kDropdownItemHeight);
            return index >= 0 && index < kTypeFilterItemCount ? index : -1;
        }

        int AssetBrowserPanel::GetSortDropdownItemAt(float mouseX, float mouseY) const
        {
            if (!IsSortDropdownPoint(mouseX, mouseY))
                return -1;

            const ToolbarMetrics metrics = GetToolbarMetrics();
            float y = metrics.ButtonY + kToolbarButtonHeight + 2.0f;
            int index = (int)((mouseY - y) / kDropdownItemHeight);
            return index >= 0 && index < kSortItemCount ? index : -1;
        }

        void AssetBrowserPanel::SetTypeFilter(TypeFilter filter)
        {
            if (m_TypeFilter == filter)
                return;

            m_TypeFilter = filter;
            ApplyFilter();
            ClearSelection();
            m_HoveredIndex = -1;
            m_ScrollState.ScrollY = 0.0f;
        }

        void AssetBrowserPanel::SetSortMode(SortMode mode)
        {
            if (m_SortMode == mode)
                return;

            m_SortMode = mode;
            ApplyFilter();
            ClearSelection();
            m_HoveredIndex = -1;
            m_ScrollState.ScrollY = 0.0f;
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
            std::string extensionFilter;
            TypeFilter queryTypeFilter = TypeFilter::All;

            std::stringstream queryStream(query);
            std::string textQuery;
            std::string token;
            while (queryStream >> token)
            {
                if (token.rfind("ext:", 0) == 0)
                {
                    extensionFilter = token.substr(4);
                    if (!extensionFilter.empty() && extensionFilter[0] != '.')
                        extensionFilter = "." + extensionFilter;
                    continue;
                }

                if (token.rfind("type:", 0) == 0)
                {
                    std::string typeText = token.substr(5);
                    if (typeText == "texture" || typeText == "tex")
                        queryTypeFilter = TypeFilter::Texture;
                    else if (typeText == "model" || typeText == "fbx" || typeText == "mesh")
                        queryTypeFilter = TypeFilter::Model;
                    else if (typeText == "material" || typeText == "mat")
                        queryTypeFilter = TypeFilter::Material;
                    else if (typeText == "prefab" || typeText == "pfb")
                        queryTypeFilter = TypeFilter::Prefab;
                    else if (typeText == "scene" || typeText == "scn")
                        queryTypeFilter = TypeFilter::Scene;
                    else if (typeText == "script" || typeText == "cs")
                        queryTypeFilter = TypeFilter::Script;
                    continue;
                }

                if (!textQuery.empty())
                    textQuery += " ";
                textQuery += token;
            }
            query = textQuery;

            if (query.empty() && extensionFilter.empty() && m_TypeFilter == TypeFilter::All && queryTypeFilter == TypeFilter::All)
            {
                for (const AssetEntry& entry : m_Entries)
                {
                    m_ViewEntries.push_back(entry);
                    AppendFbxSubAssetEntries(entry, query);
                }
                SortViewEntries();
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

                bool matchesEntry =
                    EntryMatchesAdvancedFilter(entry, query, extensionFilter, queryTypeFilter);

                if (matchesEntry)
                {
                    m_ViewEntries.push_back(entry);
                }

                AppendFbxSubAssetEntries(entry, query);
            }

            SortViewEntries();
        }

        bool AssetBrowserPanel::EntryMatchesAdvancedFilter(const AssetEntry& entry, const std::string& textQuery, const std::string& extensionFilter, TypeFilter queryTypeFilter) const
        {
            auto typeMatches = [&](TypeFilter filter)
                {
                    if (filter == TypeFilter::All)
                        return true;

                    switch (filter)
                    {
                        case TypeFilter::Texture: return entry.Type == AssetType::Texture;
                        case TypeFilter::Model: return entry.Type == AssetType::Model || entry.Type == AssetType::FbxMesh;
                        case TypeFilter::Material: return entry.Type == AssetType::Material;
                        case TypeFilter::Prefab: return entry.Type == AssetType::Prefab;
                        case TypeFilter::Scene: return entry.Type == AssetType::Scene;
                        case TypeFilter::Script: return entry.Type == AssetType::Script;
                        default: return true;
                    }
                };

            if (!typeMatches(m_TypeFilter) || !typeMatches(queryTypeFilter))
                return false;

            std::string extension = ToLowerText(entry.Path.extension().string());
            if (!extensionFilter.empty() && extension != extensionFilter)
                return false;

            if (textQuery.empty())
                return true;

            std::string name = ToLowerText(entry.DisplayName);
            std::string type = ToLowerText(GetTypeLabel(entry.Type));
            std::string path = ToLowerText(entry.Path.generic_string());
            return name.find(textQuery) != std::string::npos ||
                extension.find(textQuery) != std::string::npos ||
                type.find(textQuery) != std::string::npos ||
                path.find(textQuery) != std::string::npos;
        }

        void AssetBrowserPanel::SortViewEntries()
        {
            if (m_ViewEntries.size() <= 1)
                return;

            // 부모 폴더 이동 항목은 항상 맨 위에 고정한다.
            // 검색/정렬 결과에서도 사용자가 현재 위치를 잃지 않게 하기 위한 기본 규칙이다.
            std::stable_sort(m_ViewEntries.begin(), m_ViewEntries.end(),
                [this](const AssetEntry& a, const AssetEntry& b)
                {
                    if (a.DisplayName == "..")
                        return true;
                    if (b.DisplayName == "..")
                        return false;

                    std::string groupA = a.IsSubAsset ? a.SubAssetParentKey : GetTreeKey(a.Path);
                    std::string groupB = b.IsSubAsset ? b.SubAssetParentKey : GetTreeKey(b.Path);
                    if (groupA == groupB)
                    {
                        // FBX 하위 메시 정렬은 부모 파일을 먼저 두고 내부 순서를 유지한다.
                        // 검색이나 타입 정렬을 해도 FBX 묶음이 흩어지면 별도 에셋처럼 보여서 구조를 읽기 어렵다.
                        if (a.IsSubAsset != b.IsSubAsset)
                            return !a.IsSubAsset;

                        if (a.IsSubAsset && b.IsSubAsset)
                            return a.SubAssetOrder < b.SubAssetOrder;
                    }
                    else if (a.IsSubAsset || b.IsSubAsset)
                    {
                        return groupA < groupB;
                    }

                    if (m_SortMode == SortMode::Type && a.Type != b.Type)
                        return (int)a.Type < (int)b.Type;

                    if (m_SortMode == SortMode::ModifiedTime)
                    {
                        std::error_code ecA;
                        std::error_code ecB;
                        auto timeA = std::filesystem::last_write_time(a.Path, ecA);
                        auto timeB = std::filesystem::last_write_time(b.Path, ecB);
                        if (!ecA && !ecB && timeA != timeB)
                            return timeA > timeB;
                    }

                    if (a.Type == AssetType::Folder && b.Type != AssetType::Folder)
                        return true;
                    if (a.Type != AssetType::Folder && b.Type == AssetType::Folder)
                        return false;

                    return ToLowerText(a.DisplayName) < ToLowerText(b.DisplayName);
                });
        }

        bool AssetBrowserPanel::RunQualityRegressionChecks()
        {
            std::vector<std::string> failures;
            auto fail = [&failures](const std::string& message)
                {
                    failures.push_back(message);
                };

            auto rangesOverlap = [](float a0, float a1, float b0, float b1)
                {
                    return a0 < b1 && b0 < a1;
                };

            const ToolbarMetrics toolbar = GetToolbarMetrics();
            const float buttonRight = toolbar.PlusX + kToolbarButtonHeight;

            // 회귀 검사는 실제 마우스 입력을 보내지 않고, 입력 판정에 쓰는 좌표 규칙을 직접 확인한다.
            // 렌더 위치와 클릭 위치가 따로 계산되면 검색창, 필터, 크기 버튼이 서로 침범하는 문제가 다시 생긴다.
            if (toolbar.SearchW <= 8.0f)
                fail("Toolbar search box width is too small.");
            if (rangesOverlap(toolbar.SearchX, toolbar.SearchX + toolbar.SearchW, toolbar.TypeX, toolbar.TypeX + kTypeFilterButtonWidth))
                fail("Search box overlaps type filter.");
            if (rangesOverlap(toolbar.TypeX, toolbar.TypeX + kTypeFilterButtonWidth, toolbar.SortX, toolbar.SortX + kSortButtonWidth))
                fail("Type filter overlaps sort dropdown.");
            if (rangesOverlap(toolbar.SortX, toolbar.SortX + kSortButtonWidth, toolbar.MinusX, toolbar.MinusX + kToolbarButtonHeight))
                fail("Sort dropdown overlaps icon size buttons.");
            if (rangesOverlap(toolbar.MinusX, toolbar.MinusX + kToolbarButtonHeight, toolbar.PlusX, buttonRight))
                fail("Icon size buttons overlap.");

            for (int i = 0; i < kTypeFilterItemCount; ++i)
            {
                float x = toolbar.TypeX + 4.0f;
                float y = toolbar.ButtonY + kToolbarButtonHeight + 2.0f + (float)i * kDropdownItemHeight + kDropdownItemHeight * 0.5f;
                if (GetTypeFilterDropdownItemAt(x, y) != i)
                    fail("Type filter dropdown item hit-test failed.");
            }

            for (int i = 0; i < kSortItemCount; ++i)
            {
                float x = toolbar.SortX + 4.0f;
                float y = toolbar.ButtonY + kToolbarButtonHeight + 2.0f + (float)i * kDropdownItemHeight + kDropdownItemHeight * 0.5f;
                if (GetSortDropdownItemAt(x, y) != i)
                    fail("Sort dropdown item hit-test failed.");
            }

            AssetEntry textureEntry;
            textureEntry.Path = m_RootDirectory / "textures" / "Hero_Diffuse.png";
            textureEntry.DisplayName = "Hero_Diffuse.png";
            textureEntry.Type = AssetType::Texture;

            const TypeFilter savedTypeFilter = m_TypeFilter;
            m_TypeFilter = TypeFilter::All;
            if (!EntryMatchesAdvancedFilter(textureEntry, "hero", ".png", TypeFilter::All))
                fail("Text and extension filter should match a texture entry.");
            if (EntryMatchesAdvancedFilter(textureEntry, "hero", ".fbx", TypeFilter::All))
                fail("Extension filter accepted a different extension.");
            if (EntryMatchesAdvancedFilter(textureEntry, "hero", ".png", TypeFilter::Model))
                fail("Type filter accepted the wrong asset type.");
            m_TypeFilter = TypeFilter::Script;
            if (EntryMatchesAdvancedFilter(textureEntry, "hero", ".png", TypeFilter::All))
                fail("Panel type filter was ignored.");
            m_TypeFilter = savedTypeFilter;

            const std::vector<AssetEntry> savedViewEntries = m_ViewEntries;
            const SortMode savedSortMode = m_SortMode;
            m_SortMode = SortMode::Name;

            AssetEntry fbxParent;
            fbxParent.Path = m_RootDirectory / "models" / "Character.fbx";
            fbxParent.DisplayName = "Character.fbx";
            fbxParent.Type = AssetType::Model;

            AssetEntry fbxChildA;
            fbxChildA.Path = fbxParent.Path;
            fbxChildA.DisplayName = "Body";
            fbxChildA.Type = AssetType::FbxMesh;
            fbxChildA.IsSubAsset = true;
            fbxChildA.SubAssetParentKey = GetTreeKey(fbxParent.Path);
            fbxChildA.SubAssetOrder = 0;

            AssetEntry fbxChildB = fbxChildA;
            fbxChildB.DisplayName = "Hair";
            fbxChildB.SubAssetOrder = 1;

            AssetEntry normalEntry;
            normalEntry.Path = m_RootDirectory / "textures" / "A_Texture.png";
            normalEntry.DisplayName = "A_Texture.png";
            normalEntry.Type = AssetType::Texture;

            m_ViewEntries = { fbxChildB, normalEntry, fbxChildA, fbxParent };
            SortViewEntries();

            int parentIndex = -1;
            int childAIndex = -1;
            int childBIndex = -1;
            for (int i = 0; i < (int)m_ViewEntries.size(); ++i)
            {
                if (m_ViewEntries[i].DisplayName == "Character.fbx")
                    parentIndex = i;
                else if (m_ViewEntries[i].DisplayName == "Body")
                    childAIndex = i;
                else if (m_ViewEntries[i].DisplayName == "Hair")
                    childBIndex = i;
            }

            if (parentIndex < 0 || childAIndex < 0 || childBIndex < 0)
                fail("FBX grouping test entries were lost.");
            else if (!(parentIndex < childAIndex && childAIndex < childBIndex))
                fail("FBX sub-assets are not kept after their parent in import order.");

            m_ViewEntries = savedViewEntries;
            m_SortMode = savedSortMode;

            if (failures.empty())
            {
                ConsoleLog::Info("Asset Browser QA passed.");
                return true;
            }

            ConsoleLog::Error("Asset Browser QA failed: " + std::to_string(failures.size()) + " issue(s).");
            for (const std::string& failure : failures)
                ConsoleLog::Error(" - " + failure);
            return false;
        }

        void AssetBrowserPanel::AppendFbxSubAssetEntries(const AssetEntry& fbxEntry, const std::string& query)
        {
            if (!IsFbxContainer(fbxEntry))
                return;

            std::string key = GetTreeKey(fbxEntry.Path);
            if (m_ExpandedFbxAssets.find(key) == m_ExpandedFbxAssets.end())
                return;

            const auto& meshes = GetFbxMeshInfos(fbxEntry.Path);
            std::vector<const FbxMeshInfo*> visibleMeshes;
            visibleMeshes.reserve(meshes.size());
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

                visibleMeshes.push_back(&meshInfo);
            }

            int visibleSubAssetOrder = 0;
            for (const FbxMeshInfo* meshInfo : visibleMeshes)
            {
                if (!meshInfo)
                    continue;

                AssetEntry meshEntry;
                meshEntry.Path = fbxEntry.Path;
                meshEntry.SourceAssetPath = fbxEntry.Path;
                meshEntry.DisplayName = meshInfo->Name;
                meshEntry.Type = AssetType::FbxMesh;
                meshEntry.SubAssetIndex = meshInfo->MeshIndex;
                meshEntry.IsSubAsset = true;
                meshEntry.SubAssetParentKey = key;
                meshEntry.SubAssetOrder = visibleSubAssetOrder++;
                meshEntry.SubAssetCount = (int)visibleMeshes.size();
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
                const AssetEntry& entry = m_ViewEntries[index];
                if (m_OnAssetSelected && entry.Type == AssetType::Material && !IsVirtualSubAsset(entry))
                    m_OnAssetSelected(entry.Path.string(), GetTypeKey(entry.Type));
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

        bool AssetBrowserPanel::ShowSelectedEntryInFolder()
        {
            std::vector<AssetEntry> entries = GetSelectedEntries();
            if (entries.empty())
                return false;

            const AssetEntry& selected = entries.front();
            std::filesystem::path targetPath = selected.Path;
            std::filesystem::path parentPath = targetPath.parent_path();
            if (parentPath.empty())
                return false;

            // 검색 결과에서 위치를 열 때는 먼저 실제 폴더로 이동한 뒤 같은 파일을 다시 선택한다.
            // 이렇게 하면 이름, 타입, 정렬 필터를 바꾼 상태에서도 사용자가 에셋의 원래 위치를 잃지 않는다.
            NavigateTo(parentPath);

            std::error_code ec;
            auto targetCanonical = std::filesystem::weakly_canonical(targetPath, ec);
            if (ec)
                targetCanonical = std::filesystem::absolute(targetPath, ec);

            for (int i = 0; i < (int)m_ViewEntries.size(); ++i)
            {
                std::error_code entryEc;
                auto entryCanonical = std::filesystem::weakly_canonical(m_ViewEntries[i].Path, entryEc);
                if (entryEc)
                    entryCanonical = std::filesystem::absolute(m_ViewEntries[i].Path, entryEc);

                if (!entryEc && !ec && entryCanonical == targetCanonical)
                {
                    SelectSingle(i);
                    if (m_IconSizeStep == 0)
                    {
                        m_ScrollState.ScrollY = (std::max)(0.0f, (float)i * (m_RowHeight + m_RowGap) - 20.0f);
                    }
                    else
                    {
                        const float iconSizes[5] = { 18.0f, 36.0f, 64.0f, 96.0f, 128.0f };
                        float iconSize = iconSizes[(std::clamp)(m_IconSizeStep, 0, 4)];
                        float cellW = iconSize + 58.0f;
                        float cellH = iconSize + 42.0f;
                        float viewW = (std::max)(1.0f, m_CalculatedSize.x - m_TreeWidth - 34.0f);
                        int columns = (std::max)(1, (int)(viewW / cellW));
                        int row = i / columns;
                        m_ScrollState.ScrollY = (std::max)(0.0f, (float)row * cellH - 20.0f);
                    }
                    return true;
                }
            }

            return true;
        }

        bool AssetBrowserPanel::RevealSelectedEntryInExplorer()
        {
            std::vector<AssetEntry> entries = GetSelectedEntries();
            if (entries.empty())
                return false;

            const AssetEntry& selected = entries.front();
            std::filesystem::path targetPath = selected.Path;

            std::error_code ec;
            if (!std::filesystem::exists(targetPath, ec))
                return false;

            if (std::filesystem::is_directory(targetPath, ec))
            {
                // 폴더는 파일 선택 인자가 아니라 폴더 열기로 처리한다.
                // 탐색기에서 바로 해당 폴더 내용을 보는 쪽이 에셋 브라우저의 폴더 동작과 맞다.
                auto result = reinterpret_cast<intptr_t>(
                    ShellExecuteW(nullptr, L"open", targetPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
                return result > 32;
            }

            std::wstring explorerArgs = L"/select,\"" + targetPath.wstring() + L"\"";
            auto result = reinterpret_cast<intptr_t>(
                ShellExecuteW(nullptr, L"open", L"explorer.exe", explorerArgs.c_str(), nullptr, SW_SHOWNORMAL));
            return result > 32;
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

        void AssetBrowserPanel::ApplyMaterialPreviewOverride(const std::filesystem::path& materialPath, const MaterialAsset& material)
        {
            if (materialPath.empty())
                return;

            MaterialPreviewCacheEntry& preview = m_MaterialPreviewCache[GetTreeKey(materialPath)];
            std::error_code ec;
            preview.LastWriteTime = std::filesystem::last_write_time(materialPath, ec);
            if (ec)
                preview.LastWriteTime = {};
            preview.SourcePath = materialPath;

            // 인스펙터에서 편집 중인 값은 아직 파일에 저장되지 않았을 수 있다.
            // 썸네일은 파일 재스캔을 기다리지 말고 같은 메모리 값을 받아 즉시 다시 그린다.
            preview.Material = material;
            preview.AlbedoColor = material.AlbedoColor;
            preview.Valid = true;
            preview.Dirty = true;
            preview.CaptureFailed = false;
            preview.CapturePending = false;
            preview.CaptureAttempts = 0;
        }

        void AssetBrowserPanel::ApplyMaterialPreviewCapture(const std::filesystem::path& materialPath, uint32_t width, uint32_t height, const std::vector<uint32_t>& pixels)
        {
            if (materialPath.empty() || width == 0 || height == 0 || pixels.empty() || !HasVisibleMaterialPreviewPixels(pixels))
                return;

            // 인스펙터 하단 프리뷰는 사용자가 보는 완성된 렌더 결과다.
            // 여기서 다시 크롭하면 배경색 판정이 틀렸을 때 회색 영역만 썸네일로 남을 수 있으므로,
            // 이 경로는 프리뷰 프레임을 그대로 축소해서 에셋 브라우저에 전달한다.
            DecodedPreviewPixels capturedFrame = ResizePreviewFrame(width, height, pixels, MaterialPreviewTextureSize);
            if (!capturedFrame.Success)
            {
                AppendThumbnailDebugLog("material inspector capture resize failed: " + materialPath.string());
                return;
            }

            ForcePreviewAlphaOpaque(capturedFrame.Pixels);
            DumpThumbnailDebugImage("material_inspector_frame_capture", (uint32_t)capturedFrame.Width, (uint32_t)capturedFrame.Height, capturedFrame.Pixels);

            std::vector<std::string> targetKeys;
            targetKeys.push_back(GetTreeKey(materialPath));

            auto collectMatchingEntryKey = [&](const AssetEntry& entry)
                {
                    if ((entry.Type == AssetType::Material || IsMaterialAssetPath(entry.Path)) &&
                        PathsReferToSameExistingFile(entry.Path, materialPath))
                    {
                        targetKeys.push_back(GetTreeKey(entry.Path));
                    }
                };

            for (const AssetEntry& entry : m_Entries)
                collectMatchingEntryKey(entry);
            for (const AssetEntry& entry : m_ViewEntries)
                collectMatchingEntryKey(entry);

            std::sort(targetKeys.begin(), targetKeys.end());
            targetKeys.erase(std::unique(targetKeys.begin(), targetKeys.end()), targetKeys.end());

            for (const std::string& key : targetKeys)
            {
                MaterialPreviewCacheEntry& preview = m_MaterialPreviewCache[key];
                std::error_code ec;
                preview.LastWriteTime = std::filesystem::last_write_time(materialPath, ec);
                if (ec)
                    preview.LastWriteTime = {};
                preview.SourcePath = materialPath;

                // 인스펙터 프리뷰 캡처는 최종 안전망이다.
                // GPU 텍스처 업로드나 DrawImage 상태가 깨져도 같은 픽셀을 CPU 샘플링으로 바로 그릴 수 있게 보관한다.
                preview.CapturedPixels = capturedFrame.Pixels;
                preview.CapturedPixelWidth = capturedFrame.Width;
                preview.CapturedPixelHeight = capturedFrame.Height;
                preview.CapturedTexture.reset(Texture2D::Create((uint32_t)capturedFrame.Width, (uint32_t)capturedFrame.Height, capturedFrame.Pixels.data()));
                preview.CapturedFromInspector = true;
                preview.Valid = true;
                preview.Rendered = true;
                preview.Dirty = false;
                preview.CaptureFailed = false;
                preview.CapturePending = false;
                preview.CaptureAttempts = 0;

                AppendThumbnailDebugLogOnce(std::string("material-capture-applied|") + key,
                    std::string("material inspector capture applied: key=") + key +
                    " source=" + NormalizeAssetPathForKey(materialPath).generic_string() +
                    " size=" + std::to_string(capturedFrame.Width) + "x" + std::to_string(capturedFrame.Height) +
                    " texture=" + std::to_string(preview.CapturedTexture != nullptr) +
                    " srv=" + std::to_string(preview.CapturedTexture && preview.CapturedTexture->GetRendererID() != nullptr));
            }
        }

        void AssetBrowserPanel::ApplyMaterialPreviewTexture(const std::filesystem::path& materialPath, RendererHandle previewTexture)
        {
            if (materialPath.empty() || !previewTexture)
                return;

            std::vector<std::string> targetKeys;
            targetKeys.push_back(GetTreeKey(materialPath));
            for (const AssetEntry& entry : m_ViewEntries)
            {
                if ((entry.Type == AssetType::Material || IsMaterialAssetPath(entry.Path)) &&
                    PathsReferToSameExistingFile(entry.Path, materialPath))
                {
                    targetKeys.push_back(GetTreeKey(entry.Path));
                }
            }

            std::sort(targetKeys.begin(), targetKeys.end());
            targetKeys.erase(std::unique(targetKeys.begin(), targetKeys.end()), targetKeys.end());

            for (const std::string& key : targetKeys)
            {
                // 임시 테스트 경로:
                // 인스펙터 하단 프리뷰가 이미 정상 렌더링된 상태라면 같은 렌더 타깃을 브라우저에서도 그대로 그린다.
                // 정식 썸네일 저장 문제가 해결되면 이 borrowed preview 경로는 제거해도 된다.
                BorrowedMaterialPreview& borrowedPreview = m_BorrowedMaterialPreviews[key];
                borrowedPreview.SourcePath = materialPath;
                borrowedPreview.Texture = previewTexture;
            }
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
            bool isMaterial = entry.Type == AssetType::Material || IsMaterialAssetPath(entry.Path);
            bool isFbxContainerModel = IsFbxContainer(entry);
            bool isGeneratedPreview =
                entry.Type == AssetType::FbxMesh ||
                (entry.Type == AssetType::Model && !isFbxContainerModel);
            if (!isTexture && !isGeneratedPreview)
                return nullptr;

            std::string key = GetTreeKey(entry.IsSubAsset ? entry.SourceAssetPath : entry.Path);
            if (isMaterial)
                key += std::string("|") + MaterialThumbnailAlgorithmVersion;
            else if (isGeneratedPreview)
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

                if (isMaterial && !HasVisibleMaterialPreviewPixels(pixels))
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
                    else if (type == AssetType::Material)
                        decoded = LoadOrGenerateCachedMaterialPreview(std::filesystem::path(path), MaterialPreviewTextureSize);
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

        const DirectX::XMFLOAT4* AssetBrowserPanel::GetMaterialPreviewColor(const AssetEntry& entry)
        {
            if (entry.Type != AssetType::Material && !IsMaterialAssetPath(entry.Path))
                return nullptr;

            std::error_code ec;
            std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(entry.Path, ec);
            if (ec)
            {
                m_MaterialPreviewCache.erase(GetTreeKey(entry.Path));
                writeTime = {};
                return nullptr;
            }

            std::string key = GetTreeKey(entry.Path);
            auto cached = m_MaterialPreviewCache.find(key);
            if (cached != m_MaterialPreviewCache.end() && cached->second.Valid && cached->second.LastWriteTime == writeTime)
            {
                if (!cached->second.Rendered && !cached->second.CaptureFailed)
                    cached->second.Dirty = true;
                return &cached->second.AlbedoColor;
            }

            MaterialPreviewCacheEntry& preview = m_MaterialPreviewCache[key];
            bool keepFramebuffer = preview.PreviewFramebuffer != nullptr;
            AppendThumbnailDebugLog("material cache reload: " + key + " keepFramebuffer=" + std::to_string(keepFramebuffer));
            preview.SourcePath = entry.Path;
            preview.LastWriteTime = writeTime;
            MaterialAsset material;
            if (material.LoadFromFile(entry.Path))
            {
                preview.Material = material;
                preview.AlbedoColor = material.AlbedoColor;
                preview.Valid = true;
                preview.Dirty = true;
                preview.CaptureFailed = false;
                preview.CapturePending = false;
                preview.CaptureAttempts = 0;
                // 파일이 갱신되었어도 새 렌더 결과가 준비되기 전까지 마지막 정상 프리뷰를 유지한다.
                // 상용 에디터처럼 썸네일이 순간적으로 빈 회색 칸으로 돌아가는 깜박임을 막기 위한 캐시 정책이다.
                AppendThumbnailDebugLog("material cache loaded: " + key);
            }
            else
            {
                preview.Valid = false;
                preview.Dirty = false;
                preview.Rendered = false;
                preview.CaptureFailed = false;
                preview.CapturePending = false;
                preview.CaptureAttempts = 0;
                preview.CapturedTexture.reset();
                preview.CapturedPixels.clear();
                preview.CapturedPixelWidth = 0;
                preview.CapturedPixelHeight = 0;
                preview.CapturedFromInspector = false;
                AppendThumbnailDebugLog("material cache load failed: " + key);
            }

            // Material 썸네일은 파일 수정 시간이 바뀔 때만 다시 읽는다.
            // 그리드 렌더링 중 매 프레임 .ccmat을 파싱하면 색 조정이나 스크롤이 끊긴다.
            if (!keepFramebuffer && preview.CapturedPixels.empty())
            {
                preview.Dirty = true;
                preview.Rendered = false;
                preview.CaptureFailed = false;
                preview.CapturePending = false;
                preview.CaptureAttempts = 0;
                preview.CapturedTexture.reset();
                AppendThumbnailDebugLog("material cache reset without framebuffer: " + key);
            }
            return preview.Valid ? &preview.AlbedoColor : nullptr;
        }

        RendererHandle AssetBrowserPanel::GetMaterialPreviewTexture(const AssetEntry& entry)
        {
            GetMaterialPreviewColor(entry);
            auto cached = m_MaterialPreviewCache.find(GetTreeKey(entry.Path));
            if (cached == m_MaterialPreviewCache.end() ||
                !cached->second.Valid ||
                !cached->second.Rendered ||
                !cached->second.CapturedTexture)
            {
                AppendThumbnailDebugLogOnce(std::string("material-get-texture-fallback|") + GetTreeKey(entry.Path),
                    "material get texture fallback: key=" + GetTreeKey(entry.Path));
                return nullptr;
            }

            AppendThumbnailDebugLogOnce(std::string("material-get-texture-ok|") + GetTreeKey(entry.Path),
                "material get texture ok: key=" + GetTreeKey(entry.Path));
            return cached->second.CapturedTexture->GetRendererID();
        }

        RendererHandle AssetBrowserPanel::GetPrefabPreviewTexture(const AssetEntry& entry)
        {
            if (entry.Type != AssetType::Prefab)
                return nullptr;

            std::error_code ec;
            if (!std::filesystem::exists(entry.Path, ec))
                return nullptr;

            std::filesystem::file_time_type lastWriteTime = std::filesystem::last_write_time(entry.Path, ec);
            if (ec)
                lastWriteTime = {};

            const std::string key = GetTreeKey(entry.Path);
            PrefabPreviewCacheEntry& preview = m_PrefabPreviewCache[key];
            if (!preview.Valid || preview.LastWriteTime != lastWriteTime)
            {
                preview = PrefabPreviewCacheEntry{};
                preview.LastWriteTime = lastWriteTime;

                std::ifstream input(entry.Path);
                if (input)
                {
                    try
                    {
                        nlohmann::json data;
                        input >> data;

                        if (data.contains("Entities") && data["Entities"].is_array())
                        {
                            struct EntityPreviewData
                            {
                                int LocalID = -1;
                                int ParentID = -1;
                                DirectX::XMMATRIX LocalTransform = DirectX::XMMatrixIdentity();
                                bool HasMesh = false;
                                MeshComponent::MeshType Type = MeshComponent::MeshType::Custom;
                                DirectX::XMFLOAT4 Color = { 1.0f, 1.0f, 1.0f, 1.0f };
                                std::string AlbedoPath;
                                std::string MaterialPath;
                            };

                            std::unordered_map<int, EntityPreviewData> entityDataMap;
                            for (const auto& entityData : data["Entities"])
                            {
                                EntityPreviewData previewEntity;
                                previewEntity.LocalID = entityData.value("LocalID", -1);
                                previewEntity.ParentID = entityData.value("ParentID", -1);
                                previewEntity.LocalTransform = ReadPrefabPreviewTransform(entityData);

                                if (entityData.contains("MeshComponent"))
                                {
                                    const auto& meshData = entityData["MeshComponent"];
                                    previewEntity.HasMesh = true;
                                    previewEntity.Type = static_cast<MeshComponent::MeshType>(meshData.value("Type", (int)MeshComponent::MeshType::Custom));
                                    if (meshData.contains("BaseColor"))
                                        previewEntity.Color = ReadRenderPreviewFloat4(meshData["BaseColor"], previewEntity.Color);

                                    std::string albedoGuid = meshData.value("AlbedoGuid", "");
                                    if (!albedoGuid.empty())
                                        previewEntity.AlbedoPath = AssetDatabase::GetPathFromGuid(albedoGuid).string();
                                    if (previewEntity.AlbedoPath.empty() && meshData.contains("AlbedoPath"))
                                        previewEntity.AlbedoPath = meshData["AlbedoPath"].get<std::string>();

                                    std::string materialGuid = meshData.value("MaterialGuid", "");
                                    if (!materialGuid.empty())
                                        previewEntity.MaterialPath = AssetDatabase::GetPathFromGuid(materialGuid).string();
                                    if (previewEntity.MaterialPath.empty() && meshData.contains("MaterialPath"))
                                        previewEntity.MaterialPath = meshData["MaterialPath"].get<std::string>();
                                }

                                if (previewEntity.LocalID >= 0)
                                    entityDataMap[previewEntity.LocalID] = previewEntity;
                            }

                            std::unordered_map<int, DirectX::XMMATRIX> worldCache;
                            std::function<DirectX::XMMATRIX(int)> resolveWorld = [&](int localID) -> DirectX::XMMATRIX
                                {
                                    auto cachedWorld = worldCache.find(localID);
                                    if (cachedWorld != worldCache.end())
                                        return cachedWorld->second;

                                    auto entityIt = entityDataMap.find(localID);
                                    if (entityIt == entityDataMap.end())
                                        return DirectX::XMMatrixIdentity();

                                    DirectX::XMMATRIX world = entityIt->second.LocalTransform;
                                    if (entityIt->second.ParentID >= 0)
                                        world = world * resolveWorld(entityIt->second.ParentID);

                                    worldCache[localID] = world;
                                    return world;
                                };

                            for (const auto& [localID, previewEntity] : entityDataMap)
                            {
                                if (!previewEntity.HasMesh || previewEntity.Type == MeshComponent::MeshType::Custom)
                                    continue;

                                PrefabPreviewRenderItem item;
                                item.MeshData = CreatePreviewMeshForType(previewEntity.Type);
                                if (!item.MeshData)
                                    continue;

                                item.Transform = resolveWorld(localID);
                                item.Color = previewEntity.Color;

                                if (!previewEntity.MaterialPath.empty() && std::filesystem::exists(previewEntity.MaterialPath))
                                {
                                    MaterialAsset material;
                                    if (material.LoadFromFile(previewEntity.MaterialPath))
                                    {
                                        item.Color = material.AlbedoColor;
                                        if (material.AlbedoTexture)
                                            item.Texture = material.AlbedoTexture;
                                    }
                                }

                                if (!item.Texture && !previewEntity.AlbedoPath.empty() && std::filesystem::exists(previewEntity.AlbedoPath))
                                    item.Texture.reset(Texture2D::Create(previewEntity.AlbedoPath));

                                preview.Items.push_back(item);
                            }
                        }
                    }
                    catch (...)
                    {
                        preview.Items.clear();
                    }
                }

                // 프리팹 썸네일은 실제 렌더러로 한 번 촬영한 결과만 사용한다.
                // 여기서 가짜 픽셀을 만들면 씬에 배치되는 모습과 브라우저 썸네일이 서로 달라진다.
                preview.Valid = !preview.Items.empty();
                preview.Dirty = preview.Valid;
                preview.Rendered = false;
                preview.CaptureFailed = !preview.Valid;
                preview.CapturedTexture.reset();
            }

            if (!preview.Rendered || !preview.CapturedTexture)
                return nullptr;

            return preview.CapturedTexture->GetRendererID();
        }

        void AssetBrowserPanel::RenderMaterialPreviewThumbnail(MaterialPreviewCacheEntry& preview)
        {
            if (!preview.Valid)
                return;

            if (preview.CapturedFromInspector && !preview.CapturedPixels.empty())
            {
                // 인스펙터 프리뷰는 사용자가 실제로 보는 렌더 결과다.
                // 백그라운드 썸네일 렌더가 늦게 돌면서 그 결과를 회색 실패 캡처로 덮으면 안 된다.
                preview.Rendered = true;
                preview.Dirty = false;
                preview.CapturePending = false;
                preview.CaptureFailed = false;
                return;
            }

            if (!preview.PreviewFramebuffer)
            {
                FramebufferSpecification spec;
                spec.Width = MaterialPreviewTextureSize;
                spec.Height = MaterialPreviewTextureSize;
                preview.PreviewFramebuffer.reset(Framebuffer::Create(spec));
                preview.Dirty = true;
            }

            if (!m_MaterialPreviewMesh)
                m_MaterialPreviewMesh = MeshFactory::CreateSphere(0.75f, 32, 16);

            if (!preview.PreviewFramebuffer || !m_MaterialPreviewMesh)
                return;

            auto& window = CCEngine::Application::Get()->GetWindow();
            MaterialPreviewRenderOptions options;
            options.Yaw = 0.55f;
            options.Pitch = -0.20f;
            options.TargetWidth = preview.PreviewFramebuffer->GetSpecification().Width;
            options.TargetHeight = preview.PreviewFramebuffer->GetSpecification().Height;
            options.RestoreViewportWidth = window.GetWidth();
            options.RestoreViewportHeight = window.GetHeight();
            RenderMaterialPreviewToFramebuffer(preview.PreviewFramebuffer.get(), m_MaterialPreviewMesh, preview.Material, options);

            std::vector<uint32_t> pixels;
            const bool readSucceeded = preview.PreviewFramebuffer->ReadColorPixels(pixels);
            if (readSucceeded)
                DumpThumbnailDebugImage("material_raw_capture", preview.PreviewFramebuffer->GetSpecification().Width, preview.PreviewFramebuffer->GetSpecification().Height, pixels);

            const bool captureValid = readSucceeded && HasVisibleMaterialPreviewPixels(pixels);

            if (captureValid)
            {
                const FramebufferSpecification& spec = preview.PreviewFramebuffer->GetSpecification();
                DecodedPreviewPixels cropped = CropMaterialPreviewToContent(spec.Width, spec.Height, pixels, MaterialPreviewTextureSize);
                if (cropped.Success)
                    DumpThumbnailDebugImage("material_cropped_capture", (uint32_t)cropped.Width, (uint32_t)cropped.Height, cropped.Pixels);
                if (cropped.Success)
                    ForcePreviewAlphaOpaque(cropped.Pixels);
                if (cropped.Success)
                {
                    preview.CapturedPixels = cropped.Pixels;
                    preview.CapturedPixelWidth = cropped.Width;
                    preview.CapturedPixelHeight = cropped.Height;
                    preview.CapturedFromInspector = false;
                }
                preview.CapturedTexture.reset(cropped.Success ? Texture2D::Create((uint32_t)cropped.Width, (uint32_t)cropped.Height, cropped.Pixels.data()) : nullptr);
                AppendThumbnailDebugLog(std::string("material texture create: cropped=") + std::to_string(cropped.Success) +
                    " texture=" + std::to_string(preview.CapturedTexture != nullptr) +
                    " srv=" + std::to_string(preview.CapturedTexture && preview.CapturedTexture->GetRendererID() != nullptr));
            }
            else
            {
                if (preview.CapturedPixels.empty())
                {
                    preview.CapturedTexture.reset();
                    preview.CapturedPixelWidth = 0;
                    preview.CapturedPixelHeight = 0;
                    preview.CapturedFromInspector = false;
                }
                AppendThumbnailDebugLog(std::string("material capture invalid, keepExisting=") + std::to_string(!preview.CapturedPixels.empty()));
            }

            // 회색 클리어 화면을 성공한 썸네일로 고정하지 않는다.
            // 실제 픽셀이 잡힌 경우에만 캐시하고, 아니면 절차식 임시 아이콘으로 내려간다.
            preview.Rendered = (preview.CapturedTexture != nullptr && preview.CapturedTexture->GetRendererID() != nullptr) || !preview.CapturedPixels.empty();
            preview.Dirty = !preview.Rendered;
            preview.CapturePending = false;
            preview.CaptureFailed = !preview.Rendered;
        }

        void AssetBrowserPanel::InvalidateMaterialPreviewCache(bool discardCapturedPixels)
        {
            for (auto it = m_MaterialPreviewCache.begin(); it != m_MaterialPreviewCache.end(); )
            {
                MaterialPreviewCacheEntry& preview = it->second;

                std::error_code ec;
                if (!preview.SourcePath.empty() && !std::filesystem::exists(preview.SourcePath, ec))
                {
                    it = m_MaterialPreviewCache.erase(it);
                    continue;
                }

                if (discardCapturedPixels)
                {
                    preview.CapturedTexture.reset();
                    preview.CapturedPixels.clear();
                    preview.CapturedPixelWidth = 0;
                    preview.CapturedPixelHeight = 0;
                    preview.CapturedFromInspector = false;
                    preview.Rendered = false;
                }

                // 에셋 DB 새로고침은 파일 목록을 갱신하는 일이고, 인스펙터가 방금 넘겨준 프리뷰 픽셀은 별개의 결과다.
                // 그래서 단순 Refresh에서는 성공한 캡처를 보존하고, 다음 렌더가 준비될 때까지 회색 칸으로 되돌아가지 않게 한다.
                preview.PreviewFramebuffer.reset();
                preview.CaptureFailed = false;
                preview.CapturePending = false;
                preview.CaptureAttempts = 0;
                preview.Dirty = discardCapturedPixels || preview.CapturedPixels.empty();
                ++it;
            }
        }

        void AssetBrowserPanel::RenderPrefabPreviewThumbnail(PrefabPreviewCacheEntry& preview)
        {
            if (!preview.Valid || preview.Items.empty())
                return;

            constexpr uint32_t PrefabPreviewTextureSize = 256;
            if (!preview.PreviewFramebuffer)
            {
                FramebufferSpecification spec;
                spec.Width = PrefabPreviewTextureSize;
                spec.Height = PrefabPreviewTextureSize;
                preview.PreviewFramebuffer.reset(Framebuffer::Create(spec));
                preview.Dirty = true;
            }

            if (!preview.PreviewFramebuffer)
                return;

            DirectX::XMFLOAT3 minBounds = { 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 maxBounds = { 0.0f, 0.0f, 0.0f };
            bool hasBounds = false;
            for (const PrefabPreviewRenderItem& item : preview.Items)
                ExpandPrefabPreviewBounds(item.MeshData, item.Transform, minBounds, maxBounds, hasBounds);

            if (!hasBounds)
            {
                preview.CaptureFailed = true;
                preview.Dirty = false;
                return;
            }

            DirectX::XMFLOAT3 center =
            {
                (minBounds.x + maxBounds.x) * 0.5f,
                (minBounds.y + maxBounds.y) * 0.5f,
                (minBounds.z + maxBounds.z) * 0.5f
            };
            DirectX::XMFLOAT3 extents =
            {
                (maxBounds.x - minBounds.x) * 0.5f,
                (maxBounds.y - minBounds.y) * 0.5f,
                (maxBounds.z - minBounds.z) * 0.5f
            };
            float radius = (std::max)(0.25f, std::sqrt(extents.x * extents.x + extents.y * extents.y + extents.z * extents.z));
            float distance = (std::max)(2.0f, radius * 3.0f);

            DirectX::XMVECTOR eye = DirectX::XMVectorSet(center.x + distance * 0.70f, center.y + distance * 0.45f, center.z - distance, 1.0f);
            DirectX::XMVECTOR target = DirectX::XMVectorSet(center.x, center.y, center.z, 1.0f);
            DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(eye, target, up);
            DirectX::XMMATRIX cameraWorld = DirectX::XMMatrixInverse(nullptr, view);
            DirectX::XMVECTOR cameraRotation = DirectX::XMQuaternionRotationMatrix(cameraWorld);

            DirectX::XMFLOAT3 eyePosition;
            DirectX::XMFLOAT4 eyeRotation;
            DirectX::XMStoreFloat3(&eyePosition, eye);
            DirectX::XMStoreFloat4(&eyeRotation, cameraRotation);

            preview.PreviewFramebuffer->Bind();
            RenderCommand::SetViewport(0, 0, PrefabPreviewTextureSize, PrefabPreviewTextureSize);
            RenderCommand::SetScissorEnable(false);
            RenderCommand::SetBlendMode(RendererAPI::BlendMode::Opaque);
            RenderCommand::SetCullMode(RendererAPI::CullMode::Back);
            RenderCommand::SetDepthTest(true);
            Renderer::SetClearColor(0.10f, 0.10f, 0.11f, 1.0f);
            Renderer::Clear();

            PerspectiveCamera camera(38.0f, 1.0f, 0.05f, (std::max)(50.0f, distance * 8.0f));
            camera.SetPosition(eyePosition);
            camera.SetRotation(eyeRotation);

            SceneLightData lightData;
            lightData.LightCount = 2;
            lightData.Lights[0].Direction = { -0.45f, -0.80f, 0.30f };
            lightData.Lights[0].Color = { 1.0f, 0.96f, 0.90f };
            lightData.Lights[0].Intensity = 1.15f;
            lightData.Lights[1].Direction = { 0.65f, 0.35f, 0.30f };
            lightData.Lights[1].Color = { 0.55f, 0.65f, 1.0f };
            lightData.Lights[1].Intensity = 0.35f;

            Renderer3D::BeginScene(camera, lightData);
            for (const PrefabPreviewRenderItem& item : preview.Items)
                Renderer3D::DrawMesh(item.Transform, item.MeshData, item.Texture, item.Color, -1);
            Renderer3D::EndScene();
            preview.PreviewFramebuffer->Unbind();

            auto& window = CCEngine::Application::Get()->GetWindow();
            RenderCommand::SetViewport(0, 0, window.GetWidth(), window.GetHeight());

            std::vector<uint32_t> pixels;
            const bool readSucceeded = preview.PreviewFramebuffer->ReadColorPixels(pixels);
            if (readSucceeded)
                DumpThumbnailDebugImage("prefab_raw_capture", PrefabPreviewTextureSize, PrefabPreviewTextureSize, pixels);

            const bool captureValid = readSucceeded && HasVisibleMaterialPreviewPixels(pixels);

            if (captureValid)
            {
                DecodedPreviewPixels cropped = CropMaterialPreviewToContent(PrefabPreviewTextureSize, PrefabPreviewTextureSize, pixels, 256);
                if (cropped.Success)
                    DumpThumbnailDebugImage("prefab_cropped_capture", (uint32_t)cropped.Width, (uint32_t)cropped.Height, cropped.Pixels);
                if (cropped.Success)
                    ForcePreviewAlphaOpaque(cropped.Pixels);
                preview.CapturedTexture.reset(cropped.Success ? Texture2D::Create((uint32_t)cropped.Width, (uint32_t)cropped.Height, cropped.Pixels.data()) : nullptr);
            }
            else
            {
                preview.CapturedTexture.reset();
            }

            preview.Rendered = preview.CapturedTexture != nullptr && preview.CapturedTexture->GetRendererID() != nullptr;
            preview.Dirty = !preview.Rendered;
            preview.CaptureFailed = !preview.Rendered;
        }

        void AssetBrowserPanel::PrepareMaterialPreviewRequests()
        {
            int preparedCount = 0;
            constexpr int MaxPreparedMaterialPreviewsPerFrame = 48;
            for (const AssetEntry& entry : m_ViewEntries)
            {
                if (entry.Type != AssetType::Material && !IsMaterialAssetPath(entry.Path))
                    continue;

                // 브라우저 UI가 텍스처를 그리기 전에 캐시 항목부터 만들어 둔다.
                // 렌더는 아래 UpdateMaterialPreviewThumbnails에서 나눠 처리해서 폴더 진입 순간의 멈춤을 줄인다.
                GetMaterialPreviewColor(entry);
                if (++preparedCount >= MaxPreparedMaterialPreviewsPerFrame)
                    break;
            }
        }

        void AssetBrowserPanel::UpdateMaterialPreviewThumbnails()
        {
            if (!m_IsVisible)
                return;

            PrepareMaterialPreviewRequests();

            constexpr int MaxMaterialPreviewRendersPerFrame = 1;
            int rendered = 0;
            for (auto& [key, preview] : m_MaterialPreviewCache)
            {
                if (!preview.Valid || !preview.Dirty)
                    continue;

                RenderMaterialPreviewThumbnail(preview);
                if (++rendered >= MaxMaterialPreviewRendersPerFrame)
                    break;
            }
        }

        void AssetBrowserPanel::UpdatePrefabPreviewThumbnails()
        {
            if (!m_IsVisible)
                return;

            constexpr int MaxPrefabPreviewRendersPerFrame = 1;
            int rendered = 0;
            for (auto& [key, preview] : m_PrefabPreviewCache)
            {
                if (!preview.Valid || !preview.Dirty)
                    continue;

                RenderPrefabPreviewThumbnail(preview);
                if (++rendered >= MaxPrefabPreviewRendersPerFrame)
                    break;
            }
        }

        void AssetBrowserPanel::DrawMaterialPreview(const AssetEntry& entry, float x, float y, float size)
        {
            const std::string key = GetTreeKey(entry.Path);

            auto cached = m_MaterialPreviewCache.find(key);
            auto hasCapturedPreview = [](const MaterialPreviewCacheEntry& preview) -> bool
                {
                    return (!preview.CapturedPixels.empty() &&
                        preview.CapturedPixelWidth > 0 &&
                        preview.CapturedPixelHeight > 0) ||
                        (preview.CapturedTexture && preview.CapturedTexture->GetRendererID() != nullptr);
                };

            if (cached == m_MaterialPreviewCache.end() || !hasCapturedPreview(cached->second))
            {
                for (auto it = m_MaterialPreviewCache.begin(); it != m_MaterialPreviewCache.end(); ++it)
                {
                    if (!hasCapturedPreview(it->second))
                        continue;

                    if (!it->second.SourcePath.empty() && PathsReferToSameExistingFile(it->second.SourcePath, entry.Path))
                    {
                        // 에셋 경로는 상대/절대/canonical 형태가 섞일 수 있다.
                        // 상대경로 캐시가 먼저 만들어져도, 실제 프리뷰 캡처가 있는 같은 파일 캐시를 우선 사용한다.
                        cached = it;
                        break;
                    }
                }
            }

            if (cached != m_MaterialPreviewCache.end() &&
                cached->second.CapturedTexture &&
                cached->second.CapturedTexture->GetRendererID() != nullptr)
            {
                // 썸네일은 인스펙터 프리뷰에서 캡처한 사진 텍스처를 그대로 그린다.
                // 픽셀을 UI 사각형으로 다시 그리면 품질이 떨어지고, 렌더 경로도 실제 에셋 썸네일과 달라진다.
                AppendThumbnailDebugLogOnce(std::string("material-draw-captured|") + key,
                    std::string("material draw captured texture: entry=") + NormalizeAssetPathForKey(entry.Path).generic_string() +
                    " cacheKey=" + cached->first +
                    " source=" + NormalizeAssetPathForKey(cached->second.SourcePath).generic_string() +
                    " pixels=" + std::to_string(cached->second.CapturedPixelWidth) + "x" + std::to_string(cached->second.CapturedPixelHeight));
                UIRenderer::DrawRectFilled(x, y, size, size, { 0.075f, 0.075f, 0.082f, 1.0f });
                UIRenderer::DrawImage(x + 2.0f, y + 2.0f, size - 4.0f, size - 4.0f, cached->second.CapturedTexture->GetRendererID());
                DrawPreviewBorder(x, y, size, size, { 0.22f, 0.22f, 0.24f, 1.0f });
                return;
            }

            auto borrowed = m_BorrowedMaterialPreviews.find(key);
            if (borrowed == m_BorrowedMaterialPreviews.end() || !borrowed->second.Texture)
            {
                for (auto it = m_BorrowedMaterialPreviews.begin(); it != m_BorrowedMaterialPreviews.end(); ++it)
                {
                    if (!it->second.Texture || it->second.SourcePath.empty())
                        continue;

                    if (PathsReferToSameExistingFile(it->second.SourcePath, entry.Path))
                    {
                        // 인스펙터 프리뷰 브리지는 프레임버퍼 SRV를 직접 빌려 쓰는 임시 경로다.
                        // 캐시 key가 달라도 같은 파일이면 같은 프리뷰를 써야 브라우저가 회색 fallback으로 빠지지 않는다.
                        borrowed = it;
                        break;
                    }
                }
            }
            if (borrowed != m_BorrowedMaterialPreviews.end() && borrowed->second.Texture)
            {
                AppendThumbnailDebugLogOnce(std::string("material-draw-borrowed|") + key,
                    std::string("material draw borrowed inspector texture: entry=") + NormalizeAssetPathForKey(entry.Path).generic_string() +
                    " source=" + NormalizeAssetPathForKey(borrowed->second.SourcePath).generic_string());
                UIRenderer::DrawRectFilled(x, y, size, size, { 0.075f, 0.075f, 0.082f, 1.0f });
                UIRenderer::DrawImage(x + 2.0f, y + 2.0f, size - 4.0f, size - 4.0f, borrowed->second.Texture);
                DrawPreviewBorder(x, y, size, size, { 0.22f, 0.22f, 0.24f, 1.0f });
                return;
            }

            RendererHandle renderedPreview = GetMaterialPreviewTexture(entry);
            if (renderedPreview)
            {
                AppendThumbnailDebugLogOnce(std::string("material-draw-rendered|") + key,
                    "material draw rendered thumbnail: entry=" + NormalizeAssetPathForKey(entry.Path).generic_string());
                UIRenderer::DrawRectFilled(x, y, size, size, { 0.075f, 0.075f, 0.082f, 1.0f });
                UIRenderer::DrawImage(x + 2.0f, y + 2.0f, size - 4.0f, size - 4.0f, renderedPreview);
                DrawPreviewBorder(x, y, size, size, { 0.22f, 0.22f, 0.24f, 1.0f });
                return;
            }

            const DirectX::XMFLOAT4* color = GetMaterialPreviewColor(entry);
            if (color)
            {
                auto fallbackCache = m_MaterialPreviewCache.find(key);
                const bool hasCache = fallbackCache != m_MaterialPreviewCache.end();
                AppendThumbnailDebugLogOnce(std::string("material-draw-color-fallback|") + key,
                    std::string("material draw color fallback: entry=") + NormalizeAssetPathForKey(entry.Path).generic_string() +
                    " cache=" + std::to_string(hasCache) +
                    " valid=" + std::to_string(hasCache && fallbackCache->second.Valid) +
                    " rendered=" + std::to_string(hasCache && fallbackCache->second.Rendered) +
                    " hasTexture=" + std::to_string(hasCache && fallbackCache->second.CapturedTexture != nullptr) +
                    " hasPixels=" + std::to_string(hasCache && !fallbackCache->second.CapturedPixels.empty()));
                UIRenderer::DrawRectFilled(x, y, size, size, { 0.075f, 0.075f, 0.082f, 1.0f });
                DrawPreviewBorder(x, y, size, size, { 0.22f, 0.22f, 0.24f, 1.0f });
            }
        }

        void AssetBrowserPanel::DrawAssetPreview(const AssetEntry& entry, float x, float y, float size)
        {
            if (entry.Type == AssetType::Material || IsMaterialAssetPath(entry.Path))
            {
                DrawMaterialPreview(entry, x, y, size);
                return;
            }

            if (entry.Type == AssetType::Prefab)
            {
                RendererHandle prefabPreview = GetPrefabPreviewTexture(entry);
                if (prefabPreview)
                {
                    UIRenderer::DrawRectFilled(x, y, size, size, { 0.075f, 0.075f, 0.082f, 1.0f });
                    UIRenderer::DrawImage(x + 2.0f, y + 2.0f, size - 4.0f, size - 4.0f, prefabPreview);
                    DrawPreviewBorder(x, y, size, size, { 0.22f, 0.22f, 0.24f, 1.0f });
                    return;
                }
            }

            UIRenderer::DrawRectFilled(x, y, size, size, { 0.075f, 0.075f, 0.082f, 1.0f });
            DrawPreviewBorder(x, y, size, size, { 0.22f, 0.22f, 0.24f, 1.0f });

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
                const float iconSizes[5] = { 18.0f, 36.0f, 64.0f, 96.0f, 128.0f };
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

        void AssetBrowserPanel::OnUpdate(float deltaTime)
        {
            WindowPanel::OnUpdate(deltaTime);
            CheckExternalFileChanges();

            // 썸네일용 3D 렌더는 UI가 그려지는 OnRender 안에서 돌리면 렌더 상태가 섞인다.
            // update 단계에서 오프스크린 렌더를 끝내고, OnRender는 완성된 텍스처만 그린다.
            UpdateMaterialPreviewThumbnails();
            UpdatePrefabPreviewThumbnails();
        }

        void AssetBrowserPanel::OnRender()
        {
            if (!m_IsVisible)
                return;

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
            const ToolbarMetrics toolbar = GetToolbarMetrics();
            float btnY = toolbar.ButtonY;
            float plusX = toolbar.PlusX;
            float minusX = toolbar.MinusX;
            float sortX = toolbar.SortX;
            float typeX = toolbar.TypeX;
            float searchX = toolbar.SearchX;
            float searchW = toolbar.SearchW;
            DirectX::XMFLOAT4 searchBg = m_SearchFocused
                ? DirectX::XMFLOAT4{ 0.13f, 0.16f, 0.19f, 1.0f }
                : DirectX::XMFLOAT4{ 0.08f, 0.08f, 0.09f, 1.0f };
            if (searchW > 8.0f)
            {
                UIRenderer::DrawRectFilled(searchX, btnY, searchW, btnSize, searchBg);
                UIRenderer::DrawRect({ searchX, btnY }, { searchW, btnSize }, m_SearchFocused
                    ? DirectX::XMFLOAT4{ 0.30f, 0.42f, 0.54f, 1.0f }
                    : DirectX::XMFLOAT4{ 0.20f, 0.20f, 0.21f, 1.0f });
                std::string searchText = m_SearchQuery.empty() ? "Search..." : m_SearchQuery;
                if (searchText.size() > (size_t)(searchW / 7.0f))
                    searchText = searchText.substr(0, (size_t)(searchW / 7.0f));
                UIRenderer::DrawString(searchText, searchX + 8.0f, btnY + 17.0f,
                    m_SearchQuery.empty() ? DirectX::XMFLOAT4{ 0.48f, 0.48f, 0.50f, 1.0f } : DirectX::XMFLOAT4{ 0.86f, 0.86f, 0.86f, 1.0f });
            }
            DirectX::XMFLOAT4 typeBg = m_TypeFilter == TypeFilter::All
                ? DirectX::XMFLOAT4{ 0.18f, 0.18f, 0.19f, 1.0f }
                : DirectX::XMFLOAT4{ 0.18f, 0.30f, 0.42f, 1.0f };
            if (m_TypeFilterDropdownVisible)
                typeBg = { 0.22f, 0.34f, 0.46f, 1.0f };
            UIRenderer::DrawRectFilled(typeX, btnY, kTypeFilterButtonWidth, btnSize, typeBg);
            UIRenderer::DrawRect({ typeX, btnY }, { kTypeFilterButtonWidth, btnSize }, m_TypeFilterDropdownVisible
                ? DirectX::XMFLOAT4{ 0.42f, 0.56f, 0.68f, 1.0f }
                : DirectX::XMFLOAT4{ 0.24f, 0.24f, 0.25f, 1.0f });
            UIRenderer::DrawRectFilled(typeX + kTypeFilterButtonWidth - 24.0f, btnY, 24.0f, btnSize, { 0.13f, 0.13f, 0.14f, 1.0f });
            UIRenderer::DrawString(GetTypeFilterLabel(), typeX + 6.0f, btnY + 17.0f, { 0.88f, 0.88f, 0.90f, 1.0f });
            UIRenderer::DrawString(m_TypeFilterDropdownVisible ? "^" : "v", typeX + kTypeFilterButtonWidth - 17.0f, btnY + 17.0f, { 0.78f, 0.78f, 0.80f, 1.0f });
            DirectX::XMFLOAT4 sortBg = m_SortDropdownVisible
                ? DirectX::XMFLOAT4{ 0.22f, 0.34f, 0.46f, 1.0f }
                : DirectX::XMFLOAT4{ 0.18f, 0.18f, 0.19f, 1.0f };
            UIRenderer::DrawRectFilled(sortX, btnY, kSortButtonWidth, btnSize, sortBg);
            UIRenderer::DrawRect({ sortX, btnY }, { kSortButtonWidth, btnSize }, m_SortDropdownVisible
                ? DirectX::XMFLOAT4{ 0.42f, 0.56f, 0.68f, 1.0f }
                : DirectX::XMFLOAT4{ 0.24f, 0.24f, 0.25f, 1.0f });
            UIRenderer::DrawRectFilled(sortX + kSortButtonWidth - 24.0f, btnY, 24.0f, btnSize, { 0.13f, 0.13f, 0.14f, 1.0f });
            UIRenderer::DrawString(GetSortModeLabel(), sortX + 6.0f, btnY + 17.0f, { 0.88f, 0.88f, 0.90f, 1.0f });
            UIRenderer::DrawString(m_SortDropdownVisible ? "^" : "v", sortX + kSortButtonWidth - 17.0f, btnY + 17.0f, { 0.78f, 0.78f, 0.80f, 1.0f });
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

                    const AssetEntry& viewEntry = m_ViewEntries[i];
                    bool subAsset = IsVirtualSubAsset(viewEntry);
                    DirectX::XMFLOAT4 rowColor = subAsset
                        ? DirectX::XMFLOAT4{ 0.16f, 0.16f, 0.17f, 1.0f }
                        : DirectX::XMFLOAT4{ 0.13f, 0.13f, 0.14f, 1.0f };
                    if (IsEntrySelected((int)i)) rowColor = { 0.18f, 0.30f, 0.42f, 1.0f };
                    else if (hovered) rowColor = { 0.20f, 0.20f, 0.21f, 1.0f };

                    UIRenderer::DrawRectFilled(startX, currentY, rowWidth, m_RowHeight, rowColor);
                    if (subAsset)
                    {
                        // FBX 내부 항목은 독립 파일이 아니라 부모 FBX 안의 sub-asset이다.
                        // 리스트 모드에서는 들여쓰기와 연결선으로 부모 아래에 속한 항목임을 보여준다.
                        UIRenderer::DrawRectFilled(startX + 20.0f, currentY, 2.0f, m_RowHeight + m_RowGap, { 0.34f, 0.34f, 0.36f, 1.0f });
                        UIRenderer::DrawRectFilled(startX + 20.0f, currentY + m_RowHeight * 0.5f, 18.0f, 2.0f, { 0.34f, 0.34f, 0.36f, 1.0f });
                    }

                    float iconOffset = subAsset ? 32.0f : 0.0f;
                    DrawAssetPreview(viewEntry, startX + 7.0f + iconOffset, currentY + 4.0f, 18.0f);
                    if (IsFbxContainer(viewEntry))
                    {
                        bool expanded = m_ExpandedFbxAssets.find(GetTreeKey(viewEntry.Path)) != m_ExpandedFbxAssets.end();
                        UIRenderer::DrawRectFilled(startX + 8.0f, currentY + 6.0f, 14.0f, 14.0f, { 0.23f, 0.24f, 0.26f, 1.0f });
                        UIRenderer::DrawRect({ startX + 8.0f, currentY + 6.0f }, { 14.0f, 14.0f }, { 0.48f, 0.50f, 0.54f, 1.0f });
                        UIRenderer::DrawString(expanded ? "v" : ">", startX + 11.0f, currentY + 18.0f, { 0.88f, 0.88f, 0.90f, 1.0f });
                    }
                    UIRenderer::DrawString(GetTypeLabel(viewEntry.Type), startX + 34.0f + iconOffset, currentY + 18.0f, { 0.82f, 0.78f, 0.58f, 1.0f });
                    UIRenderer::DrawString(viewEntry.DisplayName, startX + 76.0f + iconOffset, currentY + 18.0f, { 0.86f, 0.86f, 0.86f, 1.0f });
                }
            }
            else
            {
                // 1~4단계는 아이콘 모드다. 단계가 올라갈수록 아이콘과 셀을 같이 키운다.
                const float iconSizes[5] = { 18.0f, 36.0f, 64.0f, 96.0f, 128.0f };
                float iconSize = iconSizes[(std::clamp)(m_IconSizeStep, 0, 4)];
                float cellW = iconSize + 58.0f;
                float cellH = iconSize + 42.0f;
                int columns = (std::max)(1, (int)((contentW - 16.0f) / cellW));
                int totalRows = (int)std::ceil((float)m_ViewEntries.size() / (float)columns);

                for (int row = 0; row < totalRows; ++row)
                {
                    int rowStart = row * columns;
                    int rowEnd = (std::min)((int)m_ViewEntries.size(), rowStart + columns);
                    float rowY = startY + (float)row * cellH;
                    if (rowY + cellH < treeY || rowY > treeY + treeH)
                        continue;

                    int cursor = rowStart;
                    while (cursor < rowEnd)
                    {
                        if (!IsVirtualSubAsset(m_ViewEntries[cursor]))
                        {
                            ++cursor;
                            continue;
                        }

                        std::string parentKey = m_ViewEntries[cursor].SubAssetParentKey;
                        int groupStart = cursor;
                        while (cursor < rowEnd &&
                            IsVirtualSubAsset(m_ViewEntries[cursor]) &&
                            m_ViewEntries[cursor].SubAssetParentKey == parentKey)
                        {
                            ++cursor;
                        }

                        int groupEnd = cursor - 1;
                        int startCol = groupStart - rowStart;
                        int endCol = groupEnd - rowStart;
                        float bandX = startX + (float)startCol * cellW + 2.0f;
                        float bandY = rowY + 2.0f;
                        float bandW = (float)(endCol - startCol + 1) * cellW - 10.0f;
                        float bandH = cellH - 10.0f;

                        DirectX::XMFLOAT4 bandColor = { 0.31f, 0.31f, 0.31f, 1.0f };
                        DirectX::XMFLOAT4 bandEdgeColor = { 0.36f, 0.36f, 0.36f, 1.0f };
                        DirectX::XMFLOAT4 contentBgColor = { 0.095f, 0.095f, 0.10f, 1.0f };

                        // Unity처럼 FBX 내부 항목은 한 줄 단위의 회색 컨테이너 안에 묶어 보여준다.
                        // 같은 FBX가 다음 줄로 이어질 때는 오른쪽 끝을 파고, 다음 줄 왼쪽에 연결 모양을 붙인다.
                        UIRenderer::DrawRectFilled(bandX, bandY + 3.0f, bandW, bandH - 6.0f, bandColor);
                        UIRenderer::DrawRectFilled(bandX + 4.0f, bandY, bandW - 8.0f, bandH, bandColor);
                        UIRenderer::DrawRectFilled(bandX + 2.0f, bandY + 1.0f, bandW - 4.0f, 1.0f, bandEdgeColor);
                        UIRenderer::DrawRectFilled(bandX + 2.0f, bandY + bandH - 2.0f, bandW - 4.0f, 1.0f, { 0.20f, 0.20f, 0.20f, 1.0f });

                        bool continuesFromPreviousRow = groupStart == rowStart &&
                            groupStart > 0 &&
                            IsVirtualSubAsset(m_ViewEntries[groupStart - 1]) &&
                            m_ViewEntries[groupStart - 1].SubAssetParentKey == parentKey;
                        bool continuesToNextRow = groupEnd == rowEnd - 1 &&
                            groupEnd + 1 < (int)m_ViewEntries.size() &&
                            IsVirtualSubAsset(m_ViewEntries[groupEnd + 1]) &&
                            m_ViewEntries[groupEnd + 1].SubAssetParentKey == parentKey;

                        if (continuesFromPreviousRow)
                        {
                            float cy = bandY + bandH * 0.5f;
                            UIRenderer::DrawRectFilled(bandX - 10.0f, cy - 4.0f, 10.0f, 8.0f, bandColor);
                            UIRenderer::DrawRectFilled(bandX - 7.0f, cy - 10.0f, 7.0f, 6.0f, bandColor);
                            UIRenderer::DrawRectFilled(bandX - 7.0f, cy + 4.0f, 7.0f, 6.0f, bandColor);
                            UIRenderer::DrawRectFilled(bandX - 3.0f, cy - 15.0f, 3.0f, 5.0f, bandColor);
                            UIRenderer::DrawRectFilled(bandX - 3.0f, cy + 10.0f, 3.0f, 5.0f, bandColor);
                        }
                        if (continuesToNextRow)
                        {
                            float cy = bandY + bandH * 0.5f;
                            float rx = bandX + bandW;
                            UIRenderer::DrawRectFilled(rx - 10.0f, cy - 4.0f, 10.0f, 8.0f, contentBgColor);
                            UIRenderer::DrawRectFilled(rx - 7.0f, cy - 10.0f, 7.0f, 6.0f, contentBgColor);
                            UIRenderer::DrawRectFilled(rx - 7.0f, cy + 4.0f, 7.0f, 6.0f, contentBgColor);
                            UIRenderer::DrawRectFilled(rx - 3.0f, cy - 15.0f, 3.0f, 5.0f, contentBgColor);
                            UIRenderer::DrawRectFilled(rx - 3.0f, cy + 10.0f, 3.0f, 5.0f, contentBgColor);
                        }
                    }
                }

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

            if (m_TypeFilterDropdownVisible)
            {
                const ToolbarMetrics dropdownToolbar = GetToolbarMetrics();
                float x = dropdownToolbar.TypeX;
                float y = dropdownToolbar.ButtonY + kToolbarButtonHeight + 2.0f;
                float w = kTypeFilterButtonWidth;
                float itemH = kDropdownItemHeight;
                float h = itemH * (float)kTypeFilterItemCount;
                UIRenderer::DrawRectFilled(x + 3.0f, y + 3.0f, w, h, { 0.03f, 0.03f, 0.035f, 0.55f });
                UIRenderer::DrawRectFilled(x, y, w, h, { 0.125f, 0.125f, 0.135f, 1.0f });
                UIRenderer::DrawRect({ x, y }, { w, h }, { 0.42f, 0.42f, 0.45f, 1.0f });

                for (int i = 0; i < kTypeFilterItemCount; ++i)
                {
                    TypeFilter filter = (TypeFilter)i;
                    float itemY = y + (float)i * itemH;
                    bool hovered = Widget::IsCurrentRenderWindowMouseActive() &&
                        mouseX >= x && mouseX <= x + w &&
                        mouseY >= itemY && mouseY <= itemY + itemH;
                    if (filter == m_TypeFilter)
                        UIRenderer::DrawRectFilled(x, itemY, w, itemH, { 0.18f, 0.30f, 0.42f, 1.0f });
                    else if (hovered)
                        UIRenderer::DrawRectFilled(x, itemY, w, itemH, { 0.22f, 0.22f, 0.23f, 1.0f });

                    UIRenderer::DrawString(GetTypeFilterLabel(filter), x + 8.0f, itemY + 18.0f, { 0.88f, 0.88f, 0.90f, 1.0f });
                }
            }

            if (m_SortDropdownVisible)
            {
                const ToolbarMetrics dropdownToolbar = GetToolbarMetrics();
                float x = dropdownToolbar.SortX;
                float y = dropdownToolbar.ButtonY + kToolbarButtonHeight + 2.0f;
                float w = kSortButtonWidth;
                float itemH = kDropdownItemHeight;
                float h = itemH * (float)kSortItemCount;
                UIRenderer::DrawRectFilled(x + 3.0f, y + 3.0f, w, h, { 0.03f, 0.03f, 0.035f, 0.55f });
                UIRenderer::DrawRectFilled(x, y, w, h, { 0.125f, 0.125f, 0.135f, 1.0f });
                UIRenderer::DrawRect({ x, y }, { w, h }, { 0.42f, 0.42f, 0.45f, 1.0f });

                for (int i = 0; i < kSortItemCount; ++i)
                {
                    SortMode mode = (SortMode)i;
                    float itemY = y + (float)i * itemH;
                    bool hovered = Widget::IsCurrentRenderWindowMouseActive() &&
                        mouseX >= x && mouseX <= x + w &&
                        mouseY >= itemY && mouseY <= itemY + itemH;
                    if (mode == m_SortMode)
                        UIRenderer::DrawRectFilled(x, itemY, w, itemH, { 0.18f, 0.30f, 0.42f, 1.0f });
                    else if (hovered)
                        UIRenderer::DrawRectFilled(x, itemY, w, itemH, { 0.22f, 0.22f, 0.23f, 1.0f });

                    UIRenderer::DrawString(GetSortModeLabel(mode), x + 8.0f, itemY + 18.0f, { 0.88f, 0.88f, 0.90f, 1.0f });
                }
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
                    bool clickedDropdownButton =
                        IsTypeFilterButtonPoint(me.GetX(), me.GetY()) ||
                        IsSortButtonPoint(me.GetX(), me.GetY());

                    if ((m_TypeFilterDropdownVisible || m_SortDropdownVisible) && !clickedDropdownButton)
                    {
                        // 드롭다운이 열려 있으면 먼저 드롭다운이 입력을 가져간다.
                        // 목록 뒤의 에셋이나 트리 항목이 동시에 선택되면 메뉴 조작이 불안정해진다.
                        if (m_TypeFilterDropdownVisible)
                        {
                            int item = GetTypeFilterDropdownItemAt(me.GetX(), me.GetY());
                            if (item >= 0)
                                SetTypeFilter((TypeFilter)item);
                        }
                        else if (m_SortDropdownVisible)
                        {
                            int item = GetSortDropdownItemAt(me.GetX(), me.GetY());
                            if (item >= 0)
                                SetSortMode((SortMode)item);
                        }

                        m_TypeFilterDropdownVisible = false;
                        m_SortDropdownVisible = false;
                        if (IsPointInside(me.GetX(), me.GetY()))
                        {
                            e.Handled = true;
                            return true;
                        }
                    }

                    if (me.GetY() >= btnY && me.GetY() <= btnY + btnSize &&
                        me.GetX() >= minusX && me.GetX() <= minusX + btnSize)
                    {
                        StepIconSize(-1);
                        m_SearchFocused = false;
                        e.Handled = true;
                        return true;
                    }

                    if (me.GetY() >= btnY && me.GetY() <= btnY + btnSize &&
                        me.GetX() >= plusX && me.GetX() <= plusX + btnSize)
                    {
                        StepIconSize(1);
                        m_SearchFocused = false;
                        e.Handled = true;
                        return true;
                    }

                    if (IsTypeFilterButtonPoint(me.GetX(), me.GetY()))
                    {
                        m_TypeFilterDropdownVisible = !m_TypeFilterDropdownVisible;
                        m_SortDropdownVisible = false;
                        m_SearchFocused = false;
                        m_ContextMenuVisible = false;
                        e.Handled = true;
                        return true;
                    }

                    if (IsSortButtonPoint(me.GetX(), me.GetY()))
                    {
                        m_SortDropdownVisible = !m_SortDropdownVisible;
                        m_TypeFilterDropdownVisible = false;
                        m_SearchFocused = false;
                        m_ContextMenuVisible = false;
                        e.Handled = true;
                        return true;
                    }

                    if (IsSearchBoxPoint(me.GetX(), me.GetY()))
                    {
                        m_SearchFocused = true;
                        m_ContextMenuVisible = false;
                        m_TypeFilterDropdownVisible = false;
                        m_SortDropdownVisible = false;
                        e.Handled = true;
                        return true;
                    }
                    else if (IsPointInside(me.GetX(), me.GetY()))
                    {
                        m_SearchFocused = false;
                        m_TypeFilterDropdownVisible = false;
                        m_SortDropdownVisible = false;
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
                        else if (command == "Create Material")
                            CreateMaterialInCurrentDirectory();
                        else if (command == "Refresh")
                            RefreshCurrentFolder(false);
                        else if (command == "Refresh All")
                        {
                            AssetRefreshReport report = AssetDatabase::RefreshAssets(m_RootDirectory, false);
                            for (const AssetImportResult& result : report.Results)
                            {
                                if (result.Success && result.Type == AssetKind::Script)
                                {
                                    ScriptCompiler::RequestCompile();
                                    break;
                                }
                            }
                            m_TexturePreviewCache.clear();
                            m_TexturePreviewOrder.clear();
                            InvalidateMaterialPreviewCache(false);
                            m_TreeChildCache.clear();
                            Refresh(false);
                            NotifyAssetDatabaseChanged();
                        }
                        else if (command == "Reimport")
                            ReimportSelectedAssets();
                        else if (command == "Show in Folder")
                            ShowSelectedEntryInFolder();
                        else if (command == "Show in Explorer")
                            RevealSelectedEntryInExplorer();
                        else if (command == "Run QA Checks")
                            RunQualityRegressionChecks();
                        else if (command == "Rename")
                            BeginRenameSelected();
                        else if (command == "Delete")
                            RequestDeleteSelectedAsset();

                        e.Handled = true;
                        return true;
                    }

                    m_ContextMenuVisible = false;
                }

                if (me.GetButton() == 0 && IsTreeScrollbarPoint(me.GetX(), me.GetY()))
                {
                    m_IsDraggingTreeScrollbar = true;
                    m_IsMouseDownOnEntry = false;
                    m_DragMouseStartY = me.GetY();
                    m_DragTreeScrollStartY = m_TreeScrollState.ScrollY;
                    Widget::BeginMouseInteraction(this);
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
                    Widget::BeginMouseInteraction(this);
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
                    m_TypeFilterDropdownVisible = false;
                    m_SortDropdownVisible = false;
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
                            {
                                m_ContextMenuItems.push_back("Show in Folder");
                                m_ContextMenuItems.push_back("Show in Explorer");
                                m_ContextMenuItems.push_back("Rename");
                            }
                            m_ContextMenuItems.push_back("Reimport");
                            m_ContextMenuItems.push_back("Delete");
                        }
                    }
                    else
                    {
                        ClearSelection();
                        m_ContextMenuItems.push_back("Create Folder");
                        m_ContextMenuItems.push_back("Create Material");
                        m_ContextMenuItems.push_back("Refresh");
                        m_ContextMenuItems.push_back("Refresh All");
                        m_ContextMenuItems.push_back("Run QA Checks");
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
                m_ScrollState.SetFromThumbDrag(me.GetY(), m_DragMouseStartY, m_DragScrollStartY);

                e.Handled = true;
                return true;
            }

            if (e.GetEventType() == EventType::MouseMoved && m_IsDraggingTreeScrollbar)
            {
                auto& me = static_cast<MouseMovedEvent&>(e);
                m_TreeScrollState.SetFromThumbDrag(me.GetY(), m_DragMouseStartY, m_DragTreeScrollStartY);

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
                    Widget::EndMouseInteraction(this);
                    e.Handled = true;
                    return true;
                }

                if (me.GetButton() == 0 && m_IsDraggingTreeScrollbar)
                {
                    m_IsDraggingTreeScrollbar = false;
                    Widget::EndMouseInteraction(this);
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
