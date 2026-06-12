#include "Core/MemoryScanner.h"
#include <immintrin.h>
#include <thread>
#include <mutex>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <cstring>
#include <bit>
#include <future>
#include "Utils/ThreadPool.h"

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

MemoryScanner::MemoryScanner(const ProcessManager& pm) : m_pm(pm), m_arena(10 * 1024 * 1024) {} // 10MB blocks for scan results

void MemoryScanner::Reset() {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    m_currentScan.addresses.clear();
    m_currentScan.values.clear();
    m_currentScan.addresses.shrink_to_fit();
    m_currentScan.values.shrink_to_fit();
    while (!m_history.empty()) m_history.pop();
    m_arena.Reset();
    m_progress = 0.0f;
}

void MemoryScanner::Undo() {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    if (!m_history.empty()) {
        m_currentScan = m_history.top();
        m_history.pop();
    }
}

void MemoryScanner::Cancel() {
    m_cancelRequested = true;
}

std::vector<uintptr_t> MemoryScanner::GetResults() {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    return m_currentScan.addresses;
}

std::vector<uint8_t> MemoryScanner::GetResultValues() {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    return m_currentScan.values;
}

size_t MemoryScanner::GetResultCount() {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    return m_currentScan.addresses.size();
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
        case DataType::String16: return std::get<std::string>(val.value).length() * 2;
        case DataType::AOB: {
            std::string pattern = std::get<std::string>(val.value);
            std::stringstream ss(pattern);
            std::string item;
            size_t count = 0;
            while (ss >> item) count++;
            return count;
        }
        default: return 0;
    }
}

void MemoryScanner::FirstScan(const ScanValue& val, ScanType scanType, bool modifyProtection) {
    if (m_isScanning) return;

    m_scanFuture = std::async(std::launch::async, [this, val, scanType, modifyProtection]() {
        m_isScanning = true;
        m_cancelRequested = false;

        {
            std::lock_guard<std::mutex> lock(m_resultsMutex);
            if (!m_currentScan.addresses.empty()) {
                m_history.push(m_currentScan);
            }
            m_currentScan.addresses.clear();
            m_currentScan.values.clear();
        }

        auto regions = m_pm.GetRegions();
        size_t totalSize = 0;
        for (const auto& r : regions) totalSize += r.size;

        std::mutex localResultsMutex;
        std::vector<ScanSnapshot> allThreadResults;
        size_t scannedSize = 0;

        unsigned int numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) numThreads = 1;

        Utils::ThreadPool pool(numThreads);
        std::vector<std::future<void>> futures;

        for (const auto& region : regions) {
            if (m_cancelRequested) break;

            futures.push_back(pool.Enqueue([this, region, val, scanType, modifyProtection, &localResultsMutex, &scannedSize, totalSize, &allThreadResults]() {
                ScanSnapshot res;
                if (val.type == DataType::AOB) {
                    AOBScan(region, std::get<std::string>(val.value), res.addresses, res.values);
                } else {
                    ScanRegion(region, val, scanType, res.addresses, res.values, modifyProtection);
                }

                {
                    std::lock_guard<std::mutex> lock(localResultsMutex);
                    scannedSize += region.size;
                    m_progress = (float)scannedSize / totalSize;
                    if (!res.addresses.empty()) {
                        allThreadResults.push_back(std::move(res));
                    }
                }
            }));
        }

        for (auto& f : futures) {
            f.get();
        }

        // Merge results
        ScanSnapshot allResults;
        size_t totalAddresses = 0;
        size_t totalValuesSize = 0;
        for (const auto& res : allThreadResults) {
            totalAddresses += res.addresses.size();
            totalValuesSize += res.values.size();
        }

        allResults.addresses.reserve(totalAddresses);
        allResults.values.reserve(totalValuesSize);

        for (auto& res : allThreadResults) {
            allResults.addresses.insert(allResults.addresses.end(), res.addresses.begin(), res.addresses.end());
            allResults.values.insert(allResults.values.end(), res.values.begin(), res.values.end());
        }

        {
            std::lock_guard<std::mutex> lock(m_resultsMutex);
            m_currentScan = std::move(allResults);
        }
        m_isScanning = false;
    });
}

