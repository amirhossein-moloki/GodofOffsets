#include "Core/Scanner.h"
#include <immintrin.h>
#include <fstream>
#include <sstream>
#include <thread>
#include <future>
#include <iostream>
#include <bit>

#ifdef _MSC_VER
#include <intrin.h>
#else
#include <cpuid.h>
#endif

namespace Core {

static void GetCPUID(int info[4], int ax) {
#ifdef _MSC_VER
    __cpuidex(info, ax, 0);
#else
    __cpuid_count(ax, 0, info[0], info[1], info[2], info[3]);
#endif
}

static bool SupportsAVX2() {
    int info[4];
    GetCPUID(info, 7);
    return (info[1] & (1 << 5)) != 0;
}

static bool SupportsSSE42() {
    int info[4];
    GetCPUID(info, 1);
    return (info[2] & (1 << 20)) != 0;
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

    std::vector<uint8_t> buffer(size);
    if (!m_pm.ReadMemory(base, buffer.data(), size)) return 0;

    size_t i = 0;
    if (mask[0]) {
        uint8_t firstByte = patternBytes[0];
        bool hasAVX2 = SupportsAVX2();
        bool hasSSE42 = SupportsSSE42();

        if (hasAVX2 && size >= 32) {
            __m256i firstByteVec256 = _mm256_set1_epi8(firstByte);
            for (; i <= size - 32; i += 32) {
                __m256i data = _mm256_loadu_si256((const __m256i*)(buffer.data() + i));
                __m256i cmp = _mm256_cmpeq_epi8(data, firstByteVec256);
                uint32_t bitmask = (uint32_t)_mm256_movemask_epi8(cmp);

                while (bitmask != 0) {
                    int pos = std::countr_zero(bitmask);
                    if (i + pos <= size - patternBytes.size()) {
                        bool found = true;
                        for (size_t k = 1; k < patternBytes.size(); ++k) {
                            if (mask[k] && buffer[i + pos + k] != patternBytes[k]) {
                                found = false;
                                break;
                            }
                        }
                        if (found) return base + i + pos;
                    }
                    bitmask &= ~(1 << pos);
                }
            }
        } else if (hasSSE42 && size >= 16) {
            __m128i firstByteVec128 = _mm_set1_epi8(firstByte);
            for (; i <= size - 16; i += 16) {
                __m128i data = _mm_loadu_si128((const __m128i*)(buffer.data() + i));
                __m128i cmp = _mm_cmpeq_epi8(data, firstByteVec128);
                uint32_t bitmask = (uint32_t)_mm_movemask_epi8(cmp);

                while (bitmask != 0) {
                    int pos = std::countr_zero(bitmask);
                    if (i + pos <= size - patternBytes.size()) {
                        bool found = true;
                        for (size_t k = 1; k < patternBytes.size(); ++k) {
                            if (mask[k] && buffer[i + pos + k] != patternBytes[k]) {
                                found = false;
                                break;
                            }
                        }
                        if (found) return base + i + pos;
                    }
                    bitmask &= ~(1 << pos);
                }
            }
        }
    }

    for (; i <= (size >= patternBytes.size() ? size - patternBytes.size() : 0); ++i) {
        bool found = true;
        for (size_t j = 0; j < patternBytes.size(); ++j) {
            if (mask[j] && buffer[i + j] != patternBytes[j]) {
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
