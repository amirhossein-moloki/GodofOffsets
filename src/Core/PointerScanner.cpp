#include "Core/PointerScanner.h"
#include <algorithm>
#include <iostream>
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

    std::thread([this, targetAddress, maxDepth, maxOffset]() {
        m_isScanning = true;
        m_cancelRequested = false;
        m_progress = 0.0f;

        {
            std::lock_guard<std::mutex> lock(m_resultsMutex);
            m_results.clear();
            m_arena.Reset();
            m_pointerNodes = nullptr;
            m_nodeCount = 0;
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
    }).detach();
}

void PointerScanner::BuildPointerMap() {
    auto regions = m_pm.GetRegions();

    std::mutex vectorMutex;
    std::vector<PointerNode> allNodes;

    size_t totalSize = 0;
    for (const auto& r : regions) totalSize += r.size;

    // Estimate node count to reserve space (heuristic: 1 pointer per 128 bytes)
    allNodes.reserve(totalSize / 128);

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

        futures.push_back(pool.Enqueue([this, region, &vectorMutex, &allNodes, &processedSize, totalSize]() {
            const size_t chunkSize = 1024 * 1024; // 1MB chunks
            std::vector<uint8_t> buffer(chunkSize);
            std::vector<PointerNode> localNodes;
            localNodes.reserve(chunkSize / sizeof(uintptr_t));

            for (size_t i = 0; i < region.size; i += chunkSize - sizeof(uintptr_t)) {
                if (m_cancelRequested) return;
                size_t toRead = (std::min)(chunkSize, region.size - i);
                if (!m_pm.ReadMemory(region.baseAddress + i, buffer.data(), toRead)) continue;

                for (size_t j = 0; j <= (toRead >= sizeof(uintptr_t) ? toRead - sizeof(uintptr_t) : 0); j += sizeof(uintptr_t)) {
                    uintptr_t value = *(uintptr_t*)(buffer.data() + j);

                    // Basic pointer validation (heuristic)
                    if (value > 0x10000 && (value % sizeof(uintptr_t) == 0)) {
                        localNodes.push_back({ value, region.baseAddress + i + j });
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(vectorMutex);
                allNodes.insert(allNodes.end(), localNodes.begin(), localNodes.end());
                processedSize += region.size;
                m_progress = 0.7f * ((float)processedSize / totalSize);
            }
        }));
    }

    for (auto& f : futures) {
        f.get();
    }

    if (m_cancelRequested) return;

    // Sort all nodes by value for binary search
    std::sort(allNodes.begin(), allNodes.end());

    m_nodeCount = allNodes.size();
    if (m_nodeCount > 0) {
        m_pointerNodes = (PointerNode*)m_arena.Allocate(m_nodeCount * sizeof(PointerNode), alignof(PointerNode));
        std::memcpy(m_pointerNodes, allNodes.data(), m_nodeCount * sizeof(PointerNode));
    }

    m_progress = 0.8f;
}

void PointerScanner::FindChainsRecursive(uintptr_t currentTarget, int depth, int maxDepth, size_t maxOffset, std::vector<uintptr_t>& currentOffsets, std::set<uintptr_t>& visited) {
    if (depth > maxDepth || m_cancelRequested || !m_pointerNodes) return;
    if (visited.count(currentTarget)) return;
    visited.insert(currentTarget);

    auto modules = m_pm.GetModules();

    // Binary search for range [currentTarget - maxOffset, currentTarget]
    PointerNode lower;
    lower.value = (currentTarget >= maxOffset) ? (currentTarget - maxOffset) : 0;

    PointerNode upper;
    upper.value = currentTarget;

    auto it_start = std::lower_bound(m_pointerNodes, m_pointerNodes + m_nodeCount, lower);
    auto it_end = std::upper_bound(m_pointerNodes, m_pointerNodes + m_nodeCount, upper);

    for (auto it = it_start; it != it_end; ++it) {
        if (m_cancelRequested) return;

        uintptr_t foundAddr = it->address;
        uintptr_t offset = currentTarget - it->value;

        // Use push_back and then pop_back instead of creating new vector or using insert(begin)
        currentOffsets.push_back(offset);

        // Check if this address belongs to a module (static base)
        bool foundStatic = false;
        for (const auto& mod : modules) {
            if (foundAddr >= mod.baseAddress && foundAddr < mod.baseAddress + mod.imageSize) {
                PointerChain chain;
                chain.baseAddress = mod.baseAddress;
                chain.moduleName = mod.name;

                // Construct the full chain including the base offset
                chain.offsets.reserve(currentOffsets.size() + 1);
                chain.offsets.push_back(foundAddr - mod.baseAddress);
                // Copy in reverse since we pushed them back in recursive calls
                for (auto it_off = currentOffsets.rbegin(); it_off != currentOffsets.rend(); ++it_off) {
                    chain.offsets.push_back(*it_off);
                }

                std::lock_guard<std::mutex> lock(m_resultsMutex);
                m_results.push_back(chain);
                foundStatic = true;
                break;
            }
        }

        if (!foundStatic && depth < maxDepth) {
            FindChainsRecursive(foundAddr, depth + 1, maxDepth, maxOffset, currentOffsets, visited);
        }

        currentOffsets.pop_back();
    }
}

std::vector<PointerChain> PointerScanner::GetResults() {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    return m_results;
}

} // namespace Core
