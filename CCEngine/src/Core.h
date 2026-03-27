#pragma once

// 윈도우 플랫폼인 경우
#ifdef CC_PLATFORM_WINDOWS
    // 엔진(DLL)을 빌드하는 중이라면 (Export)
    #ifdef CC_BUILD_DLL
    #define CC_API __declspec(dllexport)
    // 게임(EXE)에서 사용하는 중이라면 (Import)
    #else
    #define CC_API __declspec(dllimport)
    #endif
#else
    #error CCEngine only supports Windows!
#endif

// ==========================================
// C++ STL 클래스 DLL Export 경고(C4251) 끄기
// ==========================================
#ifdef CC_PLATFORM_WINDOWS
#pragma warning(disable: 4251)
#endif