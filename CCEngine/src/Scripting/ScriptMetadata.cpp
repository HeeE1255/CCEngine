#include "Scripting/ScriptMetadata.h"

#include "Core/ConsoleLog.h"
#include "json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace CCEngine
{
    namespace
    {
        std::vector<ScriptClassInfo> s_Classes;
        bool s_Loaded = false;

        std::filesystem::path GetManifestPath()
        {
            return std::filesystem::current_path() / "assets" / "Scripts" / "Build" / "GameScripts.manifest.json";
        }

        std::string ReadDefaultValue(const nlohmann::json& fieldData)
        {
            if (!fieldData.contains("DefaultValue") || fieldData["DefaultValue"].is_null())
                return {};

            const auto& value = fieldData["DefaultValue"];
            if (value.is_string())
                return value.get<std::string>();
            if (value.is_boolean())
                return value.get<bool>() ? "true" : "false";
            if (value.is_number_float())
                return std::to_string(value.get<float>());
            if (value.is_number_integer())
                return std::to_string(value.get<int>());
            if (value.is_object())
            {
                float x = value.value("X", 0.0f);
                float y = value.value("Y", 0.0f);
                float z = value.value("Z", 0.0f);
                return std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z);
            }
            return {};
        }

        float ReadOptionalFloat(const nlohmann::json& data, const char* key, float fallback)
        {
            if (!data.contains(key) || data[key].is_null())
                return fallback;
            return data[key].get<float>();
        }
    }

    void ScriptMetadata::Refresh()
    {
        s_Classes.clear();
        s_Loaded = true;

        const auto manifestPath = GetManifestPath();
        std::ifstream in(manifestPath);
        if (!in.is_open())
            return;

        try
        {
            nlohmann::json data;
            in >> data;

            for (const auto& classData : data.value("Classes", nlohmann::json::array()))
            {
                ScriptClassInfo classInfo;
                classInfo.ClassName = classData.value("ClassName", "");
                if (classInfo.ClassName.empty())
                    continue;

                for (const auto& fieldData : classData.value("Fields", nlohmann::json::array()))
                {
                    ScriptFieldInfo field;
                    field.Name = fieldData.value("Name", "");
                    field.Type = FieldTypeFromString(fieldData.value("Type", ""));
                    field.DefaultValue = ReadDefaultValue(fieldData);
                    if (fieldData.contains("Editor") && fieldData["Editor"].is_object())
                    {
                        const auto& editorData = fieldData["Editor"];
                        field.Display = FieldDisplayFromString(editorData.value("Display", "Input"));
                        field.Min = ReadOptionalFloat(editorData, "Min", 0.0f);
                        field.Max = ReadOptionalFloat(editorData, "Max", 1.0f);
                        field.Step = ReadOptionalFloat(editorData, "Step", field.Display == ScriptFieldDisplay::Drag ? 0.1f : 1.0f);
                        field.ReadOnly = editorData.value("ReadOnly", false);
                    }
                    if (!field.Name.empty() && field.Type != ScriptFieldType::Unknown)
                        classInfo.Fields.push_back(field);
                }

                s_Classes.push_back(classInfo);
            }

            std::sort(s_Classes.begin(), s_Classes.end(),
                [](const ScriptClassInfo& a, const ScriptClassInfo& b) { return a.ClassName < b.ClassName; });
        }
        catch (const std::exception& e)
        {
            ConsoleLog::Warning(std::string("Failed to read script metadata: ") + e.what());
            s_Classes.clear();
        }
    }

    const std::vector<ScriptClassInfo>& ScriptMetadata::GetClasses()
    {
        LoadIfNeeded();
        return s_Classes;
    }

    const ScriptClassInfo* ScriptMetadata::FindClass(const std::string& className)
    {
        LoadIfNeeded();
        for (const auto& classInfo : s_Classes)
        {
            if (classInfo.ClassName == className)
                return &classInfo;
        }
        return nullptr;
    }

    std::vector<std::string> ScriptMetadata::GetClassNames()
    {
        LoadIfNeeded();
        std::vector<std::string> names;
        names.reserve(s_Classes.size());
        for (const auto& classInfo : s_Classes)
            names.push_back(classInfo.ClassName);
        return names;
    }

    ScriptFieldType ScriptMetadata::FieldTypeFromString(const std::string& type)
    {
        if (type == "float" || type == "Single")
            return ScriptFieldType::Float;
        if (type == "int" || type == "Int32")
            return ScriptFieldType::Int;
        if (type == "bool" || type == "Boolean")
            return ScriptFieldType::Bool;
        if (type == "string" || type == "String")
            return ScriptFieldType::String;
        if (type == "Vector3" || type == "CCEngine.Vector3")
            return ScriptFieldType::Vector3;
        return ScriptFieldType::Unknown;
    }

    const char* ScriptMetadata::FieldTypeToString(ScriptFieldType type)
    {
        switch (type)
        {
        case ScriptFieldType::Float: return "float";
        case ScriptFieldType::Int: return "int";
        case ScriptFieldType::Bool: return "bool";
        case ScriptFieldType::String: return "string";
        case ScriptFieldType::Vector3: return "Vector3";
        default: return "unknown";
        }
    }

    ScriptFieldDisplay ScriptMetadata::FieldDisplayFromString(const std::string& display)
    {
        if (display == "Range")
            return ScriptFieldDisplay::Range;
        if (display == "Drag")
            return ScriptFieldDisplay::Drag;
        if (display == "Step")
            return ScriptFieldDisplay::Step;
        if (display == "ReadOnly")
            return ScriptFieldDisplay::ReadOnly;
        return ScriptFieldDisplay::Input;
    }

    void ScriptMetadata::LoadIfNeeded()
    {
        if (!s_Loaded)
            Refresh();
    }
}
