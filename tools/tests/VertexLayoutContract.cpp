#include "Renderer/Buffer.h"
#include <iostream>

using namespace CCEngine;

int main()
{
    unsigned checks = 0;
    const auto rejected = [&](BufferLayout layout, uint32_t stride = 0)
    {
        try { layout.Validate(stride); }
        catch (const std::invalid_argument&) { ++checks; return; }
        throw std::runtime_error("Invalid layout was accepted");
    };
    try
    {
        BufferLayout valid{{ShaderDataType::Float3, "POSITION"}, {ShaderDataType::Int4, "BONEIDS"}};
        valid.Validate(28);
        rejected(valid, 32);
        rejected({});
        rejected({{ShaderDataType::Bool, "FLAG"}});
        rejected({{ShaderDataType::Float3, "POSITION"}, {ShaderDataType::Float2, "POSITION"}});
        auto broken = valid;
        broken.begin()->Size = 4;
        rejected(broken);
        broken = valid;
        (++broken.begin())->Offset = 4;
        rejected(broken);
        broken = valid;
        (++broken.begin())->Offset = UINT32_MAX;
        rejected(broken);
        broken = valid;
        broken.begin()->Normalized = true;
        rejected(broken);
        std::cout << "PASS vertex layout: valid layout + " << checks << " rejection cases\n";
        BufferLayout packed{{ShaderDataType::Float2,"TEXCOORD"}, {ShaderDataType::Float3,"UNUSED"}, {ShaderDataType::Float3,"POSITION"}};
        const auto mapped = ResolveVertexInputs(packed, {{"position",ShaderDataType::Float3,7}, {"TEXCOORD0",ShaderDataType::Float2,2}});
        if (mapped.size() != 2 || mapped[0].Offset != 20 || mapped[0].Location != 7 || mapped[1].Offset != 0 || mapped[1].Location != 2)
            throw std::runtime_error("Semantic/location mapping mismatch");
        auto rejectMapping = [&](const std::vector<VertexInput>& inputs)
        {
            try { ResolveVertexInputs(packed,inputs); }
            catch (const std::invalid_argument&) { return; }
            throw std::runtime_error("Invalid input mapping was accepted");
        };
        rejectMapping({{"COLOR",ShaderDataType::Float4,0}});
        rejectMapping({{"POSITION",ShaderDataType::Int3,0}});
        rejectMapping({{"POSITION",ShaderDataType::Float3,32}});
        rejectMapping({{"POSITION",ShaderDataType::Float3,0},{"TEXCOORD",ShaderDataType::Float2,0}});
        rejectMapping({{"TEXCOORD",ShaderDataType::Float2,0},{"TEXCOORD0",ShaderDataType::Float2,1}});
        std::cout << "PASS vertex input mapping: sparse locations, semantic alias, unused field, 5 rejection cases\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
