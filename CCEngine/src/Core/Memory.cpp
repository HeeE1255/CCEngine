#include "Memory.h"

#include <iostream>

namespace CCEngine
{
    namespace
    {
        MemoryStats s_Stats;
        bool s_Initialized = false;
    }

    void MemoryManager::Init()
    {
        s_Stats = {};
        s_Initialized = true;
        std::cout << "[Memory] 메모리 진단 초기화 완료." << std::endl;
    }

    void MemoryManager::Shutdown()
    {
        if (!s_Initialized)
            return;

        // DLL과 EXE는 서로 객체 소유권을 넘긴다. 한 모듈에서만 전역 new/delete를 바꾸면
        // 다른 모듈의 포인터 앞을 자체 헤더로 오해해 Debug CRT 힙이 손상된다.
        // 전역 할당자는 표준 CRT 계약을 유지하고, Debug 누수 검사는 EntryPoint의 CRT 기능을 쓴다.
        std::cout << "[Memory] 메모리 진단 종료." << std::endl;
        s_Initialized = false;
    }

    MemoryStats MemoryManager::GetStats()
    {
        return s_Stats;
    }
}
