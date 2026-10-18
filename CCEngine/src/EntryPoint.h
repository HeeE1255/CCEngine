#pragma once

#ifdef CC_PLATFORM_WINDOWS
////// 메모리누수확인 ////
//#define _CRTDBG_MAP_ALLOC
//#include <stdlib.h>
//#include <crtdbg.h>
//#ifdef _DEBUG
//#define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
//#define new DBG_NEW 
//#endif
//////

#include <clocale>
#include <filesystem>
#include <iterator>
#include <vector>
#include <string>
#include <memory>
#include <stdexcept>
#include <iostream>
#include <Windows.h>
#include "Core/AssetDatabase.h"
#include "Core/ConsoleLog.h"
#include "Core/Memory.h"
#include "Renderer/ModelImporter.h"
#include "Renderer/Renderer.h"
#include "Renderer/RuntimeShaderLibrary.h"
#include "Renderer/ShaderCompiler.h"
#include "Scripting/ScriptEngine.h"
#ifndef CC_GAME_PLAYER
#include "UI/AssetBrowserPanel.h"
#include "UI/InspectorPanel.h"
#include "UI/InspectorRegistry.h"
#endif

// 엔트리 포인트 헤더: 플랫폼별로 메인 함수를 정의하는 헤더
// 설명 : 엔진이 실행될 때 가장 먼저 호출되는 함수인 main 함수를 정의하는 헤더
// 샌드박스에서 이 이름으로 사용자가 코드를 작성하면, 엔진이 이 함수를 호출하여 게임을 시작
extern CCEngine::Application* CCEngine::CreateApplication(const std::vector<std::string>& commandLineArgs);

namespace
{
    std::filesystem::path GetExecutableDirectory()
    {
        wchar_t buffer[32768] = {};
        DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
        if (length == 0 || length >= std::size(buffer))
            return {};
        return std::filesystem::path(buffer).parent_path();
    }

    void SetupProjectWorkingDirectory(int argc, char** argv)
    {
        std::error_code ec;
        auto current = std::filesystem::current_path(ec);
        if (ec)
            return;

        // 배포 Player는 실행 파일 옆 Manifest가 프로젝트 루트다. 에디터용 Sandbox 탐색보다
        // 먼저 판정해야 다른 PC에서 우연히 존재하는 개발 폴더로 이동하지 않는다.
        std::filesystem::path executableDirectory = GetExecutableDirectory();
        if (!executableDirectory.empty() && std::filesystem::exists(executableDirectory / "GameProject.json", ec) && !ec)
        {
            std::filesystem::current_path(executableDirectory, ec);
            return;
        }

        for (int i = 1; i < argc; ++i)
        {
            std::string argument = argv[i] ? argv[i] : "";
            constexpr const char* prefix = "--manifest=";
            if (argument.rfind(prefix, 0) != 0)
                continue;

            std::filesystem::path manifestPath = argument.substr(std::char_traits<char>::length(prefix));
            if (manifestPath.is_relative())
                manifestPath = current / manifestPath;
            if (std::filesystem::exists(manifestPath, ec) && !ec)
            {
                // 명시적 Manifest 경로는 CreateApplication이 현재 작업 폴더 기준으로 절대화한다.
                // 여기서 먼저 작업 폴더를 옮기면 상대 경로가 두 번 결합되므로 위치만 확인하고 유지한다.
                return;
            }
        }

        // assets 폴더만으로 프로젝트를 판정하면 솔루션 루트의 임시 폴더를 잘못 선택할 수 있다.
        if (std::filesystem::exists(current / "project.ccproject", ec) && !ec &&
            std::filesystem::exists(current / "assets", ec) && !ec)
            return;

        // 에디터 리소스는 Sandbox/assets를 기준으로 둔다.
        // 실행 위치가 솔루션 루트나 bin 폴더여도 같은 프로젝트 에셋을 보게 맞춘다.
        std::filesystem::path candidates[] =
        {
            current / "Sandbox",
            current.parent_path() / "Sandbox",
            current.parent_path().parent_path() / "Sandbox"
        };

        for (const auto& candidate : candidates)
        {
            ec.clear();
            if (std::filesystem::exists(candidate / "project.ccproject", ec) && !ec &&
                std::filesystem::exists(candidate / "assets", ec) && !ec)
            {
                std::filesystem::current_path(candidate, ec);
                return;
            }
        }
    }
}

int main(int argc, char** argv) 
{
    // DPI 인식 설정 
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    //// 메모리누수확인 ////
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    ////

    //C++ 표준 로케일을 '영어(미국)' 기반의 'UTF-8'로 설정/////
    setlocale(LC_ALL, "en_US.UTF-8");
    //윈도우 콘솔창의 입출력 코드 페이지를 강제로 UTF-8(65001)로 변경
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    ////////////////////////////////////////////////////////

    SetupProjectWorkingDirectory(argc, argv);

    HWND consoleWindow = GetConsoleWindow();
    if (consoleWindow != NULL)
    {
        //ShowWindow(consoleWindow, SW_HIDE); // 숨겨라!
    }

    // 엔진 애플리케이션 생성 및 실행
    int exitCode = 0;
    {
        std::vector<std::string> commandLineArgs;
        commandLineArgs.reserve(argc > 1 ? (size_t)argc - 1 : 0);
        for (int i = 1; i < argc; ++i)
            commandLineArgs.emplace_back(argv[i]);

        try
        {
            std::unique_ptr<CCEngine::Application> app(CCEngine::CreateApplication(commandLineArgs));
            if (!app)
                throw std::runtime_error("Application factory returned null.");
            app->Run();
            exitCode = app->GetExitCode();
        }
        catch (const std::exception& error)
        {
            // 무인 빌드도 같은 진입점을 쓰므로 모달 창으로 정지시키지 않는다.
            std::cerr << "CCEngine failed: " << error.what() << std::endl;
            CCEngine::ConsoleLog::Error(error.what());
            exitCode = 1;
        }
    }

    // Application이 완전히 삭제된 뒤 세션 캐시를 먼저 비운다.
    // 렌더 리소스를 물고 있는 캐시는 렌더러가 살아 있을 때 놓아야 종료 순서가 안전하다.
#ifndef CC_GAME_PLAYER
    CCEngine::UI::AssetBrowserPanel::ShutdownSharedCaches();
#endif
    CCEngine::ModelImporter::ClearCache();
    CCEngine::RuntimeShaderLibrary::Clear();
    CCEngine::ScriptEngine::Shutdown();

    // 레이어와 UI가 모두 사라지고 렌더 캐시도 해제된 뒤 렌더러를 종료한다.
    // UI가 렌더 리소스를 참조하는 동안 렌더러부터 끄면 종료 순서가 꼬여 누수와 해제 오류를 구분하기 어려워진다.
    CCEngine::Renderer::Shutdown();

    // 순수 CPU 세션 캐시는 렌더러 종료 뒤에 비워도 된다.
    CCEngine::ShaderCompiler::ClearCache();
    CCEngine::AssetDatabase::Shutdown();
    CCEngine::ConsoleLog::Clear();
#ifndef CC_GAME_PLAYER
    CCEngine::UI::InspectorPanel::ShutdownSharedCaches();
    CCEngine::UI::InspectorRegistry::Clear();
#endif

    // destructor 본문 안에서 검사하면 Window, LayerStack 같은 멤버가 아직 살아 있어 정상 해제 예정 메모리도 누수처럼 보인다.
    CCEngine::MemoryManager::Shutdown();

    // 메모리 누수 확인
    //_CrtDumpMemoryLeaks();

    return exitCode;
}

#endif
