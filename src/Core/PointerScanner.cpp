#include "Core/PointerScanner.h"
#include <algorithm>
#include <iostream>
#include <vector>
#include <unordered_set>
#include <functional>
#include <sstream>
#include <thread>
#include <mutex>
#include "Utils/ThreadPool.h"

namespace Core {

PointerScanner::PointerScanner(const ProcessManager& pm) : m_pm(pm) {}

void PointerScanner::StartScan(uintptr_t targetAddress, int maxDepth, size_t maxOffset) {
    if (m_isScanning) return;

    std::thread([this, targetAddress, maxDepth, maxOffset]() {
        m_isScanning = true;
        m_cancelRequested = false;
        m_progress = 0.0f;

        {
            std::lock_guard<std::mutex> lock(m_resultsMutex);
            m_results.clear();
            m_pointerMap.clear();
        }

        // Stage 1: Build and sort the global pointer map
        BuildPointerMap();
        if (m_cancelRequested) { m_isScanning = false; return; }

        m_progress = 0.75f; // Map building finished

        // Cache and sort modules for O(log M) lookup
        auto modules = m_pm.GetModules();
        std::sort(modules.begin(), modules.end(), [](const auto& a, const auto& b) {
            return a.baseAddress < b.baseAddress;
        });

        // Stage 2: Recursive chain discovery
        std::unordered_set<uintptr_t> visited;
        std::vector<uintptr_t> currentPath;
        currentPath.reserve(maxDepth + 1);

        FindChainsRecursive(targetAddress, 1, maxDepth, maxOffset, currentPath, visited, modules);

        m_progress = 1.0f;
        m_isScanning = false;
    }).detach();
}

void PointerScanner::BuildPointerMap() {
    auto regions = m_pm.GetRegions();

    std::mutex mapMutex;
    size_t totalSize = 0;
    for (const auto& r : regions) totalSize += r.size;
    size_t processedSize = 0;

    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 1;
    Utils::ThreadPool pool(numThreads);
    std::vector<std::future<std::vector<std::pair<uintptr_t, uintptr_t>>>> futures;

    for (const auto& region : regions) {
        if (m_cancelRequested) break;

#ifdef _WIN32
        if (!(region.protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) {
            processedSize += region.size;
            continue;
        }
#endif

        futures.push_back(pool.Enqueue([this, region, &mapMutex, &processedSize, totalSize]() {
            const size_t chunkSize = 1024 * 1024; // 1MB chunks
            std::vector<uint8_t> buffer(chunkSize);
            std::vector<std::pair<uintptr_t, uintptr_t>> localResults;
            localResults.reserve(chunkSize / sizeof(uintptr_t));

            for (size_t i = 0; i < region.size; i += chunkSize - sizeof(uintptr_t)) {
                if (m_cancelRequested) break;
                size_t toRead = (std::min)(chunkSize, region.size - i);
                if (!m_pm.ReadMemory(region.baseAddress + i, buffer.data(), toRead)) continue;

                for (size_t j = 0; j <= (toRead >= sizeof(uintptr_t) ? toRead - sizeof(uintptr_t) : 0); j += sizeof(uintptr_t)) {
                    uintptr_t value = 0;
                    memcpy(&value, buffer.data() + j, sizeof(uintptr_t));

                    // Basic validity check for pointers (aligned and not in NULL page)
                    if (value > 0x10000 && (value % sizeof(uintptr_t) == 0)) {
                        localResults.push_back({ value, region.baseAddress + i + j });
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(mapMutex);
                processedSize += region.size;
                m_progress = 0.6f * ((float)processedSize / totalSize);
            }
            return localResults;
        }));
    }

    for (auto& f : futures) {
        auto res = f.get();
        m_pointerMap.insert(m_pointerMap.end(), res.begin(), res.end());
    }

    if (m_cancelRequested) return;

    // Sorting the map for binary search lookups
    std::sort(m_pointerMap.begin(), m_pointerMap.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });
}

void PointerScanner::FindChainsRecursive(uintptr_t currentTarget, int depth, int maxDepth, size_t maxOffset, std::vector<uintptr_t>& currentPath, std::unordered_set<uintptr_t>& visited, const std::vector<ModuleInfo>& modules) {
    if (depth > maxDepth || m_cancelRequested) return;
    if (visited.count(currentTarget)) return;
    visited.insert(currentTarget);

    // Use binary search to find the range of values in [currentTarget - maxOffset, currentTarget]
    uintptr_t minVal = (currentTarget > maxOffset) ? currentTarget - maxOffset : 0;
    uintptr_t maxVal = currentTarget;

    auto it_start = std::lower_bound(m_pointerMap.begin(), m_pointerMap.end(), std::make_pair(minVal, (uintptr_t)0), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    auto it_end = std::upper_bound(m_pointerMap.begin(), m_pointerMap.end(), std::make_pair(maxVal, (uintptr_t)-1), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    for (auto it = it_start; it != it_end; ++it) {
        if (m_cancelRequested) return;

        uintptr_t foundValue = it->first;
        uintptr_t foundAddr = it->second;
        uintptr_t offset = currentTarget - foundValue;

        currentPath.push_back(offset);

        // Binary search for module containing foundAddr
        auto mod_it = std::upper_bound(modules.begin(), modules.end(), foundAddr, [](uintptr_t addr, const ModuleInfo& mod) {
            return addr < mod.baseAddress;
        });

        if (mod_it != modules.begin()) {
            const auto& mod = *(--mod_it);
            if (foundAddr >= mod.baseAddress && foundAddr < mod.baseAddress + mod.imageSize) {
                PointerChain chain;
                chain.baseAddress = mod.baseAddress;
                chain.moduleName = mod.name;
                chain.offsets = currentPath;
                // Path was built in discovery order (target to base), so we reverse for standard representation
                std::reverse(chain.offsets.begin(), chain.offsets.end());
                // Add the static base offset at the beginning
                chain.offsets.insert(chain.offsets.begin(), foundAddr - mod.baseAddress);

                {
                    std::lock_guard<std::mutex> lock(m_resultsMutex);
                    m_results.push_back(std::move(chain));
                }

                currentPath.pop_back();
                continue;
            }
        }

        if (depth < maxDepth) {
            FindChainsRecursive(foundAddr, depth + 1, maxDepth, maxOffset, currentPath, visited, modules);
        }

        currentPath.pop_back();
    }
}

std::vector<PointerChain> PointerScanner::GetResults() {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    return m_results;
}

} // namespace Core
