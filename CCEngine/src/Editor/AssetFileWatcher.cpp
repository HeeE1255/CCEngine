#include "AssetFileWatcher.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>

namespace CCEngine
{
    namespace
    {
        HANDLE ToHandle(void* value)
        {
            return reinterpret_cast<HANDLE>(value);
        }

        void* FromHandle(HANDLE value)
        {
            return reinterpret_cast<void*>(value);
        }
    }

    AssetFileWatcher::~AssetFileWatcher()
    {
        Stop();
    }

    bool AssetFileWatcher::Start(const std::filesystem::path& rootDirectory)
    {
        Stop();

        std::error_code ec;
        std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(rootDirectory, ec);
        if (ec || !std::filesystem::exists(canonicalRoot, ec) || !std::filesystem::is_directory(canonicalRoot, ec))
            return false;

        HANDLE directoryHandle = CreateFileW(
            canonicalRoot.wstring().c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            nullptr);

        if (directoryHandle == INVALID_HANDLE_VALUE)
            return false;

        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_RootDirectory = canonicalRoot;
            m_ChangedPaths.clear();
            m_HasPendingChanges = false;
        }

        m_DirectoryHandle = FromHandle(directoryHandle);
        m_Running = true;
        m_WatcherThread = std::thread(&AssetFileWatcher::WatchLoop, this);
        return true;
    }

    void AssetFileWatcher::Stop()
    {
        HANDLE directoryHandle = ToHandle(m_DirectoryHandle);
        m_Running = false;

        if (directoryHandle && directoryHandle != INVALID_HANDLE_VALUE)
            CancelIoEx(directoryHandle, nullptr);

        if (m_WatcherThread.joinable())
            m_WatcherThread.join();

        if (directoryHandle && directoryHandle != INVALID_HANDLE_VALUE)
            CloseHandle(directoryHandle);

        m_DirectoryHandle = nullptr;

        std::lock_guard<std::mutex> lock(m_Mutex);
        m_ChangedPaths.clear();
        m_HasPendingChanges = false;
    }

    bool AssetFileWatcher::ConsumeDebouncedChanges(
        std::vector<std::filesystem::path>& changedPaths,
        std::chrono::milliseconds debounceTime)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_HasPendingChanges)
            return false;

        auto now = std::chrono::steady_clock::now();
        if (now - m_LastChangeTime < debounceTime)
            return false;

        changedPaths = m_ChangedPaths;
        m_ChangedPaths.clear();
        m_HasPendingChanges = false;
        return true;
    }

    void AssetFileWatcher::WatchLoop()
    {
        HANDLE directoryHandle = ToHandle(m_DirectoryHandle);
        if (!directoryHandle || directoryHandle == INVALID_HANDLE_VALUE)
            return;

        HANDLE eventHandle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!eventHandle)
            return;

        std::vector<unsigned char> buffer(64 * 1024);

        while (m_Running)
        {
            ResetEvent(eventHandle);

            OVERLAPPED overlapped = {};
            overlapped.hEvent = eventHandle;

            const DWORD watchFlags =
                FILE_NOTIFY_CHANGE_FILE_NAME |
                FILE_NOTIFY_CHANGE_DIR_NAME |
                FILE_NOTIFY_CHANGE_LAST_WRITE |
                FILE_NOTIFY_CHANGE_SIZE |
                FILE_NOTIFY_CHANGE_CREATION;

            BOOL queued = ReadDirectoryChangesW(
                directoryHandle,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                TRUE,
                watchFlags,
                nullptr,
                &overlapped,
                nullptr);

            if (!queued)
            {
                if (!m_Running)
                    break;
                Sleep(50);
                continue;
            }

            while (m_Running)
            {
                DWORD waitResult = WaitForSingleObject(eventHandle, 100);
                if (waitResult == WAIT_OBJECT_0)
                    break;
                if (waitResult != WAIT_TIMEOUT)
                    break;
            }

            if (!m_Running)
            {
                CancelIoEx(directoryHandle, &overlapped);
                break;
            }

            DWORD bytesReturned = 0;
            if (!GetOverlappedResult(directoryHandle, &overlapped, &bytesReturned, FALSE) || bytesReturned == 0)
                continue;

            std::vector<std::filesystem::path> changedPaths;
            auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer.data());
            while (info)
            {
                std::wstring fileName(info->FileName, info->FileNameLength / sizeof(wchar_t));
                if (!fileName.empty())
                    changedPaths.push_back(m_RootDirectory / fileName);

                if (info->NextEntryOffset == 0)
                    break;

                info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                    reinterpret_cast<unsigned char*>(info) + info->NextEntryOffset);
            }

            RecordChangedPaths(changedPaths);
        }

        CloseHandle(eventHandle);
    }

    void AssetFileWatcher::RecordChangedPaths(const std::vector<std::filesystem::path>& changedPaths)
    {
        if (changedPaths.empty())
            return;

        std::lock_guard<std::mutex> lock(m_Mutex);

        // 파일 저장 하나도 생성, 쓰기, 이름 변경 같은 이벤트 여러 개로 들어온다.
        // 여기서는 바로 스캔하지 않고 묶어 두었다가 메인 스레드에서 한 번만 처리한다.
        m_HasPendingChanges = true;
        m_LastChangeTime = std::chrono::steady_clock::now();

        for (const auto& path : changedPaths)
        {
            if (m_ChangedPaths.size() >= 256)
                break;

            auto found = std::find(m_ChangedPaths.begin(), m_ChangedPaths.end(), path);
            if (found == m_ChangedPaths.end())
                m_ChangedPaths.push_back(path);
        }
    }
}
