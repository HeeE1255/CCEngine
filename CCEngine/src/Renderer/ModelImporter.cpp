#include "ModelImporter.h"
#include "Core/AssetDatabase.h"
#include "Scene/Components.h"
#include "Renderer/Texture.h"
#include "Core/ConsoleLog.h"
#include <cctype>
#include <cwctype>
#include <cmath>
#include <limits>
#include <array>
#include <stdexcept>
#include "stb_image.h"
#include <fstream>
#include <iterator>
#include <functional>
#include <chrono>
#include <random>
#include <filesystem>
#include <unordered_map>
#include <set>
#include <Windows.h>

namespace CCEngine {
    namespace
    {
        std::unordered_map<std::string, std::weak_ptr<Model>> s_ModelCache;

        bool SameImportedValue(const nlohmann::json& left, const nlohmann::json& right)
        {
            if (left == right) return true;
            if (left.is_number_float() && right.is_number_float())
            {
                const double a = left.get<double>(), b = right.get<double>();
                // 재질 런타임은 float다. JSON의 0.6과 float 저장값 0.6000000238은
                // 같은 런타임 값이므로 충돌이 아니다. 임의 epsilon은 실제 작은 수정을 숨길 수 있다.
                if (!std::isfinite(a) || !std::isfinite(b) ||
                    std::abs(a) > (std::numeric_limits<float>::max)() || std::abs(b) > (std::numeric_limits<float>::max)())
                    return false;
                return static_cast<float>(a) == static_cast<float>(b);
            }
            if (left.is_array() && right.is_array())
            {
                if (left.size() != right.size()) return false;
                for (size_t i = 0; i < left.size(); ++i)
                    if (!SameImportedValue(left[i], right[i])) return false;
                return true;
            }
            if (left.is_object() && right.is_object())
            {
                if (left.size() != right.size()) return false;
                for (const auto& [key, value] : left.items())
                {
                    const auto found = right.find(key);
                    if (found == right.end() || !SameImportedValue(value, *found)) return false;
                }
                return true;
            }
            return false;
        }

        std::string GeneratedImageFingerprint(const std::filesystem::path& path)
        {
            if (std::filesystem::file_size(path) > 256ull * 1024 * 1024 + 18)
                throw std::runtime_error("Generated image exceeds audit budget");
            std::ifstream input(path, std::ios::binary);
            if (!input) throw std::runtime_error("Cannot read generated image");
            uint64_t hash = 14695981039346656037ull;
            std::array<char, 65536> buffer;
            while (input.read(buffer.data(), buffer.size()) || input.gcount())
                for (std::streamsize i = 0; i < input.gcount(); ++i)
                {
                    hash ^= static_cast<unsigned char>(buffer[static_cast<size_t>(i)]);
                    hash *= 1099511628211ull;
                }
            if (!input.eof()) throw std::runtime_error("Generated image read failed");
            return std::to_string(hash);
        }

