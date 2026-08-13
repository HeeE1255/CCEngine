#include "Memory.h"
#include <atomic>
#include <cstdlib>  // malloc, free
#include <iostream>

namespace
{
    void DumpOutstandingAllocationSummary();
    void RecordAllocationSize(size_t size);
    void RecordFreeSize(size_t size);
}

namespace CCEngine {

    // 내부적으로만 사용할 전역 통계 변수 (static으로 숨김)
    static MemoryStats s_Stats;
    static bool s_TrackingEnabled = false;

    void MemoryManager::Init() {
        // Init 이후의 할당만 엔진 실행 중 메모리로 본다.
        // 전역 객체나 CRT가 main 전에 잡은 메모리는 종료 순서가 달라서 엔진 누수 판단에 섞이면 안 된다.
        s_Stats = {};
        s_TrackingEnabled = true;
        std::cout << "[Memory] 메모리 매니저 초기화 완료." << std::endl;
    }

    void MemoryManager::Shutdown() {
        size_t leak = s_Stats.CurrentUsage();
        std::cout << "[Memory] 메모리 매니저 종료 중..." << std::endl;

        if (leak > 0) {
            std::cout << "[경고] 메모리 누수 발생! : " << leak << " bytes 가 반환되지 않았습니다!" << std::endl;
            DumpOutstandingAllocationSummary();
        }
        else {
            std::cout << "[Memory] 메모리 누수 없음. 정상 작동" << std::endl;
        }

        s_TrackingEnabled = false;
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
        bool Tracked = false;
    };

    struct SizeBucket
    {
        size_t Size = 0;
        long long Count = 0;
    };

    constexpr int MaxSizeBuckets = 64;
    SizeBucket s_SizeBuckets[MaxSizeBuckets];
    std::atomic_flag s_SizeBucketLock = ATOMIC_FLAG_INIT;

    void LockBuckets()
    {
        while (s_SizeBucketLock.test_and_set(std::memory_order_acquire)) {}
    }

    void UnlockBuckets()
    {
        s_SizeBucketLock.clear(std::memory_order_release);
    }

    void RecordAllocationSize(size_t size)
    {
        // 전역 new/delete 내부에서는 std::unordered_map 같은 동적 컨테이너를 쓰면 안 된다.
        // 컨테이너가 다시 new를 호출해 재진입 문제가 생기므로, 고정 배열 버킷에 크기별 개수만 남긴다.
        LockBuckets();
        int emptyIndex = -1;
        for (int i = 0; i < MaxSizeBuckets; ++i)
        {
            if (s_SizeBuckets[i].Count > 0 && s_SizeBuckets[i].Size == size)
            {
                ++s_SizeBuckets[i].Count;
                UnlockBuckets();
                return;
            }
            if (emptyIndex < 0 && s_SizeBuckets[i].Count == 0)
                emptyIndex = i;
        }

        if (emptyIndex >= 0)
        {
            s_SizeBuckets[emptyIndex].Size = size;
            s_SizeBuckets[emptyIndex].Count = 1;
        }
        UnlockBuckets();
    }

    void RecordFreeSize(size_t size)
    {
        LockBuckets();
        for (int i = 0; i < MaxSizeBuckets; ++i)
        {
            if (s_SizeBuckets[i].Count > 0 && s_SizeBuckets[i].Size == size)
            {
                --s_SizeBuckets[i].Count;
                if (s_SizeBuckets[i].Count == 0)
                    s_SizeBuckets[i].Size = 0;
                break;
            }
        }
        UnlockBuckets();
    }

    void DumpOutstandingAllocationSummary()
    {
        size_t allocationCount = 0;

        LockBuckets();
        for (const SizeBucket& bucket : s_SizeBuckets)
        {
            if (bucket.Count > 0)
                allocationCount += static_cast<size_t>(bucket.Count);
        }

        std::cout << "[Memory] Outstanding allocation count: " << allocationCount << std::endl;
        std::cout << "[Memory] Outstanding allocation size buckets:" << std::endl;
        for (const SizeBucket& bucket : s_SizeBuckets)
        {
            if (bucket.Count == 0)
                continue;
            std::cout << "  size=" << bucket.Size << " count=" << bucket.Count << std::endl;
        }
        UnlockBuckets();
    }

    void* AllocateTracked(size_t size)
    {
        size_t totalSize = sizeof(AllocationHeader) + size;
        auto* raw = static_cast<AllocationHeader*>(std::malloc(totalSize));
        if (!raw)
            throw std::bad_alloc();

        raw->Size = size;
        raw->Tracked = CCEngine::s_TrackingEnabled;
        if (raw->Tracked)
        {
            CCEngine::s_Stats.TotalAllocated += size;
            RecordAllocationSize(size);
        }
        return raw + 1;
    }

    void FreeTracked(void* memory) noexcept
    {
        if (!memory)
            return;

        auto* header = static_cast<AllocationHeader*>(memory) - 1;
        if (header->Tracked)
        {
            CCEngine::s_Stats.TotalFreed += header->Size;
            RecordFreeSize(header->Size);
        }
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
