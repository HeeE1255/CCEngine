#include "Renderer/RuntimeShaderLibrary.h"

#include "Core/AssetDatabase.h"
#include "Core/ConsoleLog.h"
#include "Renderer/MaterialAsset.h"
#include "Renderer/ShaderCompiler.h"

#include <iostream>
#include <unordered_map>

namespace CCEngine
{
    namespace
    {
        struct RuntimeShaderEntry
        {
            std::shared_ptr<Shader> Program;
            bool Failed = false;
            std::string Message;
        };

        std::unordered_map<std::string, RuntimeShaderEntry> s_ShaderCache;
        std::shared_ptr<Shader> s_ErrorShader;

        std::string NormalizePathKey(const std::filesystem::path& path)
        {
            std::error_code ec;
            std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
            return (ec ? path.lexically_normal() : canonical).generic_string();
        }

        std::filesystem::path ResolveMaterialShaderPath(const MaterialAsset& material)
        {
            if (!material.ShaderGuid.empty())
            {
                std::filesystem::path path = AssetDatabase::GetPathFromGuid(material.ShaderGuid);
                if (!path.empty())
                    return path;
            }

            if (!material.ShaderPath.empty())
                return material.ShaderPath;

            return {};
        }

        std::shared_ptr<Shader> CreateShaderFromCompileResult(const ShaderCompileResult& result)
        {
            std::shared_ptr<Shader> shader;
            shader.reset(Shader::CreateFromBytecode(result.Vertex.BytecodePath, result.Pixel.BytecodePath));
            if (!shader || !shader->IsValid())
                return nullptr;

            return shader;
        }
    }

    std::shared_ptr<Shader> RuntimeShaderLibrary::GetShaderForMaterial(const MaterialAsset& material)
    {
        std::filesystem::path shaderPath = ResolveMaterialShaderPath(material);
        if (shaderPath.empty())
            return nullptr;

        const std::string key = NormalizePathKey(shaderPath);
        auto found = s_ShaderCache.find(key);
        if (found != s_ShaderCache.end())
            return found->second.Program;

        RuntimeShaderEntry entry;
        ShaderCompileResult compileResult = ShaderCompiler::CompileHlslFile(shaderPath, false);
        if (compileResult.Success)
        {
            entry.Program = CreateShaderFromCompileResult(compileResult);
        }

        if (!compileResult.Success || !entry.Program)
        {
            entry.Failed = true;
            entry.Program = GetErrorShader();
            entry.Message = compileResult.Summary.empty()
                ? "Shader failed. Error shader is active."
                : compileResult.Summary;

            // 렌더러는 매 프레임 돈다. 실패 로그는 처음 로드할 때 한 번만 남겨야 콘솔이 도배되지 않는다.
            // 화면에는 ErrorShader가 계속 보이므로 사용자는 로그를 다시 읽지 않아도 문제를 알아챌 수 있다.
            ConsoleLog::Error("Shader compile failed. Error material applied: " + shaderPath.string());
            if (!compileResult.Summary.empty())
                std::cout << compileResult.Summary << std::endl;
        }
        else
        {
            entry.Message = "Runtime shader is loaded.";
        }

        s_ShaderCache[key] = entry;
        return entry.Program;
    }

    std::shared_ptr<Shader> RuntimeShaderLibrary::GetErrorShader()
    {
        if (s_ErrorShader && s_ErrorShader->IsValid())
            return s_ErrorShader;

        std::filesystem::path errorShaderPath = std::filesystem::current_path() / "assets" / "shaders" / "ErrorShader.hlsl";
        ShaderCompileResult compileResult = ShaderCompiler::CompileHlslFile(errorShaderPath, false);
        if (compileResult.Success)
            s_ErrorShader = CreateShaderFromCompileResult(compileResult);

        if (!s_ErrorShader || !s_ErrorShader->IsValid())
        {
            // 에러 셰이더까지 깨진 경우에는 마지막 안전장치로 기본 셰이더를 쓴다.
            // 이 경로는 사용자 작업물이 아니라 엔진 내부 리소스 문제를 확인하기 위한 fallback이다.
            s_ErrorShader.reset(Shader::Create("assets/shaders/Base3D.hlsl"));
        }

        return s_ErrorShader;
    }

    RuntimeShaderStatus RuntimeShaderLibrary::GetStatusForShader(const std::filesystem::path& shaderPath)
    {
        RuntimeShaderStatus status;
        if (shaderPath.empty())
            return status;

        const std::string key = NormalizePathKey(shaderPath);
        auto found = s_ShaderCache.find(key);
        if (found == s_ShaderCache.end())
            return status;

        status.HasEntry = true;
        status.Failed = found->second.Failed;
        status.UsingErrorShader = found->second.Failed;
        status.Message = found->second.Message;
        return status;
    }

    RuntimeShaderStatus RuntimeShaderLibrary::GetStatusForMaterial(const MaterialAsset& material)
    {
        return GetStatusForShader(ResolveMaterialShaderPath(material));
    }

    void RuntimeShaderLibrary::Invalidate(const std::filesystem::path& shaderPath)
    {
        if (shaderPath.empty())
            return;

        s_ShaderCache.erase(NormalizePathKey(shaderPath));
    }

    void RuntimeShaderLibrary::Clear()
    {
        s_ShaderCache.clear();
        s_ErrorShader.reset();
    }
}
