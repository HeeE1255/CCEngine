#include "Scripting/ScriptCompiler.h"
#include "Core/ConsoleLog.h"
#include "Scripting/ScriptMetadata.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <future>
#include <functional>
#include <mutex>
#include <string>

namespace CCEngine
{
    namespace
    {
        struct CompileResult
        {
            int ExitCode = -1;
            std::string Output;
        };

        std::future<CompileResult> s_CompileTask;
        bool s_Compiling = false;
        bool s_CompilePending = false;

        struct SourceSnapshot
        {
            uint64_t Fingerprint = 1469598103934665603ull;
            bool HasSources = false;
            std::filesystem::file_time_type LatestWriteTime{};
        };

        bool s_SourceSnapshotInitialized = false;
        uint64_t s_SourceFingerprint = 0;
        bool s_AutoCompileArmed = false;
        std::chrono::steady_clock::time_point s_LastPollTime{};
        std::chrono::steady_clock::time_point s_LastSourceChangeTime{};

        constexpr std::chrono::milliseconds SourcePollInterval(400);
        constexpr std::chrono::milliseconds SourceCompileDebounce(700);

        void HashCombine(uint64_t& seed, uint64_t value)
        {
            seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
        }

        void AddSourceFileToSnapshot(SourceSnapshot& snapshot, const std::filesystem::path& file)
        {
            std::error_code ec;
            auto writeTime = std::filesystem::last_write_time(file, ec);
            if (ec)
                return;

            snapshot.HasSources = true;
            if (writeTime > snapshot.LatestWriteTime)
                snapshot.LatestWriteTime = writeTime;

            HashCombine(snapshot.Fingerprint, static_cast<uint64_t>(std::hash<std::string>{}(file.generic_string())));
            HashCombine(snapshot.Fingerprint, static_cast<uint64_t>(writeTime.time_since_epoch().count()));
        }

        SourceSnapshot BuildSourceSnapshot()
        {
            SourceSnapshot snapshot;
            const auto scriptsRoot = std::filesystem::current_path() / "assets" / "Scripts";
            const std::array<std::filesystem::path, 2> sourceRoots =
            {
                scriptsRoot / "Game",
                scriptsRoot / "ScriptCore"
            };

            for (const auto& root : sourceRoots)
            {
                std::error_code ec;
                if (!std::filesystem::exists(root, ec))
                    continue;

                for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec))
                {
                    if (it->is_regular_file(ec) && it->path().extension() == ".cs")
                        AddSourceFileToSnapshot(snapshot, it->path());
                }
            }

            return snapshot;
        }

        bool IsManifestOlderThanSources(const SourceSnapshot& snapshot)
        {
            if (!snapshot.HasSources)
                return false;

            const auto manifest = std::filesystem::current_path() / "assets" / "Scripts" / "Build" / "GameScripts.manifest.json";
            std::error_code ec;
            if (!std::filesystem::exists(manifest, ec))
                return true;

            auto manifestWriteTime = std::filesystem::last_write_time(manifest, ec);
            return !ec && manifestWriteTime < snapshot.LatestWriteTime;
        }

        void ArmAutoCompile(const SourceSnapshot& snapshot)
        {
            s_SourceFingerprint = snapshot.Fingerprint;
            s_AutoCompileArmed = true;
            s_LastSourceChangeTime = std::chrono::steady_clock::now();
        }

        CompileResult CompileScripts()
        {
            const auto buildScript = std::filesystem::current_path() / "assets" / "Scripts" / "BuildScripts.ps1";
            if (!std::filesystem::exists(buildScript))
                return { -1, "BuildScripts.ps1 was not found: " + buildScript.string() };

            std::string command =
                "powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"" +
                buildScript.string() + "\" 2>&1";

            CompileResult result;
            std::array<char, 512> buffer{};
            FILE* pipe = _popen(command.c_str(), "r");
            if (!pipe)
            {
                result.Output = "Failed to start the C# compiler process.";
                return result;
            }

            while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe))
                result.Output += buffer.data();
            result.ExitCode = _pclose(pipe);
            return result;
        }

        void StartCompile()
        {
            s_Compiling = true;
            ConsoleLog::Info("Compiling C# scripts...");
            // 컴파일은 파일 접근과 외부 프로세스 대기가 대부분이므로 렌더 스레드 밖에서 실행한다.
            s_CompileTask = std::async(std::launch::async, CompileScripts);
        }

        void RequestCompileInternal()
        {
            if (s_Compiling)
            {
                // 저장이 연속으로 들어오면 현재 작업 뒤에 한 번만 더 빌드해 불필요한 프로세스 생성을 막는다.
                s_CompilePending = true;
                return;
            }
            StartCompile();
        }

        void PollSourceChanges()
        {
            auto now = std::chrono::steady_clock::now();
            if (s_LastPollTime.time_since_epoch().count() != 0 && now - s_LastPollTime < SourcePollInterval)
                return;
            s_LastPollTime = now;

            if (s_Compiling)
                return;

            SourceSnapshot snapshot = BuildSourceSnapshot();
            if (!snapshot.HasSources)
                return;

            if (!s_SourceSnapshotInitialized)
            {
                s_SourceSnapshotInitialized = true;
                s_SourceFingerprint = snapshot.Fingerprint;
                // 에디터를 켰을 때 manifest가 소스보다 오래됐으면 한 번 자동으로 맞춰준다.
                if (IsManifestOlderThanSources(snapshot))
                    ArmAutoCompile(snapshot);
                return;
            }

            if (snapshot.Fingerprint != s_SourceFingerprint)
            {
                // 저장 중인 파일을 바로 빌드하면 중간 상태를 잡을 수 있어 잠깐 기다린 뒤 컴파일한다.
                ArmAutoCompile(snapshot);
                return;
            }

            if (s_AutoCompileArmed && now - s_LastSourceChangeTime >= SourceCompileDebounce)
            {
                s_AutoCompileArmed = false;
                ConsoleLog::Info("C# script changes detected. Recompiling...");
                RequestCompileInternal();
            }
        }
    }

    void ScriptCompiler::RequestCompile()
    {
        RequestCompileInternal();
    }

    bool ScriptCompiler::Update()
    {
        PollSourceChanges();

        if (!s_Compiling || !s_CompileTask.valid())
            return false;

        if (s_CompileTask.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return false;

        CompileResult result = s_CompileTask.get();
        s_Compiling = false;
        bool metadataChanged = false;

        if (result.ExitCode == 0)
        {
            ScriptMetadata::Refresh();
            metadataChanged = true;
            ConsoleLog::Info(result.Output.empty() ? "C# scripts compiled successfully." : result.Output);
        }
        else
            ConsoleLog::Error(result.Output.empty() ? "C# script compilation failed." : result.Output);

        if (s_CompilePending)
        {
            s_CompilePending = false;
            StartCompile();
        }

        return metadataChanged;
    }

    bool ScriptCompiler::IsCompiling()
    {
        return s_Compiling;
    }
}
