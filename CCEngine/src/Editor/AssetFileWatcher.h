#pragma once

#include "Core.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

namespace CCEngine
{
    class CC_API AssetFileWatcher
    {
    public:
        AssetFileWatcher() = default;
        ~AssetFileWatcher();

        AssetFileWatcher(const AssetFileWatcher&) = delete;
        AssetFileWatcher& operator=(const AssetFileWatcher&) = delete;

        bool Start(const std::filesystem::path& rootDirectory);
        void Stop();
        bool IsRunning() const { return m_Running.load(); }

        bool ConsumeDebouncedChanges(
            std::vector<std::filesystem::path>& changedPaths,
            std::chrono::milliseconds debounceTime = std::chrono::milliseconds(200));

    private:
        void WatchLoop();
        void RecordChangedPaths(const std::vector<std::filesystem::path>& changedPaths);

    private:
        std::filesystem::path m_RootDirectory;
        void* m_DirectoryHandle = nullptr;
        std::thread m_WatcherThread;
        std::atomic<bool> m_Running = false;

        std::mutex m_Mutex;
        bool m_HasPendingChanges = false;
        std::chrono::steady_clock::time_point m_LastChangeTime = {};
        std::vector<std::filesystem::path> m_ChangedPaths;
    };
}
