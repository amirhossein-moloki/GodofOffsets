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

PointerScanner::PointerScanner(const ProcessManager& pm) : m_pm(pm), m_arena(5 * 1024 * 1024) {}

void PointerScanner::StartScan(uintptr_t targetAddress, int maxDepth, size_t maxOffset) {
    if (m_isScanning) return;

    std::thread([this, targetAddress, maxDepth, maxOffset]() {
        m_isScanning = true;
        m_cancelRequested = false;
        m_progress = 0.0f;

        {
            std::lock_guard<std::mutex> lock(m_resultsMutex);
            m_results.clear();
            m_pointerMap = nullptr;
            m_pointerMapCount = 0;
            m_arena.Reset();
        }

        // Stage 1: Build the global pointer map
        BuildPointerMap();
        if (m_cancelRequested) { m_isScanning = false; return; }

        m_progress = 0.8f; // Map building finished

        // Stage 2: Recursive chain discovery
        std::unordered_set<uintptr_t> visited;
        std::vector<uintptr_t> currentOffsets;
        FindChainsRecursive(targetAddress, 1, maxDepth, maxOffset, currentOffsets, visited);

        m_progress = 1.0f;
        m_isScanning = false;
    }).detach();
}

void PointerScanner::BuildPointerMap() {
    auto regions = m_pm.GetRegions();
    size_t totalSize = 0;
    for (const auto& r : regions) totalSize += r.size;
    size_t processedSize = 0;

    std::vector<PointerNode> globalNodes;
    globalNodes.reserve(totalSize / 1024); // Heuristic

    std::mutex mapMutex;
    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 1;
    Utils::ThreadPool pool(numThreads);
    std::vector<std::future<std::vector<PointerNode>>> futures;

    for (const auto& region : regions) {
        if (m_cancelRequested) break;

#ifdef _WIN32
        if (!(region.protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) {
            std::lock_guard<std::mutex> lock(mapMutex);
            processedSize += region.size;
            continue;
        }
#endif

        futures.push_back(pool.Enqueue([this, region, &mapMutex, &processedSize, totalSize]() {
            std::vector<PointerNode> localNodes;
            const size_t chunkSize = 1024 * 1024;
            std::vector<uint8_t> buffer(chunkSize);

            for (size_t i = 0; i < region.size; i += chunkSize - sizeof(uintptr_t)) {
                if (m_cancelRequested) return localNodes;
                size_t toRead = (std::min)(chunkSize, region.size - i);
                if (!m_pm.ReadMemory(region.baseAddress + i, buffer.data(), toRead)) continue;

                for (size_t j = 0; j <= (toRead >= sizeof(uintptr_t) ? toRead - sizeof(uintptr_t) : 0); j += sizeof(uintptr_t)) {
                    uintptr_t value = *(uintptr_t*)(buffer.data() + j);
                    if (value > 0x10000 && (value % sizeof(uintptr_t) == 0)) {
                        localNodes.push_back({ value, region.baseAddress + i + j });
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(mapMutex);
                processedSize += region.size;
                m_progress = 0.8f * ((float)processedSize / totalSize);
            }
            return localNodes;
        }));
    }

    for (auto& f : futures) {
        auto nodes = f.get();
        globalNodes.insert(globalNodes.end(), nodes.begin(), nodes.end());
    }

    // Sort nodes by value for binary search
    std::sort(globalNodes.begin(), globalNodes.end(), [](const PointerNode& a, const PointerNode& b) {
        return a.value < b.value;
    });

    m_pointerMapCount = globalNodes.size();
    if (m_pointerMapCount > 0) {
        m_pointerMap = (PointerNode*)m_arena.Allocate(m_pointerMapCount * sizeof(PointerNode), alignof(PointerNode));
        memcpy(m_pointerMap, globalNodes.data(), m_pointerMapCount * sizeof(PointerNode));
    }
}

void PointerScanner::FindChainsRecursive(uintptr_t currentTarget, int depth, int maxDepth, size_t maxOffset, std::vector<uintptr_t>& currentOffsets, std::unordered_set<uintptr_t>& visited) {
    if (depth > maxDepth || m_cancelRequested) return;
    if (visited.count(currentTarget)) return;
    visited.insert(currentTarget);

    auto modules = m_pm.GetModules();

    // Use binary search on the sorted pointer map
    uintptr_t minVal = currentTarget - maxOffset;
    uintptr_t maxVal = currentTarget;

    auto it = std::lower_bound(m_pointerMap, m_pointerMap + m_pointerMapCount, minVal, [](const PointerNode& node, uintptr_t val) {
        return node.value < val;
    });

    for (; it != m_pointerMap + m_pointerMapCount && it->value <= maxVal; ++it) {
        if (m_cancelRequested) return;

        uintptr_t foundAddr = it->address;
        uintptr_t offset = currentTarget - it->value;

        std::vector<uintptr_t> newOffsets = currentOffsets;
        newOffsets.insert(newOffsets.begin(), offset);

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
