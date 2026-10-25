#include "Renderer/MaterialPropertyBinding.h"
#include <unordered_map>
#include <iostream>
#include <stdexcept>

using namespace CCEngine;
void Require(bool value) { if (!value) throw std::runtime_error("Material property contract failed"); }
int main()
{
    try {
        const auto definitions = ShaderPropertyParser::ParseSource(
            "// @property Float Z = 2\n// @property Float A = 3\n"
            "// @property Color Tint = 0.1,0.2,0.3,1\n// @property Toggle Enabled = true\n");
        std::unordered_map<std::string, ShaderPropertyValue> values;
        values["A"].Type = ShaderPropertyType::Float; values["A"].FloatValue = 9;
        values["Obsolete"].Type = ShaderPropertyType::Float;
        auto check = [&] {
            size_t i=0;
            VisitMaterialProperties(definitions, values, [&](const auto& name, const auto& value) {
                Require(name == definitions[i].Name);
                if (i == 0) Require(value.FloatValue == 2);
                if (i == 1) Require(value.FloatValue == 9);
                if (i == 2) Require(value.Color.y == .2f);
                if (i == 3) Require(value.BoolValue);
                ++i;
            });
            Require(i==4);
        };
        check(); values.rehash(257); check();
        values["Z"].Type = ShaderPropertyType::Color; check();
        for (const auto& bad : {std::string("// @property Float A\n// @property Float A\n"),
                              std::string("// @property Toggle Roughness\n")}) {
            bool rejected=false;
            try { ShaderPropertyParser::ParseSource(bad); }
            catch (const std::invalid_argument&) { rejected=true; }
            Require(rejected);
        }
        std::string overflow;
        for (int i=0;i<17;++i) overflow += "// @property Float F"+std::to_string(i)+"\n";
        bool rejected=false;
        try { ShaderPropertyParser::ParseSource(overflow); }
        catch (const std::invalid_argument&) { rejected=true; }
        Require(rejected);
        std::cout << "PASS material declaration order, defaults, rehash, obsolete/type mismatch, duplicate/reserved/capacity rejection\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
