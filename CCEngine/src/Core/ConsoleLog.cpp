#include "Core/ConsoleLog.h"
#include <algorithm>
#include <iostream>

namespace CCEngine
{
    namespace
    {
        std::vector<ConsoleLogEntry> s_Entries;
        constexpr size_t s_MaxEntries = 500;
    }

    void ConsoleLog::Info(const std::string& message)
    {
        Push(ConsoleLogLevel::Info, message);
    }

    void ConsoleLog::Warning(const std::string& message)
    {
        Push(ConsoleLogLevel::Warning, message);
    }

    void ConsoleLog::Error(const std::string& message)
    {
        Push(ConsoleLogLevel::Error, message);
    }

    void ConsoleLog::Clear()
    {
        // 로그 패널은 실행 중에는 capacity를 재사용해도 되지만,
        // 종료 검사 전에는 버퍼까지 반환해야 남은 메모리가 누수로 보이지 않는다.
        std::vector<ConsoleLogEntry>().swap(s_Entries);
    }

    const std::vector<ConsoleLogEntry>& ConsoleLog::GetEntries()
    {
        return s_Entries;
    }

    void ConsoleLog::Push(ConsoleLogLevel level, const std::string& message)
    {
        s_Entries.push_back({ level, message });
        if (s_Entries.size() > s_MaxEntries)
            s_Entries.erase(s_Entries.begin(), s_Entries.begin() + (s_Entries.size() - s_MaxEntries));

        std::cout << message << std::endl;
    }
}
