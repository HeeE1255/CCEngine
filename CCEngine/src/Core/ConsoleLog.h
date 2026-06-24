#pragma once

#include "Core.h"
#include <string>
#include <vector>

namespace CCEngine
{
    enum class ConsoleLogLevel
    {
        Info = 0,
        Warning,
        Error
    };

    struct ConsoleLogEntry
    {
        ConsoleLogLevel Level = ConsoleLogLevel::Info;
        std::string Message;
    };

    class CC_API ConsoleLog
    {
    public:
        static void Info(const std::string& message);
        static void Warning(const std::string& message);
        static void Error(const std::string& message);
        static void Clear();

        static const std::vector<ConsoleLogEntry>& GetEntries();

    private:
        static void Push(ConsoleLogLevel level, const std::string& message);
    };
}
