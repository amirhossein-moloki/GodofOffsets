#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

#include <vector>
#include <string>
#include <atomic>
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
    void Cancel() { m_cancelRequested = true; }

private:
    const ProcessManager& m_pm;
    std::vector<PointerChain> m_results;
    std::atomic<bool> m_isScanning{false};
    std::atomic<bool> m_cancelRequested{false};
    std::mutex m_resultsMutex;
};

} // namespace Core
