#pragma once
#include "Core.h"
#include <cstddef> // size_t 사용을 위해 포함

namespace CCEngine {

    // 메모리 사용량을 추적할 통계 구조체
    struct MemoryStats {
        size_t TotalAllocated = 0; // 할당된 총 바이트
        size_t TotalFreed = 0;     // 해제된 총 바이트

        // 현재 사용 중인 순수 메모리 량
        size_t CurrentUsage() const { return TotalAllocated - TotalFreed; }
    };

    class CC_API MemoryManager {
    public:
        static void Init();
        static void Shutdown();

        // 현재 메모리 상태를 가져오는 함수
        static MemoryStats GetStats();
    };
}
