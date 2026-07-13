#pragma once

#include "Core.h"

#include <string>
#include <vector>

namespace CCEngine
{
    enum class ScriptFieldType
    {
        Unknown = 0,
        Float,
        Int,
        Bool,
        String,
        Vector3
    };

    enum class ScriptFieldDisplay
    {
        Input = 0,
        Range,
        Drag,
        Step,
        ReadOnly
    };

    struct ScriptFieldInfo
    {
        std::string Name;
        ScriptFieldType Type = ScriptFieldType::Unknown;
        ScriptFieldDisplay Display = ScriptFieldDisplay::Input;
        std::string DefaultValue;
        float Min = 0.0f;
        float Max = 1.0f;
        float Step = 1.0f;
        bool ReadOnly = false;
    };

    struct ScriptClassInfo
    {
        std::string ClassName;
        std::vector<ScriptFieldInfo> Fields;
    };

    class CC_API ScriptMetadata
    {
    public:
        static void Refresh();
        static const std::vector<ScriptClassInfo>& GetClasses();
        static const ScriptClassInfo* FindClass(const std::string& className);
        static std::vector<std::string> GetClassNames();
        static ScriptFieldType FieldTypeFromString(const std::string& type);
        static const char* FieldTypeToString(ScriptFieldType type);
        static ScriptFieldDisplay FieldDisplayFromString(const std::string& display);

    private:
        static void LoadIfNeeded();
    };
}
