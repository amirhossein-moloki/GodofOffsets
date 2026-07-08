#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

#include <vector>
#include <string>
#include <future>
#include <nlohmann/json.hpp>
#include "Core/ProcessManager.h"
#include "Core/OffsetResolver.h"

namespace Core {

struct Signature {
    std::string name;
    std::string pattern;
    std::string moduleName;
    int offset = 0;
    bool isRelative = true;
    uintptr_t result = 0;
};

class Scanner {
public:
    Scanner(const ProcessManager& pm);

    uintptr_t FindPattern(const std::string& moduleName, const std::string& pattern);
    std::vector<Signature> LoadSignatures(const std::string& filename);
    void Run(std::vector<Signature>& sigs);

    bool IsScanning() const { return m_isScanning; }
    void Cancel() { m_cancelRequested = true; }

protected:
    const ProcessManager& m_pm;
    OffsetResolver m_resolver;
    bool m_isScanning = false;
    bool m_cancelRequested = false;
    std::future<void> m_sigScanFuture;

    uintptr_t ScanInternal(uintptr_t base, size_t size, const std::string& pattern);

private:
    std::vector<uint8_t> ParsePattern(const std::string& pattern, std::vector<bool>& mask);
};

} // namespace Core
