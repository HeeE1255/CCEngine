#include <Application.h>
#include <Core/ConsoleLog.h>
#include <EntryPoint.h>
#include <Renderer/Renderer.h>
#include <Renderer/ShaderCompiler.h>
#include <Runtime/GameLayer.h>
#include <Runtime/RuntimeManifest.h>
#include <Renderer/GraphicsContext.h>
#include <json.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>

#ifdef CC_PLATFORM_WINDOWS
#include <Windows.h>
#endif

namespace
{
    std::filesystem::path FindManifestPath(const std::vector<std::string>& arguments)
    {
        constexpr const char* prefix = "--manifest=";
        for (const std::string& argument : arguments)
        {
            if (argument.rfind(prefix, 0) == 0)
                return argument.substr(std::char_traits<char>::length(prefix));
        }
        return "GameProject.json";
    }

    std::filesystem::path FindSmokeReportPath(const std::vector<std::string>& arguments)
    {
        constexpr const char* prefix = "--smoke-report=";
        for (const std::string& argument : arguments)
        {
            if (argument.rfind(prefix, 0) == 0)
                return argument.substr(std::char_traits<char>::length(prefix));
        }
        return {};
    }

    class GameApplication final : public CCEngine::Application
    {
    public:
        GameApplication(
            const std::vector<std::string>& arguments,
            const CCEngine::RuntimeManifestData& manifest,
            const std::filesystem::path& runtimeRoot,
            std::string startupError)
            : Application(arguments, CCEngine::WindowProps(
                manifest.ProjectName,
                manifest.GameWidth,
                manifest.GameHeight,
                manifest.PlayerWindowMode))
        {
            m_SmokeReportPath = FindSmokeReportPath(arguments);
            CCEngine::ShaderCompiler::SetRuntimeBytecodeOnly(!manifest.DevelopmentBuild);
            if (startupError.empty())
            {
                std::filesystem::path contentRoot;
                std::filesystem::path shaderManifest;
                if (!CCEngine::RuntimeManifest::ResolvePathInsideRoot(runtimeRoot, manifest.ContentRoot, contentRoot) ||
                    !CCEngine::RuntimeManifest::ResolvePathInsideRoot(contentRoot, manifest.ShaderManifest, shaderManifest))
                {
                    startupError = "ShaderManifest points outside ContentRoot.";
                }
                else
                    CCEngine::ShaderCompiler::LoadRuntimeBytecodeManifest(shaderManifest, startupError);
            }


            CCEngine::Renderer::Init();

            if (!startupError.empty())
            {
#ifdef CC_PLATFORM_WINDOWS
                std::string dialog = startupError;
                if (!CCEngine::ConsoleLog::GetFilePath().empty())
                    dialog += "\n\nLog: " + CCEngine::ConsoleLog::GetFilePath().string();
                MessageBoxA(nullptr, dialog.c_str(), "CCEngine Game Player", MB_OK | MB_ICONERROR);
#endif
                SetExitCode(1);
                GetWindow().SetShouldClose(true);
                return;
            }

            m_GameLayer = new CCEngine::GameLayer(manifest, runtimeRoot);
            PushLayer(m_GameLayer);
        }

