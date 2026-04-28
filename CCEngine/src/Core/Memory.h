#pragma once
#include "Core.h"
#include <cstddef> // size_t 사용을 위해 포함
#include <new>

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
// 네임스페이스 밖에서 전역 new / delete 연산자를 오버로딩
// CCEngine 내에서 발생하는 모든 new/delete는 아래 구현된 함수로 처리
// =========================================================

// MSVC 정적 분석기의 SAL 주석 불일치 경고(C28251)를 무시
#pragma warning(push)
#pragma warning(disable: 28251)

void* operator new(size_t size);
void operator delete(void* memory, size_t size) noexcept;
void operator delete(void* memory) noexcept;

// Array forms
void* operator new[](size_t size);
void operator delete[](void* memory, size_t size) noexcept;
void operator delete[](void* memory) noexcept;

#pragma warning(pop)
