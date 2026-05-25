#pragma once
#include <windows.h>
#include <vector>
#include <string>
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

    std::vector<PointerChain> Scan(uintptr_t targetAddress, int maxDepth, size_t maxOffset);

private:
    const ProcessManager& m_pm;
};

} // namespace Core
