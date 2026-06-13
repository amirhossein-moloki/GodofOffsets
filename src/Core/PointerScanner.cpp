#include "Core/PointerScanner.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <functional>
#include <sstream>
#include <thread>
#include <mutex>
#include "Utils/ThreadPool.h"

namespace Core {

PointerScanner::PointerScanner(const ProcessManager& pm) : m_pm(pm) {}

void PointerScanner::StartScan(uintptr_t targetAddress, int maxDepth, size_t maxOffset) {
    if (m_isScanning) return;

    // Ensure we don't block the UI thread if a previous future is being cleaned up
    if (m_scanFuture.valid()) {
        auto status = m_scanFuture.wait_for(std::chrono::milliseconds(0));
        if (status != std::future_status::ready) {
            // Previous scan is still cleaning up or finishing
            return;
        }
    }

    m_scanFuture = std::async(std::launch::async, [this, targetAddress, maxDepth, maxOffset]() {
        m_isScanning = true;
        m_cancelRequested = false;
        m_progress = 0.0f;

        {
            std::lock_guard<std::mutex> lock(m_resultsMutex);
            m_results.clear();
            m_arena.Reset();
            m_pointerMap = nullptr;
            m_pointerMapCount = 0;
        }

        // Stage 1: Build the global pointer map
        BuildPointerMap();
        if (m_cancelRequested) { m_isScanning = false; return; }

        m_progress = 0.8f; // Map building finished

        // Stage 2: Recursive chain discovery
        std::set<uintptr_t> visited;
        std::vector<uintptr_t> currentOffsets;
        FindChainsRecursive(targetAddress, 1, maxDepth, maxOffset, currentOffsets, visited);

        m_progress = 1.0f;
        m_isScanning = false;
    });
}

void PointerScanner::BuildPointerMap() {
    auto regions = m_pm.GetRegions();
    auto modules = m_pm.GetModules();

    size_t totalSize = 0;
    for (const auto& r : regions) totalSize += r.size;
    size_t processedSize = 0;

    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 1;
    Utils::ThreadPool pool(numThreads);
    std::vector<std::future<std::vector<PointerNode>>> futures;

    for (const auto& region : regions) {
        if (m_cancelRequested) break;

#ifdef _WIN32
        if (!(region.protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) {
            std::lock_guard<std::mutex> lock(m_resultsMutex);
            processedSize += region.size;
            m_progress = 0.8f * ((float)processedSize / totalSize);
            continue;
        }
#endif

        futures.push_back(pool.Enqueue([this, region, &processedSize, totalSize]() {
            const size_t chunkSize = 1024 * 1024; // 1MB chunks
            std::vector<uint8_t> buffer(chunkSize);
            std::vector<PointerNode> localResults;

            for (size_t i = 0; i < region.size; i += chunkSize - sizeof(uintptr_t)) {
                if (m_cancelRequested) return localResults;
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
                std::lock_guard<std::mutex> lock(m_resultsMutex);
                processedSize += region.size;
                m_progress = 0.8f * ((float)processedSize / totalSize);
            }
            return localResults;
        }));
    }

    std::vector<std::vector<PointerNode>> allResults;
    size_t totalNodes = 0;
    for (auto& f : futures) {
        auto res = f.get();
        totalNodes += res.size();
        allResults.push_back(std::move(res));
    }

    if (m_cancelRequested) return;

    {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        m_pointerMapCount = totalNodes;
        m_pointerMap = m_arena.Allocate<PointerNode>(m_pointerMapCount);

        size_t currentIdx = 0;
        for (auto& res : allResults) {
            std::copy(res.begin(), res.end(), m_pointerMap + currentIdx);
            currentIdx += res.size();
        }

        // Sort the entire map by value for O(log N) lookups
        std::sort(m_pointerMap, m_pointerMap + m_pointerMapCount);
    }
}

void PointerScanner::FindChainsRecursive(uintptr_t currentTarget, int depth, int maxDepth, size_t maxOffset, std::vector<uintptr_t>& currentOffsets, std::set<uintptr_t>& visited) {
    if (depth > maxDepth || m_cancelRequested) return;
    if (visited.count(currentTarget)) return;
    visited.insert(currentTarget);

    auto modules = m_pm.GetModules();

    // Check pointers that point to an address within [currentTarget - maxOffset, currentTarget]
    // Use binary search to find the range of interest in our sorted map
    PointerNode startNode{ currentTarget - maxOffset, 0 };
    PointerNode endNode{ currentTarget, (uintptr_t)-1 };

    auto startIt = std::lower_bound(m_pointerMap, m_pointerMap + m_pointerMapCount, startNode);
    auto endIt = std::upper_bound(m_pointerMap, m_pointerMap + m_pointerMapCount, endNode);

    for (auto it = startIt; it != endIt; ++it) {
        if (m_cancelRequested) return;

        uintptr_t foundAddr = it->address;
            uintptr_t offset = currentTarget - it->value;

            std::vector<uintptr_t> newOffsets = currentOffsets;
            newOffsets.insert(newOffsets.begin(), offset);

            // Check if this address belongs to a module (static base)
            bool foundStatic = false;
            for (const auto& mod : modules) {
                if (foundAddr >= mod.baseAddress && foundAddr < mod.baseAddress + mod.imageSize) {
                    PointerChain chain;
                    chain.baseAddress = mod.baseAddress;
                    chain.moduleName = mod.name;
                    chain.offsets = newOffsets;
                    chain.offsets.insert(chain.offsets.begin(), foundAddr - mod.baseAddress);

                    std::lock_guard<std::mutex> lock(m_resultsMutex);
                    m_results.push_back(chain);
                    foundStatic = true;
                    break;
                }
            }

            if (!foundStatic && depth < maxDepth) {
                FindChainsRecursive(foundAddr, depth + 1, maxDepth, maxOffset, newOffsets, visited);
            }
    }
}

std::vector<PointerChain> PointerScanner::GetResults() {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    return m_results;
}

} // namespace Core
