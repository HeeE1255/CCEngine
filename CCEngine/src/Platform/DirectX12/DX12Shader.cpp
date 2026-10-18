#include "DX12Shader.h"
#if defined(CC_ENABLE_DX12_RHI)
#include <d3dcompiler.h>
#include <fstream>
#include <stdexcept>
#include <cstring>
namespace CCEngine
{
    namespace
    {
        std::vector<uint8_t> Compile(const std::filesystem::path& path, const std::string& entry, const char* profile)
        {
            if (entry.empty()) throw std::invalid_argument("Empty HLSL entry point");
            ComPtr<ID3DBlob> code, errors;
            const auto result = D3DCompileFromFile(path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                entry.c_str(), profile, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
            if (FAILED(result) && errors)
                throw std::runtime_error(std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()));
            CheckDX12(result, "Compile DX12 HLSL");
            const auto* begin = static_cast<const uint8_t*>(code->GetBufferPointer());
            return { begin, begin + code->GetBufferSize() };
        }
        std::vector<uint8_t> ReadBytecode(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file) throw std::runtime_error("Cannot open DX12 bytecode: " + path.string());
            const std::streamoff bytes = file.tellg();
            if (bytes < 32 || bytes > 64 * 1024 * 1024) throw std::runtime_error("Invalid DX12 bytecode size");
            std::vector<uint8_t> data(static_cast<size_t>(bytes)); file.seekg(0);
            if (!file.read(reinterpret_cast<char*>(data.data()), bytes)) throw std::runtime_error("Cannot read DX12 bytecode");
            return data;
        }
    }
    DX12Shader::DX12Shader(DX12Context& context, const std::filesystem::path& hlsl,
        const std::string& vertexEntry, const std::string& pixelEntry)
        : DX12Shader(context, Compile(hlsl, vertexEntry, "vs_5_1"), Compile(hlsl, pixelEntry, "ps_5_1")) {}
    DX12Shader::DX12Shader(DX12Context& context, std::vector<uint8_t> vertex, std::vector<uint8_t> pixel)
        : m_Device(context.SharedDevice()), m_Program(std::make_shared<DX12Program>())
    {
        if (vertex.size() < 32 || pixel.size() < 32 || std::memcmp(vertex.data(), "DXBC", 4) || std::memcmp(pixel.data(), "DXBC", 4))
            throw std::invalid_argument("DX12 shader requires DXBC/DXIL containers");
        m_Program->Owner = m_Device; m_Program->ID = context.NextID();
        m_Program->Vertex = std::move(vertex); m_Program->Pixel = std::move(pixel);
    }
    std::unique_ptr<DX12Shader> DX12Shader::FromBytecodeFiles(DX12Context& context,
        const std::filesystem::path& vertex, const std::filesystem::path& pixel)
    {
        return std::make_unique<DX12Shader>(context, ReadBytecode(vertex), ReadBytecode(pixel));
    }
}
#endif
