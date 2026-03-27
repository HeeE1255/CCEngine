#include "Renderer/Shader.h"
#include "Platform/DirectX11/DX11Shader.h"

namespace CCEngine
{
    Shader* Shader::Create(const std::string& filepath)
    {
        return new DX11Shader(filepath);
    }
}