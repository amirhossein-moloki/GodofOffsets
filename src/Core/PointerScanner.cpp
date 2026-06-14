#include "Core/PointerScanner.h"
#include <algorithm>
#include <iostream>
#include <functional>
#include <sstream>
#include <thread>
#include <mutex>
#include <cstring>
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
            m_arena.Reset();
            m_pointerMap = nullptr;
            m_pointerMapCount = 0;
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
    size_t totalPointers = 0;

    std::vector<std::vector<PointerNode>> localNodes(std::thread::hardware_concurrency());
    std::atomic<size_t> processedSize{0};
    size_t totalSize = 0;
    for (const auto& r : regions) totalSize += r.size;

    Utils::ThreadPool pool(localNodes.size());
    std::vector<std::future<void>> futures;

    for (size_t t = 0; t < localNodes.size(); ++t) {
        futures.push_back(pool.Enqueue([this, t, &regions, &localNodes, &processedSize, totalSize]() {
            const size_t chunkSize = 1024 * 1024;
            std::vector<uint8_t> buffer(chunkSize);

            for (size_t r = t; r < regions.size(); r += localNodes.size()) {
                if (m_cancelRequested) return;
                const auto& region = regions[r];

                for (size_t i = 0; i < region.size; i += chunkSize - sizeof(uintptr_t)) {
                    size_t toRead = (std::min)(chunkSize, region.size - i);
                    if (!m_pm.ReadMemory(region.baseAddress + i, buffer.data(), toRead)) continue;

                    for (size_t j = 0; j <= (toRead >= sizeof(uintptr_t) ? toRead - sizeof(uintptr_t) : 0); j += sizeof(uintptr_t)) {
                        uintptr_t value = *(uintptr_t*)(buffer.data() + j);
                        // Heuristic: valid user-mode pointer
                        if (value > 0x10000 && value < 0x00007FFFFFFE0000 && (value % sizeof(uintptr_t) == 0)) {
                            localNodes[t].push_back({ value, region.baseAddress + i + j });
                        }
                    }
                }
                processedSize += region.size;
                m_progress = 0.6f * ((float)processedSize / totalSize);
            }
        }));
    }

    for (auto& f : futures) f.get();

    for (const auto& vec : localNodes) totalPointers += vec.size();

    m_pointerMap = (PointerNode*)m_arena.Allocate(totalPointers * sizeof(PointerNode));
    m_pointerMapCount = totalPointers;

    size_t offset = 0;
    for (const auto& vec : localNodes) {
        memcpy(m_pointerMap + offset, vec.data(), vec.size() * sizeof(PointerNode));
        offset += vec.size();
    }

    std::sort(m_pointerMap, m_pointerMap + m_pointerMapCount);
}

void PointerScanner::FindChainsRecursive(uintptr_t currentTarget, int depth, int maxDepth, size_t maxOffset, std::vector<uintptr_t>& currentOffsets, std::unordered_set<uintptr_t>& visited) {
    if (depth > maxDepth || m_cancelRequested) return;
    if (visited.count(currentTarget)) return;
    visited.insert(currentTarget);

    auto modules = m_pm.GetModules();

    // Binary search for values in range [currentTarget - maxOffset, currentTarget]
    PointerNode startNode = { currentTarget > maxOffset ? currentTarget - maxOffset : 0, 0 };
    PointerNode endNode = { currentTarget, 0 };

    auto it_start = std::lower_bound(m_pointerMap, m_pointerMap + m_pointerMapCount, startNode);
    auto it_end = std::upper_bound(m_pointerMap, m_pointerMap + m_pointerMapCount, endNode);

    for (auto it = it_start; it != it_end; ++it) {
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
