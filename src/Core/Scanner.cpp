#include "Core/Scanner.h"
#include <fstream>
#include <sstream>
#include <thread>
#include <future>
#include <iostream>

namespace Core {

Scanner::Scanner(const MemoryManager& mm) : m_mm(mm), m_resolver(mm) {}

std::vector<uint8_t> Scanner::ParsePattern(const std::string& pattern, std::vector<bool>& mask) {
    std::vector<uint8_t> bytes;
    std::stringstream ss(pattern);
    std::string item;
    while (ss >> item) {
        if (item == "?" || item == "??") {
            bytes.push_back(0);
            mask.push_back(false);
        } else {
            bytes.push_back((uint8_t)std::stoul(item, nullptr, 16));
            mask.push_back(true);
        }
    }
    return bytes;
}

uintptr_t Scanner::ScanInternal(uintptr_t base, size_t size, const std::string& pattern) {
    std::vector<bool> mask;
    auto patternBytes = ParsePattern(pattern, mask);
    if (patternBytes.empty()) return 0;

    // Buffer Chunking: Read the entire module memory into a local buffer
    std::vector<uint8_t> moduleBuffer(size);
    if (!m_mm.ReadRaw(base, moduleBuffer.data(), size)) return 0;

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
    uintptr_t base = m_mm.GetModuleBase(moduleName);
    if (!base) return 0;

    // In a real scenario, we'd get the actual image size from PE headers
    // For this implementation, we assume a reasonable size or use GetModuleInformation
    return ScanInternal(base, 0x10000000, pattern); // Default 256MB scan range for simplicity
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
        // Adjust module name if Vulkan is detected
        if (isVulkan && sig.moduleName == "RainbowSix.exe") {
            sig.moduleName = "RainbowSix_Vulkan.exe";
        }

        futures.push_back(std::async(std::launch::async, [&]() {
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
