#pragma once
#include "Renderer/Shader.h"
#include "Renderer/Texture.h"
#include <memory>

namespace CCEngine
{
    template<class Resolve>
    void BindMaterialTextures(const Shader& shader, const std::shared_ptr<Texture2D>& fallback, Resolve resolve)
    {
        if (!fallback) throw std::invalid_argument("Material texture fallback is missing");
        // 활성 셰이더가 읽는 모든 슬롯을 채워 이전 머티리얼의 텍스처가 남지 않게 한다.
        for (const auto& [name, slot] : shader.GetTextureBindings())
        {
            auto texture = resolve(name);
            (texture ? texture : fallback)->Bind(slot);
        }
    }
}