void MemoryScanner::NextScan(const ScanValue& val, ScanType scanType, bool modifyProtection) {
    if (m_isScanning) return;

    m_scanFuture = std::async(std::launch::async, [this, val, scanType, modifyProtection]() {
        m_isScanning = true;
        m_cancelRequested = false;

        ScanSnapshot prevScan;
        {
            std::lock_guard<std::mutex> lock(m_resultsMutex);
            prevScan = m_currentScan;
            m_history.push(m_currentScan);
        }

        ScanSnapshot nextResults;
        size_t total = prevScan.addresses.size();

        nextResults.addresses.reserve(total / 2);

        size_t step = 1000;
        size_t typeSize = GetDataTypeSize(val.type, val);
        std::vector<uint8_t> buffer(typeSize);

        for (size_t i = 0; i < total; i += step) {
            if (m_cancelRequested) break;

            size_t end = (std::min)(i + step, total);
            for (size_t j = i; j < end; ++j) {
                uintptr_t addr = prevScan.addresses[j];
                if (m_pm.ReadMemory(addr, buffer.data(), typeSize, modifyProtection)) {
                    if (CompareValues(buffer.data(), prevScan.values.data() + (j * typeSize), val, scanType, typeSize)) {
                        nextResults.addresses.push_back(addr);
                        nextResults.values.insert(nextResults.values.end(), buffer.begin(), buffer.end());
                    }
                }
            }
            m_progress = (float)i / total;
        }

        {
            std::lock_guard<std::mutex> lock(m_resultsMutex);
            m_currentScan = std::move(nextResults);
        }
        m_isScanning = false;
    });
}

void MemoryScanner::ScanRegion(const RegionInfo& region, const ScanValue& val, ScanType scanType, std::vector<uintptr_t>& localResults, std::vector<uint8_t>& localValues, bool modifyProtection) {
    const size_t bufferSize = 64 * 1024;
    std::vector<uint8_t> buffer(bufferSize);
    size_t typeSize = GetDataTypeSize(val.type, val);
    if (typeSize == 0 || typeSize > bufferSize) return;

    bool useSIMD = (scanType == ScanType::ExactValue && (typeSize == 4 || typeSize == 8));
    bool hasAVX2 = useSIMD && SupportsAVX2();
    bool hasSSE42 = useSIMD && SupportsSSE42();

    for (size_t offset = 0; offset < region.size; ) {
        if (m_cancelRequested) break;

        size_t toRead = (std::min)(bufferSize, region.size - offset);
        if (!m_pm.ReadMemory(region.baseAddress + offset, buffer.data(), toRead, modifyProtection)) {
            offset += bufferSize;
            continue;
        }

        size_t scanLimit = (toRead >= typeSize) ? (toRead - typeSize) : 0;
        size_t i = 0;

        if (hasAVX2 && toRead >= 32) {
            if (typeSize == 4) {
                uint32_t target = std::get<uint32_t>(val.value);
                __m256i targetVec = _mm256_set1_epi32(target);
                for (; i <= toRead - 32; i += 32) {
                    __m256i data = _mm256_loadu_si256((const __m256i*)(buffer.data() + i));
                    __m256i cmp = _mm256_cmpeq_epi32(data, targetVec);
                    uint32_t bitmask = (uint32_t)_mm256_movemask_ps(_mm256_castsi256_ps(cmp));
                    while (bitmask != 0) {
                        int pos = std::countr_zero(bitmask);
                        localResults.push_back(region.baseAddress + offset + i + (pos * 4));
                        localValues.insert(localValues.end(), buffer.data() + i + (pos * 4), buffer.data() + i + (pos * 4) + 4);
                        bitmask &= ~(1 << pos);
                    }
                }
            } else if (typeSize == 8) {
                uint64_t target = std::get<uint64_t>(val.value);
                __m256i targetVec = _mm256_set1_epi64x(target);
                for (; i <= toRead - 32; i += 32) {
                    __m256i data = _mm256_loadu_si256((const __m256i*)(buffer.data() + i));
                    __m256i cmp = _mm256_cmpeq_epi64(data, targetVec);
                    uint32_t bitmask = (uint32_t)_mm256_movemask_pd(_mm256_castsi256_pd(cmp));
                    while (bitmask != 0) {
                        int pos = std::countr_zero(bitmask);
                        localResults.push_back(region.baseAddress + offset + i + (pos * 8));
                        localValues.insert(localValues.end(), buffer.data() + i + (pos * 8), buffer.data() + i + (pos * 8) + 8);
                        bitmask &= ~(1 << pos);
                    }
                }
            }
        } else if (hasSSE42 && toRead >= 16) {
            if (typeSize == 4) {
                uint32_t target = std::get<uint32_t>(val.value);
                __m128i targetVec = _mm_set1_epi32(target);
                for (; i <= toRead - 16; i += 16) {
                    __m128i data = _mm_loadu_si128((const __m128i*)(buffer.data() + i));
                    __m128i cmp = _mm_cmpeq_epi32(data, targetVec);
                    uint32_t bitmask = (uint32_t)_mm_movemask_ps(_mm_castsi128_ps(cmp));
                    while (bitmask != 0) {
                        int pos = std::countr_zero(bitmask);
                        localResults.push_back(region.baseAddress + offset + i + (pos * 4));
                        localValues.insert(localValues.end(), buffer.data() + i + (pos * 4), buffer.data() + i + (pos * 4) + 4);
                        bitmask &= ~(1 << pos);
                    }
                }
            } else if (typeSize == 8) {
                uint64_t target = std::get<uint64_t>(val.value);
                __m128i targetVec = _mm_set1_epi64x(target);
                for (; i <= toRead - 16; i += 16) {
                    __m128i data = _mm_loadu_si128((const __m128i*)(buffer.data() + i));
                    __m128i cmp = _mm_cmpeq_epi64(data, targetVec);
                    uint32_t bitmask = (uint32_t)_mm_movemask_pd(_mm_castsi128_pd(cmp));
                    while (bitmask != 0) {
                        int pos = std::countr_zero(bitmask);
                        localResults.push_back(region.baseAddress + offset + i + (pos * 8));
                        localValues.insert(localValues.end(), buffer.data() + i + (pos * 8), buffer.data() + i + (pos * 8) + 8);
                        bitmask &= ~(1 << pos);
                    }
                }
            }
        }

        for (; i <= scanLimit; ++i) {
            if (CompareValues(buffer.data() + i, nullptr, val, scanType, typeSize)) {
                localResults.push_back(region.baseAddress + offset + i);
                localValues.insert(localValues.end(), buffer.data() + i, buffer.data() + i + typeSize);
            }
        }

        if (toRead < bufferSize) break;
        offset += (bufferSize - typeSize + 1);
    }
}

