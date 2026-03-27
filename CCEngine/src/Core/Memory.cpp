#include "Memory.h"
#include <cstdlib>  // malloc, free
#include <iostream>

namespace CCEngine {

    // 내부적으로만 사용할 전역 통계 변수 (static으로 숨김)
    static MemoryStats s_Stats;

    void MemoryManager::Init() {
        std::cout << "[Memory] 메모리 매니저 초기화 완료." << std::endl;
    }

    void MemoryManager::Shutdown() {
        size_t leak = s_Stats.CurrentUsage();
        std::cout << "[Memory] 메모리 매니저 종료 중..." << std::endl;

        if (leak > 0) {
            std::cout << "[경고] 메모리 누수 발생! : " << leak << " bytes 가 반환되지 않았습니다!" << std::endl;
        }
        else {
            std::cout << "[Memory] 메모리 누수 없음. 정상 작동" << std::endl;
        }
    }

    MemoryStats MemoryManager::GetStats() {
        return s_Stats;
    }
}

// =========================================================
// 전역 new / delete 구현부
// =========================================================

void* operator new(size_t size) {
    // 1. 통계 기록
    CCEngine::s_Stats.TotalAllocated += size;

    // 2. 실제 메모리 할당
    return malloc(size);
}

void operator delete(void* memory, size_t size) noexcept {
    // 1. 통계 기록
    CCEngine::s_Stats.TotalFreed += size;

    // 2. 메모리 해제
    free(memory);
}

void operator delete(void* memory) noexcept {
    // 크기를 모르는 delete 호출 시 (Fallback)
    // 윈도우 환경에서는 크기를 정확히 빼기 어려우므로 일단 해제만 합니다.
    free(memory);
}