#include "Renderer/ShaderCompiler.h"
#include "Core/PathContext.h"
#include "Renderer/ShaderProperty.h"
#include "json.hpp"

#define NOMINMAX
#include <d3dcompiler.h>
#include <d3d11shader.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace CCEngine
{
    namespace
    {
        std::string ToLowerExtension(const std::filesystem::path& path)
        {
            std::string extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return extension;
        }

        std::string MakeStablePathHash(const std::filesystem::path& path)
        {
            std::error_code ec;
            std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
            std::string text = (ec ? path : normalized).generic_string();

            // 캐시 폴더 이름은 파일 경로에서 만든다.
            // 같은 이름의 셰이더가 다른 폴더에 있어도 서로 bytecode를 덮어쓰지 않게 하기 위해서다.
            uint64_t hash = 14695981039346656037ull;
            for (unsigned char c : text)
            {
                hash ^= c;
                hash *= 1099511628211ull;
            }

            std::ostringstream stream;
            stream << std::hex << std::setfill('0') << std::setw(16) << hash;
            return stream.str();
        }

        std::filesystem::path GetShaderCacheDirectory(
            const std::filesystem::path& sourcePath,
            const std::string& variantKey = "default")
        {
            return PathContext::ProjectRoot() / "local" / "shader-cache" /
                MakeStablePathHash(sourcePath) / MakeStablePathHash(variantKey);
        }

        bool IsBytecodeFresh(const std::filesystem::path& sourcePath, const std::filesystem::path& bytecodePath)
        {
            std::error_code ec;
            if (!std::filesystem::exists(bytecodePath, ec) || ec)
                return false;

            auto sourceTime = std::filesystem::last_write_time(sourcePath, ec);
            if (ec)
                return false;

            auto bytecodeTime = std::filesystem::last_write_time(bytecodePath, ec);
            if (ec)
                return false;

            return bytecodeTime >= sourceTime;
        }

        std::string BlobToString(ID3DBlob* blob)
        {
            if (!blob || !blob->GetBufferPointer() || blob->GetBufferSize() == 0)
                return "";

            std::string message(
                static_cast<const char*>(blob->GetBufferPointer()),
                static_cast<size_t>(blob->GetBufferSize()));
            while (!message.empty() && message.back() == '\0')
                message.pop_back();
            return message;
        }

        bool WriteBlobToFile(ID3DBlob* blob, const std::filesystem::path& outputPath)
        {
            if (!blob)
                return false;

            std::error_code ec;
            std::filesystem::create_directories(outputPath.parent_path(), ec);
            if (ec)
                return false;

            std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
            if (!output.is_open())
                return false;

            output.write(static_cast<const char*>(blob->GetBufferPointer()), static_cast<std::streamsize>(blob->GetBufferSize()));
            return output.good();
        }

        std::vector<char> ReadBinaryFile(const std::filesystem::path& path)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input.is_open())
                return {};

            std::streamsize size = input.tellg();
            if (size <= 0)
                return {};

            std::vector<char> data((size_t)size);
            input.seekg(0, std::ios::beg);
            input.read(data.data(), size);
            if (!input.good())
                return {};

            return data;
        }

        struct ReflectedResourceBinding
        {
            std::string Name;
            D3D_SHADER_INPUT_TYPE Type = D3D_SIT_CBUFFER;
            uint32_t BindPoint = 0;
            uint32_t BindCount = 0;
        };

        std::vector<ReflectedResourceBinding> ReflectBoundResources(const std::filesystem::path& bytecodePath, std::string& error)
        {
            std::vector<char> bytecode = ReadBinaryFile(bytecodePath);
            if (bytecode.empty())
            {
                error = "Could not read shader bytecode for reflection: " + bytecodePath.string();
                return {};
            }

            ID3D11ShaderReflection* reflection = nullptr;
            HRESULT hr = D3DReflect(bytecode.data(), bytecode.size(), __uuidof(ID3D11ShaderReflection), reinterpret_cast<void**>(&reflection));
            if (FAILED(hr) || !reflection)
            {
                error = "D3DReflect failed for bytecode: " + bytecodePath.string();
                return {};
            }

            D3D11_SHADER_DESC shaderDesc = {};
            reflection->GetDesc(&shaderDesc);

            std::vector<ReflectedResourceBinding> resources;
            resources.reserve(shaderDesc.BoundResources);
            for (uint32_t i = 0; i < shaderDesc.BoundResources; ++i)
            {
                D3D11_SHADER_INPUT_BIND_DESC bindDesc = {};
                if (FAILED(reflection->GetResourceBindingDesc(i, &bindDesc)))
                    continue;

                ReflectedResourceBinding binding;
                binding.Name = bindDesc.Name ? bindDesc.Name : "";
                binding.Type = bindDesc.Type;
                binding.BindPoint = bindDesc.BindPoint;
                binding.BindCount = bindDesc.BindCount;
                resources.push_back(binding);
            }

            reflection->Release();
            return resources;
        }

        const ReflectedResourceBinding* FindResource(
            const std::vector<ReflectedResourceBinding>& resources,
            const std::string& name,
            D3D_SHADER_INPUT_TYPE type)
        {
            for (const ReflectedResourceBinding& resource : resources)
            {
                if (resource.Name == name && resource.Type == type)
                    return &resource;
            }

            return nullptr;
        }

        void ValidateMaterialPropertyReflection(const std::filesystem::path& sourcePath, ShaderCompileResult& result)
        {
            std::vector<ShaderPropertyDefinition> properties = ShaderPropertyParser::LoadFromShaderFile(sourcePath);
            if (properties.empty())
                return;

            std::string reflectionError;
            std::vector<ReflectedResourceBinding> resources = ReflectBoundResources(result.Pixel.BytecodePath, reflectionError);
            if (!reflectionError.empty())
            {
                result.ReflectionErrors.push_back(reflectionError);
                return;
            }

            const ReflectedResourceBinding* materialBuffer = FindResource(resources, "MaterialPropertyBuffer", D3D_SIT_CBUFFER);
            if (!materialBuffer)
            {
                result.ReflectionErrors.push_back("Shader has @property declarations but no MaterialPropertyBuffer cbuffer.");
            }
            else if (materialBuffer->BindPoint != 4)
            {
                result.ReflectionErrors.push_back("MaterialPropertyBuffer must use register(b4). Current slot is b" + std::to_string(materialBuffer->BindPoint) + ".");
            }

            uint32_t extraTextureSlot = 1;
            for (const ShaderPropertyDefinition& property : properties)
            {
                if (property.Type != ShaderPropertyType::Texture2D)
                    continue;

                const uint32_t expectedSlot = property.Name == "AlbedoTexture" ? 0 : extraTextureSlot++;
                const ReflectedResourceBinding* texture = FindResource(resources, property.Name, D3D_SIT_TEXTURE);
                if (!texture)
                {
                    result.ReflectionWarnings.push_back("Texture property '" + property.Name + "' is exposed in the Material but not sampled by the pixel shader.");
                    continue;
                }

                if (texture->BindPoint != expectedSlot)
                {
                    result.ReflectionErrors.push_back(
                        "Texture property '" + property.Name + "' must use register(t" + std::to_string(expectedSlot) +
                        "). Current slot is t" + std::to_string(texture->BindPoint) + ".");
                }
            }

            uint32_t colorCount = 0;
            uint32_t scalarCount = 0;
            uint32_t toggleCount = 0;
            for (const ShaderPropertyDefinition& property : properties)
            {
                if (property.Type == ShaderPropertyType::Color && property.Name != "AlbedoColor")
                    ++colorCount;
                else if (property.Type == ShaderPropertyType::Float && property.Name != "Roughness" && property.Name != "Metallic")
                    ++scalarCount;
                else if (property.Type == ShaderPropertyType::Toggle)
                    ++toggleCount;
            }

            if (colorCount > 8)
                result.ReflectionErrors.push_back("Too many Color properties. Renderer currently supports 8 custom colors.");
            if (scalarCount > 16)
                result.ReflectionErrors.push_back("Too many Float properties. Renderer currently supports 16 custom float values.");
            if (toggleCount > 16)
                result.ReflectionErrors.push_back("Too many Toggle properties. Renderer currently supports 16 toggle values.");

            // 여기서 검사하는 기준은 사람이 쓴 HLSL과 엔진이 실제로 바인딩하는 슬롯의 약속이다.
            // 나중에 Visual Shader가 HLSL을 생성해도 같은 검증을 통과해야 안전하게 Material UI와 연결된다.
        }

        uint32_t GetCompileFlags()
        {
            uint32_t flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(CC_DEBUG) || defined(_DEBUG)
            flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
            flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
            return flags;
        }

        std::unordered_map<std::string, ShaderCompileResult> s_LastCompileResults;

        struct RuntimeBytecodePaths
        {
            std::filesystem::path Vertex;
            std::filesystem::path Pixel;
        };

        std::unordered_map<std::string, RuntimeBytecodePaths> s_RuntimeBytecode;
        bool s_RuntimeBytecodeOnly = false;

        std::string NormalizeShaderPath(const std::filesystem::path& path)
        {
            std::error_code ec;
            const auto canonical = std::filesystem::weakly_canonical(path, ec);
            std::string key = (ec ? path.lexically_normal() : canonical).generic_string();
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
            return key;
        }

        std::string NormalizePortableShaderKey(const std::filesystem::path& path)
        {
            std::string key = path.lexically_normal().generic_string();
            while (key.rfind("./", 0) == 0)
                key.erase(0, 2);
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
            return key;
        }

        bool ResolveManifestPath(
            const std::filesystem::path& root,
            const std::filesystem::path& storedPath,
            std::filesystem::path& resolved)
        {
            if (storedPath.empty() || storedPath.is_absolute())
                return false;

            resolved = (root / storedPath).lexically_normal();
            const auto relative = resolved.lexically_relative(root);
            if (relative.empty() || relative.is_absolute())
                return false;
            for (const auto& part : relative)
            {
                if (part == "..")
                    return false;
            }
            return true;
        }
    }

    bool ShaderCompiler::IsHlslSource(const std::filesystem::path& path)
    {
        return ToLowerExtension(path) == ".hlsl";
    }

    ShaderCacheStatus ShaderCompiler::GetHlslCacheStatus(const std::filesystem::path& sourcePath)
    {
        ShaderCacheStatus status;
        status.IsHlsl = IsHlslSource(sourcePath);
        if (!status.IsHlsl)
        {
            status.Message = "Legacy shader metadata";
            return status;
        }

        std::error_code ec;
        status.SourceExists = std::filesystem::exists(sourcePath, ec) && !ec;
        if (!status.SourceExists)
        {
            status.Message = "Source file is missing";
            return status;
        }

        const std::filesystem::path cacheDirectory = GetShaderCacheDirectory(sourcePath);
        const std::filesystem::path vertexBytecode = cacheDirectory / "VSMain_vs_5_0.cso";
        const std::filesystem::path pixelBytecode = cacheDirectory / "PSMain_ps_5_0.cso";

        status.VertexBytecodeExists = std::filesystem::exists(vertexBytecode, ec) && !ec;
        status.PixelBytecodeExists = std::filesystem::exists(pixelBytecode, ec) && !ec;
        status.VertexBytecodeFresh = IsBytecodeFresh(sourcePath, vertexBytecode);
        status.PixelBytecodeFresh = IsBytecodeFresh(sourcePath, pixelBytecode);

        if (status.VertexBytecodeFresh && status.PixelBytecodeFresh)
            status.Message = "Compiled bytecode cache is ready";
        else if (status.VertexBytecodeExists || status.PixelBytecodeExists)
            status.Message = "Shader source changed. Recompile required";
        else
            status.Message = "Not compiled yet";

        return status;
    }

    bool ShaderCompiler::GetLastCompileResult(const std::filesystem::path& sourcePath, ShaderCompileResult& outResult)
    {
        auto found = s_LastCompileResults.find(MakeStablePathHash(sourcePath) + "|default");
        if (found == s_LastCompileResults.end())
            return false;

        outResult = found->second;
        return true;
    }

    void ShaderCompiler::ClearCache()
    {
        // 컴파일 결과 문자열은 에디터 세션용 진단 캐시다.
        // 종료 시 비워 두면 실제 해제되지 않은 리소스만 메모리 검사에 남는다.
        std::unordered_map<std::string, ShaderCompileResult>().swap(s_LastCompileResults);
        std::unordered_map<std::string, RuntimeBytecodePaths>().swap(s_RuntimeBytecode);
    }

    ShaderCompileResult ShaderCompiler::CompileHlslFile(const std::filesystem::path& sourcePath, bool forceRecompile)
    {
        return CompileHlslFile(sourcePath, forceRecompile, {});
    }

    std::vector<std::string> ShaderCompiler::NormalizeKeywords(const std::vector<std::string>& keywords)
    {
        std::vector<std::string> normalized;
        normalized.reserve(keywords.size());
        for (const std::string& keyword : keywords)
        {
            if (keyword.empty() || !std::isalpha(static_cast<unsigned char>(keyword.front())) && keyword.front() != '_')
                continue;
            if (!std::all_of(keyword.begin(), keyword.end(), [](unsigned char character)
            {
                return std::isalnum(character) || character == '_';
            }))
                continue;
            normalized.push_back(keyword);
        }
        std::sort(normalized.begin(), normalized.end());
        normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
        return normalized;
    }

    std::string ShaderCompiler::MakeVariantKey(const std::vector<std::string>& keywords)
    {
        const auto normalized = NormalizeKeywords(keywords);
        if (normalized.empty())
            return "default";
        std::string key;
        for (const std::string& keyword : normalized)
        {
            if (!key.empty())
                key += ';';
            key += keyword;
        }
        return key;
    }

    ShaderCompileResult ShaderCompiler::CompileHlslFile(
        const std::filesystem::path& sourcePath,
        bool forceRecompile,
        const std::vector<std::string>& requestedKeywords)
    {
        const auto keywords = NormalizeKeywords(requestedKeywords);
        const std::string variantKey = MakeVariantKey(keywords);
        std::filesystem::path runtimeVertex;
        std::filesystem::path runtimePixel;
        if (TryGetRuntimeBytecode(sourcePath, keywords, runtimeVertex, runtimePixel))
        {
            ShaderCompileResult packaged;
            packaged.Success = true;
            packaged.SourcePath = sourcePath;
            packaged.CacheDirectory = runtimeVertex.parent_path();
            packaged.Vertex = { true, true, runtimeVertex, "VSMain", "vs_5_0", "Using packaged bytecode." };
            packaged.Pixel = { true, true, runtimePixel, "PSMain", "ps_5_0", "Using packaged bytecode." };
            packaged.Summary = "Loaded packaged shader bytecode.";
            return packaged;
        }

        if (s_RuntimeBytecodeOnly)
        {
            ShaderCompileResult result;
            result.SourcePath = sourcePath;
            result.Summary = "Packaged shader bytecode is required but missing: " + sourcePath.string();
            return result;
        }

        ShaderCompileResult result;
        result.SourcePath = sourcePath;
        result.CacheDirectory = GetShaderCacheDirectory(sourcePath, variantKey);

        if (!IsHlslSource(sourcePath))
        {
            result.Summary = "Shader compile skipped. Only .hlsl source files are compiled.";
            return result;
        }

        std::error_code ec;
        if (!std::filesystem::exists(sourcePath, ec) || ec)
        {
            result.Summary = "Shader compile failed. Source file is missing: " + sourcePath.string();
            return result;
        }

        const std::filesystem::path vertexBytecode = result.CacheDirectory / "VSMain_vs_5_0.cso";
        const std::filesystem::path pixelBytecode = result.CacheDirectory / "PSMain_ps_5_0.cso";

        // 셰이더 컴파일 결과는 local 캐시에 둔다.
        // 원본 HLSL은 사람이 편집하는 파일이고, cso는 언제든 다시 만들 수 있는 산출물이기 때문이다.
        result.Vertex = CompileStage(sourcePath, vertexBytecode, ShaderStage::Vertex, "VSMain", "vs_5_0", forceRecompile, keywords);
        result.Pixel = CompileStage(sourcePath, pixelBytecode, ShaderStage::Pixel, "PSMain", "ps_5_0", forceRecompile, keywords);

        result.Success = result.Vertex.Success && result.Pixel.Success;
        if (result.Success)
        {
            ValidateMaterialPropertyReflection(sourcePath, result);
            if (!result.ReflectionErrors.empty())
                result.Success = false;
        }

        if (result.Success)
        {
            const bool usedCacheOnly = result.Vertex.UsedCache && result.Pixel.UsedCache;
            result.Summary = usedCacheOnly
                ? "Shader bytecode cache is up to date: " + sourcePath.string()
                : "Shader compiled: " + sourcePath.string();
            for (const std::string& warning : result.ReflectionWarnings)
                result.Summary += "\n[Reflection Warning] " + warning;
        }
        else
        {
            result.Summary = "Shader compile failed: " + sourcePath.string();
            if (!result.Vertex.Success && !result.Vertex.Message.empty())
                result.Summary += "\n[Vertex] " + result.Vertex.Message;
            if (!result.Pixel.Success && !result.Pixel.Message.empty())
                result.Summary += "\n[Pixel] " + result.Pixel.Message;
            for (const std::string& error : result.ReflectionErrors)
                result.Summary += "\n[Reflection] " + error;
            for (const std::string& warning : result.ReflectionWarnings)
                result.Summary += "\n[Reflection Warning] " + warning;
        }

        // Inspector와 Runtime이 같은 컴파일 상태를 보여줄 수 있도록 마지막 결과를 메모리에 남긴다.
        // 디스크에는 bytecode만 저장하고, 진단 문자열은 에디터 세션 안에서만 유지한다.
        s_LastCompileResults[MakeStablePathHash(sourcePath) + "|" + variantKey] = result;
        return result;
    }

    bool ShaderCompiler::LoadRuntimeBytecodeManifest(const std::filesystem::path& manifestPath, std::string& error)
    {
        std::ifstream input(manifestPath);
        if (!input.is_open())
        {
            error = "Shader build manifest not found: " + manifestPath.string();
            return false;
        }

        nlohmann::json manifest;
        try
        {
            input >> manifest;
        }
        catch (const std::exception& exception)
        {
            error = "Shader build manifest parse failed: " + std::string(exception.what());
            return false;
        }

        const uint32_t version = manifest.value("Version", 0u);
        if ((version != 1u && version != 2u && version != 3u) || !manifest.contains("Shaders") || !manifest["Shaders"].is_array())
        {
            error = "Unsupported or malformed shader build manifest.";
            return false;
        }

        std::error_code ec;
        const auto root = std::filesystem::absolute(manifestPath.parent_path(), ec).lexically_normal();
        if (ec)
        {
            error = "Failed to resolve shader build root: " + ec.message();
            return false;
        }

        std::unordered_map<std::string, RuntimeBytecodePaths> runtimeBytecode;
        for (const auto& shader : manifest["Shaders"])
        {
            if (!shader.is_object())
                continue;

            const std::filesystem::path storedSource = shader.value("SourcePath", "");
            std::vector<std::string> keywords;
            for (const auto& keyword : shader.value("Keywords", nlohmann::json::array()))
            {
                if (keyword.is_string())
                    keywords.push_back(keyword.get<std::string>());
            }
            const std::string variantSuffix = "|" + MakeVariantKey(keywords);
            std::filesystem::path source;
            RuntimeBytecodePaths paths;
            if (!ResolveManifestPath(root, storedSource, source) ||
                !ResolveManifestPath(root, shader.value("VertexBytecode", ""), paths.Vertex) ||
                !ResolveManifestPath(root, shader.value("PixelBytecode", ""), paths.Pixel))
            {
                error = "Shader build manifest contains an unsafe path.";
                return false;
            }
            if (!std::filesystem::is_regular_file(paths.Vertex, ec) || ec ||
                !std::filesystem::is_regular_file(paths.Pixel, ec) || ec)
            {
                error = "Packaged shader bytecode is missing for: " + source.string();
                return false;
            }

            // Release 패키지에는 HLSL 원본이 없으므로 weakly_canonical 결과만 키로 쓰면
            // 실행 위치나 존재하지 않는 마지막 경로 구간에 따라 같은 셰이더가 달라질 수 있다.
            // Manifest의 배포 상대 경로도 함께 키로 보존해 패키지를 어디로 옮겨도 찾게 한다.
            runtimeBytecode[NormalizeShaderPath(source) + variantSuffix] = paths;
            runtimeBytecode[NormalizePortableShaderKey(storedSource) + variantSuffix] = std::move(paths);
        }

        s_RuntimeBytecode = std::move(runtimeBytecode);
        error.clear();
        return true;
    }

    bool ShaderCompiler::TryGetRuntimeBytecode(
        const std::filesystem::path& sourcePath,
        std::filesystem::path& vertexBytecode,
        std::filesystem::path& pixelBytecode)
    {
        return TryGetRuntimeBytecode(sourcePath, {}, vertexBytecode, pixelBytecode);
    }

    bool ShaderCompiler::TryGetRuntimeBytecode(
        const std::filesystem::path& sourcePath,
        const std::vector<std::string>& keywords,
        std::filesystem::path& vertexBytecode,
        std::filesystem::path& pixelBytecode)
    {
        const std::string suffix = "|" + MakeVariantKey(keywords);
        const auto found = s_RuntimeBytecode.find(NormalizeShaderPath(sourcePath) + suffix);
        auto resolved = found;
        if (resolved == s_RuntimeBytecode.end() && sourcePath.is_relative())
            resolved = s_RuntimeBytecode.find(NormalizePortableShaderKey(sourcePath) + suffix);
        if (resolved == s_RuntimeBytecode.end())
            return false;
        vertexBytecode = resolved->second.Vertex;
        pixelBytecode = resolved->second.Pixel;
        return true;
    }

    void ShaderCompiler::SetRuntimeBytecodeOnly(bool enabled)
    {
        s_RuntimeBytecodeOnly = enabled;
    }

    bool ShaderCompiler::IsRuntimeBytecodeOnly()
    {
        return s_RuntimeBytecodeOnly;
    }

    ShaderCompileStageResult ShaderCompiler::CompileStage(
        const std::filesystem::path& sourcePath,
        const std::filesystem::path& bytecodePath,
        ShaderStage stage,
        const std::string& entryPoint,
        const std::string& targetProfile,
        bool forceRecompile,
        const std::vector<std::string>& keywords)
    {
        ShaderCompileStageResult result;
        result.BytecodePath = bytecodePath;
        result.EntryPoint = entryPoint;
        result.TargetProfile = targetProfile;

        if (!forceRecompile && IsBytecodeFresh(sourcePath, bytecodePath))
        {
            result.Success = true;
            result.UsedCache = true;
            result.Message = "Using cached bytecode.";
            return result;
        }

        ID3DBlob* shaderBlob = nullptr;
        ID3DBlob* errorBlob = nullptr;

        std::vector<D3D_SHADER_MACRO> macros;
        macros.reserve(keywords.size() + 1);
        for (const std::string& keyword : keywords)
            macros.push_back({ keyword.c_str(), "1" });
        macros.push_back({ nullptr, nullptr });

        HRESULT hr = D3DCompileFromFile(
            sourcePath.wstring().c_str(),
            keywords.empty() ? nullptr : macros.data(),
            D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entryPoint.c_str(),
            targetProfile.c_str(),
            GetCompileFlags(),
            0,
            &shaderBlob,
            &errorBlob);

        const std::string diagnostic = BlobToString(errorBlob);
        if (FAILED(hr))
        {
            result.Success = false;
            result.Message = diagnostic.empty()
                ? "D3DCompileFromFile failed for " + entryPoint + " (" + targetProfile + ")."
                : diagnostic;
            if (shaderBlob)
                shaderBlob->Release();
            if (errorBlob)
                errorBlob->Release();
            return result;
        }

        if (!WriteBlobToFile(shaderBlob, bytecodePath))
        {
            result.Success = false;
            result.Message = "Compiled " + entryPoint + " but could not write bytecode: " + bytecodePath.string();
            shaderBlob->Release();
            if (errorBlob)
                errorBlob->Release();
            return result;
        }

        result.Success = true;
        result.Message = (stage == ShaderStage::Vertex ? "Vertex" : "Pixel") +
            std::string(" shader bytecode written: ") + bytecodePath.string();

        shaderBlob->Release();
        if (errorBlob)
            errorBlob->Release();
        return result;
    }
}
