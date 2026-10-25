#include "VulkanShader.h"
#if defined(CC_ENABLE_VULKAN_RHI)
#include <fstream>
#include <stdexcept>
namespace CCEngine
{
    namespace
    {
        std::vector<uint32_t> ReadSpirv(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file) throw std::runtime_error("Cannot open SPIR-V: " + path.string());
            const std::streamoff length = file.tellg();
            if (length < 20 || length > 64 * 1024 * 1024 || length % 4) throw std::runtime_error("Invalid SPIR-V file size");
            std::vector<uint32_t> words(static_cast<size_t>(length) / 4);
            file.seekg(0);
            if (!file.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(length)))
                throw std::runtime_error("Cannot read SPIR-V");
            return words;
        }
        VkShaderModule CreateModule(VulkanContext& context, const std::vector<uint32_t>& words)
        {
            if (words.size() < 5 || words[0] != 0x07230203 || words[4] != 0)
                throw std::invalid_argument("Shader input must be SPIR-V words, not HLSL/GLSL source");
            VkShaderModuleCreateInfo info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            info.codeSize = words.size() * sizeof(uint32_t); info.pCode = words.data();
            VkShaderModule module = VK_NULL_HANDLE;
            CheckVulkan(vkCreateShaderModule(context.Device(), &info, nullptr, &module), "Create SPIR-V shader module");
            return module;
        }
    }
    VulkanShader::VulkanShader(VulkanContext& context, const std::vector<uint32_t>& vertex,
        const std::vector<uint32_t>& fragment, const std::string& vertexEntry, const std::string& fragmentEntry)
        : m_Device(context.SharedDevice()), m_Program(std::make_shared<VulkanShaderProgram>(context))
    {
        if (vertexEntry.empty() || fragmentEntry.empty()) throw std::invalid_argument("Empty shader entry point");
        m_Program->VertexEntry = vertexEntry; m_Program->FragmentEntry = fragmentEntry;
        m_Program->Vertex = CreateModule(context, vertex);
        m_Program->Fragment = CreateModule(context, fragment);
    }
    VulkanShader::VulkanShader(VulkanContext& context, const std::filesystem::path& vertex,
        const std::filesystem::path& fragment, const std::string& vertexEntry, const std::string& fragmentEntry)
        : VulkanShader(context, ReadSpirv(vertex), ReadSpirv(fragment), vertexEntry, fragmentEntry) {}
}
#endif
