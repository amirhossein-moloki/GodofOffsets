#include "Core/Scanner.h"
#include <fstream>
#include <sstream>
#include <thread>
#include <future>
#include <iostream>
#include <array>

#ifdef _WIN32
#include <intrin.h>
#endif

namespace Core {

static bool CheckAVX2() {
#ifdef _WIN32
    std::array<int, 4> cpui;
    __cpuid(cpui.data(), 0);
    if (cpui[0] < 7) return false;
    __cpuidex(cpui.data(), 7, 0);
    return (cpui[1] & (1 << 5)) != 0;
#else
    return true; // Assume AVX2 for Linux tests
#endif
}

static bool CheckSSE42() {
#ifdef _WIN32
    std::array<int, 4> cpui;
    __cpuid(cpui.data(), 1);
    return (cpui[2] & (1 << 20)) != 0;
#else
    return true;
#endif
}

Scanner::Scanner(const ProcessManager& pm) : m_pm(pm), m_resolver(pm) {}

std::vector<uint8_t> Scanner::ParsePattern(const std::string& pattern, std::vector<bool>& mask) {
    std::vector<uint8_t> bytes;
    std::stringstream ss(pattern);
    std::string item;
    while (ss >> item) {
        if (item == "?" || item == "??") {
            bytes.push_back(0);
            mask.push_back(false);
        } else {
            try {
                bytes.push_back((uint8_t)std::stoul(item, nullptr, 16));
                mask.push_back(true);
            } catch (...) {
                continue;
            }
        }
    }
    return bytes;
}

uintptr_t Scanner::ScanInternal(uintptr_t base, size_t size, const std::string& pattern) {
    std::vector<bool> mask;
    auto patternBytes = ParsePattern(pattern, mask);
    if (patternBytes.empty()) return 0;

    std::vector<uint8_t> moduleBuffer(size);
    if (!m_pm.ReadMemory(base, moduleBuffer.data(), size)) return 0;

    for (size_t i = 0; i <= size - patternBytes.size(); ++i) {
        bool found = true;
        for (size_t j = 0; j < patternBytes.size(); ++j) {
            if (mask[j] && moduleBuffer[i + j] != patternBytes[j]) {
                found = false;
                break;
            }
        }
        if (found) return base + i;
    }
    return 0;
}

uintptr_t Scanner::FindPattern(const std::string& moduleName, const std::string& pattern) {
    auto mod = m_pm.GetModuleInfo(moduleName);
    if (mod.baseAddress == 0) return 0;

    return ScanInternal(mod.baseAddress, mod.imageSize, pattern);
}

std::vector<Signature> Scanner::LoadSignatures(const std::string& filename) {
    std::vector<Signature> sigs;
    std::ifstream f(filename);
    if (!f.is_open()) return sigs;

    nlohmann::json j;
    f >> j;

    for (auto& item : j["signatures"]) {
        sigs.push_back({
            item["name"],
            item["pattern"],
            item["module"],
            item.value("offset", 0),
            item.value("relative", true)
        });
    }
    return sigs;
}

void Scanner::Run(std::vector<Signature>& sigs, bool isVulkan) {
    std::vector<std::future<void>> futures;

    for (auto& sig : sigs) {
        if (isVulkan && sig.moduleName == "RainbowSix.exe") {
            sig.moduleName = "RainbowSix_Vulkan.exe";
        }

        futures.push_back(std::async(std::launch::async, [this, &sig]() {
            uintptr_t addr = FindPattern(sig.moduleName, sig.pattern);
            if (addr) {
                addr += sig.offset;
                if (sig.isRelative) {
                    sig.result = m_resolver.ResolveWithZydis(addr);
                } else {
                    sig.result = addr;
                }
            }
        }));
    }

    for (auto& f : futures) f.wait();
}

} // namespace Core
