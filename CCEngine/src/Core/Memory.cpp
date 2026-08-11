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

namespace
{
    struct alignas(std::max_align_t) AllocationHeader
    {
        size_t Size = 0;
    };

    void* AllocateTracked(size_t size)
    {
        size_t totalSize = sizeof(AllocationHeader) + size;
        auto* raw = static_cast<AllocationHeader*>(std::malloc(totalSize));
        if (!raw)
            throw std::bad_alloc();

        raw->Size = size;
        CCEngine::s_Stats.TotalAllocated += size;
        return raw + 1;
    }

    void FreeTracked(void* memory) noexcept
    {
        if (!memory)
            return;

        auto* header = static_cast<AllocationHeader*>(memory) - 1;
        CCEngine::s_Stats.TotalFreed += header->Size;
        std::free(header);
    }
}

// =========================================================
// 전역 new / delete 구현부
// =========================================================

void* operator new(size_t size) {
    return AllocateTracked(size);
}

void operator delete(void* memory, size_t size) noexcept {
    (void)size;
    FreeTracked(memory);
}

void operator delete(void* memory) noexcept {
    // 크기를 모르는 delete도 header에 저장된 크기로 해제량을 계산한다.
    // 이 처리가 없으면 실제로 free된 메모리도 통계상 누수처럼 남는다.
    FreeTracked(memory);
}

void* operator new[](size_t size) {
    return AllocateTracked(size);
}

void operator delete[](void* memory, size_t size) noexcept {
    (void)size;
    FreeTracked(memory);
}

void operator delete[](void* memory) noexcept {
    FreeTracked(memory);
}