        std::string ExtractEmbeddedTexture(const ImportedEmbeddedTexture& image, const std::filesystem::path& folder,
            const std::string& modelGuid)
        {
            // 내용별 추출물을 사용하면 재임포트 충돌이나 실패가 기존 이미지까지 바꾸지 않는다.
            // 해시는 경로 식별용이며 기존 파일과의 실제 바이트 비교로 충돌을 검출한다.
            uint64_t hash = 14695981039346656037ull;
            for (auto byte : image.Bytes) { hash ^= byte; hash *= 1099511628211ull; }
            const auto output = folder / ("Embedded_" + std::to_string(image.SourceIndex) + "_" +
                std::to_string(image.Width) + "x" + std::to_string(image.Height) + "_" + std::to_string(hash) + ".tga");
            int width = 0, height = 0, channels = 0;
            if (std::filesystem::exists(output))
            {
                if (!std::filesystem::is_regular_file(output) || !stbi_info(output.string().c_str(), &width, &height, &channels))
                    throw std::runtime_error("Existing extracted texture is unreadable; file preserved");
            }
            std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> decoded(nullptr, stbi_image_free);
            const unsigned char* pixels = image.Bytes.data();
            if (image.Height == 0)
            {
                if (image.Bytes.empty() || image.Bytes.size() > 256u * 1024u * 1024u ||
                    !stbi_info_from_memory(pixels, static_cast<int>(image.Bytes.size()), &width, &height, &channels))
                    throw std::runtime_error("Unsupported or corrupt embedded image");
            }
            else
            {
                width = static_cast<int>(image.Width);
                height = static_cast<int>(image.Height);
            }
            if (width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
                uint64_t(width) * height * 4u > 256u * 1024u * 1024u)
                throw std::runtime_error("Embedded image exceeds dimension or decoded size limit");
            if (image.Height == 0)
            {
                // 기존 텍스처 로더와 같은 원본 방향으로 읽고 TGA 헤더에 상단 원점을 기록한다.
                decoded.reset(stbi_load_from_memory(pixels, static_cast<int>(image.Bytes.size()), &width, &height, &channels, 4));
                if (!decoded) throw std::runtime_error("Embedded image decoding failed");
                pixels = decoded.get();
            }
            else if (image.Bytes.size() != size_t(width) * height * 4u)
                throw std::runtime_error("Embedded BGRA data size mismatch");

            std::filesystem::create_directories(folder);
            std::random_device random;
            auto temporary = output;
            temporary += "." + std::to_string(random()) + "." + std::to_string(random()) + ".tmp";
            if (std::filesystem::exists(temporary)) throw std::runtime_error("Temporary texture filename collision");
            try
            {
                std::ofstream file(temporary, std::ios::binary);
                if (!file) throw std::runtime_error("Cannot create extracted texture");
                std::array<unsigned char, 18> header{};
                header[2] = 2; // 비압축 true-color TGA, BGRA 32비트, 좌측 상단 원점.
                header[12] = static_cast<unsigned char>(width);
                header[13] = static_cast<unsigned char>(width >> 8);
                header[14] = static_cast<unsigned char>(height);
                header[15] = static_cast<unsigned char>(height >> 8);
                header[16] = 32;
                header[17] = 0x28;
                file.write(reinterpret_cast<const char*>(header.data()), header.size());
                std::vector<unsigned char> row(size_t(width) * 4);
                for (int y = 0; y < height; ++y)
                {
                    const auto* source = pixels + size_t(y) * row.size();
                    std::copy_n(source, row.size(), row.data());
                    if (decoded)
                        for (size_t x = 0; x < row.size(); x += 4) std::swap(row[x], row[x + 2]);
                    file.write(reinterpret_cast<const char*>(row.data()), row.size());
                }
                file.flush();
                if (!file) throw std::runtime_error("Extracted texture write failed");
                file.close();
                if (!file) throw std::runtime_error("Extracted texture close failed");
                if (std::filesystem::exists(output))
                {
                    std::ifstream oldFile(output, std::ios::binary), newFile(temporary, std::ios::binary);
                    if (!oldFile || !newFile || std::filesystem::file_size(output) != std::filesystem::file_size(temporary) ||
                        !std::equal(std::istreambuf_iterator<char>(oldFile), std::istreambuf_iterator<char>(),
                            std::istreambuf_iterator<char>(newFile)))
                        throw std::runtime_error("Extracted image was edited or hash collided; existing file preserved");
                    oldFile.close(); newFile.close();
                    std::filesystem::remove(temporary);
                }
                else
                {
                    std::filesystem::rename(temporary, output);
                    // 새로 생성한 파일에만 소유 기록을 남긴다. 기존 파일을 검사했다는 이유로 소유권을 가져오지 않는다.
                    const auto marker = std::filesystem::path(output.string() + ".ccgenerated");
                    auto stagedMarker = temporary;
                    stagedMarker += ".ccgenerated";
                    try
                    {
                        if (std::filesystem::exists(marker)) throw std::runtime_error("Generated image ownership record already exists");
                        nlohmann::json record = { { "Version", 1 }, { "Owner", "CCEngine.ModelImporter" },
                            { "SourceGuid", modelGuid }, { "FileName", output.filename().string() },
                            { "Size", std::filesystem::file_size(output) }, { "Fingerprint", GeneratedImageFingerprint(output) } };
                        std::ofstream stamp(stagedMarker, std::ios::binary);
                        stamp << record.dump(2);
                        stamp.flush();
                        if (!stamp) throw std::runtime_error("Cannot write generated image ownership record");
                        stamp.close();
                        if (!stamp) throw std::runtime_error("Cannot close generated image ownership record");
                        std::filesystem::rename(stagedMarker, marker);
                    }
                    catch (const std::exception& error)
                    {
                        std::error_code ignored;
                        std::filesystem::remove(stagedMarker, ignored);
                        // 추출 이미지는 유효하다. 이력 저장 실패는 자동 정리 불가로 남기고 임포트는 유지한다.
                        ConsoleLog::Warning("Generated image retained without verified ownership: " + output.string() + ": " + error.what());
                    }
                }
            }
            catch (...)
            {
                std::error_code error;
                std::filesystem::remove(temporary, error);
                throw;
            }
            return output.string();
        }

        std::shared_ptr<Model> GetOrLoadModel(const std::string& filepath)
        {
            std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(std::filesystem::path(filepath));
            std::string cacheKey = canonicalPath.string();

            auto it = s_ModelCache.find(cacheKey);
            if (it != s_ModelCache.end())
            {
                if (auto cachedModel = it->second.lock())
                {
                    return cachedModel;
                }
            }

            auto model = std::make_shared<Model>(filepath);
            s_ModelCache[cacheKey] = model;
            return model;
        }
    }

    void ModelImporter::ClearCache()
    {
        // 모델 캐시는 같은 FBX를 여러 번 불러올 때 파싱 비용을 줄이기 위한 세션 캐시다.
        // weak_ptr만 담고 있어도 맵 버킷 메모리는 남으므로 종료 시 비워 준다.
        std::unordered_map<std::string, std::weak_ptr<Model>>().swap(s_ModelCache);
    }

    bool ModelImporter::ReimportMaterials(const std::string& filepath, bool adoptLegacyBaseline,
        std::vector<std::string>* refreshedMaterials)
    {
        if (refreshedMaterials) refreshedMaterials->clear();
        try
        {
            // 명시적 재임포트는 살아 있는 weak cache도 우회해야 변경된 원본을 읽는다.
            Model model(filepath);
            if (model.GetMeshes().empty()) throw std::runtime_error("Source model contains no meshes");
            const auto materials = PrepareMaterials(model, true, adoptLegacyBaseline);
            // 일부 재질만 성공해도 그 파일은 이미 저장됐다. 성공 결과만 호출자에게 전달한다.
            for (const auto& [slot, entry] : materials)
                if (entry.second && refreshedMaterials) refreshedMaterials->push_back(entry.first);
            const bool success = !materials.empty() && std::all_of(materials.begin(), materials.end(),
                [](const auto& item) { return bool(item.second.second); });
            if (std::any_of(materials.begin(), materials.end(), [](const auto& item) { return bool(item.second.second); }))
                ClearCache();
            return success;
        }
        catch (const std::exception& error)
        {
            ConsoleLog::Error("Material reimport failed: " + filepath + ": " + error.what());
            return false;
        }
    }

