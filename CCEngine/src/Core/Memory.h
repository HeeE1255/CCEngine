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

// =========================================================
// [중요] 네임스페이스 밖에서 전역 new / delete 연산자를 오버로딩
// CCEngine 내에서 발생하는 모든 new/delete는 아래 구현된 함수로 처리
// =========================================================
void* operator new(size_t size);
void operator delete(void* memory, size_t size) noexcept;
void operator delete(void* memory) noexcept;
// Array forms
void* operator new[](size_t size);
void operator delete[](void* memory, size_t size) noexcept;
void operator delete[](void* memory) noexcept;
