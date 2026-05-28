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

namespace Core {

MemoryScanner::MemoryScanner(const ProcessManager& pm) : m_pm(pm) {}

void MemoryScanner::Reset() {
    std::lock_guard<std::mutex> lock(m_resultsMutex);
    m_currentScan.addresses.clear();
    m_currentScan.values.clear();
    while (!m_history.empty()) m_history.pop();
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

void MemoryScanner::FirstScan(const ScanValue& val, ScanType scanType) {
    if (m_isScanning) return;

    std::thread([this, val, scanType]() {
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
        ScanSnapshot allResults;
        // Pre-allocate space for results to avoid frequent reallocations
        allResults.addresses.reserve(10000);
        allResults.values.reserve(10000 * sizeof(uint64_t));

        size_t scannedSize = 0;

        std::vector<std::thread> threads;
        unsigned int numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) numThreads = 1;

        for (size_t i = 0; i < regions.size(); ++i) {
            if (m_cancelRequested) break;

            const auto& region = regions[i];

            threads.push_back(std::thread([this, region, val, scanType, &localResultsMutex, &allResults, &scannedSize, totalSize]() {
                std::vector<uintptr_t> localAddrs;
                std::vector<uint8_t> localVals;
                if (val.type == DataType::AOB) {
                    AOBScan(region, std::get<std::string>(val.value), localAddrs, localVals);
                } else {
                    ScanRegion(region, val, scanType, localAddrs, localVals);
                }

                std::lock_guard<std::mutex> lock(localResultsMutex);
                allResults.addresses.insert(allResults.addresses.end(), localAddrs.begin(), localAddrs.end());
                allResults.values.insert(allResults.values.end(), localVals.begin(), localVals.end());
                scannedSize += region.size;
                m_progress = (float)scannedSize / totalSize;
            }));

            if (threads.size() >= numThreads) {
                for (auto& t : threads) t.join();
                threads.clear();
            }
        }
        for (auto& t : threads) t.join();

        {
            std::lock_guard<std::mutex> lock(m_resultsMutex);
            m_currentScan = std::move(allResults);
        }
        m_isScanning = false;
    }).detach();
}

void MemoryScanner::NextScan(const ScanValue& val, ScanType scanType) {
    if (m_isScanning) return;

    std::thread([this, val, scanType]() {
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
        size_t typeSize = GetDataTypeSize(val.type, val);
        nextResults.values.reserve(nextResults.addresses.capacity() * typeSize);

        size_t step = 1000;

        std::vector<uint8_t> buffer(typeSize);

        for (size_t i = 0; i < total; i += step) {
            if (m_cancelRequested) break;

            size_t end = (std::min)(i + step, total);
            for (size_t j = i; j < end; ++j) {
                uintptr_t addr = prevScan.addresses[j];
                if (m_pm.ReadMemory(addr, buffer.data(), typeSize)) {
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
    }).detach();
}

void MemoryScanner::ScanRegion(const RegionInfo& region, const ScanValue& val, ScanType scanType, std::vector<uintptr_t>& localResults, std::vector<uint8_t>& localValues) {
    const size_t bufferSize = 64 * 1024;
    std::vector<uint8_t> buffer(bufferSize);
    size_t typeSize = GetDataTypeSize(val.type, val);
    if (typeSize == 0 || typeSize > bufferSize) return;

    for (size_t offset = 0; offset < region.size; ) {
        if (m_cancelRequested) break;

        size_t toRead = (std::min)(bufferSize, region.size - offset);
        bool readSuccess = m_pm.ReadMemory(region.baseAddress + offset, buffer.data(), toRead);

#ifdef _WIN32
        // If read fails, try to temporarily change protection (optional and with caution)
        if (!readSuccess && !(region.protect & PAGE_GUARD) && (region.protect & PAGE_NOACCESS)) {
            DWORD oldProtect;
            if (VirtualProtectEx(m_pm.GetHandle(), (LPVOID)(region.baseAddress + offset), toRead, PAGE_EXECUTE_READ, &oldProtect)) {
                readSuccess = m_pm.ReadMemory(region.baseAddress + offset, buffer.data(), toRead);
                VirtualProtectEx(m_pm.GetHandle(), (LPVOID)(region.baseAddress + offset), toRead, oldProtect, &oldProtect);
            }
        }
#endif

        if (!readSuccess) {
            offset += bufferSize;
            continue;
        }

        size_t scanLimit = (toRead >= typeSize) ? (toRead - typeSize) : 0;
        for (size_t i = 0; i <= scanLimit; ++i) {
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

            // Try AVX2 if possible (assuming AVX2 is available as per memory instructions)
            // In a real-world scenario, we would use a CPU feature check here.
            __m256i firstByteVec256 = _mm256_set1_epi8(firstByte);
            size_t i = 0;
            for (; i <= (toRead >= 32 ? toRead - 32 : 0); i += 32) {
                __m256i data = _mm256_loadu_si256((const __m256i*)(buffer.data() + i));
                __m256i cmp = _mm256_cmpeq_epi8(data, firstByteVec256);
                uint32_t bitmask = _mm256_movemask_epi8(cmp);

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

            // Fallback to SSE4.2 for the remainder
            __m128i firstByteVec128 = _mm_set1_epi8(firstByte);
            for (; i <= (toRead >= 16 ? toRead - 16 : 0); i += 16) {
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

            // Remainder for the last < 16 bytes
            for (; i <= (toRead >= bytes.size() ? toRead - bytes.size() : 0); ++i) {
                if (buffer[i] == firstByte) {
                    bool found = true;
                    for (size_t k = 1; k < bytes.size(); ++k) {
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
        } else {
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
