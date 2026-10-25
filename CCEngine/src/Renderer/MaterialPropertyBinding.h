#pragma once
#include "Renderer/ShaderProperty.h"

namespace CCEngine
{
    template<class Values, class Write>
    void VisitMaterialProperties(const std::vector<ShaderPropertyDefinition>& definitions,
        const Values& values, Write write)
    {
        // 누락되거나 타입이 바뀐 값도 선언의 슬롯을 소비한다. 뒤 속성을 앞으로 당기면 안 된다.
        for (const auto& definition : definitions)
        {
            ShaderPropertyValue value;
            value.Type = definition.Type;
            value.Color = definition.DefaultColor;
            value.FloatValue = definition.DefaultFloat;
            value.BoolValue = definition.DefaultBool;
            const auto found = values.find(definition.Name);
            if (found != values.end() && found->second.Type == definition.Type) value = found->second;
            write(definition.Name, value);
        }
    }
}