template<typename T>
bool Compare(T mVal, T pVal, T vVal, T vVal2, ScanType scanType) {
    switch (scanType) {
        case ScanType::ExactValue: return mVal == vVal;
        case ScanType::GreaterThan: return mVal > vVal;
        case ScanType::LessThan: return mVal < vVal;
        case ScanType::Between: return mVal >= vVal && mVal <= vVal2;
        case ScanType::Increased: return mVal > pVal;
        case ScanType::Decreased: return mVal < pVal;
        case ScanType::Changed: return mVal != pVal;
        case ScanType::Unchanged: return mVal == pVal;
        case ScanType::UnknownInitial: return true;
        default: return false;
    }
}

bool MemoryScanner::CompareValues(const void* current, const void* previous, const ScanValue& val, ScanType scanType, size_t size) {
    if (!previous && (scanType == ScanType::Increased || scanType == ScanType::Decreased ||
                     scanType == ScanType::Changed || scanType == ScanType::Unchanged)) return false;

    switch (val.type) {
        case DataType::Int8:   return Compare(*(int8_t*)current, (previous ? *(int8_t*)previous : (int8_t)0), std::get<int8_t>(val.value), (scanType == ScanType::Between ? std::get<int8_t>(val.value2) : (int8_t)0), scanType);
        case DataType::Uint8:  return Compare(*(uint8_t*)current, (previous ? *(uint8_t*)previous : (uint8_t)0), std::get<uint8_t>(val.value), (scanType == ScanType::Between ? std::get<uint8_t>(val.value2) : (uint8_t)0), scanType);
        case DataType::Int16:  return Compare(*(int16_t*)current, (previous ? *(int16_t*)previous : (int16_t)0), std::get<int16_t>(val.value), (scanType == ScanType::Between ? std::get<int16_t>(val.value2) : (int16_t)0), scanType);
        case DataType::Uint16: return Compare(*(uint16_t*)current, (previous ? *(uint16_t*)previous : (uint16_t)0), std::get<uint16_t>(val.value), (scanType == ScanType::Between ? std::get<uint16_t>(val.value2) : (uint16_t)0), scanType);
        case DataType::Int32:  return Compare(*(int32_t*)current, (previous ? *(int32_t*)previous : (int32_t)0), std::get<int32_t>(val.value), (scanType == ScanType::Between ? std::get<int32_t>(val.value2) : (int32_t)0), scanType);
        case DataType::Uint32: return Compare(*(uint32_t*)current, (previous ? *(uint32_t*)previous : (uint32_t)0), std::get<uint32_t>(val.value), (scanType == ScanType::Between ? std::get<uint32_t>(val.value2) : (uint32_t)0), scanType);
        case DataType::Int64:  return Compare(*(int64_t*)current, (previous ? *(int64_t*)previous : (int64_t)0), std::get<int64_t>(val.value), (scanType == ScanType::Between ? std::get<int64_t>(val.value2) : (int64_t)0), scanType);
        case DataType::Uint64: return Compare(*(uint64_t*)current, (previous ? *(uint64_t*)previous : (uint64_t)0), std::get<uint64_t>(val.value), (scanType == ScanType::Between ? std::get<uint64_t>(val.value2) : (uint64_t)0), scanType);
        case DataType::Float:  return Compare(*(float*)current, (previous ? *(float*)previous : 0.0f), std::get<float>(val.value), (scanType == ScanType::Between ? std::get<float>(val.value2) : 0.0f), scanType);
        case DataType::Double: return Compare(*(double*)current, (previous ? *(double*)previous : 0.0), std::get<double>(val.value), (scanType == ScanType::Between ? std::get<double>(val.value2) : 0.0), scanType);
        case DataType::String: {
            std::string v = std::get<std::string>(val.value);
            return memcmp(current, v.data(), v.length()) == 0;
        }
        case DataType::String16: {
            // Treat the input string as a sequence of UTF-16LE characters.
            // In a real scenario, the UI would convert UTF-8 input to UTF-16LE bytes.
            // Here we assume std::get<std::string>(val.value) already contains the UTF-16LE encoded bytes.
            std::string v = std::get<std::string>(val.value);
            return memcmp(current, v.data(), v.length()) == 0;
        }
        case DataType::AOB: {
            if (previous && (scanType == ScanType::Changed || scanType == ScanType::Unchanged)) {
                bool identical = memcmp(current, previous, size) == 0;
                return (scanType == ScanType::Unchanged) ? identical : !identical;
            }
            return true;
        }
        default: return false;
    }
}

