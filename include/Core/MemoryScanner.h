#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

#include <vector>
#include <string>
#include <variant>
#include <atomic>
#include <functional>
#include <stack>
#include <mutex>
#include "Core/ProcessManager.h"

namespace Core {

enum class ScanType {
    ExactValue,
    UnknownInitial,
    Increased,
    Decreased,
    Changed,
    Unchanged,
    GreaterThan,
    LessThan,
    Between
};

enum class DataType {
    Int8, Uint8,
    Int16, Uint16,
    Int32, Uint32,
    Int64, Uint64,
    Float, Double,
    String,
    AOB
};

struct ScanValue {
    DataType type;
    std::variant<int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t, int64_t, uint64_t, float, double, std::string, std::vector<uint8_t>> value;
    std::variant<int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t, int64_t, uint64_t, float, double> value2;
};

class MemoryScanner {
public:
    MemoryScanner(const ProcessManager& pm);

    void FirstScan(const ScanValue& val, ScanType scanType);
    void NextScan(const ScanValue& val, ScanType scanType);
    void Undo();

    std::vector<uintptr_t> GetResults();
    size_t GetResultCount();
    void Reset();

    bool IsScanning() const { return m_isScanning; }
    float GetProgress() const { return m_progress; }
    void Cancel();

private:
    struct ScanSnapshot {
        std::vector<uintptr_t> addresses;
        std::vector<uint8_t> values;
    };

    const ProcessManager& m_pm;
    ScanSnapshot m_currentScan;
    std::stack<ScanSnapshot> m_history;
    std::mutex m_resultsMutex;

    std::atomic<bool> m_isScanning{false};
    std::atomic<bool> m_cancelRequested{false};
    std::atomic<float> m_progress{0.0f};

    void ScanRegion(const RegionInfo& region, const ScanValue& val, ScanType scanType, std::vector<uintptr_t>& localResults, std::vector<uint8_t>& localValues);
    bool CompareValues(const void* current, const void* previous, const ScanValue& val, ScanType scanType, size_t size);

    void AOBScan(const RegionInfo& region, const std::string& pattern, std::vector<uintptr_t>& results, std::vector<uint8_t>& values);
};

} // namespace Core
