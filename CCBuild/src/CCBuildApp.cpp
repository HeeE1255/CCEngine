#include <Build/GameBuildService.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <array>

namespace
{
    std::string FindValue(int argc, char** argv, const std::string& name)
    {
        const std::string prefix = "--" + name + "=";
        for (int index = 1; index < argc; ++index)
        {
            const std::string argument = argv[index] ? argv[index] : "";
            if (argument.rfind(prefix, 0) == 0)
                return argument.substr(prefix.size());
        }
        return {};
    }

    bool ValidateBuilderDependencies(const std::filesystem::path& binaryDirectory)
    {
        // CCEngine.dll은 지연 로드된다. 엔진 함수를 호출하기 전에 파일을 직접 검사해야
        // Windows 로더 코드 대신 사용자가 고칠 수 있는 파일명을 출력할 수 있다.
        constexpr std::array<const char*, 2> requiredFiles = {
            "CCEngine.dll",
            "assimp-vc145-mt.dll"
        };
        bool valid = true;
        for (const char* filename : requiredFiles)
        {
            const auto path = binaryDirectory / filename;
            std::error_code ec;
            if (!std::filesystem::is_regular_file(path, ec) || ec)
            {
                std::cerr << "[CCBuild] Required runtime DLL is missing: " << path.string() << '\n';
                valid = false;
            }
        }
        return valid;
    }

    uint64_t FindUnsignedValue(int argc, char** argv, const std::string& name)
    {
        const std::string value = FindValue(argc, argv, name);
        if (value.empty())
            return 0;
        try
        {
            return std::stoull(value);
        }
        catch (...)
        {
            std::cerr << "[CCBuild] Invalid unsigned value for --" << name << ": " << value << '\n';
            return 0;
        }
    }

}

int main(int argc, char** argv)
{
    const std::filesystem::path binaryDirectory = FindValue(argc, argv, "binaries");
    const uint64_t availableDiskBytesOverride = FindUnsignedValue(argc, argv, "test-available-bytes");
    if (!binaryDirectory.empty() && !ValidateBuilderDependencies(binaryDirectory))
        return 3;

    CCEngine::GameBuildRequest request;
    request.ProjectRoot = FindValue(argc, argv, "project");
    request.OutputDirectory = FindValue(argc, argv, "output");
    request.BinaryDirectory = binaryDirectory;
    request.DevelopmentBuild = FindValue(argc, argv, "configuration") != "Release";
    const std::string profile = FindValue(argc, argv, "profile");

    if (request.ProjectRoot.empty() || request.BinaryDirectory.empty() || (request.OutputDirectory.empty() && profile.empty()))
    {
        std::cerr << "Usage: CCBuild --project=<project root> --binaries=<engine binary directory> (--profile=<profile> | --output=<build directory>) [--configuration=Development] [--test-available-bytes=<bytes>]\n";
        return 2;
    }

    if (!profile.empty())
    {
        return CCEngine::RunGameBuildProfileCommand(
            request.ProjectRoot.string().c_str(),
            profile.c_str(),
            request.BinaryDirectory.string().c_str(),
            request.OutputDirectory.string().c_str(),
            availableDiskBytesOverride);
    }

    return CCEngine::RunGameBuildCommand(
        request.ProjectRoot.string().c_str(),
        request.OutputDirectory.string().c_str(),
        request.BinaryDirectory.string().c_str(),
        request.DevelopmentBuild,
        availableDiskBytesOverride);
}