        void OnMainFrameReady() override
        {
            if (m_SmokeReportPath.empty() || m_SmokeCompleted || !m_GameLayer || !m_GameLayer->IsLoaded())
                return;
            if (++m_RenderedFrames < 3)
                return;

            std::vector<uint8_t> pixels;
            uint32_t width = 0;
            uint32_t height = 0;
            bool readback = false;
            if (auto* context = GetWindow().GetContext())
                readback = context->ReadBackBuffer(pixels, width, height);

            uint8_t minimum = 255;
            uint8_t maximum = 0;
            std::array<bool, 256> luminanceBuckets{};
            size_t sampledPixels = 0;
            if (readback)
            {
                const size_t pixelCount = pixels.size() / 4u;
                const size_t step = (std::max)(size_t(1), pixelCount / 65536u);
                for (size_t pixel = 0; pixel < pixelCount; pixel += step)
                {
                    const size_t offset = pixel * 4u;
                    const uint8_t luminance = static_cast<uint8_t>((
                        static_cast<uint32_t>(pixels[offset + 0]) * 29u +
                        static_cast<uint32_t>(pixels[offset + 1]) * 150u +
                        static_cast<uint32_t>(pixels[offset + 2]) * 77u) >> 8u);
                    minimum = (std::min)(minimum, luminance);
                    maximum = (std::max)(maximum, luminance);
                    luminanceBuckets[luminance] = true;
                    ++sampledPixels;
                }
            }

            size_t distinctLuminance = 0;
            for (bool used : luminanceBuckets)
                distinctLuminance += used ? 1u : 0u;

            constexpr uint32_t GridColumns = 16;
            constexpr uint32_t GridRows = 9;
            std::array<uint64_t, GridColumns * GridRows> gridSums{};
            std::array<uint32_t, GridColumns * GridRows> gridCounts{};
            if (readback)
            {
                for (uint32_t y = 0; y < height; y += 4)
                {
                    for (uint32_t x = 0; x < width; x += 4)
                    {
                        const size_t offset = (static_cast<size_t>(y) * width + x) * 4u;
                        const uint8_t luminance = static_cast<uint8_t>((
                            static_cast<uint32_t>(pixels[offset + 0]) * 29u +
                            static_cast<uint32_t>(pixels[offset + 1]) * 150u +
                            static_cast<uint32_t>(pixels[offset + 2]) * 77u) >> 8u);
                        const uint32_t column = (std::min)(GridColumns - 1u, x * GridColumns / width);
                        const uint32_t row = (std::min)(GridRows - 1u, y * GridRows / height);
                        const size_t cell = static_cast<size_t>(row) * GridColumns + column;
                        gridSums[cell] += luminance;
                        ++gridCounts[cell];
                    }
                }
            }

            // 단색 Clear 화면도 정상 Present로 보일 수 있다. 최소 색 범위와 명암 단계 수를 함께
            // 확인해야 시작 씬이 실제로 그려졌는지 자동 빌드에서 구분할 수 있다.
            const bool validFrame = readback && width > 0 && height > 0 &&
                sampledPixels > 0 && maximum > minimum + 4u && distinctLuminance >= 4u;

            nlohmann::json report;
            report["Version"] = 1;
            report["Success"] = validFrame;
            report["SceneLoaded"] = m_GameLayer->IsLoaded();
            report["Readback"] = readback;
            report["Width"] = width;
            report["Height"] = height;
            report["MinimumLuminance"] = minimum;
            report["MaximumLuminance"] = maximum;
            report["DistinctLuminance"] = distinctLuminance;
            report["RenderedFrames"] = m_RenderedFrames;
            report["LuminanceGridColumns"] = GridColumns;
            report["LuminanceGridRows"] = GridRows;
            report["LuminanceGrid"] = nlohmann::json::array();
            for (size_t cell = 0; cell < gridSums.size(); ++cell)
                report["LuminanceGrid"].push_back(gridCounts[cell] == 0 ? 0 : gridSums[cell] / gridCounts[cell]);

            std::error_code ec;
            if (!m_SmokeReportPath.parent_path().empty())
                std::filesystem::create_directories(m_SmokeReportPath.parent_path(), ec);
            std::ofstream output(m_SmokeReportPath, std::ios::trunc);
            if (output.is_open())
                output << report.dump(2);

            m_SmokeCompleted = true;
            SetExitCode(validFrame ? 0 : 3);
            GetWindow().SetShouldClose(true);
        }

    private:
        CCEngine::GameLayer* m_GameLayer = nullptr;
        std::filesystem::path m_SmokeReportPath;
        uint32_t m_RenderedFrames = 0;
        bool m_SmokeCompleted = false;
    };
}

CCEngine::Application* CCEngine::CreateApplication(const std::vector<std::string>& commandLineArgs)
{
    const std::filesystem::path manifestPath = std::filesystem::absolute(FindManifestPath(commandLineArgs));
    RuntimeManifestData manifest;
    std::string error;
    RuntimeManifest::Load(manifestPath, manifest, error);

    std::string logFolder = manifest.ProjectName;
    std::replace_if(logFolder.begin(), logFolder.end(), [](unsigned char character)
    {
        return character < 32 || std::string("<>:\"/\\|?*").find(static_cast<char>(character)) != std::string::npos;
    }, '_');
    std::filesystem::path logRoot;
    char* localAppData = nullptr;
    size_t localAppDataLength = 0;
    if (_dupenv_s(&localAppData, &localAppDataLength, "LOCALAPPDATA") == 0 && localAppData && *localAppData)
        logRoot = localAppData;
    if (localAppData)
        free(localAppData);
    if (logRoot.empty())
        logRoot = manifestPath.parent_path();
    const ConsoleLogLevel fileLevel = manifest.DevelopmentBuild ? ConsoleLogLevel::Info : ConsoleLogLevel::Warning;
    if (!ConsoleLog::ConfigureFile(logRoot / "CCEngine" / "Logs" / logFolder / "Player.log", fileLevel))
    {
        // 스토어/샌드박스 실행에서는 LOCALAPPDATA가 읽기 전용일 수 있다. 게임 폴더에 로그를
        // 만들면 BuildManifest가 오염되므로, 쓰기 가능한 OS 임시 폴더를 두 번째 위치로 쓴다.
        std::error_code tempError;
        const auto temporaryRoot = std::filesystem::temp_directory_path(tempError);
        if (!tempError)
            ConsoleLog::ConfigureFile(temporaryRoot / "CCEngine" / "Logs" / logFolder / "Player.log", fileLevel);
    }
    if (!error.empty())
        ConsoleLog::Error(error);

    return new GameApplication(commandLineArgs, manifest, manifestPath.parent_path(), error);
}
