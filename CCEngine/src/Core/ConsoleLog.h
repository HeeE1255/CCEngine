#pragma once

#include "Core.h"
#include <cstdint>
#include <string>
#include <filesystem>
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
        static bool ConfigureFile(
            const std::filesystem::path& path,
            ConsoleLogLevel minimumLevel = ConsoleLogLevel::Info,
            uint64_t maximumBytes = 5ull * 1024ull * 1024ull,
            uint32_t retainedFiles = 5);
        static const std::filesystem::path& GetFilePath();

        static const std::vector<ConsoleLogEntry>& GetEntries();

    private:
        static void Push(ConsoleLogLevel level, const std::string& message);
    };
}
