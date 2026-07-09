#include "Scripting/ScriptCompiler.h"
#include "Core/ConsoleLog.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <future>
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
    }

    void ScriptCompiler::RequestCompile()
    {
        if (s_Compiling)
        {
            // 저장이 연속으로 들어오면 현재 작업 뒤에 한 번만 더 빌드해 불필요한 프로세스 생성을 막는다.
            s_CompilePending = true;
            return;
        }
        StartCompile();
    }

    void ScriptCompiler::Update()
    {
        if (!s_Compiling || !s_CompileTask.valid())
            return;

        if (s_CompileTask.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;

        CompileResult result = s_CompileTask.get();
        s_Compiling = false;

        if (result.ExitCode == 0)
            ConsoleLog::Info(result.Output.empty() ? "C# scripts compiled successfully." : result.Output);
        else
            ConsoleLog::Error(result.Output.empty() ? "C# script compilation failed." : result.Output);

        if (s_CompilePending)
        {
            s_CompilePending = false;
            StartCompile();
        }
    }

    bool ScriptCompiler::IsCompiling()
    {
        return s_Compiling;
    }
}
