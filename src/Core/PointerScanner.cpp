#include "Core/PointerScanner.h"
#include <algorithm>
#include <iostream>
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

        // Stage 1: Build the global pointer map
        BuildPointerMap();
        if (m_cancelRequested) { m_isScanning = false; return; }

        m_progress = 0.8f; // Map building finished

        // Stage 2: Recursive chain discovery
        auto modules = m_pm.GetModules();
        std::unordered_set<uintptr_t> visited;
        std::vector<uintptr_t> currentOffsets;
        FindChainsRecursive(targetAddress, 1, maxDepth, maxOffset, currentOffsets, visited, modules);

        m_progress = 1.0f;
        m_isScanning = false;
    }).detach();
}

void PointerScanner::BuildPointerMap() {
    auto regions = m_pm.GetRegions();
    auto modules = m_pm.GetModules();

    std::mutex mapMutex;
    size_t totalSize = 0;
    for (const auto& r : regions) totalSize += r.size;
    size_t processedSize = 0;

    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 1;
    Utils::ThreadPool pool(numThreads);
    std::vector<std::future<void>> futures;

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
            std::vector<PointerEntry> localResults;

            for (size_t i = 0; i < region.size; i += chunkSize - sizeof(uintptr_t)) {
                if (m_cancelRequested) return;
                size_t toRead = (std::min)(chunkSize, region.size - i);
                if (!m_pm.ReadMemory(region.baseAddress + i, buffer.data(), toRead)) continue;

                for (size_t j = 0; j <= (toRead >= sizeof(uintptr_t) ? toRead - sizeof(uintptr_t) : 0); j += sizeof(uintptr_t)) {
                    uintptr_t value = *(uintptr_t*)(buffer.data() + j);

                    if (value > 0x10000 && (value % sizeof(uintptr_t) == 0)) {
                        localResults.push_back({ value, region.baseAddress + i + j });
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(mapMutex);
                m_pointerMap.insert(m_pointerMap.end(), localResults.begin(), localResults.end());
                processedSize += region.size;
                m_progress = 0.8f * ((float)processedSize / totalSize);
            }
        }));
    }

    for (auto& f : futures) {
        f.get();
    }

    // Sort map for binary search
    std::sort(m_pointerMap.begin(), m_pointerMap.end());
}

void PointerScanner::FindChainsRecursive(uintptr_t currentTarget, int depth, int maxDepth, size_t maxOffset, std::vector<uintptr_t>& currentOffsets, std::unordered_set<uintptr_t>& visited, const std::vector<ModuleInfo>& modules) {
    if (depth > maxDepth || m_cancelRequested) return;
    if (visited.count(currentTarget)) return;
    visited.insert(currentTarget);

    // Binary search for the range of values [currentTarget - maxOffset, currentTarget]
    PointerEntry searchMin = { currentTarget - maxOffset, 0 };
    PointerEntry searchMax = { currentTarget, 0xFFFFFFFFFFFFFFFF };

    auto itStart = std::lower_bound(m_pointerMap.begin(), m_pointerMap.end(), searchMin);
    auto itEnd = std::upper_bound(m_pointerMap.begin(), m_pointerMap.end(), searchMax);

    for (auto it = itStart; it != itEnd; ++it) {
        if (m_cancelRequested) return;

        uintptr_t foundAddr = it->address;
        uintptr_t offset = currentTarget - it->value;

        // Use push_back/pop_back for efficiency and reverse for display if needed
        currentOffsets.push_back(offset);

        // Check if this address belongs to a module (static base)
        bool foundStatic = false;
        for (const auto& mod : modules) {
            if (foundAddr >= mod.baseAddress && foundAddr < mod.baseAddress + mod.imageSize) {
                PointerChain chain;
                chain.baseAddress = mod.baseAddress;
                chain.moduleName = mod.name;

                // Construct the full chain in the expected order (base + offsets)
                chain.offsets.reserve(currentOffsets.size() + 1);
                chain.offsets.push_back(foundAddr - mod.baseAddress);
                for (auto itRev = currentOffsets.rbegin(); itRev != currentOffsets.rend(); ++itRev) {
                    chain.offsets.push_back(*itRev);
                }

                std::lock_guard<std::mutex> lock(m_resultsMutex);
                m_results.push_back(chain);
                foundStatic = true;
                break;
            }
        }

        if (!foundStatic && depth < maxDepth) {
            FindChainsRecursive(foundAddr, depth + 1, maxDepth, maxOffset, currentOffsets, visited, modules);
        }

        // Backtrack
        currentOffsets.pop_back();
    }
}

std::vector<PointerChain> PointerScanner::GetResults() {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    return m_results;
}

} // namespace Core
