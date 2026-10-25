#include "BackendShaderPackage.h"
#include "Renderer/Shader.h"
#include "Renderer/UniformRepack.h"
#include "json.hpp"
#include <fstream>
#include <stdexcept>
#include <algorithm>
#if defined(CC_ENABLE_DX12_RHI)
#include "Platform/DirectX12/DX12Shader.h"
#endif
#include <Windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#include <array>
#include <iomanip>
#include <sstream>
#include <set>
#if defined(CC_ENABLE_OPENGL_RHI)
#include "Platform/OpenGL/OpenGLShader.h"
#endif
#if defined(CC_ENABLE_VULKAN_RHI)
#include "Platform/Vulkan/VulkanShader.h"
#endif
namespace CCEngine
{
    namespace
    {
        struct Package
        {
            std::filesystem::path Vertex, Pixel;
            std::string VertexEntry, PixelEntry, Properties;
            Shader::TextureBindings Textures;
            std::vector<VertexInput> Inputs;
            bool HasUniformRepack = false;
            std::vector<UniformRepackLayout> UniformLayouts;
        };
        std::vector<UniformRepackLayout> ReadUniformRepack(const nlohmann::json& metadata)
        {
            if (metadata.at("contract") != "cc-uniform-repack-v1" ||
                !metadata.at("buffers").is_array() || metadata.at("buffers").size() > 8)
                throw std::runtime_error("Invalid OpenGL uniform repack contract");
            auto number = [](const nlohmann::json& value)
            {
                if (!value.is_number_unsigned() || value.get<uint64_t>() > 65536)
                    throw std::runtime_error("Invalid uniform repack integer");
                return value.get<uint32_t>();
            };
            std::vector<UniformRepackLayout> layouts;
            for (const auto& buffer : metadata.at("buffers"))
            {
                UniformRepackLayout layout;
                layout.Binding = number(buffer.at("binding"));
                layout.SourceSize = number(buffer.at("sourceSize"));
                layout.DestinationSize = number(buffer.at("destinationSize"));
                const auto& copies = buffer.at("copies");
                if (!copies.is_array() || copies.size() > 16384)
                    throw std::runtime_error("Invalid uniform repack copies");
                for (const auto& copy : copies)
                    layout.Copies.push_back({number(copy.at("source")), number(copy.at("destination")), number(copy.at("size"))});
                layouts.push_back(std::move(layout));
            }
            ValidateUniformRepack(layouts);
            return layouts;
        }
        std::vector<VertexInput> ReadVertexInputs(const nlohmann::json& shader)
        {
            const auto& entries = shader.at("vertexInputs");
            if (!entries.is_array()) throw std::runtime_error("Missing shader vertex input mapping");
            const std::map<std::string, ShaderDataType> types = {
                {"float", ShaderDataType::Float}, {"vec2", ShaderDataType::Float2},
                {"vec3", ShaderDataType::Float3}, {"vec4", ShaderDataType::Float4},
                {"int", ShaderDataType::Int}, {"ivec2", ShaderDataType::Int2},
                {"ivec3", ShaderDataType::Int3}, {"ivec4", ShaderDataType::Int4}};
            std::vector<VertexInput> result;
            for (const auto& entry : entries)
            {
                const auto& location = entry.at("location");
                if (!location.is_number_unsigned() || location.get<uint64_t>() >= 32)
                    throw std::runtime_error("Invalid shader vertex input location");
                result.push_back({entry.at("semantic").get<std::string>(), types.at(entry.at("type").get<std::string>()), location.get<uint32_t>()});
            }
            ValidateVertexInputs(result);
            return result;
        }
        Shader::TextureBindings ReadTextureBindings(const nlohmann::json& shader)
        {
            const auto& entries = shader.at("textureBindings");
            if (!entries.is_object()) throw std::runtime_error("Missing shader texture mapping");
            Shader::TextureBindings result;
            std::set<uint32_t> slots;
            for (auto it = entries.begin(); it != entries.end(); ++it)
            {
                if (!it.value().is_number_unsigned() || it.value().get<uint64_t>() >= 32 ||
                    it.key().empty() || !slots.insert(it.value().get<uint32_t>()).second)
                    throw std::runtime_error("Invalid shader texture mapping");
                result.emplace(it.key(), it.value().get<uint32_t>());
            }
            return result;
        }
        std::filesystem::path Resolve(const std::filesystem::path& root, const std::string& relative)
        {
            const auto path = std::filesystem::path(relative);
            if (relative.empty() || path.is_absolute() || path.has_root_name()) throw std::runtime_error("Shader package path must be relative");
            const auto candidate = std::filesystem::canonical(root / path);
            const auto inside = candidate.lexically_relative(root);
            if (inside.empty() || *inside.begin() == ".." || !std::filesystem::is_regular_file(candidate))
                throw std::runtime_error("Shader package path escapes package directory");
            return candidate;
        }
        Package ReadPackage(const std::filesystem::path& manifest, const std::string& source, const char* backend)
        {
            std::ifstream file(manifest);
            if (!file) throw std::runtime_error("Cannot open backend shader manifest");
            const auto json = nlohmann::json::parse(file);
            if (json.at("schema").get<int>() != 1 || !json.at("success").get<bool>() ||
                json.at("bindingContract").get<std::string>() != "cc-hlsl-rhi-v1")
                throw std::runtime_error("Incomplete or incompatible backend shader package");
            const auto root = std::filesystem::canonical(manifest.parent_path());
            const nlohmann::json* selected = nullptr;
            for (const auto& shader : json.at("shaders"))
                if (shader.at("source").get<std::string>() == source)
                {
                    if (selected) throw std::runtime_error("Duplicate source in shader package");
                    selected = &shader;
                }
            if (!selected) throw std::runtime_error("Source not found in backend shader package: " + source);
            const auto& vertex = selected->at("stages").at("vertex").at("outputs").at(backend);
            const auto& pixel = selected->at("stages").at("pixel").at("outputs").at(backend);
            const auto directory = selected->at("directory").get<std::string>();
            Package result;
            result.Textures = ReadTextureBindings(*selected);
            result.Inputs = ReadVertexInputs(*selected);
            result.Properties = selected->at("materialPropertySource").get<std::string>();
            if (std::string(backend) == "opengl" && selected->contains("openglUniformRepack"))
            {
                result.UniformLayouts = ReadUniformRepack(selected->at("openglUniformRepack"));
                result.HasUniformRepack = true;
            }
            ShaderPropertyParser::ParseSource(result.Properties);
            result.Vertex = Resolve(root, directory + '/' + vertex.at("file").get<std::string>());
            result.Pixel = Resolve(root, directory + '/' + pixel.at("file").get<std::string>());
            VerifyBackendShaderHash(result.Vertex, vertex.at("sha256").get<std::string>());
            VerifyBackendShaderHash(result.Pixel, pixel.at("sha256").get<std::string>());
            result.VertexEntry = vertex.at("entry").get<std::string>();
            result.PixelEntry = pixel.at("entry").get<std::string>();
            return result;
        }
    }
    namespace
    {
        std::string FileSHA256(const std::filesystem::path& path)
        {
            struct Hash
            {
                BCRYPT_ALG_HANDLE Algorithm = nullptr;
                BCRYPT_HASH_HANDLE Handle = nullptr;
                ~Hash() { if (Handle) BCryptDestroyHash(Handle); if (Algorithm) BCryptCloseAlgorithmProvider(Algorithm, 0); }
            } hash;
            auto check = [](NTSTATUS status) { if (status < 0) throw std::runtime_error("Shader package SHA-256 failed"); };
            check(BCryptOpenAlgorithmProvider(&hash.Algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0));
            check(BCryptCreateHash(hash.Algorithm, &hash.Handle, nullptr, 0, nullptr, 0, 0));
            std::ifstream file(path, std::ios::binary);
            if (!file) throw std::runtime_error("Cannot read shader package output");
            std::array<char, 16384> buffer{};
            while (file.read(buffer.data(), buffer.size()) || file.gcount())
                check(BCryptHashData(hash.Handle, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(file.gcount()), 0));
            if (file.bad() || !file.eof()) throw std::runtime_error("Shader package output read failed");
            std::array<unsigned char, 32> digest{};
            check(BCryptFinishHash(hash.Handle, digest.data(), static_cast<ULONG>(digest.size()), 0));
            std::ostringstream text;
            text << std::hex << std::setfill('0');
            for (auto byte : digest) text << std::setw(2) << unsigned(byte);
            return text.str();
        }
    }
namespace
    {
        struct PortableRegistration { std::filesystem::path Manifest; std::string Source; };
        struct PortablePackages
        {
            std::filesystem::path Root;
            std::vector<std::filesystem::path> Manifests;
            std::map<std::pair<std::filesystem::path, std::filesystem::path>, PortableRegistration> Registered;
        };
        std::map<std::string, PortablePackages> s_PortablePackages;
        void RequirePortableBackend(const std::string& backend)
        {
#if defined(CC_ENABLE_OPENGL_RHI)
            if (backend == "opengl") return;
#endif
#if defined(CC_ENABLE_VULKAN_RHI)
            if (backend == "vulkan") return;
#endif
            throw std::invalid_argument("Portable shader backend is not compiled: " + backend);
        }
        nlohmann::json ReadPortableManifest(const std::filesystem::path& path, const std::string& backend)
        {
            std::ifstream input(path);
            if (!input) throw std::runtime_error("Cannot read shader manifest: " + path.string());
            auto json = nlohmann::json::parse(input);
            const auto targets = json.at("targets").get<std::vector<std::string>>();
            if (json.at("schema") != 1 || json.at("success") != true ||
                json.at("bindingContract") != "cc-hlsl-rhi-v1" ||
                !json.at("shaders").is_array() || !json.at("defines").is_array() ||
                std::find(targets.begin(), targets.end(), backend) == targets.end())
                throw std::runtime_error("Incompatible shader manifest: " + path.string());
            return json;
        }
    }
    void ClearPortableShaderPackages() { s_PortablePackages.clear(); }
    void ConfigurePortableShaderPackages(const std::string& backend,
        const std::vector<std::filesystem::path>& manifests, const std::filesystem::path& sourceRoot)
    {
        RequirePortableBackend(backend);
        if (manifests.empty() || sourceRoot.empty())
            throw std::invalid_argument("Shader packages and source root are required");
        PortablePackages next;
        next.Root = std::filesystem::weakly_canonical(std::filesystem::absolute(sourceRoot));
        for (const auto& path : manifests)
        {
            const auto canonical = std::filesystem::canonical(path);
            ReadPortableManifest(canonical, backend);
            if (std::find(next.Manifests.begin(), next.Manifests.end(), canonical) != next.Manifests.end())
                throw std::invalid_argument("Duplicate shader manifest");
            next.Manifests.push_back(canonical);
        }
        // 새 설정 전체가 유효할 때만 교체한다. 성공 시 이전 파일 쌍 등록도 함께 비운다.
        s_PortablePackages[backend] = std::move(next);
    }
    void GetPortablePackageFiles(const std::string& backend, const std::filesystem::path& source,
        const std::vector<std::string>& keywords, std::filesystem::path& vertex, std::filesystem::path& pixel)
    {
        vertex.clear(); pixel.clear();
        RequirePortableBackend(backend);
        auto config = s_PortablePackages.find(backend);
        if (config == s_PortablePackages.end()) throw std::runtime_error("Shader packages are not configured: " + backend);
        auto& registry = config->second;
        const auto absolute = std::filesystem::weakly_canonical(source.is_absolute() ? source : registry.Root / source);
        const auto relative = absolute.lexically_relative(registry.Root);
        if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
            throw std::invalid_argument("Shader source is outside package source root");
        auto normalize = [](std::vector<std::string> values) {
            std::sort(values.begin(), values.end());
            values.erase(std::unique(values.begin(), values.end()), values.end());
            return values;
        };
        const auto requested = normalize(keywords);
        PortableRegistration selected;
        for (const auto& manifest : registry.Manifests)
        {
            const auto json = ReadPortableManifest(manifest, backend);
            if (normalize(json.at("defines").get<std::vector<std::string>>()) != requested) continue;
            for (const auto& shader : json.at("shaders"))
                if (shader.at("source").get<std::string>() == relative.generic_string())
                {
                    if (!selected.Manifest.empty()) throw std::runtime_error("Duplicate shader source/keyword variant");
                    selected = {manifest, relative.generic_string()};
                }
        }
        if (selected.Manifest.empty()) throw std::runtime_error("Shader variant is missing: " + relative.generic_string());
        const auto package = ReadPackage(selected.Manifest, selected.Source, backend.c_str());
        registry.Registered[{package.Vertex, package.Pixel}] = selected;
        vertex = package.Vertex; pixel = package.Pixel;
    }
    std::unique_ptr<Shader> LoadRegisteredPortableShader(const std::string& backend,
        const std::filesystem::path& vertex, const std::filesystem::path& pixel)
    {
        RequirePortableBackend(backend);
        const auto config = s_PortablePackages.find(backend);
        if (config == s_PortablePackages.end()) throw std::runtime_error("Shader packages are not configured: " + backend);
        const auto pair = std::make_pair(std::filesystem::canonical(vertex), std::filesystem::canonical(pixel));
        const auto found = config->second.Registered.find(pair);
        if (found == config->second.Registered.end())
            throw std::runtime_error("Shader file pair was not resolved from the current package");
        const auto& selected = found->second;
        // 파일을 직접 받아 GLSL/SPIR-V를 추측하지 않는다. 재로딩 때도 해시와 파일 쌍을 검증한다.
        const auto package = ReadPackage(selected.Manifest, selected.Source, backend.c_str());
        if (package.Vertex != pair.first || package.Pixel != pair.second)
            throw std::runtime_error("Shader package changed after resolution");
        std::unique_ptr<Shader> shader;
#if defined(CC_ENABLE_OPENGL_RHI)
        if (backend == "opengl")
        {
            auto glShader = OpenGLShader::FromFiles(OpenGLContext::Current(), package.Vertex, package.Pixel);
            if (package.HasUniformRepack) glShader->SetUniformRepack(package.UniformLayouts);
            shader = std::move(glShader);
        }
#endif
#if defined(CC_ENABLE_VULKAN_RHI)
        if (backend == "vulkan") shader = std::make_unique<VulkanShader>(VulkanContext::Current(),
            package.Vertex, package.Pixel, package.VertexEntry, package.PixelEntry);
#endif
        if (!shader) throw std::runtime_error("Shader backend unavailable");
        shader->SetTextureBindings(package.Textures);
        shader->SetVertexInputs(package.Inputs);
        shader->SetMaterialPropertySource(package.Properties);
        return shader;
    }
    void VerifyBackendShaderHash(const std::filesystem::path& path, const std::string& expected)
    {
        // 생성 도구의 성공 여부와 별개로 실제 게시할 파일을 다시 읽어 확인한다.
        if (expected.size() != 64 || FileSHA256(path) != expected)
            throw std::runtime_error("Shader package hash mismatch: " + path.string());
    }
    void ValidateBackendShaderManifest(const std::filesystem::path& manifest)
    {
        std::ifstream input(manifest);
        if (!input)
            throw std::runtime_error("Cannot open backend shader manifest: " + manifest.string());
        const auto metadata = nlohmann::json::parse(input);
        if (metadata.at("schema") != 1 || metadata.at("success") != true ||
            metadata.at("bindingContract") != "cc-hlsl-rhi-v1" ||
            !metadata.at("targets").is_array() || !metadata.at("shaders").is_array())
            throw std::runtime_error("Incomplete or incompatible backend shader package");

        const auto root = std::filesystem::canonical(manifest.parent_path());
        std::set<std::string> sources;
        for (const auto& shader : metadata.at("shaders"))
        {
            const auto source = shader.at("source").get<std::string>();
            if (source.empty() || !sources.insert(source).second)
                throw std::runtime_error("Invalid or duplicate shader source in package");
            ReadTextureBindings(shader);
            ReadVertexInputs(shader);
            ShaderPropertyParser::ParseSource(shader.at("materialPropertySource").get<std::string>());
            const auto directory = shader.at("directory").get<std::string>();
            for (const char* stageName : { "vertex", "pixel" })
            {
                const auto& outputs = shader.at("stages").at(stageName).at("outputs");
                for (const auto& target : metadata.at("targets"))
                {
                    const auto backend = target.get<std::string>();
                    const auto& output = outputs.at(backend);
                    const auto artifact = Resolve(root, directory + '/' + output.at("file").get<std::string>());
                    VerifyBackendShaderHash(artifact, output.at("sha256").get<std::string>());
                }
            }
            if (shader.contains("openglUniformRepack"))
                ReadUniformRepack(shader.at("openglUniformRepack"));
        }
        if (sources.empty())
            throw std::runtime_error("Backend shader package contains no shaders");
    }
#if defined(CC_ENABLE_DX12_RHI)
    namespace
    {
        std::vector<std::filesystem::path> s_DX12Manifests;
        std::filesystem::path s_DX12SourceRoot;
        std::map<std::filesystem::path, Shader::TextureBindings> s_DX12TextureBindings;
        std::map<std::filesystem::path, std::vector<VertexInput>> s_DX12VertexInputs;
        std::map<std::filesystem::path, std::string> s_DX12MaterialProperties;
        nlohmann::json ReadDX12Manifest(const std::filesystem::path& path)
        {
            std::ifstream file(path);
            if (!file) throw std::runtime_error("Cannot open DX12 shader manifest: " + path.string());
            auto json = nlohmann::json::parse(file);
            if (json.at("schema").get<int>() != 1 || !json.at("success").get<bool>() ||
                json.at("bindingContract").get<std::string>() != "cc-hlsl-rhi-v1" ||
                !json.at("shaders").is_array() || !json.at("defines").is_array())
                throw std::runtime_error("Incomplete DX12 shader package");
            const auto targets = json.at("targets").get<std::vector<std::string>>();
            if (std::find(targets.begin(), targets.end(), "dx12") == targets.end())
                throw std::runtime_error("Shader package has no DX12 outputs");
            return json;
        }
    }
    void ClearDX12ShaderPackages()
    {
        s_DX12Manifests.clear(); s_DX12SourceRoot.clear();
        s_DX12TextureBindings.clear(); s_DX12VertexInputs.clear();
        s_DX12MaterialProperties.clear();
    }
    std::string GetDX12MaterialPropertySource(const std::filesystem::path& pixel)
    {
        const auto it = s_DX12MaterialProperties.find(std::filesystem::canonical(pixel));
        if (it == s_DX12MaterialProperties.end()) throw std::runtime_error("Material property mapping was not prepared; rebuild shader package");
        return it->second;
    }
    std::vector<VertexInput> GetDX12VertexInputs(const std::filesystem::path& vertex)
    {
        const auto it = s_DX12VertexInputs.find(std::filesystem::canonical(vertex));
        if (it == s_DX12VertexInputs.end()) throw std::runtime_error("Shader vertex mapping was not prepared");
        return it->second;
    }
    Shader::TextureBindings GetDX12TextureBindings(const std::filesystem::path& pixel)
    {
        const auto it = s_DX12TextureBindings.find(std::filesystem::canonical(pixel));
        if (it == s_DX12TextureBindings.end()) throw std::runtime_error("Shader texture mapping was not prepared");
        return it->second;
    }
    void ConfigureDX12ShaderPackages(const std::vector<std::filesystem::path>& manifests,
        const std::filesystem::path& sourceRoot)
    {
        if (manifests.empty() || sourceRoot.empty()) throw std::invalid_argument("DX12 requires shader packages and source root");
        const auto root = std::filesystem::weakly_canonical(std::filesystem::absolute(sourceRoot));
        std::vector<std::filesystem::path> paths;
        for (const auto& path : manifests)
        {
            auto canonical = std::filesystem::canonical(path);
            ReadDX12Manifest(canonical);
            if (std::find(paths.begin(), paths.end(), canonical) != paths.end())
                throw std::invalid_argument("Duplicate DX12 shader package");
            paths.push_back(std::move(canonical));
        }
        s_DX12Manifests = std::move(paths); s_DX12SourceRoot = root;
        s_DX12TextureBindings.clear();
        s_DX12VertexInputs.clear();
        s_DX12MaterialProperties.clear();
    }
    void GetDX12PackageBytecode(const std::filesystem::path& source, const std::vector<std::string>& keywords,
        std::filesystem::path& vertex, std::filesystem::path& pixel)
    {
        vertex.clear(); pixel.clear();
        if (s_DX12Manifests.empty()) throw std::runtime_error("DX12 shader packages are not configured");
        const auto absolute = std::filesystem::weakly_canonical(
            source.is_absolute() ? source : s_DX12SourceRoot / source);
        const auto relative = absolute.lexically_relative(s_DX12SourceRoot);
        if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
            throw std::runtime_error("Shader source is outside the configured package source root");
        auto requested = keywords;
        std::sort(requested.begin(), requested.end());
        requested.erase(std::unique(requested.begin(), requested.end()), requested.end());
        bool found = false;
        std::filesystem::path selectedVertex, selectedPixel;
        Shader::TextureBindings selectedTextures;
        std::vector<VertexInput> selectedInputs;
        std::string selectedProperties;
        for (const auto& manifest : s_DX12Manifests)
        {
            const auto json = ReadDX12Manifest(manifest);
            auto defines = json.at("defines").get<std::vector<std::string>>();
            std::sort(defines.begin(), defines.end());
            defines.erase(std::unique(defines.begin(), defines.end()), defines.end());
            if (defines != requested) continue;
            for (const auto& shader : json.at("shaders"))
            {
                if (shader.at("source").get<std::string>() != relative.generic_string()) continue;
                if (found) throw std::runtime_error("Duplicate DX12 shader variant in packages");
                const auto root = std::filesystem::canonical(manifest.parent_path());
                auto stage = [&](const char* name, const char* profile, const char* entry)
                {
                    const auto& output = shader.at("stages").at(name).at("outputs").at("dx12");
                    if (output.at("profile").get<std::string>() != profile || output.at("entry").get<std::string>() != entry)
                        throw std::runtime_error("DX12 shader profile/entry does not match the engine contract");
                    const auto path = Resolve(root, shader.at("directory").get<std::string>() + "/" + output.at("file").get<std::string>());
                    VerifyBackendShaderHash(path, output.at("sha256").get<std::string>());
                    return path;
                };
                selectedVertex = stage("vertex", "vs_6_0", "VSMain");
                selectedPixel = stage("pixel", "ps_6_0", "PSMain");
                selectedTextures = ReadTextureBindings(shader);
                selectedInputs = ReadVertexInputs(shader);
                selectedProperties = shader.at("materialPropertySource").get<std::string>();
                ShaderPropertyParser::ParseSource(selectedProperties);
                found = true;
            }
        }
        if (!found) throw std::runtime_error("DX12 shader variant is missing: " + relative.generic_string());
        vertex = std::move(selectedVertex); pixel = std::move(selectedPixel);
        s_DX12TextureBindings[pixel] = std::move(selectedTextures);
        s_DX12VertexInputs[vertex] = std::move(selectedInputs);
        s_DX12MaterialProperties[pixel] = std::move(selectedProperties);
    }
    std::unique_ptr<Shader> LoadDX12ShaderPackage(DX12Context& context, const std::filesystem::path& manifest, const std::string& source)
    {
        const auto package = ReadPackage(manifest, source, "dx12");
        auto shader = DX12Shader::FromBytecodeFiles(context, package.Vertex, package.Pixel);
        shader->SetTextureBindings(package.Textures);
        shader->SetMaterialPropertySource(package.Properties);
        shader->SetVertexInputs(package.Inputs);
        return shader;
    }
#endif
#if defined(CC_ENABLE_OPENGL_RHI)
    std::unique_ptr<Shader> LoadOpenGLShaderPackage(OpenGLContext& context, const std::filesystem::path& manifest, const std::string& source)
    {
        const auto package = ReadPackage(manifest, source, "opengl");
        auto shader = OpenGLShader::FromFiles(context, package.Vertex, package.Pixel);
        if (package.HasUniformRepack) shader->SetUniformRepack(package.UniformLayouts);
        shader->SetTextureBindings(package.Textures);
        shader->SetMaterialPropertySource(package.Properties);
        shader->SetVertexInputs(package.Inputs);
        return shader;
    }
#endif
#if defined(CC_ENABLE_VULKAN_RHI)
    std::unique_ptr<Shader> LoadVulkanShaderPackage(VulkanContext& context, const std::filesystem::path& manifest, const std::string& source)
    {
        const auto package = ReadPackage(manifest, source, "vulkan");
        auto shader = std::make_unique<VulkanShader>(context, package.Vertex, package.Pixel, package.VertexEntry, package.PixelEntry);
        shader->SetTextureBindings(package.Textures);
        shader->SetMaterialPropertySource(package.Properties);
        shader->SetVertexInputs(package.Inputs);
        return shader;
    }
#endif
}
