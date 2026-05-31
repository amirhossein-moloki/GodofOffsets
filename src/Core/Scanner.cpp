#include "Core/Scanner.h"
#include <fstream>
#include <sstream>
#include <thread>
#include <future>
#include <iostream>
#include <immintrin.h>
#include <bit>
#include <cpuid.h>

namespace Core {

struct CPUFeatures {
    bool avx2;
    bool sse42;

    CPUFeatures() {
        unsigned int eax, ebx, ecx, edx;
        if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
            sse42 = (ecx & (1 << 20)) != 0;
        } else {
            sse42 = false;
        }

        if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
            avx2 = (ebx & (1 << 5)) != 0;
        } else {
            avx2 = false;
        }
    }
};

static const CPUFeatures g_cpuFeatures;

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
    auto bytes = ParsePattern(pattern, mask);
    if (bytes.empty()) return 0;

    const size_t bufferSize = 256 * 1024;
    std::vector<uint8_t> buffer(bufferSize);

    for (size_t offset = 0; offset < size; ) {
        if (m_cancelRequested) break;

        size_t toRead = (std::min)(bufferSize, size - offset);
        if (!m_pm.ReadMemory(base + offset, buffer.data(), toRead)) {
            offset += bufferSize;
            continue;
        }

        if (mask[0]) {
            uint8_t firstByte = bytes[0];
            size_t i = 0;

            if (g_cpuFeatures.avx2) {
                __m256i firstByteVec256 = _mm256_set1_epi8(firstByte);
                for (; i <= (toRead >= 32 ? toRead - 32 : 0); i += 32) {
                    if (m_cancelRequested) return 0;
                    __m256i data = _mm256_loadu_si256((const __m256i*)(buffer.data() + i));
                    __m256i cmp = _mm256_cmpeq_epi8(data, firstByteVec256);
                    uint32_t bitmask = _mm256_movemask_epi8(cmp);

                    while (bitmask != 0) {
                        int pos = std::countr_zero(bitmask);
                        if (i + pos <= toRead - bytes.size()) {
                            bool found = true;
                            for (size_t k = 1; k < bytes.size(); ++k) {
                                if (mask[k] && buffer[i + pos + k] != bytes[k]) {
                                    found = false;
                                    break;
                                }
                            }
                            if (found) return base + offset + i + pos;
                        }
                        bitmask &= ~(1U << pos);
                    }
                }
            }

            if (g_cpuFeatures.sse42) {
                __m128i firstByteVec128 = _mm_set1_epi8(firstByte);
                for (; i <= (toRead >= 16 ? toRead - 16 : 0); i += 16) {
                    if (m_cancelRequested) return 0;
                    __m128i data = _mm_loadu_si128((const __m128i*)(buffer.data() + i));
                    __m128i cmp = _mm_cmpeq_epi8(data, firstByteVec128);
                    uint32_t bitmask = (uint32_t)_mm_movemask_epi8(cmp);

                    while (bitmask != 0) {
                        int pos = std::countr_zero(bitmask);
                        if (i + pos <= toRead - bytes.size()) {
                            bool found = true;
                            for (size_t k = 1; k < bytes.size(); ++k) {
                                if (mask[k] && buffer[i + pos + k] != bytes[k]) {
                                    found = false;
                                    break;
                                }
                            }
                            if (found) return base + offset + i + pos;
                        }
                        bitmask &= ~(1U << pos);
                    }
                }
            }

            // Remainder
            for (; i < toRead; ++i) {
                if (m_cancelRequested) return 0;
                if (i <= toRead - bytes.size() && buffer[i] == firstByte) {
                    bool found = true;
                    for (size_t k = 1; k < bytes.size(); ++k) {
                        if (mask[k] && buffer[i + k] != bytes[k]) {
                            found = false;
                            break;
                        }
                    }
                    if (found) return base + offset + i;
                }
            }
        } else {
            for (size_t i = 0; i <= (toRead >= bytes.size() ? toRead - bytes.size() : 0); ++i) {
                if (m_cancelRequested) return 0;
                bool found = true;
                for (size_t k = 0; k < bytes.size(); ++k) {
                    if (mask[k] && buffer[i + k] != bytes[k]) {
                        found = false;
                        break;
                    }
                }
                if (found) return base + offset + i;
            }
        }

        if (toRead < bufferSize) break;
        offset += (bufferSize - bytes.size() + 1);
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
    m_cancelRequested = false;
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