void MemoryScanner::AOBScan(const RegionInfo& region, const std::string& pattern, std::vector<uintptr_t>& results, std::vector<uint8_t>& values) {
    std::vector<uint8_t> bytes;
    std::vector<bool> mask;
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

    if (bytes.empty()) return;

    const size_t bufferSize = 256 * 1024;
    std::vector<uint8_t> buffer(bufferSize);

    for (size_t offset = 0; offset < region.size; ) {
        if (m_cancelRequested) break;
        size_t toRead = (std::min)(bufferSize, region.size - offset);
        if (!m_pm.ReadMemory(region.baseAddress + offset, buffer.data(), toRead)) {
            offset += bufferSize;
            continue;
        }

        if (mask[0]) {
            uint8_t firstByte = bytes[0];

            // Try AVX2 if possible
            bool hasAVX2 = SupportsAVX2();
            bool hasSSE42 = SupportsSSE42();

            size_t i = 0;
            if (hasAVX2 && toRead >= 32) {
                __m256i firstByteVec256 = _mm256_set1_epi8(firstByte);
                for (; i <= toRead - 32; i += 32) {
                    __m256i data = _mm256_loadu_si256((const __m256i*)(buffer.data() + i));
                    __m256i cmp = _mm256_cmpeq_epi8(data, firstByteVec256);
                    uint32_t bitmask = (uint32_t)_mm256_movemask_epi8(cmp);

                    while (bitmask != 0) {
                        int pos = std::countr_zero(bitmask);
                        if (i + pos <= toRead - bytes.size()) {
                            bool found = true;
                            for (size_t k = 1; k < bytes.size(); ++k) {
                                if (mask[k] && buffer[i + pos + k] != bytes[k]) {
                                    found = false;
                                    break;
                                }
                            }
                            if (found) {
                                results.push_back(region.baseAddress + offset + i + pos);
                                values.insert(values.end(), buffer.data() + i + pos, buffer.data() + i + pos + bytes.size());
                            }
                        }
                        bitmask &= ~(1 << pos);
                    }
                }
            } else if (hasSSE42 && toRead >= 16) {
                __m128i firstByteVec128 = _mm_set1_epi8(firstByte);
                for (; i <= toRead - 16; i += 16) {
                    __m128i data = _mm_loadu_si128((const __m128i*)(buffer.data() + i));
                    __m128i cmp = _mm_cmpeq_epi8(data, firstByteVec128);
                    uint32_t bitmask = (uint32_t)_mm_movemask_epi8(cmp);

                    while (bitmask != 0) {
                        int pos = std::countr_zero(bitmask);
                        if (i + pos <= toRead - bytes.size()) {
                            bool found = true;
                            for (size_t k = 1; k < bytes.size(); ++k) {
                                if (mask[k] && buffer[i + pos + k] != bytes[k]) {
                                    found = false;
                                    break;
                                }
                            }
                            if (found) {
                                results.push_back(region.baseAddress + offset + i + pos);
                                values.insert(values.end(), buffer.data() + i + pos, buffer.data() + i + pos + bytes.size());
                            }
                        }
                        bitmask &= ~(1 << pos);
                    }
                }
            }

            // Fallback: Boyer-Moore-Horspool inspired search for non-SIMD or remaining bytes
            size_t badCharTable[256];

            // Find the last non-wildcard byte
            int lastRealByteIdx = (int)bytes.size() - 1;
            while (lastRealByteIdx >= 0 && !mask[lastRealByteIdx]) lastRealByteIdx--;

            if (lastRealByteIdx >= 0) {
                size_t skipValue = (size_t)lastRealByteIdx + 1;
                for (int k = 0; k < 256; ++k) badCharTable[k] = skipValue;
                for (int k = 0; k < lastRealByteIdx; ++k) {
                    if (mask[k]) badCharTable[bytes[k]] = (size_t)lastRealByteIdx - k;
                }

                for (; i <= (toRead >= bytes.size() ? toRead - bytes.size() : 0); ) {
                    bool found = true;
                    for (int k = lastRealByteIdx; k >= 0; --k) {
                        if (mask[k] && buffer[i + k] != bytes[k]) {
                            found = false;
                            break;
                        }
                    }

                    if (found) {
                        // Double check the rest of the pattern if there were trailing wildcards
                        for (size_t k = (size_t)lastRealByteIdx + 1; k < bytes.size(); ++k) {
                            if (mask[k] && buffer[i + k] != bytes[k]) {
                                found = false;
                                break;
                            }
                        }
                    }

                    if (found) {
                        results.push_back(region.baseAddress + offset + i);
                        values.insert(values.end(), buffer.data() + i, buffer.data() + i + bytes.size());
                        i++;
                    } else {
                        i += badCharTable[buffer[i + lastRealByteIdx]];
                    }
                }
            } else {
                // All wildcards or empty pattern
                for (; i <= (toRead >= bytes.size() ? toRead - bytes.size() : 0); ++i) {
                    results.push_back(region.baseAddress + offset + i);
                    values.insert(values.end(), buffer.data() + i, buffer.data() + i + bytes.size());
                }
            }
        } else {
            // If the first byte is a wildcard, we can't use BMH effectively
            for (size_t i = 0; i <= (toRead >= bytes.size() ? toRead - bytes.size() : 0); ++i) {
                bool found = true;
                for (size_t k = 0; k < bytes.size(); ++k) {
                    if (mask[k] && buffer[i + k] != bytes[k]) {
                        found = false;
                        break;
                    }
                }
                if (found) {
                    results.push_back(region.baseAddress + offset + i);
                    values.insert(values.end(), buffer.data() + i, buffer.data() + i + bytes.size());
                }
            }
        }
        if (toRead < bufferSize) break;
        offset += (bufferSize - bytes.size() + 1);
    }
}

} // namespace Core
