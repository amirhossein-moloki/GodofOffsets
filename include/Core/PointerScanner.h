#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

#include <vector>
#include <string>
#include <atomic>
#include <unordered_set>
#include <mutex>
#include <future>
#include "Core/ProcessManager.h"
#include "Utils/ArenaAllocator.h"

namespace Core {

struct PointerChain {
    uintptr_t baseAddress;
    std::string moduleName;
    std::vector<uintptr_t> offsets;
};

struct PointerNode {
    uintptr_t value;
    uintptr_t address;

    bool operator<(const PointerNode& other) const {
        return value < other.value;
    }
};

class PointerScanner {
public:
    PointerScanner(const ProcessManager& pm);

    void StartScan(uintptr_t targetAddress, int maxDepth, size_t maxOffset);
    std::vector<PointerChain> GetResults();
    bool IsScanning() const { return m_isScanning; }
    float GetProgress() const { return m_progress; }
    void Cancel() { m_cancelRequested = true; }

private:
    const ProcessManager& m_pm;
    std::vector<PointerChain> m_results;
    std::atomic<bool> m_isScanning{false};
    std::atomic<bool> m_cancelRequested{false};
    std::atomic<float> m_progress{0.0f};
    std::mutex m_resultsMutex;
    std::future<void> m_scanFuture;

    // Optimized pointer map
    Utils::ArenaAllocator m_arena;
    PointerNode* m_pointerNodes = nullptr;
    size_t m_nodeCount = 0;

    void BuildPointerMap();
    void FindChainsRecursive(uintptr_t currentTarget, int depth, int maxDepth, size_t maxOffset, std::vector<uintptr_t>& currentOffsets, std::unordered_set<uintptr_t>& visited);
};

} // namespace Core
