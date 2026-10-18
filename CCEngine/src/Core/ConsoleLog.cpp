#include "Core/ConsoleLog.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace CCEngine
{
    namespace
    {
        std::vector<ConsoleLogEntry> s_Entries;
        constexpr size_t s_MaxEntries = 500;
        std::filesystem::path s_FilePath;
        ConsoleLogLevel s_MinimumFileLevel = ConsoleLogLevel::Info;
        uint64_t s_MaximumFileBytes = 5ull * 1024ull * 1024ull;
        uint32_t s_RetainedFiles = 5;
        std::mutex s_LogMutex;

        std::filesystem::path RotatedLogPath(const std::filesystem::path& path, uint32_t index)
        {
            return path.parent_path() /
                (path.stem().string() + "." + std::to_string(index) + path.extension().string());
        }

        bool RotateLogFiles(const std::filesystem::path& path)
        {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(path, ec) || ec)
                return true;
            if (s_RetainedFiles == 0)
            {
                std::filesystem::remove(path, ec);
                return !ec;
            }

            // 오래된 파일부터 역순으로 옮겨야 아직 보존할 로그를 덮어쓰지 않는다.
            std::filesystem::remove(RotatedLogPath(path, s_RetainedFiles), ec);
            ec.clear();
            for (uint32_t index = s_RetainedFiles; index > 1; --index)
            {
                const auto source = RotatedLogPath(path, index - 1);
                if (!std::filesystem::is_regular_file(source, ec) || ec)
                {
                    ec.clear();
                    continue;
                }
                std::filesystem::rename(source, RotatedLogPath(path, index), ec);
                if (ec)
                    return false;
            }
            std::filesystem::rename(path, RotatedLogPath(path, 1), ec);
            return !ec;
        }

        bool AppendBoundedLogLine(const std::string& line)
        {
            size_t offset = 0;
            while (offset < line.size())
            {
                std::error_code ec;
                uint64_t currentSize = std::filesystem::is_regular_file(s_FilePath, ec)
                    ? std::filesystem::file_size(s_FilePath, ec) : 0;
                if (ec)
                    return false;
                if (currentSize >= s_MaximumFileBytes)
                {
                    if (!RotateLogFiles(s_FilePath))
                        return false;
                    currentSize = 0;
                }

                const uint64_t available = s_MaximumFileBytes - currentSize;
                const size_t chunkSize = static_cast<size_t>((std::min<uint64_t>)(available, line.size() - offset));
                std::ofstream output(s_FilePath, std::ios::binary | std::ios::app);
                if (!output.is_open())
                    return false;
                output.write(line.data() + offset, static_cast<std::streamsize>(chunkSize));
                output.close();
                offset += chunkSize;

                // 한 메시지가 상한보다 커도 파일 하나가 커지지 않도록 다음 조각 전에 회전한다.
                if (offset < line.size() && !RotateLogFiles(s_FilePath))
                    return false;
            }
            return true;
        }
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

    bool ConsoleLog::ConfigureFile(
        const std::filesystem::path& path,
        ConsoleLogLevel minimumLevel,
        uint64_t maximumBytes,
        uint32_t retainedFiles)
    {
        std::lock_guard<std::mutex> lock(s_LogMutex);
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
            return false;
        s_MaximumFileBytes = (std::max)(maximumBytes, 64ull * 1024ull);
        s_RetainedFiles = retainedFiles;
        if (!RotateLogFiles(path))
            return false;
        std::ofstream output(path, std::ios::trunc);
        if (!output.is_open())
            return false;
        s_FilePath = path;
        s_MinimumFileLevel = minimumLevel;
        return true;
    }

    const std::filesystem::path& ConsoleLog::GetFilePath()
    {
        return s_FilePath;
    }

    const std::vector<ConsoleLogEntry>& ConsoleLog::GetEntries()
    {
        return s_Entries;
    }

    void ConsoleLog::Push(ConsoleLogLevel level, const std::string& message)
    {
        std::lock_guard<std::mutex> lock(s_LogMutex);
        s_Entries.push_back({ level, message });
        if (s_Entries.size() > s_MaxEntries)
            s_Entries.erase(s_Entries.begin(), s_Entries.begin() + (s_Entries.size() - s_MaxEntries));

        std::cout << message << std::endl;

        if (!s_FilePath.empty() && static_cast<int>(level) >= static_cast<int>(s_MinimumFileLevel))
        {
            const auto now = std::chrono::system_clock::now();
            const std::time_t time = std::chrono::system_clock::to_time_t(now);
            std::tm localTime{};
            localtime_s(&localTime, &time);
            const char* levelName = level == ConsoleLogLevel::Error ? "Error" : (level == ConsoleLogLevel::Warning ? "Warning" : "Info");
            std::ostringstream line;
            line << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S") << " [" << levelName << "] " << message << '\n';
            AppendBoundedLogLine(line.str());
        }
    }
}
