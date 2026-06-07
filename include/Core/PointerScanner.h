#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

#include <vector>
#include <string>
#include <atomic>
#include <vector>
#include <set>
#include <mutex>
#include <future>
#include "Core/ProcessManager.h"

namespace Core {

struct PointerChain {
    uintptr_t baseAddress;
    std::string moduleName;
    std::vector<uintptr_t> offsets;
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

    // Two-stage pointer scanning
    // Store as sorted vector for efficient range-based lookups (value, address)
    std::vector<std::pair<uintptr_t, uintptr_t>> m_pointerMap;
    void BuildPointerMap();
    void FindChainsRecursive(uintptr_t currentTarget, int depth, int maxDepth, size_t maxOffset, std::vector<uintptr_t>& currentOffsets, std::set<uintptr_t>& visited);
};

} // namespace Core
