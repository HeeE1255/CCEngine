#include "ShaderReflectionCommand.h"
#define NOMINMAX
#include <Windows.h>
#include <dxcapi.h>
#include <d3d12shader.h>
#include <wrl/client.h>
#include <json.hpp>
#include <iostream>
#include <stdexcept>

namespace
{
    using Microsoft::WRL::ComPtr;
    using Json = nlohmann::json;

    void Check(HRESULT result, const char* operation)
    {
        if (FAILED(result))
            throw std::runtime_error(std::string(operation) + " failed: HRESULT " + std::to_string(result));
    }

    Json ReadType(ID3D12ShaderReflectionType* type, unsigned depth = 0)
    {
        if (!type || depth > 32) throw std::runtime_error("Invalid or excessively nested shader type");
        D3D12_SHADER_TYPE_DESC desc{};
        Check(type->GetDesc(&desc), "Get shader type");
        Json result = { {"class", desc.Class}, {"scalar", desc.Type}, {"rows", desc.Rows},
            {"columns", desc.Columns}, {"elements", desc.Elements}, {"offset", desc.Offset},
            {"members", Json::array()} };
        for (UINT i = 0; i < desc.Members; ++i)
        {
            auto member = ReadType(type->GetMemberTypeByIndex(i), depth + 1);
            const char* name = type->GetMemberTypeName(i);
            if (!name) throw std::runtime_error("Shader member name missing");
            member["name"] = name;
            result["members"].push_back(std::move(member));
        }
        return result;
    }
}

int RunShaderReflectionCommand(const std::filesystem::path& bytecode,
    const std::filesystem::path& compilerLibrary)
{
    try
    {
        if (!compilerLibrary.is_absolute() || !std::filesystem::is_regular_file(compilerLibrary))
            throw std::runtime_error("--dxcompiler must point to an existing absolute dxcompiler.dll path");
        // 컴파일에 사용한 DXC 옆 DLL만 로드한다. 작업 폴더의 동명 DLL을 찾지 않는다.
        // COM 객체를 먼저 해제한 뒤 DLL이 내려가도록 수명 순서도 고정한다.
        struct Library
        {
            HMODULE Handle;
            ~Library() { if (Handle) FreeLibrary(Handle); }
        } library{ LoadLibraryExW(compilerLibrary.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32) };
        if (!library.Handle) throw std::runtime_error("Cannot load DXC reflection library");
        auto create = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(library.Handle, "DxcCreateInstance"));
        if (!create) throw std::runtime_error("DxcCreateInstance is missing");
        ComPtr<IDxcUtils> utils;
        Check(create(CLSID_DxcUtils, IID_PPV_ARGS(&utils)), "Create DXC utilities");
        ComPtr<IDxcBlobEncoding> blob;
        Check(utils->LoadFile(bytecode.c_str(), nullptr, &blob), "Read DXIL");
        DxcBuffer buffer{ blob->GetBufferPointer(), blob->GetBufferSize(), 0 };
        ComPtr<ID3D12ShaderReflection> reflection;
        Check(utils->CreateReflection(&buffer, IID_PPV_ARGS(&reflection)), "Reflect DXIL");
        D3D12_SHADER_DESC desc{};
        Check(reflection->GetDesc(&desc), "Get shader description");
        Json output = { {"schema", 1}, {"buffers", Json::array()}, {"resources", Json::array()},
            {"inputs", Json::array()}, {"outputs", Json::array()} };
        for (bool input : { true, false })
        {
            const UINT count = input ? desc.InputParameters : desc.OutputParameters;
            for (UINT i = 0; i < count; ++i)
            {
                D3D12_SIGNATURE_PARAMETER_DESC parameter{};
                Check(input ? reflection->GetInputParameterDesc(i, &parameter) :
                    reflection->GetOutputParameterDesc(i, &parameter), "Get stage signature");
                // 출력의 ReadWriteMask는 '쓰지 않은 성분'이다. 입력과 같은 의미로 읽으면
                // 실제 출력되지 않은 값을 다음 단계가 사용할 수 있다고 잘못 판단하게 된다.
                const UINT activeMask = parameter.Mask & (input ? parameter.ReadWriteMask : ~parameter.ReadWriteMask);
                output[input ? "inputs" : "outputs"].push_back({ {"semantic", parameter.SemanticName},
                    {"index", parameter.SemanticIndex}, {"componentType", parameter.ComponentType},
                    {"mask", parameter.Mask}, {"activeMask", activeMask}, {"systemValue", parameter.SystemValueType},
                    {"stream", parameter.Stream}, {"precision", parameter.MinPrecision} });
            }
        }
        for (UINT i = 0; i < desc.BoundResources; ++i)
        {
            D3D12_SHADER_INPUT_BIND_DESC resource{};
            Check(reflection->GetResourceBindingDesc(i, &resource), "Get resource binding");
            output["resources"].push_back({ {"name", resource.Name}, {"type", resource.Type},
                {"binding", resource.BindPoint}, {"count", resource.BindCount}, {"space", resource.Space},
                {"dimension", resource.Dimension} });
            if (resource.Type != D3D_SIT_CBUFFER) continue;
            auto* constantBuffer = reflection->GetConstantBufferByName(resource.Name);
            D3D12_SHADER_BUFFER_DESC cb{};
            Check(constantBuffer->GetDesc(&cb), "Get constant buffer");
            Json item = { {"name", cb.Name}, {"size", cb.Size}, {"binding", resource.BindPoint},
                {"space", resource.Space}, {"members", Json::array()} };
            for (UINT v = 0; v < cb.Variables; ++v)
            {
                auto* variable = constantBuffer->GetVariableByIndex(v);
                D3D12_SHADER_VARIABLE_DESC member{};
                Check(variable->GetDesc(&member), "Get constant buffer member");
                item["members"].push_back({ {"name", member.Name}, {"offset", member.StartOffset},
                    {"size", member.Size}, {"type", ReadType(variable->GetType())} });
            }
            output["buffers"].push_back(std::move(item));
        }
        std::cout << output.dump(2) << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "DXIL reflection failed: " << error.what() << '\n';
        return 1;
    }
}
