#pragma once

#include "Core.h"
#include "Build/GameBuildProfile.h"

#include <cstdint>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace CCEngine
{
    enum class GameBuildMessageSeverity
    {
        Info,
        Warning,
        Error
    };

    struct GameBuildMessage
    {
        GameBuildMessageSeverity Severity = GameBuildMessageSeverity::Info;
        std::string Stage;
        std::string Message;
        std::filesystem::path Path;
    };

    struct GameBuildRequest
    {
        std::filesystem::path ProjectRoot;
        std::filesystem::path OutputDirectory;
        std::filesystem::path BinaryDirectory;
        std::string ProductName;
        std::string ProductVersion = "0.1.0";
        std::string UpdateChannel = "Stable";
        std::string ExecutableName = "Game";
        bool DevelopmentBuild = true;
        ManagedRuntimeDeployment ManagedRuntime = ManagedRuntimeDeployment::SelfContained;
        WindowMode PlayerWindowMode = WindowMode::Windowed;
        bool IncludeDebugSymbols = true;
        bool ExcludeLicensedSampleAssets = true;
        bool VerifyPlayerFrame = true;
        bool AllowDevelopmentShaderFallback = true;
        std::vector<std::filesystem::path> AlwaysIncludeAssets;
        std::shared_ptr<std::atomic_bool> CancelRequested;
        // 자동 QA가 실제 디스크를 채우지 않고 용량 부족 경로를 검증할 때만 사용한다.
        std::optional<uint64_t> AvailableDiskBytesOverride;
        std::function<void(const std::string&, float, const std::filesystem::path&)> Progress;
    };

    struct GameBuildReport
    {
        bool Success = false;
        uint32_t FilesWritten = 0;
        uint32_t FilesReused = 0;
        uint64_t BytesWritten = 0;
        uint64_t BytesReused = 0;
        std::filesystem::path OutputDirectory;
        std::vector<GameBuildMessage> Messages;
    };

    class GameBuildService
    {
    public:
        GameBuildReport Build(const GameBuildRequest& request) const;
    };

    // 명령행 도구는 STL 컨테이너가 든 Report의 소유권을 DLL 밖으로 넘기지 않는다.
    // 빌드와 출력, 메모리 해제를 엔진 모듈 안에서 끝내고 프로세스에는 종료 코드만 반환한다.
    CC_API int RunGameBuildCommand(
        const char* projectRoot,
        const char* outputDirectory,
        const char* binaryDirectory,
        bool developmentBuild,
        uint64_t availableDiskBytesOverride = 0);
    CC_API int RunGameBuildProfileCommand(
        const char* projectRoot,
        const char* profilePath,
        const char* binaryDirectory,
        const char* outputOverride,
        uint64_t availableDiskBytesOverride = 0);
}