    bool ModelImporter::BindMaterial(const std::string& filepath, unsigned int slot, const std::string& expectedName, const std::string& materialPath)
    {
        try
        {
            if (AssetDatabase::GetAssetKind(materialPath) != AssetKind::Material) return false;
            Model model(filepath);
            bool found = false;
            for (const auto& mesh : model.GetMeshes())
                if (mesh && mesh->ImportedMaterial && mesh->ImportedMaterial->SourceIndex == slot && mesh->ImportedMaterial->Name == expectedName)
                    found = true;
            if (!found) throw std::runtime_error("Source slot changed while picker was open");
            const auto materials = PrepareMaterials(model, true, true, static_cast<int>(slot), materialPath);
            const auto result = materials.find(slot);
            if (result == materials.end() || !result->second.second) return false;
            ClearCache();
            return true;
        }
        catch (const std::exception& error)
        {
            ConsoleLog::Error("Material binding failed: " + std::string(error.what()));
            return false;
        }
    }

    ModelImporter::ImportedMaterialMap ModelImporter::PrepareMaterials(const Model& model, bool reimport, bool adoptLegacyBaseline,
        int explicitSlot, const std::string& explicitPath)
    {
        ImportedMaterialMap result;
        const auto modelPath = std::filesystem::path(model.GetFilePath());
        const auto folder = modelPath.parent_path() / (modelPath.filename().string() + ".materials");
        const auto shaderPath = std::filesystem::path("assets/shaders/Base3D_PBR.hlsl");
        const auto modelGuid = AssetDatabase::GetGuidFromPath(modelPath);
        std::unordered_map<std::string, std::vector<std::filesystem::path>> knownMaterials;
        std::unordered_map<unsigned int, std::vector<std::filesystem::path>> explicitMaterials;
        std::unordered_map<std::string, std::vector<unsigned int>> sourceNames;
        for (const auto& mesh : model.GetMeshes())
        {
            if (!mesh || !mesh->ImportedMaterial) continue;
            const auto& source = *mesh->ImportedMaterial;
            auto& indices = sourceNames[source.Name];
            if (std::find(indices.begin(), indices.end(), source.SourceIndex) == indices.end())
                indices.push_back(source.SourceIndex);
        }
        // 모델과 재질은 따로 이동할 수 있다. 폴더 이름 대신 원본 GUID로 프로젝트 후보를 찾는다.
        auto candidates = AssetDatabase::GetAssetPaths(AssetKind::Material);
        if (std::filesystem::is_directory(folder))
            for (const auto& file : std::filesystem::directory_iterator(folder))
                if (file.is_regular_file() && AssetDatabase::GetAssetKind(file.path()) == AssetKind::Material)
                    candidates.push_back(file.path());
        std::set<std::wstring> visited;
        for (const auto& candidate : candidates)
        {
            std::error_code error;
            if (!std::filesystem::is_regular_file(candidate, error) || error) continue;
            auto normalized = std::filesystem::weakly_canonical(candidate, error).wstring();
            if (error) continue;
            std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            if (!visited.insert(normalized).second) continue;
            {
                try
                {
                    std::ifstream stream(candidate);
                    nlohmann::json root; stream >> root;
                    const auto& data = root.contains("Material") ? root.at("Material") : root;
                    const auto record = data.value("ImportRecord", nlohmann::json::object());
                    if (record.value("Version", 0) == 1 && !modelGuid.empty() && record.value("SourceGuid", "") == modelGuid)
                    {
                        if (record.value("ExplicitSlot", false))
                            explicitMaterials[record.at("SourceIndex").get<unsigned int>()].push_back(candidate);
                        knownMaterials[record.at("MaterialName").get<std::string>()].push_back(candidate);
                    }
                }
                catch (const std::exception& error)
                {
                    ConsoleLog::Warning("Material identity unreadable; preserved: " + candidate.string() + ": " + error.what());
                }
            }
        }
        for (const auto& mesh : model.GetMeshes())
        {
            if (!mesh || !mesh->ImportedMaterial) continue;
            const auto& source = *mesh->ImportedMaterial;
            if (explicitSlot >= 0 && source.SourceIndex != static_cast<unsigned int>(explicitSlot)) continue;
            if (result.contains(source.SourceIndex)) continue;
            auto path = folder / ("Material_" + std::to_string(source.SourceIndex) + ".ccmat");
            // 실패한 재질도 캐시에 남겨 같은 재질을 쓰는 메시마다 IO/경고를 반복하지 않는다.
            auto& entry = result[source.SourceIndex];
            try
            {
                const auto known = knownMaterials.find(source.Name);
                const auto bound = explicitMaterials.find(source.SourceIndex);
                if (explicitSlot >= 0)
                {
                    path = explicitPath;
                    if (bound != explicitMaterials.end() && (bound->second.size() != 1 ||
                        !std::filesystem::equivalent(path, bound->second.front())))
                        throw std::runtime_error("Slot already has a different explicit binding; existing files preserved");
                }
                else if (bound != explicitMaterials.end())
                {
                    if (bound->second.size() != 1) throw std::runtime_error("Multiple explicit material bindings; resolve duplicates first");
                    path = bound->second.front();
                }
                else if (known != knownMaterials.end())
                {
                    if (source.Name.empty() || sourceNames[source.Name].size() != 1 || known->second.size() != 1)
                        throw std::runtime_error("Ambiguous source material identity; automatic reassignment refused");
                    path = known->second.front();
                }
                auto material = std::make_shared<MaterialAsset>();
                nlohmann::json previous;
                if (adoptLegacyBaseline && !std::filesystem::exists(path))
                    throw std::runtime_error("Baseline adoption requires an existing indexed material; no file created");
                if (std::filesystem::exists(path))
                {
                    // 재임포트는 사용자가 수정한 재질을 덮어쓰지 않는다. 손상된 파일도 보존한다.
                    if (!material->LoadFromFile(path, false))
                        throw std::runtime_error("Cannot load existing material");
                    if (!material->ImportRecord.empty())
                    {
                        if (material->ImportRecord.value("Version", 0) != 1 ||
                            (explicitSlot < 0 && material->ImportRecord.value("ExplicitSlot", false) &&
                                material->ImportRecord.value("SourceIndex", ~0u) != source.SourceIndex) ||
                            material->ImportRecord.value("SourceGuid", "") != modelGuid ||
                            (material->ImportRecord.value("MaterialName", "") != source.Name && explicitSlot < 0))
                            throw std::runtime_error("Source identity changed; existing material preserved without reassignment");
                    }
                    else
                        ConsoleLog::Warning("Legacy material has no import baseline; keeping index binding without updating: " + path.string());
                    if (reimport)
                    {
                        if (explicitSlot >= 0 && !material->ImportRecord.empty() &&
                            material->ImportRecord.value("SourceIndex", ~0u) != source.SourceIndex)
                            throw std::runtime_error("Material already belongs to another source slot");
                        if (adoptLegacyBaseline && !material->ImportRecord.empty() && explicitSlot < 0)
                        {
                            // 이미 등록한 기준은 다시 잡으면 사용자 변경 이력이 사라지므로 그대로 둔다.
                            entry = { path.string(), material };
                            continue;
                        }
                        if (adoptLegacyBaseline && (modelGuid.empty() || (explicitSlot < 0 && (source.Name.empty() ||
                            sourceNames[source.Name].size() != 1 || material->Name != source.Name))))
                            throw std::runtime_error("Legacy material identity is ambiguous; explicit remapping required");
                        if (!adoptLegacyBaseline && (!material->ImportRecord.contains("Baseline") || !material->ImportRecord["Baseline"].is_object()))
                            throw std::runtime_error("Reimport requires an import baseline; existing material preserved");
                        std::ifstream input(path);
                        input >> previous;
                        material = std::make_shared<MaterialAsset>();
                    }
                }
                if (!std::filesystem::exists(path) || reimport)
                {
                    if (!std::filesystem::is_regular_file(shaderPath))
                        throw std::runtime_error("Base3D_PBR.hlsl is missing");
                    material->Name = source.Name;
                    material->ShaderPath = shaderPath.string();
                    material->ShaderGuid = AssetDatabase::GetGuidFromPath(shaderPath);
                    material->ShaderName = "Base3D_PBR";
                    material->AlbedoColor = source.BaseColor;
                    material->AlbedoColor.w *= source.Opacity;
                    material->Metallic = source.Metallic;
                    material->Roughness = source.Roughness;
                    if (material->AlbedoColor.w < 1.0f)
                        material->SurfaceMode = MaterialSurfaceMode::Transparent;
                    auto& linear = material->ShaderProperties["LinearColorSpace"];
                    linear.Type = ShaderPropertyType::Toggle;
                    linear.BoolValue = true;
                    std::string extension = modelPath.extension().string();
                    std::transform(extension.begin(), extension.end(), extension.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    const bool gltf = extension == ".gltf" || extension == ".glb";
                    std::array<bool, 4> assigned{};
                    std::optional<SamplerDescription> commonSampler;
                    bool compatibleSampler = true;
                    for (const auto& texture : source.Textures)
                    {
                        // 원본 참조와 슬롯별 설정을 함께 남긴다. 같은 이미지도 슬롯에 따라 UV/샘플러가 다르다.
                        auto settings = nlohmann::json{
                            { "Version", 1 }, { "Convention", "AssimpPostProcess" },
                            { "Semantic", static_cast<int>(texture.Semantic) },
                            { "SourceReference", texture.SourceReference },
                            { "SourceTextureType", texture.SourceTextureType }, { "SourceTextureSlot", texture.SourceTextureSlot },
                            { "UVChannel", texture.UVChannel }, { "Mapping", texture.Mapping },
                            { "AddressU", texture.AddressU }, { "AddressV", texture.AddressV },
                            { "Operation", texture.Operation }, { "Flags", texture.Flags },
                            { "MinFilter", texture.MinFilter }, { "MagFilter", texture.MagFilter },
                            { "ValidSampling", texture.ValidSampling }, { "Applied", false }
                        };
                        // 비정상 수치를 JSON null로 조용히 바꾸지 않고 명시적으로 무효 처리한다.
                        if (texture.ValidSampling)
                        {
                            settings["Blend"] = texture.Blend;
                            settings["UVOffset"] = { texture.UVOffset.x, texture.UVOffset.y };
                            settings["UVScale"] = { texture.UVScale.x, texture.UVScale.y };
                            settings["UVRotationRadians"] = texture.UVRotation;
                        }
                        material->ImportedTextureSettings.push_back(std::move(settings));
                        if (texture.UVChannel != 0 || texture.Mapping != aiTextureMapping_UV || !texture.ValidSampling || !texture.Exists)
                        {
                            ConsoleLog::Warning("Material import skipped texture (unsupported mapping, invalid sampling or missing): " +
                                source.Name + ": " + texture.SourceReference);
                            continue;
                        }
                        if (texture.Operation != aiTextureOp_Multiply || texture.Blend != 1 || texture.Flags != 0)
                            ConsoleLog::Warning("Imported texture operation preserved but not applied by renderer: " +
                                source.Name + ": " + texture.SourceReference);
                        const auto semantic = static_cast<size_t>(texture.Semantic);
                        if (assigned[semantic])
                        {
                            ConsoleLog::Warning("Material import supports one texture per semantic: " + source.Name);
                            continue;
                        }
                        std::string texturePath = texture.FilePath;
                        MaterialUVTransform transform{ texture.UVOffset, texture.UVScale, texture.UVRotation };
                        DirectX::XMFLOAT4 rowU, rowV;
                        if (!transform.GetRows(rowU, rowV))
                        {
                            ConsoleLog::Warning("Material import skipped overflowing UV transform: " + texture.SourceReference);
                            continue;
                        }
                        if (texture.Embedded)
                        {
                            try
                            {
                                if (!texture.EmbeddedData) throw std::runtime_error("Embedded payload missing");
                                texturePath = ExtractEmbeddedTexture(*texture.EmbeddedData, folder / "Textures", modelGuid);
                            }
                            catch (const std::exception& error)
                            {
                                ConsoleLog::Warning("Embedded texture extraction failed: " + texture.SourceReference + ": " + error.what());
                                throw;
                            }
                        }
                        assigned[semantic] = true;
                        material->UVTransforms[semantic] = transform;
                        material->ImportedTextureSettings.back()["UVConfigured"] = true;
                        material->ImportedTextureSettings.back()["TextureBound"] = true;
                        // 슬롯별 설정을 저장하고, 같은 설정이면 구버전 셰이더용 s0도 유지한다.
                        const auto filterSupported = [](int value) { return value == -1 || value == 9728 || value == 9729; };
                        const bool mipFilter = texture.MinFilter >= 9984 && texture.MinFilter <= 9987;
                        if (texture.AddressU > 2 || texture.AddressV > 2 ||
                            (!filterSupported(texture.MinFilter) && !mipFilter) || !filterSupported(texture.MagFilter))
                        {
                            compatibleSampler = false;
                            ConsoleLog::Warning("Material sampler unsupported (filter or address mode): " + texture.SourceReference);
                        }
                        else
                        {
                            SamplerDescription sampling{ SamplerAddress(texture.AddressU), SamplerAddress(texture.AddressV),
                                texture.MinFilter == 9728 ? SamplerFilter::Nearest : SamplerFilter::Linear,
                                texture.MagFilter == 9728 ? SamplerFilter::Nearest : SamplerFilter::Linear };
                            if (mipFilter)
                            {
                                // glTF의 9984~9987은 texel 필터와 mip 레벨 필터의 네 조합이다.
                                sampling.Min = (texture.MinFilter == 9984 || texture.MinFilter == 9986)
                                    ? SamplerFilter::Nearest : SamplerFilter::Linear;
                                sampling.Mip = texture.MinFilter >= 9986 ? SamplerMipFilter::Linear : SamplerMipFilter::Nearest;
                            }
                            material->MaterialSamplers[semantic] = sampling;
                            if (commonSampler && commonSampler->Key() != sampling.Key()) compatibleSampler = false;
                            else if (!commonSampler) commonSampler = sampling;
                        }
                        const auto guid = AssetDatabase::GetGuidFromPath(texturePath);
                        if (texture.Semantic == ImportedTextureSemantic::BaseColor)
                        {
                            material->AlbedoTexturePath = texturePath;
                            material->AlbedoTextureGuid = guid;
                        }
                        else if (texture.Semantic == ImportedTextureSemantic::Normal)
                        {
                            material->NormalTexturePath = texturePath;
                            material->NormalTextureGuid = guid;
                        }
                        else
                        {
                            const bool metallic = texture.Semantic == ImportedTextureSemantic::Metallic;
                            auto& value = material->ShaderProperties[metallic ? "MetallicTexture" : "RoughnessTexture"];
                            value.Type = ShaderPropertyType::Texture2D;
                            value.TexturePath = texturePath;
                            value.TextureGuid = guid;
                            auto& channel = material->ShaderProperties[metallic ? "MetallicChannel" : "RoughnessChannel"];
                            channel.Type = ShaderPropertyType::Float;
                            // glTF는 한 이미지의 G/B에 거칠기/금속성을 저장한다. 일반 맵의 기본 채널은 R이다.
                            channel.FloatValue = gltf ? (metallic ? 2.0f : 1.0f) : 0.0f;
                        }
                    }
                    if (compatibleSampler && commonSampler) material->CommonSampler = commonSampler;
                    for (auto& settings : material->ImportedTextureSettings)
                    {
                        const auto slot = settings.at("Semantic").get<size_t>();
                        settings["SamplerConfigured"] = settings.value("TextureBound", false) &&
                            slot < material->MaterialSamplers.size() && material->MaterialSamplers[slot].has_value();
                        settings["Applied"] = settings.value("UVConfigured", false) && settings["SamplerConfigured"].get<bool>() &&
                            settings.value("Operation", 0u) == aiTextureOp_Multiply && settings.value("Blend", 1.0f) == 1.0f &&
                            settings.value("Flags", 0u) == 0;
                    }
                    material->ImportRecord = {
                        { "Version", 1 }, { "SourceGuid", modelGuid }, { "MaterialName", source.Name },
                        { "SourceIndex", source.SourceIndex }, { "Baseline", material->ToJson(false).at("Material") }
                    };
                    auto output = material->ToJson();
                    if (explicitSlot >= 0 || (!previous.is_null() &&
                        (previous.contains("Material") ? previous["Material"] : previous).value("ImportRecord", nlohmann::json::object()).value("ExplicitSlot", false)))
                        output["Material"]["ImportRecord"]["ExplicitSlot"] = true;
                    if (adoptLegacyBaseline)
                    {
                        auto& current = previous.contains("Material") ? previous["Material"] : previous;
                        // 과거 원본은 복원할 수 없다. 현재 사용자 값은 보존하고 오늘의 원본만 향후 비교 기준으로 삼는다.
                        current["ImportRecord"] = output.at("Material").at("ImportRecord");
                        current["ImportRecord"]["AdoptedLegacy"] = true;
                        output = previous.contains("Material") ? previous : nlohmann::json{ { "Material", previous } };
                    }
                    else if (!previous.is_null())
                    {
                        auto& current = previous.contains("Material") ? previous["Material"] : previous;
                        const auto& baseline = current.at("ImportRecord").at("Baseline");
                        const auto& incoming = output.at("Material");
                        std::set<std::string> keys;
                        for (const auto& [key, value] : baseline.items()) keys.insert(key);
                        for (const auto& [key, value] : incoming.items()) keys.insert(key);
                        // 연관된 배열/객체는 한 단위로 비교한다. 일부만 합쳐 잘못된 텍스처 계약을 만들지 않는다.
                        for (const auto& key : keys)
                        {
                            if (key == "ImportRecord") continue;
                            const auto old = baseline.find(key);
                            const auto user = current.find(key);
                            const auto fresh = incoming.find(key);
                            const bool userUnchanged = (old == baseline.end() && user == current.end()) ||
                                (old != baseline.end() && user != current.end() && SameImportedValue(*old, *user));
                            const bool sourceUnchanged = (old == baseline.end() && fresh == incoming.end()) ||
                                (old != baseline.end() && fresh != incoming.end() && SameImportedValue(*old, *fresh));
                            const bool sameResult = (user == current.end() && fresh == incoming.end()) ||
                                (user != current.end() && fresh != incoming.end() && SameImportedValue(*user, *fresh));
                            if (!userUnchanged && !sourceUnchanged && !sameResult)
                                throw std::runtime_error("Reimport conflict at " + key + "; entire material preserved");
                            if (userUnchanged)
                            {
                                if (fresh == incoming.end()) current.erase(key);
                                else current[key] = *fresh;
                            }
                        }
                        current["ImportRecord"] = incoming.at("ImportRecord");
                        output = previous.contains("Material") ? previous : nlohmann::json{ { "Material", previous } };
                    }
                    std::filesystem::create_directories(path.parent_path());
                    auto temporary = path;
                    temporary += "." + std::to_string(std::random_device{}()) + ".tmp";
                    if (std::filesystem::exists(temporary)) throw std::runtime_error("Temporary material collision");
                    try
                    {
                        std::ofstream file(temporary, std::ios::binary);
                        file << output.dump(4);
                        file.flush();
                        if (!file) throw std::runtime_error("Cannot write staged material");
                        file.close();
                        if (!file || !material->LoadFromFile(temporary, false))
                            throw std::runtime_error("Cannot validate staged material");
                        // 같은 폴더의 완성 파일로 교체한다. 저장/검증 실패 시 원본과 meta GUID는 유지한다.
                        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                            throw std::runtime_error("Cannot replace material: " + std::to_string(GetLastError()));
                        if (adoptLegacyBaseline)
                            ConsoleLog::Info("Legacy material baseline registered; current values preserved: " + path.string());
                    }
                    catch (...)
                    {
                        std::error_code error;
                        std::filesystem::remove(temporary, error);
                        throw;
                    }
                }
                AssetDatabase::GetGuidFromPath(path);
                entry = { path.string(), std::move(material) };
            }
            catch (const std::exception& error)
            {
                ConsoleLog::Warning("Material import failed: " + path.string() + ": " + error.what());
            }
        }
        return result;
    }

    std::vector<std::string> ModelImporter::AuditExtractedImages(const std::filesystem::path& modelPath,
        const std::filesystem::path& assetsRoot)
    {
        std::vector<std::string> report;
        try
        {
            const auto root = std::filesystem::canonical(assetsRoot);
            const auto model = std::filesystem::canonical(modelPath);
            const auto inside = [&](const std::filesystem::path& path)
            {
                const auto relative = std::filesystem::weakly_canonical(path).lexically_relative(root);
                if (relative.empty() || relative.is_absolute()) return false;
                for (const auto& part : relative) if (part == "..") return false;
                return true;
            };
            const auto folder = model.parent_path() / (model.filename().string() + ".materials") / "Textures";
            if (!inside(model) || !inside(folder)) throw std::runtime_error("Extraction folder is outside project assets");
            if (!std::filesystem::exists(folder)) return { "No extracted image folder. No files changed." };
            const auto readJson = [](const std::filesystem::path& path)
            {
                if (std::filesystem::file_size(path) > 8u * 1024u * 1024u) throw std::runtime_error("JSON audit size limit exceeded");
                std::ifstream input(path);
                if (!input) throw std::runtime_error("Cannot read audit JSON: " + path.string());
                nlohmann::json data;
                input >> data;
                return data;
            };
            const auto sourceGuid = readJson(AssetDatabase::GetMetaPath(model)).value("guid", "");
            if (sourceGuid.empty()) throw std::runtime_error("Model GUID missing");
            const auto lower = [](std::string text)
            {
                std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return text;
            };
            std::set<std::string> references;
            std::function<void(const nlohmann::json&)> collect = [&](const nlohmann::json& value)
            {
                if (value.is_string())
                {
                    const auto text = value.get<std::string>();
                    references.insert(lower(text));
                    references.insert(lower(std::filesystem::path(text).filename().string()));
                }
                else if (value.is_structured()) for (const auto& child : value) collect(child);
            };
            size_t fileCount = 0;
            uintmax_t bytes = 0;
            for (const auto& file : std::filesystem::recursive_directory_iterator(root))
            {
                if (++fileCount > 20000) throw std::runtime_error("Asset audit entry limit exceeded");
                if ((GetFileAttributesW(file.path().c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                    throw std::runtime_error("Reparse point found; reference scan cannot prove coverage");
                if (!file.is_regular_file()) continue;
                const auto kind = AssetDatabase::GetAssetKind(file.path());
                const auto extension = lower(file.path().extension().string());
                const bool container = kind == AssetKind::Scene || kind == AssetKind::Prefab || kind == AssetKind::Material ||
                    kind == AssetKind::VisualShader || kind == AssetKind::AnimatorController || kind == AssetKind::Avatar ||
                    extension == ".json" || extension == ".gltf";
                if (!container) continue;
                bytes += file.file_size();
                if (bytes > 32u * 1024u * 1024u) throw std::runtime_error("Reference audit byte limit exceeded");
                // 배포 의존성 검사와 달리 ImportRecord의 과거 기준값도 보존한다. 복구용 이미지를 정리 후보로 만들지 않는다.
                collect(readJson(file.path()));
            }
            size_t images = 0;
            for (const auto& file : std::filesystem::directory_iterator(folder))
            {
                if (lower(file.path().extension().string()) != ".tga") continue;
                if (++images > 512) throw std::runtime_error("Extracted image audit limit exceeded");
                const auto name = file.path().filename().string();
                std::string state = "PROTECTED: ownership not verified";
                try
                {
                    if (!inside(file.path()) || (GetFileAttributesW(file.path().c_str()) & FILE_ATTRIBUTE_REPARSE_POINT))
                        throw std::runtime_error("Path redirect is not eligible");
                    const auto record = readJson(std::filesystem::path(file.path().string() + ".ccgenerated"));
                    if (record.value("Version", 0) != 1 || record.value("Owner", "") != "CCEngine.ModelImporter" ||
                        record.value("SourceGuid", "") != sourceGuid || record.value("FileName", "") != name)
                        throw std::runtime_error("Ownership does not match selected model");
                    if (record.value("Size", uintmax_t(0)) != file.file_size() ||
                        record.value("Fingerprint", "") != GeneratedImageFingerprint(file.path()))
                        throw std::runtime_error("Image changed since extraction; preserve user data");
                    const auto guid = readJson(AssetDatabase::GetMetaPath(file.path())).value("guid", "");
                    if (guid.empty()) throw std::runtime_error("Image GUID missing");
                    if (references.count(lower(guid)) || references.count(lower(name)))
                        state = "PROTECTED: saved reference or import baseline";
                    else if (std::filesystem::file_time_type::clock::now() - file.last_write_time() < std::chrono::hours(24 * 7))
                        state = "PROTECTED: seven-day recovery grace period";
                    else
                        state = "REVIEW ONLY: no saved JSON reference; verify live/Undo/script/model references before removal";
                }
                catch (const std::exception& error) { state = "PROTECTED: " + std::string(error.what()); }
                report.push_back(name + " | " + state);
            }
            std::sort(report.begin(), report.end());
            report.insert(report.begin(), "Extracted image audit (read-only). No deletion or movement. Binary models, scripts and unsaved/Undo references are not proven unused.");
        }
        catch (const std::exception& error)
        {
            // 불완전한 스캔의 중간 후보는 반환하지 않는다.
            return { "AUDIT BLOCKED: " + std::string(error.what()) + ". All files preserved." };
        }
        return report;
    }

    Entity ModelImporter::ImportModel(Scene* scene, const std::string& filepath)
    {
        // 1. 모델 로드
        std::shared_ptr<Model> myModel = GetOrLoadModel(filepath);
        const auto materials = PrepareMaterials(*myModel);

        // 2. 파일명에서 확장자를 제외한 이름을 추출해서 최상위 엔티티 이름으로 사용 (예: "FBX_MAYO")
        std::filesystem::path path(filepath);
        std::string modelName = path.stem().string();

        // 3. 모델 루트 엔티티 생성
        Entity modelRootEntity = scene->CreateEntity(modelName);
        auto& rootTc = modelRootEntity.GetComponent<TransformComponent>();

        rootTc.Translation = { 0.0f, 0.0f, 0.0f };
        rootTc.Rotation = { 0.0f, 0.0f, 0.0f };
        rootTc.QuaternionRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
        rootTc.Scale = { 1.0f, 1.0f, 1.0f };

        modelRootEntity.AddComponent<RelationshipComponent>();
        auto& modelComponent = modelRootEntity.AddComponent<ModelComponent>(myModel);
        // 임포트된 모델 루트에 원본 에셋 GUID를 남겨 저장/프리팹에서 같은 파일을 다시 찾는다.
        modelComponent.AssetGuid = AssetDatabase::GetGuidFromPath(filepath);

        if (!myModel->GetBoneInfoMap().empty()) 
        {
            modelRootEntity.AddComponent<AnimatorComponent>();
        }

        // 4. 트리 빌드 시작
        BuildTree(scene, myModel->GetRootNode(), (entt::entity)modelRootEntity, true, modelComponent, materials);

        return modelRootEntity;
    }

    void ModelImporter::BuildTree(Scene* scene, const ModelNode& node, entt::entity parentHandle, bool isRootNode, ModelComponent& modelComponent, const ImportedMaterialMap& materials)
    {
        entt::entity currentHandle = parentHandle;

        // 1. Assimp RootNode 자체는 파일명 루트 엔티티가 대신하고,
        //    Armature 같은 자식 노드의 원본 보정(-90도 등)은 하이어라키에 그대로 유지합니다.
        if (isRootNode && node.Meshes.empty())
        {
            for (const auto& childNode : node.Children)
            {
                BuildTree(scene, childNode, parentHandle, false, modelComponent, materials);
            }
            return;
        }

        // 2. 일반 노드인 경우 (RootNode의 자식 포함)
        std::string entityName = node.Name.empty() ? "UnnamedNode" : node.Name;
        Entity currentEntity = scene->CreateEntity(entityName);
        currentHandle = (entt::entity)currentEntity;
        modelComponent.NodeEntityMap[entityName] = currentHandle;
        modelComponent.NodePathEntityMap[node.Path] = currentHandle;

        auto& tc = currentEntity.GetComponent<TransformComponent>();

        // 노드 본연의 로컬 행렬을 계산합니다.
        DirectX::XMMATRIX localS = DirectX::XMMatrixScaling(node.Scale.x, node.Scale.y, node.Scale.z);
        DirectX::XMMATRIX localR = DirectX::XMMatrixRotationQuaternion(DirectX::XMLoadFloat4(&node.Rotation));
        DirectX::XMMATRIX localT = DirectX::XMMatrixTranslation(node.Translation.x, node.Translation.y, node.Translation.z);

        DirectX::XMMATRIX localSR = DirectX::XMMatrixMultiply(localS, localR);
        DirectX::XMMATRIX localMat = DirectX::XMMatrixMultiply(localSR, localT);

        // Unity처럼 임포트 보정은 하이어라키 트랜스폼에 남기고, 노드 로컬 트랜스폼을 그대로 씁니다.
        DirectX::XMVECTOR s, r, t;
        if (DirectX::XMMatrixDecompose(&s, &r, &t, localMat))
        {
            DirectX::XMStoreFloat3(&tc.Scale, s);
            DirectX::XMStoreFloat4(&tc.QuaternionRotation, r);
            DirectX::XMStoreFloat3(&tc.Translation, t);
        }

        currentEntity.AddComponent<RelationshipComponent>();
        currentEntity.GetComponent<RelationshipComponent>().Parent = parentHandle;
        scene->GetRegistry().get<RelationshipComponent>(parentHandle).Children.push_back(currentHandle);

        // 3. 메쉬 세팅 (기존 로직 동일)
        for (size_t i = 0; i < node.Meshes.size(); ++i)
        {
            entt::entity targetHandle = currentHandle;
            if (node.Meshes.size() > 1) {
                Entity targetEntity = scene->CreateEntity(entityName + "_SubMesh_" + std::to_string(i));
                targetHandle = (entt::entity)targetEntity;
                targetEntity.AddComponent<RelationshipComponent>();
                targetEntity.GetComponent<RelationshipComponent>().Parent = currentHandle;
                scene->GetRegistry().get<RelationshipComponent>(currentHandle).Children.push_back(targetHandle);
            }
            Entity targetEnt{ targetHandle, scene };
            auto& meshComp = targetEnt.AddComponent<MeshComponent>(MeshComponent::MeshType::Custom);
            meshComp.MeshData = node.Meshes[i];
            if (node.Meshes[i]->ImportedMaterial)
            {
                const auto found = materials.find(node.Meshes[i]->ImportedMaterial->SourceIndex);
                if (found != materials.end() && found->second.second)
                {
                    meshComp.MaterialPath = found->second.first;
                    meshComp.Material = found->second.second;
                    meshComp.MaterialAssetGuid = AssetDatabase::GetGuidFromPath(meshComp.MaterialPath);
                    MeshComponent::MaterialSlot slot;
                    slot.Name = meshComp.Material->Name;
                    slot.Material = meshComp.Material;
                    slot.MaterialPath = meshComp.MaterialPath;
                    slot.MaterialAssetGuid = meshComp.MaterialAssetGuid;
                    meshComp.MaterialSlots.push_back(std::move(slot));
                }
            }
            if (!node.Meshes[i]->TexturePath.empty() && std::filesystem::exists(node.Meshes[i]->TexturePath))
            {
                // FBX가 물고 온 텍스처도 저장용 경로/GUID를 남긴다.
                // 그래야 씬 저장 후 다시 열 때 텍스처 파일 이동을 AssetDatabase가 따라갈 수 있다.
                meshComp.AlbedoPath = node.Meshes[i]->TexturePath;
                meshComp.AlbedoAssetGuid = AssetDatabase::GetGuidFromPath(meshComp.AlbedoPath);
                meshComp.AlbedoMap.reset(Texture2D::Create(node.Meshes[i]->TexturePath));
            }
        }

        // 4. 자식 노드 재귀 호출 (상속 끝났으니 다음 레벨은 Identity를 넘김)
        for (const auto& childNode : node.Children)
        {
            BuildTree(scene, childNode, currentHandle, false, modelComponent, materials);
        }
    }

}
