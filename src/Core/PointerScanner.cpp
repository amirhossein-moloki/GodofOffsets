#include "Core/PointerScanner.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <functional>
#include <sstream>
#include <thread>
#include <mutex>

namespace Core {

PointerScanner::PointerScanner(const ProcessManager& pm) : m_pm(pm) {}

void PointerScanner::StartScan(uintptr_t targetAddress, int maxDepth, size_t maxOffset) {
    if (m_isScanning) return;

    std::thread([this, targetAddress, maxDepth, maxOffset]() {
        m_isScanning = true;
        m_cancelRequested = false;

        {
            std::lock_guard<std::mutex> lock(m_resultsMutex);
            m_results.clear();
        }

        auto regions = m_pm.GetRegions();
        auto modules = m_pm.GetModules();
        std::set<uintptr_t> visited;
        std::vector<PointerChain> localResults;

        std::function<void(uintptr_t, int, std::vector<uintptr_t>)> scanRecursive;
        scanRecursive = [&](uintptr_t currentTarget, int depth, std::vector<uintptr_t> currentOffsets) {
            if (depth > maxDepth || m_cancelRequested) return;
            if (visited.count(currentTarget)) return;
            visited.insert(currentTarget);

            for (const auto& region : regions) {
#ifdef _WIN32
                if (!(region.protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
                    continue;
#endif

                const size_t chunkSize = 1024 * 64;
                std::vector<uint8_t> buffer(chunkSize);

                for (size_t i = 0; i < region.size; i += chunkSize - sizeof(uintptr_t)) {
                    if (m_cancelRequested) return;
                    size_t toRead = (std::min)(chunkSize, region.size - i);
                    if (!m_pm.ReadMemory(region.baseAddress + i, buffer.data(), toRead)) continue;

                    for (size_t j = 0; j <= (toRead >= sizeof(uintptr_t) ? toRead - sizeof(uintptr_t) : 0); j += sizeof(uintptr_t)) {
                        uintptr_t value = *(uintptr_t*)(buffer.data() + j);

                        if (value >= currentTarget - maxOffset && value <= currentTarget) {
                            uintptr_t foundAddr = region.baseAddress + i + j;
                            uintptr_t offset = currentTarget - value;

                            std::vector<uintptr_t> newOffsets = currentOffsets;
                            newOffsets.insert(newOffsets.begin(), offset);

                            for (const auto& mod : modules) {
                                if (foundAddr >= mod.baseAddress && foundAddr < mod.baseAddress + mod.imageSize) {
                                    PointerChain chain;
                                    chain.baseAddress = mod.baseAddress;
                                    chain.moduleName = mod.name;
                                    chain.offsets = newOffsets;
                                    chain.offsets.insert(chain.offsets.begin(), foundAddr - mod.baseAddress);

                                    localResults.push_back(chain);
                                    return;
                                }
                            }

                            if (depth < maxDepth) {
                                scanRecursive(foundAddr, depth + 1, newOffsets);
                            }
                        }
                    }
                }
            }
        };

        scanRecursive(targetAddress, 1, {});

        {
            std::lock_guard<std::mutex> lock(m_resultsMutex);
            m_results = std::move(localResults);
        }
        m_isScanning = false;
    }).detach();
}

std::vector<PointerChain> PointerScanner::GetResults() {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    return m_results;
}

} // namespace Core
