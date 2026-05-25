#include "Core/MemoryScanner.h"
#include <immintrin.h>
#include <thread>
#include <mutex>
#include <algorithm>
#include <iostream>
#include <sstream>

namespace Core {

MemoryScanner::MemoryScanner(const ProcessManager& pm) : m_pm(pm) {}

void MemoryScanner::Reset() {
    m_results.clear();
    m_progress = 0.0f;
}

void MemoryScanner::Cancel() {
    m_cancelRequested = true;
}

static size_t GetDataTypeSize(DataType type, const ScanValue& val) {
    switch (type) {
        case DataType::Int8:   case DataType::Uint8:  return 1;
        case DataType::Int16:  case DataType::Uint16: return 2;
        case DataType::Int32:  case DataType::Uint32: return 4;
        case DataType::Int64:  case DataType::Uint64: return 8;
        case DataType::Float:  return 4;
        case DataType::Double: return 8;
        case DataType::String: return std::get<std::string>(val.value).length();
        default: return 0;
    }
}

void MemoryScanner::FirstScan(const ScanValue& val, ScanType scanType) {
    m_isScanning = true;
    m_cancelRequested = false;
    m_results.clear();

    auto regions = m_pm.GetRegions();
    size_t totalSize = 0;
    for (const auto& r : regions) totalSize += r.size;

    std::mutex resultsMutex;
    size_t scannedSize = 0;

    std::vector<std::thread> threads;
    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 1;

    for (size_t i = 0; i < regions.size(); ++i) {
        if (m_cancelRequested) break;

        const auto& region = regions[i];

        // Simple thread pool approach for regions
        threads.push_back(std::thread([this, &region, &val, scanType, &resultsMutex, &scannedSize, totalSize]() {
            std::vector<uintptr_t> localResults;
            if (val.type == DataType::AOB) {
                localResults = AOBScan(region, std::get<std::string>(val.value));
            } else {
                ScanRegion(region, val, scanType, localResults);
            }

            std::lock_guard<std::mutex> lock(resultsMutex);
            m_results.insert(m_results.end(), localResults.begin(), localResults.end());
            scannedSize += region.size;
            m_progress = (float)scannedSize / totalSize;
        }));

        if (threads.size() >= numThreads) {
            for (auto& t : threads) t.join();
            threads.clear();
        }
    }
    for (auto& t : threads) t.join();

    m_isScanning = false;
}

void MemoryScanner::NextScan(const ScanValue& val, ScanType scanType) {
    if (m_results.empty()) return;
    m_isScanning = true;
    m_cancelRequested = false;

    std::vector<uintptr_t> nextResults;
    size_t total = m_results.size();
    size_t step = 1000;

    size_t typeSize = GetDataTypeSize(val.type, val);

    for (size_t i = 0; i < total; i += step) {
        if (m_cancelRequested) break;

        size_t end = (std::min)(i + step, total);
        for (size_t j = i; j < end; ++j) {
            uintptr_t addr = m_results[j];
            std::vector<uint8_t> buffer(typeSize);
            if (m_pm.ReadMemory(addr, buffer.data(), typeSize)) {
                if (CompareValues(buffer.data(), val, scanType, typeSize)) {
                    nextResults.push_back(addr);
                }
            }
        }
        m_progress = (float)i / total;
    }

    m_results = std::move(nextResults);
    m_isScanning = false;
}

void MemoryScanner::ScanRegion(const RegionInfo& region, const ScanValue& val, ScanType scanType, std::vector<uintptr_t>& localResults) {
    const size_t bufferSize = 64 * 1024;
    std::vector<uint8_t> buffer(bufferSize);
    size_t typeSize = GetDataTypeSize(val.type, val);
    if (typeSize == 0) return;

    for (size_t offset = 0; offset < region.size; offset += bufferSize - typeSize) {
        if (m_cancelRequested) break;

        size_t toRead = (std::min)(bufferSize, region.size - offset);
        if (!m_pm.ReadMemory(region.baseAddress + offset, buffer.data(), toRead)) continue;

        for (size_t i = 0; i <= toRead - typeSize; ++i) {
            if (CompareValues(buffer.data() + i, val, scanType, typeSize)) {
                localResults.push_back(region.baseAddress + offset + i);
            }
        }
    }
}

template<typename T>
bool Compare(T mVal, T vVal, T vVal2, ScanType scanType) {
    switch (scanType) {
        case ScanType::ExactValue: return mVal == vVal;
        case ScanType::GreaterThan: return mVal > vVal;
        case ScanType::LessThan: return mVal < vVal;
        case ScanType::Between: return mVal >= vVal && mVal <= vVal2;
        default: return false;
    }
}

bool MemoryScanner::CompareValues(const void* mem, const ScanValue& val, ScanType scanType, size_t size) {
    switch (val.type) {
        case DataType::Int8:   return Compare(*(int8_t*)mem, std::get<int8_t>(val.value), (scanType == ScanType::Between ? std::get<int8_t>(val.value2) : (int8_t)0), scanType);
        case DataType::Uint8:  return Compare(*(uint8_t*)mem, std::get<uint8_t>(val.value), (scanType == ScanType::Between ? std::get<uint8_t>(val.value2) : (uint8_t)0), scanType);
        case DataType::Int16:  return Compare(*(int16_t*)mem, std::get<int16_t>(val.value), (scanType == ScanType::Between ? std::get<int16_t>(val.value2) : (int16_t)0), scanType);
        case DataType::Uint16: return Compare(*(uint16_t*)mem, std::get<uint16_t>(val.value), (scanType == ScanType::Between ? std::get<uint16_t>(val.value2) : (uint16_t)0), scanType);
        case DataType::Int32:  return Compare(*(int32_t*)mem, std::get<int32_t>(val.value), (scanType == ScanType::Between ? std::get<int32_t>(val.value2) : (int32_t)0), scanType);
        case DataType::Uint32: return Compare(*(uint32_t*)mem, std::get<uint32_t>(val.value), (scanType == ScanType::Between ? std::get<uint32_t>(val.value2) : (uint32_t)0), scanType);
        case DataType::Int64:  return Compare(*(int64_t*)mem, std::get<int64_t>(val.value), (scanType == ScanType::Between ? std::get<int64_t>(val.value2) : (int64_t)0), scanType);
        case DataType::Uint64: return Compare(*(uint64_t*)mem, std::get<uint64_t>(val.value), (scanType == ScanType::Between ? std::get<uint64_t>(val.value2) : (uint64_t)0), scanType);
        case DataType::Float:  return Compare(*(float*)mem, std::get<float>(val.value), (scanType == ScanType::Between ? std::get<float>(val.value2) : 0.0f), scanType);
        case DataType::Double: return Compare(*(double*)mem, std::get<double>(val.value), (scanType == ScanType::Between ? std::get<double>(val.value2) : 0.0), scanType);
        case DataType::String: {
            std::string v = std::get<std::string>(val.value);
            return memcmp(mem, v.data(), v.length()) == 0;
        }
        default: return false;
    }
}

std::vector<uintptr_t> MemoryScanner::AOBScan(const RegionInfo& region, const std::string& pattern) {
    std::vector<uintptr_t> results;

    // Parse pattern
    std::vector<uint8_t> bytes;
    std::vector<bool> mask; // true if byte is known, false if '?'
    std::stringstream ss(pattern);
    std::string item;
    while (ss >> item) {
        if (item == "?" || item == "??") {
            bytes.push_back(0);
            mask.push_back(false);
        } else {
            bytes.push_back((uint8_t)std::stoul(item, nullptr, 16));
            mask.push_back(true);
        }
    }

    if (bytes.empty()) return results;

    const size_t bufferSize = 256 * 1024;
    std::vector<uint8_t> buffer(bufferSize);

    for (size_t offset = 0; offset < region.size; offset += bufferSize - bytes.size()) {
        if (m_cancelRequested) break;
        size_t toRead = (std::min)(bufferSize, region.size - offset);
        if (!m_pm.ReadMemory(region.baseAddress + offset, buffer.data(), toRead)) continue;

        // SIMD Optimization for AOB: Search for the first byte of the pattern
        if (mask[0]) {
            uint8_t firstByte = bytes[0];
            __m128i firstByteVec = _mm_set1_epi8(firstByte);

            for (size_t i = 0; i <= toRead - bytes.size(); i += 16) {
                if (m_cancelRequested) break;

                size_t remaining = toRead - i;
                if (remaining < 16) {
                    // Fallback to scalar for end of buffer
                    for (size_t j = i; j <= toRead - bytes.size(); ++j) {
                        bool found = true;
                        for (size_t k = 0; k < bytes.size(); ++k) {
                            if (mask[k] && buffer[j + k] != bytes[k]) {
                                found = false;
                                break;
                            }
                        }
                        if (found) results.push_back(region.baseAddress + offset + j);
                    }
                    break;
                }

                __m128i data = _mm_loadu_si128((const __m128i*)(buffer.data() + i));
                __m128i cmp = _mm_cmpeq_epi8(data, firstByteVec);
                int bitmask = _mm_movemask_epi8(cmp);

                while (bitmask != 0) {
                    int pos = __builtin_ctz(bitmask);
                    if (i + pos <= toRead - bytes.size()) {
                        bool found = true;
                        for (size_t k = 1; k < bytes.size(); ++k) {
                            if (mask[k] && buffer[i + pos + k] != bytes[k]) {
                                found = false;
                                break;
                            }
                        }
                        if (found) results.push_back(region.baseAddress + offset + i + pos);
                    }
                    bitmask &= ~(1 << pos);
                }
            }
        } else {
            // Scalar fallback if first byte is wildcard
            for (size_t i = 0; i <= toRead - bytes.size(); ++i) {
                bool found = true;
                for (size_t k = 0; k < bytes.size(); ++k) {
                    if (mask[k] && buffer[i + k] != bytes[k]) {
                        found = false;
                        break;
                    }
                }
                if (found) results.push_back(region.baseAddress + offset + i);
            }
        }
    }

    return results;
}

} // namespace Core
