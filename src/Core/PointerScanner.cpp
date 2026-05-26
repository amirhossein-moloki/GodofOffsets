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

        // 1. Static Analysis: Build a map of all pointers in memory
        // value -> address where that value was found
        std::multimap<uintptr_t, uintptr_t> pointerMap;
        std::mutex mapMutex;

        std::vector<std::thread> threads;
        unsigned int numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) numThreads = 1;

        for (const auto& region : regions) {
            if (m_cancelRequested) break;
#ifdef _WIN32
            if (!(region.protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
                continue;
#endif
            threads.push_back(std::thread([&, region]() {
                const size_t chunkSize = 1024 * 1024; // 1MB chunks
                std::vector<uint8_t> buffer(chunkSize);

                for (size_t i = 0; i < region.size; i += chunkSize - sizeof(uintptr_t)) {
                    if (m_cancelRequested) return;
                    size_t toRead = (std::min)(chunkSize, region.size - i);
                    if (!m_pm.ReadMemory(region.baseAddress + i, buffer.data(), toRead)) continue;

                    for (size_t j = 0; j <= (toRead >= sizeof(uintptr_t) ? toRead - sizeof(uintptr_t) : 0); j += sizeof(uintptr_t)) {
                        uintptr_t value = *(uintptr_t*)(buffer.data() + j);

                        // Check if value points to any valid memory region
                        bool isValid = false;
                        for (const auto& r : regions) {
                            if (value >= r.baseAddress && value < r.baseAddress + r.size) {
                                isValid = true;
                                break;
                            }
                        }

                        if (isValid) {
                            std::lock_guard<std::mutex> lock(mapMutex);
                            pointerMap.insert({ value, region.baseAddress + i + j });
                        }
                    }
                }
            }));

            if (threads.size() >= numThreads) {
                for (auto& t : threads) t.join();
                threads.clear();
            }
        }
        for (auto& t : threads) t.join();

        // 2. Recursive search on the map
        std::vector<PointerChain> localResults;
        std::set<uintptr_t> visited;

        std::function<void(uintptr_t, int, std::vector<uintptr_t>)> findChains;
        findChains = [&](uintptr_t currentTarget, int depth, std::vector<uintptr_t> currentOffsets) {
            if (depth > maxDepth || m_cancelRequested) return;
            if (visited.count(currentTarget)) return;
            visited.insert(currentTarget);

            // Find all addresses in the map that point to [currentTarget - maxOffset, currentTarget]
            auto itLow = pointerMap.lower_bound(currentTarget - maxOffset);
            auto itHigh = pointerMap.upper_bound(currentTarget);

            for (auto it = itLow; it != itHigh; ++it) {
                uintptr_t foundAddr = it->second;
                uintptr_t valueFound = it->first;
                uintptr_t offset = currentTarget - valueFound;

                std::vector<uintptr_t> newOffsets = currentOffsets;
                newOffsets.insert(newOffsets.begin(), offset);

                bool matchedModule = false;
                for (const auto& mod : modules) {
                    if (foundAddr >= mod.baseAddress && foundAddr < mod.baseAddress + mod.imageSize) {
                        PointerChain chain;
                        chain.baseAddress = mod.baseAddress;
                        chain.moduleName = mod.name;
                        chain.offsets = newOffsets;
                        chain.offsets.insert(chain.offsets.begin(), foundAddr - mod.baseAddress);

                        localResults.push_back(chain);
                        matchedModule = true;
                        break;
                    }
                }

                if (!matchedModule && depth < maxDepth) {
                    findChains(foundAddr, depth + 1, newOffsets);
                }
            }
        };

        findChains(targetAddress, 1, {});

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
