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
#include <Windows.h>

// 엔트리 포인트 헤더: 플랫폼별로 메인 함수를 정의하는 헤더
// 설명 : 엔진이 실행될 때 가장 먼저 호출되는 함수인 main 함수를 정의하는 헤더
// 샌드박스에서 이 이름으로 사용자가 코드를 작성하면, 엔진이 이 함수를 호출하여 게임을 시작
extern CCEngine::Application* CCEngine::CreateApplication();

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

    HWND consoleWindow = GetConsoleWindow();
    if (consoleWindow != NULL)
    {
        //ShowWindow(consoleWindow, SW_HIDE); // 숨겨라!
    }

    // 엔진 애플리케이션 생성 및 실행
    {   
        auto app = CCEngine::CreateApplication();
        app->Run();

        delete app;
    }

    // 메모리 누수 확인
    //_CrtDumpMemoryLeaks();

    return 0;
}

#endif